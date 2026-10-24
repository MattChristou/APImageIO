/*
 * exrbridge.cpp - OpenEXR decode behind the exrbridge.h C boundary.
 *
 * Uses the OpenEXR C++ API (Imf) so multi-layer channel selection is
 * straightforward. Compiled with the static /MT runtime to match the vcpkg
 * static OpenEXR build, so the resulting .p has no external C++ runtime dep.
 *
 * Source policy (collapsing a possibly multi-part, multi-layer EXR to one
 * RGBA image). Every RGB layer of every non-deep part is a candidate, ranked:
 *   1. the default R/G/B layer (a part named like a beauty pass wins ties);
 *   2. a "diffuse" layer, with a "specular" layer of the same part added in
 *      (reproduces the Blender matcap diffuse+specular look automatically);
 *   3. a layer whose last name segment says beauty ("Combined", "beauty",
 *      "rgba", ...), so Blender's ViewLayer.Combined beats ViewLayer.AO;
 *   4. any other complete RGB layer.
 * With no RGB anywhere: a luminance (Y, optionally RY/BY) image is decoded to
 * RGB, else the first single channel is shown as grey. We replace the stock
 * EXR loader, so refusing these would leave the file with no loader at all.
 *
 * The result is placed in the display window: a data window smaller than it
 * (e.g. a Nuke bbox crop) is padded with zero, a larger one (overscan) is
 * cropped, so the image arrives at its intended format size.
 */

#include "exrbridge.h"

#include <ImfMultiPartInputFile.h>
#include <ImfInputPart.h>
#include <ImfRgbaFile.h>
#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfCompression.h>
#include <ImfPixelType.h>
#include <ImfPartType.h>
#include <ImathBox.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <set>

using namespace Imf;
using namespace Imath;

static const char *compName(Compression c)
{
    switch (c) {
        case NO_COMPRESSION:    return "none";
        case RLE_COMPRESSION:   return "rle";
        case ZIPS_COMPRESSION:  return "zips";
        case ZIP_COMPRESSION:   return "zip";
        case PIZ_COMPRESSION:   return "piz";
        case PXR24_COMPRESSION: return "pxr24";
        case B44_COMPRESSION:   return "b44";
        case B44A_COMPRESSION:  return "b44a";
        case DWAA_COMPRESSION:  return "dwaa";
        case DWAB_COMPRESSION:  return "dwab";
        default:                return "other";
    }
}

/* Channel name within a layer: "" -> "R", "diffuse" -> "diffuse.R". */
static std::string chanName(const std::string &layer, char c)
{
    if (layer.empty()) return std::string(1, c);
    return layer + "." + c;
}

static bool layerHasRGB(const ChannelList &ch, const std::string &layer)
{
    return ch.findChannel(chanName(layer, 'R')) &&
           ch.findChannel(chanName(layer, 'G')) &&
           ch.findChannel(chanName(layer, 'B'));
}

