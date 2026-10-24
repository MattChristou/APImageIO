/*
 * oiiobridge.cpp - OpenImageIO decode/encode behind oiiobridge.h.
 *
 * Uses the high-level ImageBuf API. 8-bit-native images come back as RGBA8;
 * anything deeper (16-bit / half / float) comes back as float RGBA. Channels
 * are normalised to RGB/RGBA (gray replicated, alpha defaulted opaque).
 */

#include "oiiobridge.h"

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace OIIO;

/* Extensions we want OIIO to own: the formats our dedicated loaders and LW's
   built-ins don't handle (or handle worse). Deliberately NOT: exr/webp/avif/
   png/svg (ours) or jpg/tga/bmp/gif (LW built-in / APAnimIO). */
static int ext_in_allowlist(const char *filename)
{
    static const char *exts[] = {
        "tif","tiff","dpx","cin","psd","hdr","jp2","j2k","j2c","jxl",
        "dds","sgi","rla","pic","tx","tex",
        /* native OIIO plugins (no extra deps): FITS / ICO / IFF / PNM / Zfile */
        "fits","fit","fts","ico","iff","pnm","ppm","pgm","pbm","zfile",
        /* camera RAW */
        "cr2","cr3","nef","arw","dng","raf","orf","rw2","pef","srw","3fr",
        "dcr","mrw","raw","x3f","nrw","srf","mos","kdc","iiq","erf","mef",
        NULL
    };
    const char *dot = strrchr(filename, '.');
    int i;
    char e[16];
    size_t n;
    if (!dot || !dot[1]) return 0;
    n = strlen(dot + 1);
    if (n >= sizeof(e)) return 0;
    for (i = 0; i < (int)n; i++) { char c = dot[1 + i]; e[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    e[n] = '\0';
    for (i = 0; exts[i]; i++) if (strcmp(e, exts[i]) == 0) return 1;
    return 0;
}

extern "C" int oiiobridge_can_load(const char *filename)
{
    return (filename && ext_in_allowlist(filename)) ? 1 : 0;
}

extern "C" void oiiobridge_configure(const OiioConfig *cfg)
{
    if (!cfg) return;
    try {
        /* 0 = auto in both our prefs and OIIO, so no special-casing. */
        OIIO::attribute("threads", cfg->threads);

        /* Shared cache: budget + automip. The whole-image load path below reads
           directly, but a cache-mediated read (and the future streaming texture
           node) picks these up from the same shared instance. */
        std::shared_ptr<ImageCache> ic = ImageCache::create(true);
        if (ic) {
            if (cfg->cache_mb > 0)
                ic->attribute("max_memory_MB", (float)cfg->cache_mb);
            ic->attribute("automip", cfg->automip ? 1 : 0);
        }
    } catch (...) {
        /* configuration is best-effort; a decode still proceeds with defaults */
    }
}

template <typename T>
static void expand_to_rgba(const T *src, int readch, T *dst, size_t npix, T amax)
{
    size_t i;
    for (i = 0; i < npix; i++) {
        const T *s = src + i * readch;
        T *d = dst + i * 4;
        if (readch == 1)      { d[0]=d[1]=d[2]=s[0]; d[3]=amax; }
        else if (readch == 2) { d[0]=d[1]=d[2]=s[0]; d[3]=s[1]; }
        else if (readch == 3) { d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=amax; }
        else                  { d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3]; }
    }
}

extern "C" int oiiobridge_load(const char *filename, const OiioConfig *cfg, OiioImage *out, char *err, int errlen)
{
    if (!out) return 0;
    try {
        /* Per-decode config spec. Camera-RAW readers honour these hints; every
           other reader ignores unknown attributes, so this is harmless to pass
           for non-RAW files. */
        ImageSpec config;
        if (cfg) {
            config.attribute("raw:use_camera_wb", cfg->raw_use_camera_wb ? 1 : 0);
            config.attribute("raw:auto_bright",   cfg->raw_auto_bright ? 1 : 0);
            config.attribute("raw:HighlightMode", cfg->raw_highlight_mode);
        }
        ImageBuf buf(filename, 0, 0, {}, cfg ? &config : nullptr);
        if (!buf.read()) {   /* read native format */
            std::string e = buf.geterror();
            if (err) snprintf(err, errlen, "%s", e.empty() ? "OIIO read failed" : e.c_str());
            return 0;
        }
        const ImageSpec &spec = buf.spec();
        int w = spec.width, h = spec.height, nch = spec.nchannels;
        if (w <= 0 || h <= 0 || nch <= 0) { if (err) snprintf(err, errlen, "empty image"); return 0; }

        int readch = nch < 4 ? nch : 4;
        int hdr = (spec.format != TypeDesc::UINT8) ? 1 : 0;
        size_t npix = (size_t)w * h;

        out->width = w; out->height = h;
        out->has_alpha = (readch == 4 || readch == 2) ? 1 : 0;
        out->is_hdr = hdr;

        ROI roi(0, w, 0, h, 0, 1, 0, readch);
        if (hdr) {
            std::vector<float> tmp(npix * readch);
            if (!buf.get_pixels(roi, TypeDesc::FLOAT, tmp.data())) { if (err) snprintf(err, errlen, "get_pixels"); return 0; }
            float *dst = (float *)malloc(npix * 4 * sizeof(float));
            if (!dst) { if (err) snprintf(err, errlen, "oom"); return 0; }
            expand_to_rgba<float>(tmp.data(), readch, dst, npix, 1.0f);
            out->rgbaf = dst; out->rgba8 = nullptr;
        } else {
            std::vector<unsigned char> tmp(npix * readch);
            if (!buf.get_pixels(roi, TypeDesc::UINT8, tmp.data())) { if (err) snprintf(err, errlen, "get_pixels"); return 0; }
            unsigned char *dst = (unsigned char *)malloc(npix * 4);
            if (!dst) { if (err) snprintf(err, errlen, "oom"); return 0; }
            expand_to_rgba<unsigned char>(tmp.data(), readch, dst, npix, 255);
            out->rgba8 = dst; out->rgbaf = nullptr;
        }
        return 1;
    } catch (const std::exception &e) {
        if (err) snprintf(err, errlen, "%s", e.what());
        return 0;
    } catch (...) {
        if (err) snprintf(err, errlen, "unknown OIIO error");
        return 0;
    }
}

extern "C" int oiiobridge_save_exr(const char *filename, const float *rgba, int width, int height,
                                   int nchan, int half, const char *compression, char *err, int errlen)
{
    if (!rgba || width <= 0 || height <= 0) { if (err) snprintf(err, errlen, "invalid image"); return 0; }
    if (nchan < 3) nchan = 3; if (nchan > 4) nchan = 4;
    try {
        /* EXR stores floating point: HALF (16-bit) or FLOAT (32-bit). */
        TypeDesc fmt = half ? TypeDesc::HALF : TypeDesc::FLOAT;
        ImageSpec spec(width, height, nchan, fmt);
        if (compression && compression[0])
            spec.attribute("compression", compression);   /* e.g. "zip", "dwaa:45" */
        ImageBuf buf(spec);
        ROI roi(0, width, 0, height, 0, 1, 0, nchan);
        if (!buf.set_pixels(roi, TypeDesc::FLOAT, rgba, 4 * sizeof(float))) {
            if (err) snprintf(err, errlen, "set_pixels"); return 0;
        }
        if (!buf.write(filename)) {                        /* .exr extension -> EXR */
            std::string e = buf.geterror();
            if (err) snprintf(err, errlen, "%s", e.empty() ? "OIIO EXR write failed" : e.c_str());
            return 0;
        }
        return 1;
    } catch (const std::exception &e) {
        if (err) snprintf(err, errlen, "%s", e.what());
        return 0;
    } catch (...) {
        if (err) snprintf(err, errlen, "unknown OIIO error");
        return 0;
    }
}

extern "C" int oiiobridge_save_exr_multi(const char *filename, int width, int height, int nchan,
                                         const char *const *channelNames, const float *interleaved,
                                         int half, const char *compression, char *err, int errlen)
{
    return oiiobridge_save_exr_multi_meta(filename, width, height, nchan, channelNames,
                                          interleaved, half, compression, nullptr, nullptr, 0,
                                          err, errlen);
}

extern "C" int oiiobridge_save_exr_multi_meta(const char *filename, int width, int height, int nchan,
                                              const char *const *channelNames, const float *interleaved,
                                              int half, const char *compression,
                                              const char *const *attrNames, const char *const *attrValues,
                                              int nattr, char *err, int errlen)
{
    std::vector<unsigned char> h((size_t)(nchan > 0 ? nchan : 0), half ? 1 : 0);
    return oiiobridge_save_exr_layers(filename, width, height, nchan, channelNames, interleaved,
                                      h.empty() ? nullptr : h.data(), compression,
                                      attrNames, attrValues, nattr, err, errlen);
}

extern "C" int oiiobridge_save_exr_layers(const char *filename, int width, int height, int nchan,
                                          const char *const *channelNames, const float *interleaved,
                                          const unsigned char *chanHalf, const char *compression,
                                          const char *const *attrNames, const char *const *attrValues,
                                          int nattr, char *err, int errlen)
{
    if (!interleaved || width <= 0 || height <= 0 || nchan <= 0) {
        if (err) snprintf(err, errlen, "invalid image"); return 0;
    }
    try {
        /* Per-channel storage type: OpenEXR stores each channel at its own
           precision, so data channels can stay 32-bit while colour is half. */
        int nhalf = 0;
        for (int c = 0; c < nchan; ++c) if (chanHalf && chanHalf[c]) ++nhalf;
        TypeDesc fmt = (nhalf == nchan) ? TypeDesc::HALF : TypeDesc::FLOAT;
        ImageSpec spec(width, height, nchan, fmt);
        if (nhalf > 0 && nhalf < nchan) {
            spec.channelformats.clear();
            for (int c = 0; c < nchan; ++c)
                spec.channelformats.push_back(chanHalf[c] ? TypeDesc::HALF : TypeDesc::FLOAT);
        }
        if (channelNames) {                       /* name the channels/layers */
            spec.channelnames.clear();
            for (int c = 0; c < nchan; ++c)
                spec.channelnames.push_back(channelNames[c] ? channelNames[c] : "");
        }
        if (compression && compression[0])
            spec.attribute("compression", compression);
        for (int a = 0; a < nattr; ++a)           /* string header attrs (crypto manifest) */
            if (attrNames && attrNames[a])
                spec.attribute(attrNames[a], attrValues && attrValues[a] ? attrValues[a] : "");

        auto out = ImageOutput::create(filename);
        if (!out) {
            std::string e = OIIO::geterror();
            if (err) snprintf(err, errlen, "%s", e.empty() ? "no EXR writer" : e.c_str());
            return 0;
        }
        /* write_image converts the float source to each channel's format */
        if (!out->open(filename, spec) ||
            !out->write_image(TypeDesc::FLOAT, interleaved) ||
            !out->close()) {
            std::string e = out->geterror();
            if (err) snprintf(err, errlen, "%s", e.empty() ? "OIIO EXR write failed" : e.c_str());
            return 0;
        }
        return 1;
    } catch (const std::exception &e) {
        if (err) snprintf(err, errlen, "%s", e.what());
        return 0;
    } catch (...) {
        if (err) snprintf(err, errlen, "unknown OIIO error");
        return 0;
    }
}

extern "C" void oiiobridge_free(OiioImage *img)
{
    if (!img) return;
    if (img->rgba8) { free(img->rgba8); img->rgba8 = nullptr; }
    if (img->rgbaf) { free(img->rgbaf); img->rgbaf = nullptr; }
}

extern "C" int oiiobridge_save(const char *filename, const float *rgba, int width, int height,
                               int nchan, int bitdepth, char *err, int errlen)
{
    if (!rgba || width <= 0 || height <= 0) { if (err) snprintf(err, errlen, "invalid image"); return 0; }
    if (nchan < 3) nchan = 3; if (nchan > 4) nchan = 4;
    try {
        TypeDesc fmt = (bitdepth >= 32) ? TypeDesc::FLOAT
                     : (bitdepth == 16) ? TypeDesc::UINT16 : TypeDesc::UINT8;
        ImageSpec spec(width, height, nchan, fmt);
        ImageBuf buf(spec);
        /* source is 4-wide RGBA float; write the first `nchan` channels. */
        ROI roi(0, width, 0, height, 0, 1, 0, nchan);
        if (!buf.set_pixels(roi, TypeDesc::FLOAT, rgba, 4 * sizeof(float))) {
            if (err) snprintf(err, errlen, "set_pixels"); return 0;
        }
        if (!buf.write(filename)) {
            std::string e = buf.geterror();
            if (err) snprintf(err, errlen, "%s", e.empty() ? "OIIO write failed" : e.c_str());
            return 0;
        }
        return 1;
    } catch (const std::exception &e) {
        if (err) snprintf(err, errlen, "%s", e.what());
        return 0;
    } catch (...) {
        if (err) snprintf(err, errlen, "unknown OIIO error");
        return 0;
    }
}
