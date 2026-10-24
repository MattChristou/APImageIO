/*
 * webpbridge.c - libwebp decode/encode behind the webpbridge.h C boundary.
 *
 * libwebp is a C library, so this is plain C and links statically (/MT)
 * alongside the rest of APImageIO.
 */

#include "webpbridge.h"

#include <webp/decode.h>
#include <webp/encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read an entire file into a malloc'd buffer. Caller frees. */
static unsigned char *read_file(const char *filename, size_t *out_size)
{
    FILE *fp = fopen(filename, "rb");
    unsigned char *buf;
    long sz;
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return 0; }
    rewind(fp);
    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 0; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); fclose(fp); return 0; }
    fclose(fp);
    *out_size = (size_t)sz;
    return buf;
}

int webpbridge_is_webp(const char *filename)
{
    size_t n;
    FILE *fp;
    unsigned char m[12];
    size_t got;

    if (!filename) return 0;
    n = strlen(filename);
    if (n >= 5) {
        const char *e = filename + n - 5;
        if (e[0] == '.' &&
            (e[1] == 'w' || e[1] == 'W') &&
            (e[2] == 'e' || e[2] == 'E') &&
            (e[3] == 'b' || e[3] == 'B') &&
            (e[4] == 'p' || e[4] == 'P'))
            return 1;
    }
    /* RIFF....WEBP */
    fp = fopen(filename, "rb");
    if (fp) {
        got = fread(m, 1, 12, fp);
        fclose(fp);
        if (got == 12 &&
            m[0] == 'R' && m[1] == 'I' && m[2] == 'F' && m[3] == 'F' &&
            m[8] == 'W' && m[9] == 'E' && m[10] == 'B' && m[11] == 'P')
            return 1;
    }
    return 0;
}

int webpbridge_load(const char *filename, WebpImage *out, char *err, int errlen)
{
    size_t size = 0;
    unsigned char *data;
    WebPBitstreamFeatures feat;
    int w = 0, h = 0;
    unsigned char *rgba;

    if (!out) return 0;
    data = read_file(filename, &size);
    if (!data) { if (err) snprintf(err, errlen, "cannot read file"); return 0; }

    memset(&feat, 0, sizeof(feat));
    if (WebPGetFeatures(data, size, &feat) != VP8_STATUS_OK) {
        free(data);
        if (err) snprintf(err, errlen, "not a valid WebP bitstream");
        return 0;
    }

    rgba = WebPDecodeRGBA(data, size, &w, &h);
    free(data);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) WebPFree(rgba);
        if (err) snprintf(err, errlen, "WebP decode failed");
        return 0;
    }

    out->width = w;
    out->height = h;
    out->has_alpha = feat.has_alpha ? 1 : 0;
    out->rgba = rgba;   /* freed via webpbridge_free -> WebPFree */
    return 1;
}

void webpbridge_free(WebpImage *img)
{
    if (img && img->rgba) {
        WebPFree(img->rgba);
        img->rgba = 0;
    }
}

int webpbridge_save(const char *filename, const unsigned char *rgba,
                    int width, int height, int has_alpha,
                    int lossless, float quality, char *err, int errlen)
{
    uint8_t *output = 0;
    size_t out_size = 0;
    const int stride = width * 4;
    FILE *fp;
    (void)has_alpha;  /* always encode RGBA; alpha is 255 where unused */

    if (!rgba || width <= 0 || height <= 0) {
        if (err) snprintf(err, errlen, "invalid image");
        return 0;
    }

    if (lossless)
        out_size = WebPEncodeLosslessRGBA(rgba, width, height, stride, &output);
    else
        out_size = WebPEncodeRGBA(rgba, width, height, stride, quality, &output);

    if (out_size == 0 || !output) {
        if (output) WebPFree(output);
        if (err) snprintf(err, errlen, "WebP encode failed");
        return 0;
    }

    fp = fopen(filename, "wb");
    if (!fp) {
        WebPFree(output);
        if (err) snprintf(err, errlen, "cannot open output file");
        return 0;
    }
    {
        /* fclose flushes the buffer, so its failure is a failed write too. */
        int ok = fwrite(output, 1, out_size, fp) == out_size;
        if (fclose(fp) != 0) ok = 0;
        WebPFree(output);
        if (!ok) {
            remove(filename);
            if (err) snprintf(err, errlen, "write failed (disk full or path unavailable?)");
            return 0;
        }
    }
    return 1;
}
