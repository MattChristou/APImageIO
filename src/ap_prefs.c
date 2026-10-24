/*
 * ap_prefs.c - APImageIO preferences: the parameter table + typed reader.
 *
 * All the publish/read/panel machinery lives in the shared apprefs library;
 * this file only declares what is APImageIO-specific.
 */

#include "ap_prefs.h"
#include "apprefs.h"
#include "apimageio_version.h"

#include <lwhost.h>       /* LWFILEACTIVATEFUNC_GLOBAL */
#include <lwdialog.h>     /* LWFileReqLocal, FREQ_* */
#include <string.h>
#include <stddef.h>

/* '/'->'-', matching apprefs' name sanitisation, so we can address a published
 * parameter directly by its sanitised key. */
static const char *ap_sanitize(const char *in, char *out, size_t n)
{
    size_t i = 0;
    if (!in || !out || n == 0) return in;
    for (; in[i] && i + 1 < n; ++i) out[i] = (in[i] == '/') ? '-' : in[i];
    out[i] = '\0';
    return out;
}

/* Open a native LightWave file requester; on OK the chosen path is in fullName. */
static int ap_file_req(GlobalFunc *global, int reqType, const char *title,
                       const char *fileType, char *fullName, int bufLen)
{
    LWFileActivateFunc *fa = global ?
        (LWFileActivateFunc *)global(LWFILEACTIVATEFUNC_GLOBAL, GFUSE_TRANSIENT) : NULL;
    LWFileReqLocal fr;
    char path[1024], base[1024];
    if (!fa || !fullName) return 0;
    memset(&fr, 0, sizeof fr);
    path[0] = base[0] = '\0';
    fr.reqType  = reqType;
    fr.title    = title;
    fr.fileType = fileType;
    fr.path     = path;
    fr.baseName = base;
    fr.fullName = fullName;      /* pre-seeded with the current value */
    fr.bufLen   = bufLen;
    fr.pickName = NULL;
    if (fa(LWFILEREQ_VERSION, &fr) == AFUNC_OK && fr.result == 1) return 1;
    return 0;
}


/* Parameter table. Order defines the read indices below. */
enum {
    P_CACHE_MB = 0, P_THREADS, P_AUTOMIP, P_STREAMING, P_INPUT_CS,
    P_RAW_WB, P_RAW_BRIGHT, P_RAW_HL, P_EXR_COMP, P_EXR_HALF, P_CRYPTO_MODE,
    P_ML_PATH, P_OCIO, P_VERSION
};

