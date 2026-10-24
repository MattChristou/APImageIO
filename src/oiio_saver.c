/*
 * oiio_saver.c - LightWave "ImageSaver" servers backed by OpenImageIO.
 *
 * Covers the high-bit-depth / DCC-interchange formats LW can't write itself:
 *   - TIFF 16-bit and TIFF float (32-bit) - high-precision interchange
 *   - DPX                                  - film/online finishing (16-bit)
 *   - HDR (Radiance RGBE)                  - HDRI / environment maps
 *
 * We request LWIMTYP_RGBAFP so LightWave hands us the full-precision float
 * buffer (HDR preserved); oiiobridge_save picks the on-disk format from the
 * filename extension and quantises to the requested bit depth. The float
 * buffer is passed through unchanged (same convention as the stock LW EXR
 * float saver: scene-linear floats).
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <lwgeneric.h>          /* EXR-compression command (LayoutGeneric) */
#include <stdlib.h>
#include <string.h>

#include "oiiobridge.h"
#include "ap_prefs.h"
#include "exr_session.h"       /* shared per-render EXR compression override */

typedef struct st_OiioSaveCtx {
    int     w, h;
    float  *rgba;       /* w*h*4 float, RGBA */
    int     result;
} OiioSaveCtx;

static void osSetSize(void *priv, int w, int h)
{
    OiioSaveCtx *c = (OiioSaveCtx *)priv;
    c->w = w; c->h = h;
    c->rgba = (float *)malloc((size_t)w * h * 4 * sizeof(float));
    if (!c->rgba) c->result = IPSTAT_FAILED;
}

static int osSetParam(void *priv, LWImageParam p, const char *v)
{
    (void)priv; (void)p; (void)v;
    return 0;
}

static void osSetMap(void *priv, int i, const unsigned char rgb[3])
{
    (void)priv; (void)i; (void)rgb;
}

static int osSendLine(void *priv, int y, const LWPixelID data)
{
    OiioSaveCtx *c = (OiioSaveCtx *)priv;
    const LWPixelRGBAFP *px = (const LWPixelRGBAFP *)data;
    float *dst;
    int x;

    if (!c->rgba || y < 0 || y >= c->h) return 0;
    dst = c->rgba + (size_t)y * c->w * 4;
    for (x = 0; x < c->w; x++) {
        dst[x * 4 + 0] = px[x].r;
        dst[x * 4 + 1] = px[x].g;
        dst[x * 4 + 2] = px[x].b;
        dst[x * 4 + 3] = px[x].a;
    }
    return 0;
}

static int osDone(void *priv, int err)
{
    OiioSaveCtx *c = (OiioSaveCtx *)priv;
    if (err != IPSTAT_OK) c->result = err;
    return c->result;
}

/* Stream LightWave's float RGBA buffer into ctx (ctx->rgba/w/h/result). Shared
 * by every OIIO saver. Also applies the engine prefs (thread count, cache). */
static void oiio_gather(GlobalFunc *global, LWImageSaverLocal *local, OiioSaveCtx *ctx)
{
    LWImageProtocol ip;
    APImageIOPrefs prefs;
    OiioConfig cfg;

    apprefs_read(global, &prefs);
    cfg.cache_mb           = prefs.cache_mb;
    cfg.threads            = prefs.threads;
    cfg.automip            = prefs.automip;
    cfg.raw_use_camera_wb  = prefs.raw_use_camera_wb;
    cfg.raw_auto_bright    = prefs.raw_auto_bright;
    cfg.raw_highlight_mode = prefs.raw_highlight_mode;
    oiiobridge_configure(&cfg);

    memset(ctx, 0, sizeof(*ctx));
    ctx->result = IPSTAT_OK;

    ip.type      = LWIMTYP_RGBAFP;   /* full-precision float buffer */
    ip.priv_data = ctx;
    ip.done      = osDone;
    ip.setSize   = osSetSize;
    ip.setParam  = osSetParam;
    ip.sendLine  = osSendLine;
    ip.setMap    = osSetMap;

    local->sendData(local->priv_data, &ip, 0);
    if (!ctx->rgba) ctx->result = IPSTAT_FAILED;
}

