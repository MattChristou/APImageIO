/*
 * exrbridge.h - Clean C boundary over OpenEXRCore.
 *
 * The whole OpenEXR dependency is confined to exrbridge.c (compiled against
 * OpenEXRCore). The LightWave-facing ImageLoader code talks only to this
 * header, so the SDK plugin code stays free of any EXR/Imath headers.
 *
 * The bridge resolves a possibly multi-layer EXR down to a single RGBA(F)
 * image per the layer policy documented in exrbridge.c, decoding any standard
 * OpenEXR compression (incl. DWAA/DWAB, which LightWave's stock loader can't).
 */

#ifndef EXRBRIDGE_H
#define EXRBRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExrImage_s {
    int     width;
    int     height;
    int     has_alpha;          /* 1 if an alpha layer was resolved */
    float  *rgba;               /* width*height*4 floats, scene-linear, row 0 = top */
    char    compression[32];    /* e.g. "dwaa", "zip", "piz" - for diagnostics */
    char    layer[64];          /* resolved layer/source description */
} ExrImage;

/* Cheap recognition: extension + EXR magic bytes. Returns 1 if this looks like
   an EXR we should claim (so the loader can return IPSTAT_NOREC otherwise). */
int  exrbridge_is_exr(const char *filename);

/* Decode `filename` into `out` (caller-zeroed). Allocates out->rgba.
   Returns 1 on success; on failure returns 0 and writes a message to err. */
int  exrbridge_load(const char *filename, ExrImage *out, char *err, int errlen);

/* Free buffers owned by an ExrImage filled by exrbridge_load. */
void exrbridge_free(ExrImage *img);

#ifdef __cplusplus
}
#endif

#endif /* EXRBRIDGE_H */
