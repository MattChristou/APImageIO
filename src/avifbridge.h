/*
 * avifbridge.h - C boundary over libavif (AV1 still-image decode + encode).
 *
 * AVIF supports 8/10/12-bit and alpha. 8-bit files are handed back as RGBA8;
 * higher bit depths are handed back as normalised float RGBA so precision and
 * HDR range survive. The LightWave loader delivers RGBA32 (8-bit) or RGBAFP
 * (float) accordingly; the user selects the input colour space per-clip in the
 * Image Editor (the loader does not signal gamma/colour space).
 *
 * The whole libavif dependency is confined to avifbridge.c.
 */

#ifndef AVIFBRIDGE_H
#define AVIFBRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AvifImage_s {
    int             width;
    int             height;
    int             has_alpha;
    int             is_hdr;      /* 1 -> rgbaf valid (bit depth > 8); 0 -> rgba8 valid */
    unsigned char  *rgba8;       /* w*h*4 bytes, when !is_hdr */
    float          *rgbaf;       /* w*h*4 floats (0..1 normalised), when is_hdr */
} AvifImage;

/* Recognition: extension + ISOBMFF 'ftyp' brand sniff. */
int  avifbridge_is_avif(const char *filename);

/* Decode `filename` into `out` (caller-zeroed). Allocates one of rgba8/rgbaf.
   Returns 1 on success; 0 + message in err on failure. */
int  avifbridge_load(const char *filename, AvifImage *out, char *err, int errlen);

/* Free whichever buffer the image owns. */
void avifbridge_free(AvifImage *img);

/* Encode a top-row-first RGBA8 buffer (w*h*4) to an 8-bit AVIF.
   quality in [0,100] (100 = lossless). Returns 1 on success; 0 + err on fail.
   (HDR/high-bit-depth encode is a later enhancement.) */
int  avifbridge_save(const char *filename, const unsigned char *rgba,
                     int width, int height, int has_alpha,
                     int quality, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif /* AVIFBRIDGE_H */
