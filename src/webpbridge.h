/*
 * webpbridge.h - C boundary over libwebp (decode + encode).
 *
 * WebP is 8-bit RGBA (lossy or lossless, optional alpha); there is no HDR.
 * The bridge hands back / accepts plain 8-bit RGBA buffers so the LightWave
 * loader/saver code never touches libwebp headers directly.
 */

#ifndef WEBPBRIDGE_H
#define WEBPBRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WebpImage_s {
    int             width;
    int             height;
    int             has_alpha;       /* 1 if the file carried an alpha channel */
    unsigned char  *rgba;            /* width*height*4 bytes, row 0 = top */
} WebpImage;

/* Recognition: extension + RIFF/WEBP magic. */
int  webpbridge_is_webp(const char *filename);

/* Decode `filename` into `out` (caller-zeroed). Allocates out->rgba.
   Returns 1 on success; 0 + message in err on failure. */
int  webpbridge_load(const char *filename, WebpImage *out, char *err, int errlen);

/* Free out->rgba. */
void webpbridge_free(WebpImage *img);

/* Encode a top-row-first RGBA8 buffer (width*height*4) to `filename`.
   lossless != 0 -> lossless encode (quality ignored); else lossy at
   quality in [0,100]. Returns 1 on success; 0 + message in err on failure. */
int  webpbridge_save(const char *filename, const unsigned char *rgba,
                     int width, int height, int has_alpha,
                     int lossless, float quality, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif /* WEBPBRIDGE_H */
