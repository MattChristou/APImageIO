/*
 * avifbridge.c - libavif (AV1 still image) decode/encode behind avifbridge.h.
 *
 * libavif is C, so this is plain C, linked statically (/MT) with dav1d (decode)
 * and aom (encode).
 */

#include "avifbridge.h"

#include <avif/avif.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int avifbridge_is_avif(const char *filename)
{
    size_t n;
    FILE *fp;
    unsigned char hdr[16];
    size_t got;

    if (!filename) return 0;
    n = strlen(filename);
    if (n >= 5) {
        const char *e = filename + n - 5;
        if (e[0] == '.' &&
            (e[1] == 'a' || e[1] == 'A') &&
            (e[2] == 'v' || e[2] == 'V') &&
            (e[3] == 'i' || e[3] == 'I') &&
            (e[4] == 'f' || e[4] == 'F'))
            return 1;
    }
    /* ISOBMFF: bytes 4..7 == "ftyp", brand at 8..11 contains "avif"/"avis". */
    fp = fopen(filename, "rb");
    if (fp) {
        got = fread(hdr, 1, 16, fp);
        fclose(fp);
        if (got == 16 && memcmp(hdr + 4, "ftyp", 4) == 0 &&
            (memcmp(hdr + 8, "avif", 4) == 0 || memcmp(hdr + 8, "avis", 4) == 0))
            return 1;
    }
    return 0;
}

int avifbridge_load(const char *filename, AvifImage *out, char *err, int errlen)
{
    avifDecoder *decoder;
    avifImage *image;
    avifRGBImage rgb;
    avifResult r;
    int w, h, hdr, has_alpha;
    int y;

    if (!out) return 0;

    decoder = avifDecoderCreate();
    if (!decoder) { if (err) snprintf(err, errlen, "decoder alloc failed"); return 0; }

    image = avifImageCreateEmpty();
    if (!image) { avifDecoderDestroy(decoder); if (err) snprintf(err, errlen, "image alloc failed"); return 0; }

    r = avifDecoderReadFile(decoder, image, filename);
    if (r != AVIF_RESULT_OK) {
        if (err) snprintf(err, errlen, "decode: %s", avifResultToString(r));
        avifImageDestroy(image);
        avifDecoderDestroy(decoder);
        return 0;
    }

    w = (int)image->width;
    h = (int)image->height;
    hdr = (image->depth > 8) ? 1 : 0;
    has_alpha = (image->alphaPlane != NULL) ? 1 : 0;

    memset(&rgb, 0, sizeof(rgb));
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth  = hdr ? 16 : 8;
    rgb.isFloat = AVIF_FALSE;
    r = avifRGBImageAllocatePixels(&rgb);
    if (r != AVIF_RESULT_OK) {
        if (err) snprintf(err, errlen, "rgb alloc: %s", avifResultToString(r));
        avifImageDestroy(image);
        avifDecoderDestroy(decoder);
        return 0;
    }
    r = avifImageYUVToRGB(image, &rgb);
    if (r != AVIF_RESULT_OK) {
        if (err) snprintf(err, errlen, "yuv->rgb: %s", avifResultToString(r));
        avifRGBImageFreePixels(&rgb);
        avifImageDestroy(image);
        avifDecoderDestroy(decoder);
        return 0;
    }

    out->width = w;
    out->height = h;
    out->has_alpha = has_alpha;
    out->is_hdr = hdr;

    if (hdr) {
        float *dst = (float *)malloc((size_t)w * h * 4 * sizeof(float));
        if (!dst) { avifRGBImageFreePixels(&rgb); avifImageDestroy(image); avifDecoderDestroy(decoder);
                    if (err) snprintf(err, errlen, "out of memory"); return 0; }
        for (y = 0; y < h; y++) {
            const unsigned short *src = (const unsigned short *)(rgb.pixels + (size_t)y * rgb.rowBytes);
            float *drow = dst + (size_t)y * w * 4;
            int i, count = w * 4;
            for (i = 0; i < count; i++) drow[i] = (float)src[i] / 65535.0f;
        }
        out->rgbaf = dst;
        out->rgba8 = NULL;
    } else {
        unsigned char *dst = (unsigned char *)malloc((size_t)w * h * 4);
        if (!dst) { avifRGBImageFreePixels(&rgb); avifImageDestroy(image); avifDecoderDestroy(decoder);
                    if (err) snprintf(err, errlen, "out of memory"); return 0; }
        for (y = 0; y < h; y++)
            memcpy(dst + (size_t)y * w * 4, rgb.pixels + (size_t)y * rgb.rowBytes, (size_t)w * 4);
        out->rgba8 = dst;
        out->rgbaf = NULL;
    }

    avifRGBImageFreePixels(&rgb);
    avifImageDestroy(image);
    avifDecoderDestroy(decoder);
    return 1;
}

