/*
 * pngbridge.c - 16-bit PNG decode via lodepng (C API), behind pngbridge.h.
 */

#include "pngbridge.h"

#include <lodepng.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read PNG IHDR bit depth (byte 24) and colour type (byte 25). Returns 1 if
   the file is a PNG and the header was read. */
static int read_ihdr(const char *filename, int *bitdepth, int *colortype)
{
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    unsigned char h[26];
    size_t got;
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;
    got = fread(h, 1, sizeof(h), fp);
    fclose(fp);
    if (got < sizeof(h)) return 0;
    if (memcmp(h, sig, 8) != 0) return 0;
    if (memcmp(h + 12, "IHDR", 4) != 0) return 0;
    *bitdepth  = h[24];
    *colortype = h[25];
    return 1;
}

int pngbridge_is_png16(const char *filename)
{
    int bd = 0, ct = 0;
    size_t n;
    if (!filename) return 0;
    n = strlen(filename);
    if (n >= 4) {
        const char *e = filename + n - 4;
        if (!(e[0] == '.' &&
              (e[1] | 32) == 'p' && (e[2] | 32) == 'n' && (e[3] | 32) == 'g'))
            return 0;
    } else {
        return 0;
    }
    if (!read_ihdr(filename, &bd, &ct)) return 0;
    return (bd == 16) ? 1 : 0;
}

int pngbridge_load16(const char *filename, PngImage *out, char *err, int errlen)
{
    int bd = 0, ct = 0;
    unsigned char *raw = NULL;
    unsigned w = 0, h = 0, code;
    float *dst;
    size_t i, count;

    if (!out) return 0;
    if (!read_ihdr(filename, &bd, &ct)) { if (err) snprintf(err, errlen, "not a PNG"); return 0; }

    /* Decode to 16-bit RGBA (big-endian, 2 bytes/channel). */
    code = lodepng_decode_file(&raw, &w, &h, filename, LCT_RGBA, 16);
    if (code) { if (err) snprintf(err, errlen, "lodepng: %s", lodepng_error_text(code));
                if (raw) free(raw); return 0; }
    if (!raw || w == 0 || h == 0) { if (raw) free(raw); if (err) snprintf(err, errlen, "empty image"); return 0; }

    count = (size_t)w * h * 4;
    dst = (float *)malloc(count * sizeof(float));
    if (!dst) { free(raw); if (err) snprintf(err, errlen, "out of memory"); return 0; }

    for (i = 0; i < count; i++) {
        unsigned hi = raw[i * 2 + 0];   /* big-endian */
        unsigned lo = raw[i * 2 + 1];
        dst[i] = (float)((hi << 8) | lo) / 65535.0f;
    }
    free(raw);

    out->width = (int)w;
    out->height = (int)h;
    out->has_alpha = (ct == 4 || ct == 6) ? 1 : 0;   /* 4=gray+alpha, 6=RGBA */
    out->rgba = dst;
    return 1;
}

void pngbridge_free(PngImage *img)
{
    if (img && img->rgba) { free(img->rgba); img->rgba = 0; }
}