/* nchan = 3 (RGB) or 4 (RGBA); bitdepth = 8/16/32 (32 = float). */
static int oiio_save_impl(GlobalFunc *global, LWImageSaverLocal *local, int nchan, int bitdepth)
{
    OiioSaveCtx ctx;
    char errbuf[256];

    oiio_gather(global, local, &ctx);
    if (ctx.result == IPSTAT_OK && ctx.rgba) {
        if (!oiiobridge_save(local->filename, ctx.rgba, ctx.w, ctx.h,
                             nchan, bitdepth, errbuf, sizeof(errbuf)))
            ctx.result = IPSTAT_FAILED;
    }

    if (ctx.rgba) free(ctx.rgba);
    local->result = ctx.result;
    return AFUNC_OK;
}

XCALL_(int)
OiioSaverTiff16(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 4, 16);
}

XCALL_(int)
OiioSaverTiffFloat(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 4, 32);
}

XCALL_(int)
OiioSaverDpx(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* DPX is an RGB film format; 16-bit on disk. */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 3, 16);
}

XCALL_(int)
OiioSaverHdr(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* Radiance RGBE: float in, RGB only (no alpha). */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 3, 32);
}

XCALL_(int)
OiioSaverFits(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* FITS: scientific float, RGB. */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 3, 32);
}

XCALL_(int)
OiioSaverIco(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* Windows icon: 8-bit RGBA. */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 4, 8);
}

XCALL_(int)
OiioSaverIff(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* Maya IFF: 8-bit RGBA. */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 4, 8);
}

XCALL_(int)
OiioSaverPnm(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* PNM/PPM: 8-bit RGB. */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 3, 8);
}

XCALL_(int)
OiioSaverZfile(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    /* Zfile: float (depth-oriented format; written from the float buffer). */
    return oiio_save_impl(global, (LWImageSaverLocal *)vlocal, 4, 32);
}

/* OpenEXR saver: RGBA half/float with a chosen compression. Compression comes
 * from the per-render command override if set, else the EXR pref; bit depth
 * (half vs float) from the pref. This is the saver the Render Pass Manager
 * points its AOV buffers at to control compression per pass. */
XCALL_(int)
OiioSaverExr(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageSaverLocal *local = (LWImageSaverLocal *)vlocal;
    OiioSaveCtx    ctx;
    APImageIOPrefs prefs;
    char errbuf[256];
    char comp[32];
    (void)serverData;

    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;

    apprefs_read(global, &prefs);
    if (ap_exr_compression()[0]) {                     /* command override wins */
        strncpy(comp, ap_exr_compression(), sizeof comp - 1);
    } else {
        strncpy(comp, apprefs_exr_compression_name(prefs.exr_compression), sizeof comp - 1);
    }
    comp[sizeof comp - 1] = '\0';

    oiio_gather(global, local, &ctx);
    if (ctx.result == IPSTAT_OK && ctx.rgba) {
        int half = (ap_exr_half() >= 0) ? ap_exr_half() : prefs.exr_half;  /* override > pref */
        if (!oiiobridge_save_exr(local->filename, ctx.rgba, ctx.w, ctx.h,
                                 4, half, comp, errbuf, sizeof(errbuf)))
            ctx.result = IPSTAT_FAILED;
    }

    if (ctx.rgba) free(ctx.rgba);
    local->result = ctx.result;
    return AFUNC_OK;
}

/* "APImageIO_EXRCompression <name>" command -- sets the per-render EXR
 * compression the saver uses next. Argument is an OIIO compression name
 * ("zip"/"piz"/"dwaa"/...); empty argument clears the override (revert to pref).
 * Invoked by the Render Pass Manager before each pass, or by the user. */
XCALL_(int)
ApExrCompressionCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWLayoutGeneric *lg = (LWLayoutGeneric *)vlocal;
    (void)global; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    if (lg && lg->commandArguments)
        ap_exr_set_compression(lg->commandArguments(lg->data));
    else
        ap_exr_set_compression("");
    return AFUNC_OK;
}

/* "APImageIO_EXRBitDepth <0|1>" command -- 1 = 16-bit half, 0 = 32-bit float;
 * empty argument clears the override (revert to the pref). */
XCALL_(int)
ApExrBitDepthCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWLayoutGeneric *lg = (LWLayoutGeneric *)vlocal;
    (void)global; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    if (lg && lg->commandArguments) {
        LWCStringUTF8 a = lg->commandArguments(lg->data);
        ap_exr_set_half((a && a[0]) ? (atoi(a) ? 1 : 0) : -1);
    } else {
        ap_exr_set_half(-1);
    }
    return AFUNC_OK;
}