static bool ieq(const std::string &a, const char *b)
{
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* "ViewLayer.Combined" -> true; matches on the last '.'-separated segment. */
static bool isBeautyName(const std::string &name)
{
    static const char *const k[] = { "combined", "beauty", "rgba", "rgb", "color",
                                     "colour", "image", "main", 0 };
    size_t dot = name.rfind('.');
    std::string seg = (dot == std::string::npos) ? name : name.substr(dot + 1);
    for (int i = 0; k[i]; ++i) if (ieq(seg, k[i])) return true;
    return false;
}

enum SrcKind { SRC_NONE, SRC_RGB, SRC_LUMA, SRC_GREY };

struct Source {
    SrcKind     kind = SRC_NONE;
    int         part = -1;
    std::string primary;    /* RGB: layer name; GREY: full channel name */
    std::string addLayer;   /* RGB: optional additive layer (specular) */
    int         score = -1;
};

static Source chooseSource(MultiPartInputFile &mp)
{
    Source best, luma, grey;
    for (int p = 0; p < mp.parts(); ++p) {
        const Header &h = mp.header(p);
        if (h.hasType() && isDeepData(h.type())) continue;   /* deep: not an image */
        const ChannelList &ch = h.channels();
        const bool beautyPart = h.hasName() && isBeautyName(h.name());

        std::set<std::string> layers;
        ch.layers(layers);
        layers.insert("");                                   /* the default layer */
        for (std::set<std::string>::const_iterator it = layers.begin(); it != layers.end(); ++it) {
            if (!layerHasRGB(ch, *it)) continue;
            Source s;
            s.kind = SRC_RGB; s.part = p; s.primary = *it;
            if (it->empty())                 s.score = beautyPart ? 150 : 100;
            else if (*it == "diffuse") {
                s.score = 90;
                if (layers.count("specular") && layerHasRGB(ch, "specular")) s.addLayer = "specular";
            }
            else if (isBeautyName(*it))      s.score = 80;
            else                             s.score = 10;
            if (s.score > best.score) best = s;              /* ties: first part / alphabetical */
        }
        if (luma.part < 0 && ch.findChannel("Y")) { luma.kind = SRC_LUMA; luma.part = p; }
        if (grey.part < 0 && ch.begin() != ch.end()) {
            grey.kind = SRC_GREY; grey.part = p; grey.primary = ch.begin().name();
        }
    }
    if (best.kind != SRC_NONE) return best;
    if (luma.kind != SRC_NONE) return luma;
    return grey;
}

/* Read up to four named channels of one part into planes (w*h each, data
   window order). Empty names / missing channels keep their fill value. */
static void readPlanes(MultiPartInputFile &mp, int partIdx, const Box2i &dw, int w, int h,
                       const std::string names[4], const float fills[4],
                       std::vector<float> planes[4])
{
    InputPart part(mp, partIdx);
    const ChannelList &ch = part.header().channels();
    FrameBuffer fb;
    for (int k = 0; k < 4; ++k) {
        planes[k].assign((size_t)w * h, fills[k]);
        if (names[k].empty() || !ch.findChannel(names[k])) continue;
        char *base = (char *)planes[k].data()
                   - ((size_t)dw.min.x + (size_t)dw.min.y * w) * sizeof(float);
        fb.insert(names[k], Slice(Imf::FLOAT, base, sizeof(float), sizeof(float) * w,
                                  1, 1, fills[k]));
    }
    part.setFrameBuffer(fb);
    part.readPixels(dw.min.y, dw.max.y);
}

struct FreeDeleter { void operator()(float *p) const { free(p); } };

extern "C" int exrbridge_is_exr(const char *filename)
{
    if (!filename) return 0;
    size_t n = strlen(filename);
    if (n >= 4) {
        const char *e = filename + n - 4;
        if ((e[0] == '.') &&
            (e[1] == 'e' || e[1] == 'E') &&
            (e[2] == 'x' || e[2] == 'X') &&
            (e[3] == 'r' || e[3] == 'R'))
            return 1;
    }
    /* Magic number 0x76, 0x2f, 0x31, 0x01 */
    FILE *fp = fopen(filename, "rb");
    if (fp) {
        unsigned char m[4] = {0};
        size_t got = fread(m, 1, 4, fp);
        fclose(fp);
        if (got == 4 && m[0] == 0x76 && m[1] == 0x2f && m[2] == 0x31 && m[3] == 0x01)
            return 1;
    }
    return 0;
}

extern "C" int exrbridge_load(const char *filename, ExrImage *out, char *err, int errlen)
{
    if (!out) return 0;
    try {
        MultiPartInputFile mp(filename);
        Source src = chooseSource(mp);
        if (src.kind == SRC_NONE) { if (err) snprintf(err, errlen, "no image channels (deep-only file?)"); return 0; }

        const Header &h = mp.header(src.part);
        Box2i dw = h.dataWindow();
        int w = dw.max.x - dw.min.x + 1;
        int h_ = dw.max.y - dw.min.y + 1;
        if (w <= 0 || h_ <= 0) { if (err) snprintf(err, errlen, "empty data window"); return 0; }

        /* Decode the chosen source into data-window-sized RGBA. */
        const size_t n = (size_t)w * h_;
        std::vector<float> data(n * 4);
        bool hasA = false;
        std::string desc;

        if (src.kind == SRC_LUMA && src.part == 0) {
            /* RgbaInputFile converts Y / RY / BY (incl. subsampled chroma) to RGB. */
            RgbaInputFile rf(filename);
            Array2D<Rgba> px(h_, w);
            rf.setFrameBuffer(&px[0][0] - (ptrdiff_t)dw.min.x - (ptrdiff_t)dw.min.y * w, 1, w);
            rf.readPixels(dw.min.y, dw.max.y);
            hasA = (rf.channels() & WRITE_A) != 0;
            for (int y = 0; y < h_; ++y)
                for (int x = 0; x < w; ++x) {
                    const Rgba &p = px[y][x];
                    float *d = &data[((size_t)y * w + x) * 4];
                    d[0] = p.r; d[1] = p.g; d[2] = p.b; d[3] = hasA ? (float)p.a : 1.0f;
                }
            desc = "luminance";
        } else {
            std::vector<float> pl[4];
            const float fills[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            std::string names[4];
            if (src.kind == SRC_RGB) {
                for (int k = 0; k < 4; ++k) names[k] = chanName(src.primary, "RGBA"[k]);
                hasA = h.channels().findChannel(names[3]) != 0;
                desc = src.primary.empty() ? "(default)" : src.primary;
            } else {   /* SRC_LUMA outside part 0 (grey from Y), or SRC_GREY */
                names[0] = (src.kind == SRC_LUMA) ? "Y" : src.primary;
                if (src.kind == SRC_LUMA && h.channels().findChannel("A")) { names[3] = "A"; hasA = true; }
                desc = names[0] + " (grey)";
            }
            readPlanes(mp, src.part, dw, w, h_, names, fills, pl);
            const bool grey = (src.kind != SRC_RGB);
            for (size_t i = 0; i < n; ++i) {
                data[i * 4 + 0] = pl[0][i];
                data[i * 4 + 1] = grey ? pl[0][i] : pl[1][i];
                data[i * 4 + 2] = grey ? pl[0][i] : pl[2][i];
                data[i * 4 + 3] = pl[3][i];
            }
            if (!src.addLayer.empty()) {
                std::string an[4];
                for (int k = 0; k < 3; ++k) an[k] = chanName(src.addLayer, "RGB"[k]);
                readPlanes(mp, src.part, dw, w, h_, an, fills, pl);
                for (size_t i = 0; i < n; ++i)
                    for (int k = 0; k < 3; ++k) data[i * 4 + k] += pl[k][i];   /* alpha: primary's */
                desc += "+" + src.addLayer;
            }
        }

        /* Place into the display window (pad or crop). An invalid display
           window falls back to the data window. */
        Box2i disp = h.displayWindow();
        if (disp.max.x < disp.min.x || disp.max.y < disp.min.y) disp = dw;
        const int W = disp.max.x - disp.min.x + 1;
        const int H = disp.max.y - disp.min.y + 1;
        std::unique_ptr<float, FreeDeleter> rgba((float *)calloc((size_t)W * H * 4, sizeof(float)));
        if (!rgba) { if (err) snprintf(err, errlen, "out of memory"); return 0; }
        const bool padded = dw.min.x > disp.min.x || dw.min.y > disp.min.y ||
                            dw.max.x < disp.max.x || dw.max.y < disp.max.y;
        if (padded && !hasA)
            for (size_t i = 0; i < (size_t)W * H; ++i) rgba.get()[i * 4 + 3] = 1.0f;
        const int x0 = dw.min.x > disp.min.x ? dw.min.x : disp.min.x;
        const int x1 = dw.max.x < disp.max.x ? dw.max.x : disp.max.x;
        const int y0 = dw.min.y > disp.min.y ? dw.min.y : disp.min.y;
        const int y1 = dw.max.y < disp.max.y ? dw.max.y : disp.max.y;
        if (x0 <= x1)
            for (int y = y0; y <= y1; ++y)
                memcpy(rgba.get() + (((size_t)(y - disp.min.y) * W) + (x0 - disp.min.x)) * 4,
                       &data[(((size_t)(y - dw.min.y) * w) + (x0 - dw.min.x)) * 4],
                       (size_t)(x1 - x0 + 1) * 4 * sizeof(float));

        out->width = W;
        out->height = H;
        out->has_alpha = hasA ? 1 : 0;
        out->rgba = rgba.release();
        snprintf(out->compression, sizeof(out->compression), "%s", compName(h.compression()));
        snprintf(out->layer, sizeof(out->layer), "%s", desc.c_str());
        return 1;
    } catch (const std::exception &e) {
        if (err) snprintf(err, errlen, "%s", e.what());
        return 0;
    } catch (...) {
        if (err) snprintf(err, errlen, "unknown OpenEXR error");
        return 0;
    }
}

extern "C" void exrbridge_free(ExrImage *img)
{
    if (img && img->rgba) {
        free(img->rgba);
        img->rgba = 0;
    }
}
