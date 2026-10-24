/*
 * oiiobridge.h - C boundary over OpenImageIO (catch-all still-image I/O).
 *
 * Confines all OIIO use to oiiobridge.cpp. The LightWave loader/saver talk
 * only to this header. OIIO covers the formats our dedicated loaders don't:
 * TIFF, DPX, Cineon, PSD, HDR/RGBE, JPEG-2000, Targa, DDS, BMP, SGI, JPEG XL,
 * camera RAW, etc. 8-bit-native images come back as RGBA8; higher bit depth /
 * float come back as float (HDR-capable).
 */

#ifndef OIIOBRIDGE_H
#define OIIOBRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OiioImage_s {
    int             width;
    int             height;
    int             has_alpha;
    int             is_hdr;      /* 1 -> rgbaf valid (>8-bit/float); 0 -> rgba8 valid */
    unsigned char  *rgba8;       /* w*h*4 bytes, when !is_hdr */
    float          *rgbaf;       /* w*h*4 floats, when is_hdr */
} OiioImage;

/* Engine + per-decode settings, sourced from the APImageIO preferences. The
   bridge speaks its own vocabulary here (no dependency on the prefs module);
   the loader/saver translate APImageIOPrefs into this. */
typedef struct OiioConfig_s {
    int cache_mb;            /* shared ImageCache RAM budget, MB (<=0 = leave OIIO default) */
    int threads;            /* OIIO worker threads (0 = auto/all cores)                    */
    int automip;            /* shared ImageCache: generate mips on the fly (bool)          */
    int raw_use_camera_wb;  /* camera RAW: apply the camera's white balance (bool)         */
    int raw_auto_bright;    /* camera RAW: auto-scale exposure (bool)                      */
    int raw_highlight_mode; /* camera RAW: clipped-highlight mode (0..3)                   */
} OiioConfig;

/* Apply the process-wide engine settings (thread count, shared ImageCache
   budget + automip). Idempotent and best-effort; safe with a NULL cfg. */
void oiiobridge_configure(const OiioConfig *cfg);

/* 1 if `filename`'s extension is one we want OIIO to handle (the formats our
   dedicated loaders/the built-ins don't). */
int  oiiobridge_can_load(const char *filename);

/* Decode `filename` into `out` (caller-zeroed). `cfg` supplies per-decode hints
   (camera-RAW options); may be NULL. Allocates one of rgba8/rgbaf. Returns 1 on
   success; 0 + message in err on failure. */
int  oiiobridge_load(const char *filename, const OiioConfig *cfg, OiioImage *out, char *err, int errlen);

void oiiobridge_free(OiioImage *img);

/* Save a float RGBA buffer (w*h*4) to `filename`. OIIO picks the format from
   the extension; `bitdepth` hints the on-disk type (8/16/32; 32 = float, for
   formats that support it). `nchan` = 3 or 4. Returns 1 on success. */
int  oiiobridge_save(const char *filename, const float *rgba, int width, int height,
                     int nchan, int bitdepth, char *err, int errlen);

/* Save a float RGBA buffer (w*h*4) as OpenEXR. `half` != 0 writes 16-bit half,
   else 32-bit float. `compression` is an OIIO EXR compression name ("none",
   "rle", "zips", "zip", "piz", "pxr24", "b44", "b44a", "dwaa", "dwab"; may add
   ":level", e.g. "dwaa:45"); NULL/empty leaves the OIIO default. `nchan` = 3/4.
   Returns 1 on success. */
int  oiiobridge_save_exr(const char *filename, const float *rgba, int width, int height,
                         int nchan, int half, const char *compression, char *err, int errlen);

/* Save a MULTI-CHANNEL / multilayer EXR. `interleaved` is width*height*nchan
   floats (channel-interleaved per pixel); `channelNames[nchan]` names each
   channel (use "Layer.R"/"Layer.G"/... so comp apps see named layers). `half`,
   `compression` as above. Returns 1 on success. */
int  oiiobridge_save_exr_multi(const char *filename, int width, int height, int nchan,
                               const char *const *channelNames, const float *interleaved,
                               int half, const char *compression, char *err, int errlen);

/* As oiiobridge_save_exr_multi, plus `nattr` string header attributes
 * (attrNames[i] -> attrValues[i]) -- used to write the Cryptomatte manifest
 * metadata ("cryptomatte/<key>/name", ".../manifest", etc.). Attribute args may
 * be NULL/0 (then identical to oiiobridge_save_exr_multi). Returns 1 on success. */
int  oiiobridge_save_exr_multi_meta(const char *filename, int width, int height, int nchan,
                                    const char *const *channelNames, const float *interleaved,
                                    int half, const char *compression,
                                    const char *const *attrNames, const char *const *attrValues,
                                    int nattr, char *err, int errlen);

/* As oiiobridge_save_exr_multi_meta, but with a per-channel storage type:
 * chanHalf[c] != 0 stores channel c as 16-bit half, else 32-bit float
 * (chanHalf NULL = all float). Lets data AOVs (depth, ids, positions) stay
 * exact while colour AOVs are written half. Returns 1 on success. */
int  oiiobridge_save_exr_layers(const char *filename, int width, int height, int nchan,
                                const char *const *channelNames, const float *interleaved,
                                const unsigned char *chanHalf, const char *compression,
                                const char *const *attrNames, const char *const *attrValues,
                                int nattr, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif /* OIIOBRIDGE_H */
