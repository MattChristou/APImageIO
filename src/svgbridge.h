/*
 * svgbridge.h - C boundary over lunasvg (SVG rasterization).
 *
 * SVG is vector, so loading means RASTERIZING at a pixel size. v1 renders at
 * the document's intrinsic size (from its width/height/viewBox), clamped to a
 * sane range. Output is straight 8-bit RGBA with alpha (SVG transparency).
 * Loader only - there is no raster->SVG saver.
 */

#ifndef SVGBRIDGE_H
#define SVGBRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvgImage_s {
    int             width;
    int             height;
    unsigned char  *rgba;       /* width*height*4, straight (un-premultiplied) RGBA, row 0 = top */
} SvgImage;

/* Recognition: .svg extension or '<svg' / '<?xml' sniff. */
int  svgbridge_is_svg(const char *filename);

/* Rasterize `filename` into `out` (caller-zeroed) at its intrinsic size.
   Returns 1 on success; 0 + message in err on failure. */
int  svgbridge_load(const char *filename, SvgImage *out, char *err, int errlen);

void svgbridge_free(SvgImage *img);

#ifdef __cplusplus
}
#endif

#endif /* SVGBRIDGE_H */
