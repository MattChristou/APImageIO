/*
 * avif_saver.c - LightWave "ImageSaver" servers for AVIF (8-bit), via libavif.
 *
 * Requests RGBA32 from LightWave, accumulates the frame, encodes on completion.
 * Registered as quality variants (Lossless / q90 / q60), LightWave-idiomatic.
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "avifbridge.h"
#include "ap_report.h"

typedef struct st_AvifSaveCtx {
    int             w, h;
    unsigned char  *rgba;
    int             result;
} AvifSaveCtx;

static void asSetSize(void *priv, int w, int h)
{
    AvifSaveCtx *c = (AvifSaveCtx *)priv;
    c->w = w; c->h = h;
    c->rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!c->rgba) c->result = IPSTAT_FAILED;
}

static int  asSetParam(void *priv, LWImageParam p, const char *v) { (void)priv; (void)p; (void)v; return 0; }
static void asSetMap(void *priv, int i, const unsigned char rgb[3]) { (void)priv; (void)i; (void)rgb; }

static int asSendLine(void *priv, int y, const LWPixelID data)
{
    AvifSaveCtx *c = (AvifSaveCtx *)priv;
    const LWPixelRGBA32 *px = (const LWPixelRGBA32 *)data;
    unsigned char *dst;
    int x;
    if (!c->rgba || y < 0 || y >= c->h) return 0;
    dst = c->rgba + (size_t)y * c->w * 4;
    for (x = 0; x < c->w; x++) {
        dst[x * 4 + 0] = px[x].r; dst[x * 4 + 1] = px[x].g;
        dst[x * 4 + 2] = px[x].b; dst[x * 4 + 3] = px[x].a;
    }
    return 0;
}

static int asDone(void *priv, int err)
{
    AvifSaveCtx *c = (AvifSaveCtx *)priv;
    if (err != IPSTAT_OK) c->result = err;
    return c->result;
}

static int avif_save_impl(GlobalFunc *global, LWImageSaverLocal *local, int quality)
{
    LWImageProtocol ip;
    AvifSaveCtx ctx;
    char errbuf[256];

    memset(&ctx, 0, sizeof(ctx));
    ctx.result = IPSTAT_OK;

    ip.type      = LWIMTYP_RGBA32;
    ip.priv_data = &ctx;
    ip.done      = asDone;
    ip.setSize   = asSetSize;
    ip.setParam  = asSetParam;
    ip.sendLine  = asSendLine;
    ip.setMap    = asSetMap;

    local->sendData(local->priv_data, &ip, 0);

    if (ctx.result == IPSTAT_OK && ctx.rgba) {
        if (!avifbridge_save(local->filename, ctx.rgba, ctx.w, ctx.h, 1,
                             quality, errbuf, sizeof(errbuf))) {
            ap_report_save_error(global, "AVIF", local->filename, errbuf);
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
AvifSaverLossless(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return avif_save_impl(global, (LWImageSaverLocal *)vlocal, 100);
}

XCALL_(int)
AvifSaverQ90(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return avif_save_impl(global, (LWImageSaverLocal *)vlocal, 90);
}

XCALL_(int)
AvifSaverQ60(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    (void)serverData;
    if (version != LWIMAGESAVER_VERSION) return AFUNC_BADVERSION;
    return avif_save_impl(global, (LWImageSaverLocal *)vlocal, 60);
}
