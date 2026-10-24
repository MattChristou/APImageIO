/*
 * oiio_loader.c - LightWave "ImageLoader" server backed by OpenImageIO.
 *
 * Catch-all loader for the formats our dedicated loaders don't cover (TIFF,
 * DPX, Cineon, PSD, HDR, JPEG-2000, Targa, DDS, JPEG XL, camera RAW, ...).
 * Gated by extension so it never steals files the built-ins or our EXR/WebP/
 * AVIF/PNG16/SVG loaders handle. 8-bit -> RGBA32; higher/float -> RGBAFP.
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "oiiobridge.h"
#include "ap_prefs.h"

XCALL_(int)
OiioLoader(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageLoaderLocal *local = (LWImageLoaderLocal *)vlocal;
    OiioImage img;
    LWImageProtocolID proto;
    APImageIOPrefs prefs;
    OiioConfig cfg;
    char err[256];
    int x, y, aborted = 0;

    (void)serverData;

    if (version != LWIMAGELOADER_VERSION)
        return AFUNC_BADVERSION;

    if (!oiiobridge_can_load(local->filename)) {
        local->result = IPSTAT_NOREC;
        return AFUNC_OK;
    }

    /* Pull the engine/RAW settings from Preferences, apply the process-wide
       ones, and hand the per-decode ones to the load. */
    apprefs_read(global, &prefs);
    cfg.cache_mb           = prefs.cache_mb;
    cfg.threads            = prefs.threads;
    cfg.automip            = prefs.automip;
    cfg.raw_use_camera_wb  = prefs.raw_use_camera_wb;
    cfg.raw_auto_bright    = prefs.raw_auto_bright;
    cfg.raw_highlight_mode = prefs.raw_highlight_mode;
    oiiobridge_configure(&cfg);

    memset(&img, 0, sizeof(img));
    if (!oiiobridge_load(local->filename, &cfg, &img, err, sizeof(err)) ||
        (!img.rgba8 && !img.rgbaf)) {
        /* NOREC (not BADFILE) so LW falls through to a built-in loader for
           overlapping formats (e.g. ordinary 8-bit TIFF) we couldn't decode. */
        local->result = IPSTAT_NOREC;
        oiiobridge_free(&img);
        return AFUNC_OK;
    }

    if (img.is_hdr)
        proto = local->begin(local->priv_data, img.has_alpha ? LWIMTYP_RGBAFP : LWIMTYP_RGBFP);
    else
        proto = local->begin(local->priv_data, img.has_alpha ? LWIMTYP_RGBA32 : LWIMTYP_RGB24);
    if (!proto) {
        oiiobridge_free(&img);
        local->result = IPSTAT_FAILED;
        return AFUNC_OK;
    }

    LWIP_SETSIZE(proto, img.width, img.height);

    if (img.is_hdr && img.has_alpha) {
        LWPixelRGBAFP *row = (LWPixelRGBAFP *)malloc((size_t)img.width * sizeof(LWPixelRGBAFP));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const float *s = img.rgbaf + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) { row[x].r=s[x*4]; row[x].g=s[x*4+1]; row[x].b=s[x*4+2]; row[x].a=s[x*4+3]; }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else if (img.is_hdr) {
        LWPixelRGBFP *row = (LWPixelRGBFP *)malloc((size_t)img.width * sizeof(LWPixelRGBFP));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const float *s = img.rgbaf + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) { row[x].r=s[x*4]; row[x].g=s[x*4+1]; row[x].b=s[x*4+2]; }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else if (img.has_alpha) {
        LWPixelRGBA32 *row = (LWPixelRGBA32 *)malloc((size_t)img.width * sizeof(LWPixelRGBA32));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const unsigned char *s = img.rgba8 + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) { row[x].r=s[x*4]; row[x].g=s[x*4+1]; row[x].b=s[x*4+2]; row[x].a=s[x*4+3]; }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else {
        LWPixelRGB24 *row = (LWPixelRGB24 *)malloc((size_t)img.width * sizeof(LWPixelRGB24));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const unsigned char *s = img.rgba8 + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) { row[x].r=s[x*4]; row[x].g=s[x*4+1]; row[x].b=s[x*4+2]; }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    }

    local->result = LWIP_DONE(proto, aborted ? IPSTAT_FAILED : IPSTAT_OK);
    local->done(local->priv_data, proto);
    oiiobridge_free(&img);
    return AFUNC_OK;
}