void avifbridge_free(AvifImage *img)
{
    if (!img) return;
    if (img->rgba8) { free(img->rgba8); img->rgba8 = NULL; }
    if (img->rgbaf) { free(img->rgbaf); img->rgbaf = NULL; }
}

int avifbridge_save(const char *filename, const unsigned char *rgba,
                    int width, int height, int has_alpha,
                    int quality, char *err, int errlen)
{
    avifImage *image;
    avifRGBImage rgb;
    avifEncoder *encoder;
    avifRWData output = AVIF_DATA_EMPTY;
    avifResult r;
    FILE *fp;
    (void)has_alpha;

    if (!rgba || width <= 0 || height <= 0) { if (err) snprintf(err, errlen, "invalid image"); return 0; }

    image = avifImageCreate((uint32_t)width, (uint32_t)height, 8, AVIF_PIXEL_FORMAT_YUV444);
    if (!image) { if (err) snprintf(err, errlen, "image alloc failed"); return 0; }

    /* Mathematically lossless requires the identity matrix (RGB stored directly). */
    if (quality >= AVIF_QUALITY_LOSSLESS)
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;

    memset(&rgb, 0, sizeof(rgb));
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format   = AVIF_RGB_FORMAT_RGBA;
    rgb.depth    = 8;
    rgb.pixels   = (uint8_t *)rgba;
    rgb.rowBytes = (uint32_t)(width * 4);

    r = avifImageRGBToYUV(image, &rgb);
    if (r != AVIF_RESULT_OK) {
        if (err) snprintf(err, errlen, "rgb->yuv: %s", avifResultToString(r));
        avifImageDestroy(image);
        return 0;
    }

    encoder = avifEncoderCreate();
    if (!encoder) { avifImageDestroy(image); if (err) snprintf(err, errlen, "encoder alloc failed"); return 0; }
    encoder->quality = quality;
    encoder->speed   = 6;

    r = avifEncoderWrite(encoder, image, &output);
    if (r != AVIF_RESULT_OK) {
        if (err) snprintf(err, errlen, "encode: %s", avifResultToString(r));
        avifEncoderDestroy(encoder);
        avifImageDestroy(image);
        return 0;
    }

    fp = fopen(filename, "wb");
    if (!fp) {
        avifRWDataFree(&output);
        avifEncoderDestroy(encoder);
        avifImageDestroy(image);
        if (err) snprintf(err, errlen, "cannot open output file");
        return 0;
    }
    {
        /* Both checks matter: a short fwrite is a full disk, and fclose is
           where buffered data is flushed (and where a network share fails). A
           failed write leaves no truncated file behind to be mistaken for a
           good frame. */
        int ok = fwrite(output.data, 1, output.size, fp) == output.size;
        if (fclose(fp) != 0) ok = 0;
        avifRWDataFree(&output);
        avifEncoderDestroy(encoder);
        avifImageDestroy(image);
        if (!ok) {
            remove(filename);
            if (err) snprintf(err, errlen, "write failed (disk full or path unavailable?)");
            return 0;
        }
    }
    return 1;
}
