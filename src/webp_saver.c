/*
 * webp_saver.c - LightWave "ImageSaver" servers for WebP, via libwebp.
 *
 * We request RGBA32 from LightWave (it converts whatever the source image is),
 * accumulate the frame, then encode to WebP on completion. Two servers are
 * registered for the lossy / lossless choice (LightWave-idiomatic, like the
 * stock LW_OpenEXR_RGBAFP / RGBAHALF saver variants).
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "webpbridge.h"
#include "ap_report.h"

typedef struct st_WebpSaveCtx {
    int             w, h;
    unsigned char  *rgba;       /* w*h*4 */
    int             result;
} WebpSaveCtx;

/* --- protocol callbacks LightWave drives via local->sendData --- */

static void wsSetSize(void *priv, int w, int h)
{
    WebpSaveCtx *c = (WebpSaveCtx *)priv;
    c->w = w; c->h = h;
    c->rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!c->rgba) c->result = IPSTAT_FAILED;
}

static int wsSetParam(void *priv, LWImageParam p, const char *v)
{
    (void)priv; (void)p; (void)v;
    return 0;
}

static void wsSetMap(void *priv, int i, const unsigned char rgb[3])
{
    (void)priv; (void)i; (void)rgb;
}

static int wsSendLine(void *priv, int y, const LWPixelID data)
{
    WebpSaveCtx *c = (WebpSaveCtx *)priv;
    const LWPixelRGBA32 *px = (const LWPixelRGBA32 *)data;
    unsigned char *dst;
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

static int wsDone(void *priv, int err)
{
    WebpSaveCtx *c = (WebpSaveCtx *)priv;
    if (err != IPSTAT_OK) c->result = err;
    return c->result;
}

/* --- shared saver body --- */

static int webp_save_impl(GlobalFunc *global, LWImageSaverLocal *local, int lossless, float quality)
{
    LWImageProtocol ip;
    WebpSaveCtx ctx;
    char errbuf[256];

    memset(&ctx, 0, sizeof(ctx));
    ctx.result = IPSTAT_OK;

    ip.type      = LWIMTYP_RGBA32;   /* ask LW to hand us 8-bit RGBA */
    ip.priv_data = &ctx;
    ip.done      = wsDone;
    ip.setSize   = wsSetSize;
    ip.setParam  = wsSetParam;
    ip.sendLine  = wsSendLine;
    ip.setMap    = wsSetMap;

    /* Drives the callbacks above; returns when the frame is fully sent. */
    local->sendData(local->priv_data, &ip, 0);

    if (ctx.result == IPSTAT_OK && ctx.rgba) {
        if (!webpbridge_save(local->filename, ctx.rgba, ctx.w, ctx.h, 1,
                             lossless, quality, errbuf, sizeof(errbuf))) {
            ap_report_save_error(global, "WebP", local->filename, errbuf);
            ctx.result = IPSTAT_FAILED;
        }
    } else if (!ctx.rgba) {
        ctx.result = IPSTAT_FAILED;
    }

    if (ctx.rgba) free(ctx.rgba);
    local->result = ctx.result;
    return AFUNC_OK;
}

XCALL_(int)
WebpSaverLossless(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return webp_save_impl(global, (LWImageSaverLocal *)vlocal, 1, 100.0f);
}

XCALL_(int)
WebpSaverLossy100(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return webp_save_impl(global, (LWImageSaverLocal *)vlocal, 0, 100.0f);
}

XCALL_(int)
WebpSaverLossy90(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return webp_save_impl(global, (LWImageSaverLocal *)vlocal, 0, 90.0f);
}

XCALL_(int)
WebpSaverLossy75(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return webp_save_impl(global, (LWImageSaverLocal *)vlocal, 0, 75.0f);
}
