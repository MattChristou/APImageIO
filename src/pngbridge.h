/*
 * pngbridge.h - C boundary over lodepng, for 16-bit PNG only.
 *
 * LightWave's built-in PNG loader truncates 16-bit PNGs to 8-bit. We intercept
 * ONLY 16-bit PNGs (the loader returns NOREC otherwise, so the built-in keeps
 * handling 8-bit/palette/grayscale exactly as before) and deliver them as
 * normalised float RGBA to preserve the full precision. The user selects the
 * input colour space per-clip in the Image Editor.
 */

#ifndef PNGBRIDGE_H
#define PNGBRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PngImage_s {
    int     width;
    int     height;
    int     has_alpha;   /* from the source colour type */
    float  *rgba;        /* width*height*4 floats (0..1), row 0 = top */
} PngImage;

/* Returns 1 only for a PNG whose IHDR bit depth is 16 (the case we intercept). */
int  pngbridge_is_png16(const char *filename);

/* Decode a 16-bit PNG into `out` (caller-zeroed) as float RGBA.
   Returns 1 on success; 0 + message in err on failure. */
int  pngbridge_load16(const char *filename, PngImage *out, char *err, int errlen);

void pngbridge_free(PngImage *img);

#ifdef __cplusplus
}
#endif

#endif /* PNGBRIDGE_H */
