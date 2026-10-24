/*
 * krabridge.h - C boundary over minizip-ng + lodepng for Krita .kra and
 * OpenRaster .ora files.
 *
 * Both formats are ZIP containers holding a flattened composite of the whole
 * document at "mergedimage.png" (mandated by the OpenRaster spec; Krita writes
 * it into .kra as well). We unzip that one member into memory and decode it
 * with the lodepng already linked for pngbridge, so LightWave can use an
 * artist's *working* file as a texture with no export step.
 *
 * Krita also stores a small "preview.png" thumbnail. We deliberately never
 * fall back to it - handing back a 256px thumbnail as if it were the texture
 * would fail silently and look like a resolution bug.
 */

#ifndef KRABRIDGE_H
#define KRABRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KraImage_s {
    int             width;
    int             height;
    int             has_alpha;
    int             is_float;    /* 1 when the source was 16-bit: use rgbaf */
    unsigned char  *rgba8;       /* w*h*4 bytes, when is_float == 0 */
    float          *rgbaf;       /* w*h*4 floats (0..1), when is_float == 1 */
} KraImage;

/* Returns 1 for a file named *.kra or *.ora whose first bytes are a ZIP
   signature. Cheap enough to run on every candidate the loader is offered. */
int  krabridge_is_kra(const char *filename);

/* Extract and decode mergedimage.png into `out` (caller-zeroed).
   Returns 1 on success; 0 + message in err on failure. */
int  krabridge_load(const char *filename, KraImage *out, char *err, int errlen);

void krabridge_free(KraImage *img);

#ifdef __cplusplus
}
#endif

#endif /* KRABRIDGE_H */