static const ApPrefParam PARAMS[] = {
    AP_INT("ImageCache Memory (MB)", 1024, 64, 65536, "",
           "Peak RAM OpenImageIO may use for decoded image tiles. Higher values speed up re-reads of large or numerous textures"),

    AP_INT("Worker Threads", 0, 0, 256, "",
           "Worker threads OpenImageIO uses to decode and encode. 0 lets OIIO choose based on the number of CPU cores"),

    /* Checkbox defaults are NUMERIC (1/0). LW ignores value.default=true/false. */
    AP_CHECK("Auto-Mipmap Textures", 0, "",
             "Generate mip levels on the fly for textures that lack them. Costs memory but smooths minification"),

    AP_CHECK("Enable Texture Streaming", 0, "",
             "Sample large textures on demand through the OpenImageIO cache instead of loading them whole. Used by the streaming texture node"),

    AP_POPUP("Default Input Colour Space", 0, "sRGB|Linear|ACEScg|Rec.709|Raw", "",
             "Colour space assumed for imported images that carry no embedded profile"),

    AP_CHECK("RAW: Use Camera White Balance", 1, "ui.divider=above;",
             "For camera RAW files, apply the white balance recorded by the camera"),

    AP_CHECK("RAW: Auto-Brighten", 0, "",
             "For camera RAW files, let the decoder auto-scale exposure. Off keeps linear values"),

    AP_POPUP("RAW: Highlight Mode", 0, "Clip|Unclip|Blend|Rebuild", "",
             "How clipped highlights in camera RAW are handled during demosaic"),

    AP_POPUP("EXR: Default Compression", APEXR_ZIP,
             "ZIP|PIZ|RLE|PXR24|ZIPS|DWAA|DWAB|B44|B44A|None", "ui.divider=above;",
             "Compression used by the APImageIO OpenEXR saver. DWAA/DWAB are lossy but very compact for renders. A per-render override may be set by the Render Pass Manager"),

    AP_CHECK("EXR: 16-bit Half Float", 1, "",
             "Write EXR channels as 16-bit half (compact, standard for beauty/AOVs). Off writes 32-bit float (full precision, larger)"),

    AP_POPUP("Cryptomatte Output", 0, "Own EXR (.cryptomatte.exr)|Merge into Multilayer EXR",
             "ui.divider=above;",
             "Where the Cryptomatte generator writes. Own EXR uses the AP Cryptomatte Output filter. Merge folds the Crypto channels into the AP MultilayerEXR output (that file is then written as 32-bit float)"),

    /* path fields with a browse button: "directory" (as Layout > Paths) and
       "fileopen" are widgets.dll controls. The output is a FOLDER - files are
       named <scene>_NNNN.exr in it (see ap_exr_resolve_base); an older
       filename-prefix value still works. */
    AP_STRING("Multilayer/Cryptomatte Output Path", "ui.control=directory;",
              "Folder for the Multilayer EXR and Cryptomatte files, named after the scene (MyScene_0001.exr and MyScene_0001.cryptomatte.exr). Browse for it here or use Utilities > 'APImageIO Set Output Path...'. Leave blank to rely on a per-render path (e.g. the Render Pass Manager)"),

    AP_STRING("Default OCIO Config", "ui.divider=above;ui.control=fileopen;",
              "Path to a fallback .ocio config used when the OCIO environment variable is not set. Browse for it here or use Utilities > 'APImageIO Set OCIO Config...'. Leave blank to rely on OCIO"),

    /* Read-only "about" row. The text lives in the VALUE (a label control's
       text overflows its narrow column), written every session by
       ap_publish_version(), and greyed out by a condition that is never true.
       Last on the page: LW reloads saved definitions, so a newly added key
       appears after them anyway. */
    AP_STRING("APImageIO Version", "ui.divider=above;ui.enabled=(Worker Threads < 0);",
              "The APImageIO build you are running. Please quote it in bug reports"),
};

static ApPrefs g_prefs = { APPREFS_BRANCH, PARAMS, AP_COUNT(PARAMS), 0x4901, 0 };

const char *apprefs_colorspace_name(int apcs)
{
    switch (apcs) {
    case APCS_LINEAR: return "linear";
    case APCS_ACESCG: return "ACEScg";
    case APCS_REC709: return "Rec709";
    case APCS_RAW:    return "Raw";
    case APCS_SRGB:
    default:          return "sRGB";
    }
}

const char *apprefs_exr_compression_name(int apexr)
{
    switch (apexr) {
    case APEXR_PIZ:   return "piz";
    case APEXR_RLE:   return "rle";
    case APEXR_PXR24: return "pxr24";
    case APEXR_ZIPS:  return "zips";
    case APEXR_DWAA:  return "dwaa";
    case APEXR_DWAB:  return "dwab";
    case APEXR_B44:   return "b44";
    case APEXR_B44A:  return "b44a";
    case APEXR_NONE:  return "none";
    case APEXR_ZIP:
    default:          return "zip";
    }
}

/* Publish the page and write the version row - once per session, so the
   value saved from an older build is always replaced by the running one. */
static void ap_publish_version(GlobalFunc *global)
{
    static int done = 0;
    LWPreferenceSystem *ps;
    char key[256];
    if (done || !global) return;
    apprefs_publish(global, &g_prefs);
    ps = (LWPreferenceSystem *)global(LWPREFERENCESYSTEM_GLOBAL, GFUSE_TRANSIENT);
    if (!ps || !ps->setStringValue) return;
    ap_sanitize(g_prefs.params[P_VERSION].name, key, sizeof key);
    ps->setStringValue(APPREFS_BRANCH, key, APIMAGEIO_VERSION);
    done = 1;
}

