/*
 * svgbridge.cpp - SVG rasterization via lunasvg (3.x), behind svgbridge.h.
 *
 * lunasvg is C++ (renders through plutovg), so this is C++ compiled /MT and
 * linked statically. Renders at the document's intrinsic size, clamped.
 */

#include "svgbridge.h"

#include <lunasvg/lunasvg.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#define SVG_MAX_DIM 8192
#define SVG_DEFAULT_DIM 1024

extern "C" int svgbridge_is_svg(const char *filename)
{
    if (!filename) return 0;
    size_t n = strlen(filename);
    if (n >= 4) {
        const char *e = filename + n - 4;
        if (e[0] == '.' &&
            (e[1] == 's' || e[1] == 'S') &&
            (e[2] == 'v' || e[2] == 'V') &&
            (e[3] == 'g' || e[3] == 'G'))
            return 1;
    }
    /* Content sniff: look for "<svg" in the first chunk. */
    FILE *fp = fopen(filename, "rb");
    if (fp) {
        char buf[513];
        size_t got = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        buf[got] = '\0';
        if (strstr(buf, "<svg") || strstr(buf, "<SVG"))
            return 1;
    }
    return 0;
}

extern "C" int svgbridge_load(const char *filename, SvgImage *out, char *err, int errlen)
{
    if (!out) return 0;
    try {
        auto document = lunasvg::Document::loadFromFile(filename);
        if (!document) {
            if (err) snprintf(err, errlen, "failed to parse SVG");
            return 0;
        }

        /* Size in double and check before converting: a huge or NaN size cast
           straight to int is undefined (MSVC gives INT_MIN). Clamp by ONE
           factor - renderToBitmap scales each axis independently, so clamping
           w and h separately would squash a wide or tall document. */
        double dw = document->width(), dh = document->height();
        int w, h;
        if (!std::isfinite(dw) || !std::isfinite(dh) || dw <= 0.0 || dh <= 0.0) {
            w = SVG_DEFAULT_DIM; h = SVG_DEFAULT_DIM;
        } else {
            double longest = dw > dh ? dw : dh;
            double s = longest > SVG_MAX_DIM ? SVG_MAX_DIM / longest : 1.0;
            w = (int)std::ceil(dw * s);
            h = (int)std::ceil(dh * s);
            if (w < 1) w = 1; else if (w > SVG_MAX_DIM) w = SVG_MAX_DIM;   /* fp round-up */
            if (h < 1) h = 1; else if (h > SVG_MAX_DIM) h = SVG_MAX_DIM;
        }

        lunasvg::Bitmap bitmap = document->renderToBitmap(w, h);
        if (!bitmap.data() || bitmap.width() <= 0 || bitmap.height() <= 0) {
            if (err) snprintf(err, errlen, "SVG render produced no pixels");
            return 0;
        }
        bitmap.convertToRGBA();  /* premultiplied plutovg surface -> straight RGBA */

        int bw = bitmap.width();
        int bh = bitmap.height();
        int stride = bitmap.stride();
        const unsigned char *src = bitmap.data();

        unsigned char *dst = (unsigned char *)malloc((size_t)bw * bh * 4);
        if (!dst) { if (err) snprintf(err, errlen, "out of memory"); return 0; }
        for (int y = 0; y < bh; y++)
            memcpy(dst + (size_t)y * bw * 4, src + (size_t)y * stride, (size_t)bw * 4);

        out->width = bw;
        out->height = bh;
        out->rgba = dst;
        return 1;
    } catch (const std::exception &e) {
        if (err) snprintf(err, errlen, "%s", e.what());
        return 0;
    } catch (...) {
        if (err) snprintf(err, errlen, "unknown SVG error");
        return 0;
    }
}

extern "C" void svgbridge_free(SvgImage *img)
{
    if (img && img->rgba) {
        free(img->rgba);
        img->rgba = 0;
    }
}
