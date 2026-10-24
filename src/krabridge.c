/*
 * krabridge.c - .kra / .ora decode: unzip mergedimage.png, decode via lodepng.
 */

#include "krabridge.h"

#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
#include <lodepng.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KRA_MERGED "mergedimage.png"

static int has_ext(const char *filename, const char *ext3)
{
    size_t n = filename ? strlen(filename) : 0;
    const char *e;
    if (n < 4) return 0;
    e = filename + n - 4;
    return e[0] == '.' &&
           (e[1] | 32) == ext3[0] && (e[2] | 32) == ext3[1] && (e[3] | 32) == ext3[2];
}

/* PK\003\004 - a local file header at offset 0. */
static int is_zip(const char *filename)
{
    unsigned char sig[4];
    size_t got;
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;
    got = fread(sig, 1, sizeof(sig), fp);
    fclose(fp);
    return got == sizeof(sig) &&
           sig[0] == 'P' && sig[1] == 'K' && sig[2] == 0x03 && sig[3] == 0x04;
}

int krabridge_is_kra(const char *filename)
{
    if (!filename) return 0;
    if (!has_ext(filename, "kra") && !has_ext(filename, "ora")) return 0;
    return is_zip(filename);
}

/* Read a member into a freshly malloc'd buffer. Caller frees. */
static unsigned char *read_member(const char *filename, const char *member,
                                  size_t *out_len, char *err, int errlen)
{
    void *reader = NULL;
    unsigned char *buf = NULL;
    int32_t len = 0, rc;

    *out_len = 0;

    reader = mz_zip_reader_create();
    if (!reader) { if (err) snprintf(err, errlen, "zip reader alloc failed"); return NULL; }

    if (mz_zip_reader_open_file(reader, filename) != MZ_OK) {
        if (err) snprintf(err, errlen, "not a readable ZIP container");
        goto fail;
    }
    if (mz_zip_reader_locate_entry(reader, member, 1) != MZ_OK) {
        if (err) snprintf(err, errlen, "no %s inside (re-save from Krita with the merged image enabled)", member);
        goto fail;
    }

    len = mz_zip_reader_entry_save_buffer_length(reader);
    if (len <= 0) {
        mz_zip_file *info = NULL;
        if (mz_zip_reader_entry_get_info(reader, &info) == MZ_OK && info &&
            info->uncompressed_size > 0 && info->uncompressed_size < 0x7FFFFFFF)
            len = (int32_t)info->uncompressed_size;
    }
    if (len <= 0) { if (err) snprintf(err, errlen, "%s is empty", member); goto fail; }

    buf = (unsigned char *)malloc((size_t)len);
    if (!buf) { if (err) snprintf(err, errlen, "out of memory"); goto fail; }

    rc = mz_zip_reader_entry_save_buffer(reader, buf, len);
    if (rc != MZ_OK) {
        if (err) snprintf(err, errlen, "failed to inflate %s (mz %d)", member, (int)rc);
        free(buf); buf = NULL;
        goto fail;
    }

    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    *out_len = (size_t)len;
    return buf;

fail:
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    return NULL;
}

/* IHDR bit depth (byte 24) and colour type (byte 25) of an in-memory PNG. */
static int png_ihdr(const unsigned char *png, size_t len, int *bitdepth, int *colortype)
{
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (len < 26) return 0;
    if (memcmp(png, sig, 8) != 0) return 0;
    if (memcmp(png + 12, "IHDR", 4) != 0) return 0;
    *bitdepth  = png[24];
    *colortype = png[25];
    return 1;
}

int krabridge_load(const char *filename, KraImage *out, char *err, int errlen)
{
    unsigned char *png = NULL, *raw = NULL;
    size_t png_len = 0;
    int bd = 0, ct = 0;
    unsigned w = 0, h = 0, code;

    if (!out) return 0;

    png = read_member(filename, KRA_MERGED, &png_len, err, errlen);
    if (!png) return 0;

    if (!png_ihdr(png, png_len, &bd, &ct)) {
        if (err) snprintf(err, errlen, "%s is not a PNG", KRA_MERGED);
        free(png);
        return 0;
    }

    /* 8-bit documents stay 8-bit so LightWave's colour-space handling matches
       an ordinary PNG; only true 16-bit documents are promoted to float. */
    code = lodepng_decode_memory(&raw, &w, &h, png, png_len, LCT_RGBA, bd == 16 ? 16 : 8);
    free(png);
    if (code) {
        if (err) snprintf(err, errlen, "lodepng: %s", lodepng_error_text(code));
        if (raw) free(raw);
        return 0;
    }
    if (!raw || w == 0 || h == 0) {
        if (raw) free(raw);
        if (err) snprintf(err, errlen, "empty image");
        return 0;
    }

    out->width     = (int)w;
    out->height    = (int)h;
    out->has_alpha = (ct == 4 || ct == 6) ? 1 : 0;   /* 4=gray+alpha, 6=RGBA */

    if (bd == 16) {
        size_t i, count = (size_t)w * h * 4;
        float *dst = (float *)malloc(count * sizeof(float));
        if (!dst) { free(raw); if (err) snprintf(err, errlen, "out of memory"); return 0; }
        for (i = 0; i < count; i++) {
            unsigned hi = raw[i * 2 + 0];   /* big-endian */
            unsigned lo = raw[i * 2 + 1];
            dst[i] = (float)((hi << 8) | lo) / 65535.0f;
        }
        free(raw);
        out->is_float = 1;
        out->rgbaf    = dst;
    } else {
        out->is_float = 0;
        out->rgba8    = raw;
    }
    return 1;
}

void krabridge_free(KraImage *img)
{
    if (!img) return;
    if (img->rgba8) { free(img->rgba8); img->rgba8 = NULL; }
    if (img->rgbaf) { free(img->rgbaf); img->rgbaf = NULL; }
}