void apprefs_read(GlobalFunc *global, APImageIOPrefs *out)
{
    static int healed = 0;
    const char *ocio;

    if (!out) return;
    ap_publish_version(global);

    /* Legacy self-heal: an old "I/O Threads" name spawned a phantom branch. */
    if (!healed) { apprefs_remove_branch(global, "/APImageIO/I/"); healed = 1; }

    out->cache_mb           = apprefs_int(global, &g_prefs, P_CACHE_MB,   1024);
    out->threads            = apprefs_int(global, &g_prefs, P_THREADS,    0);
    out->automip            = apprefs_int(global, &g_prefs, P_AUTOMIP,    0);
    out->streaming          = apprefs_int(global, &g_prefs, P_STREAMING,  0);
    out->input_colorspace   = apprefs_int(global, &g_prefs, P_INPUT_CS,   APCS_SRGB);
    out->raw_use_camera_wb  = apprefs_int(global, &g_prefs, P_RAW_WB,     1);
    out->raw_auto_bright    = apprefs_int(global, &g_prefs, P_RAW_BRIGHT, 0);
    out->raw_highlight_mode = apprefs_int(global, &g_prefs, P_RAW_HL,     APRAW_HL_CLIP);
    out->exr_compression    = apprefs_int(global, &g_prefs, P_EXR_COMP,   APEXR_ZIP);
    out->exr_half           = apprefs_int(global, &g_prefs, P_EXR_HALF,   1);
    out->crypto_mode        = apprefs_int(global, &g_prefs, P_CRYPTO_MODE, 0);

    { const char *mlp = apprefs_string(global, &g_prefs, P_ML_PATH, "");
      strncpy(out->ml_output, mlp, sizeof(out->ml_output) - 1);
      out->ml_output[sizeof(out->ml_output) - 1] = '\0'; }

    ocio = apprefs_string(global, &g_prefs, P_OCIO, "");
    strncpy(out->ocio_config, ocio, sizeof(out->ocio_config) - 1);
    out->ocio_config[sizeof(out->ocio_config) - 1] = '\0';
}

/* "APImageIO Settings" LayoutGeneric (heals the legacy phantom, then opens).
 * Also puts the build version on the message line, for bug reports. */
XCALL_(int)
ApPrefsPanel(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWMessageFuncs *msg;
    (void)vlocal; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    msg = global ? (LWMessageFuncs *)global(LWMESSAGEFUNCS_GLOBAL, GFUSE_TRANSIENT) : NULL;
    if (msg && msg->info) msg->info("APImageIO " APIMAGEIO_VERSION, NULL);
    apprefs_remove_branch(global, "/APImageIO/I/");
    ap_publish_version(global);
    apprefs_open_panel(global, &g_prefs);
    return AFUNC_OK;
}

/* Pick a path with a native file requester and store it into pref `index`. */
static void ap_pick_pref_path(GlobalFunc *global, int index, int reqType,
                              const char *title, const char *filter)
{
    LWPreferenceSystem *ps;
    const char *cur;
    char full[1024], sname[256];

    apprefs_publish(global, &g_prefs);   /* ensure the param exists */
    ps = global ? (LWPreferenceSystem *)global(LWPREFERENCESYSTEM_GLOBAL, GFUSE_TRANSIENT) : NULL;
    if (!ps) return;

    cur = apprefs_string(global, &g_prefs, index, "");
    strncpy(full, cur ? cur : "", sizeof full - 1); full[sizeof full - 1] = '\0';
    if (ap_file_req(global, reqType, title, filter, full, (int)sizeof full)) {
        ap_sanitize(g_prefs.params[index].name, sname, sizeof sname);
        ps->setStringValue(APPREFS_BRANCH, sname, full);
    }
}

/* "APImageIO Set Output Path..." menu command -> requester -> pref. */
XCALL_(int)
ApSetOutputPathCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)vlocal; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    ap_pick_pref_path(global, P_ML_PATH, FREQ_DIRECTORY,
                      "Multilayer / Cryptomatte Output Folder", "Images");
    return AFUNC_OK;
}

/* "APImageIO Set OCIO Config..." menu command -> requester -> pref. */
XCALL_(int)
ApSetOcioCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)vlocal; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    ap_pick_pref_path(global, P_OCIO, FREQ_LOAD, "OCIO Config", "*.ocio");
    return AFUNC_OK;
}
