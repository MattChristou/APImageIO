/*
 * ap_prefs.h - APImageIO global preferences, published into LightWave's
 * unified Preferences panel via LWPreferenceSystem.
 *
 * These are app-level OIIO engine settings shared by every OIIO-backed
 * loader/saver (and the future OIIO texture-streaming node): cache budget,
 * thread count, auto-mip, assumed input colour space, camera-RAW decode
 * options, plus a fallback OCIO config path. Per-render OCIO display/view/look
 * choices deliberately do NOT live here - those stay on the ToneMapperPro /
 * node XPanel where they belong (they're instance state, not app state).
 *
 * Branch is a single flat plain-path category "/APImageIO/" that sits under
 * Layout as a sibling of File Formats / Defaults / Paths. Publish uses the
 * plain path; reads use the SAME plain path (see the path-form gotcha in the
 * PreferenceSystem notes - $/ and /Layout/ forms are rejected on publish).
 */

#ifndef AP_PREFS_H
#define AP_PREFS_H

#include <lwserver.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APPREFS_BRANCH "/APImageIO/"

/* Assumed input colour space, indexes into "sRGB|Linear|ACEScg|Rec.709|Raw". */
enum {
    APCS_SRGB = 0,
    APCS_LINEAR,
    APCS_ACESCG,
    APCS_REC709,
    APCS_RAW
};

/* Camera-RAW clipped-highlight handling, "Clip|Unclip|Blend|Rebuild". */
enum {
    APRAW_HL_CLIP = 0,
    APRAW_HL_UNCLIP,
    APRAW_HL_BLEND,
    APRAW_HL_REBUILD
};

/* EXR compression, indexes into "ZIP|PIZ|RLE|PXR24|ZIPS|DWAA|DWAB|B44|B44A|None". */
enum {
    APEXR_ZIP = 0,
    APEXR_PIZ,
    APEXR_RLE,
    APEXR_PXR24,
    APEXR_ZIPS,
    APEXR_DWAA,
    APEXR_DWAB,
    APEXR_B44,
    APEXR_B44A,
    APEXR_NONE
};

typedef struct APImageIOPrefs_s {
    int  cache_mb;           /* ImageCache RAM budget, MB          -> OIIO "IC:max_memory_MB" */
    int  threads;            /* decode worker threads, 0 = auto    -> OIIO "threads"          */
    int  automip;            /* generate mips on the fly (bool)    -> TextureSystem "automip" */
    int  streaming;          /* sample through the cache (bool)    -> texture-node toggle     */
    int  input_colorspace;   /* APCS_* for images with no profile                             */
    int  raw_use_camera_wb;  /* bool -> OIIO input cfg "raw:use_camera_wb"                     */
    int  raw_auto_bright;    /* bool -> OIIO input cfg "raw:auto_bright"                       */
    int  raw_highlight_mode; /* APRAW_HL_* -> OIIO input cfg "raw:HighlightMode"               */
    int  exr_compression;    /* APEXR_* default EXR compression (saver)                        */
    int  exr_half;           /* EXR: write 16-bit half (bool); else 32-bit float              */
    int  crypto_mode;        /* Cryptomatte output: 0 = own EXR, 1 = merge into multilayer     */
    char ml_output[1024];    /* Multilayer EXR output base path; empty = inert (RPM overrides)  */
    char ocio_config[1024];  /* fallback .ocio path; empty = rely on the OCIO env var         */
} APImageIOPrefs;

/* Fill `out` from the current pref values (publishing first if needed). Safe to
 * call with a NULL global (leaves `out` zeroed but well-defined defaults). */
void apprefs_read(GlobalFunc *global, APImageIOPrefs *out);

/* OIIO colour-space name for an APCS_* value ("sRGB", "linear", ...). */
const char *apprefs_colorspace_name(int apcs);

/* OIIO EXR compression name for an APEXR_* value ("zip", "piz", "dwaa", ...). */
const char *apprefs_exr_compression_name(int apexr);

/* LayoutGeneric activation: publishes the branch and opens Preferences on it.
 * Registered as the "APImageIO Settings" command so the category exists from
 * startup after being run once (a pure loader/saver only publishes lazily). */
XCALL_(int) ApPrefsPanel(int version, GlobalFunc *global, void *vlocal, void *serverData);

/* LayoutGeneric menu commands: pick the output / OCIO path via a file requester
 * and store it into the corresponding preference. */
XCALL_(int) ApSetOutputPathCmd(int version, GlobalFunc *global, void *vlocal, void *serverData);
XCALL_(int) ApSetOcioCmd(int version, GlobalFunc *global, void *vlocal, void *serverData);

#ifdef __cplusplus
}
#endif

#endif /* AP_PREFS_H */
