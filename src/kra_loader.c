/*
 * kra_loader.c - LightWave "ImageLoader" server for Krita .kra and OpenRaster
 * .ora documents. LightWave has no built-in loader for either, so this claims
 * them outright (no SRVTAG_REPLACES needed); anything else returns NOREC.
 *
 * Loading the working file directly is what makes the Krita live-texture link
 * work without an export step: Krita saves, LWImageList replace() re-reads.
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "krabridge.h"
#include "ap_report.h"

XCALL_(int)
KraLoader(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageLoaderLocal *local = (LWImageLoaderLocal *)vlocal;
    KraImage img;
    LWImageProtocolID proto;
    char err[256];
    int x, y, aborted = 0;

    (void)serverData;

    if (version != LWIMAGELOADER_VERSION)
        return AFUNC_BADVERSION;

    if (!krabridge_is_kra(local->filename)) {
        local->result = IPSTAT_NOREC;
        return AFUNC_OK;
    }

    memset(&img, 0, sizeof(img));
    err[0] = 0;
    if (!krabridge_load(local->filename, &img, err, sizeof(err))) {
        ap_report_load_error(global, "Krita/OpenRaster", local->filename, err);
        local->result = IPSTAT_BADFILE;
        krabridge_free(&img);
        return AFUNC_OK;
    }

    if (img.is_float)
        proto = local->begin(local->priv_data, img.has_alpha ? LWIMTYP_RGBAFP : LWIMTYP_RGBFP);
    else
        proto = local->begin(local->priv_data, img.has_alpha ? LWIMTYP_RGBA32 : LWIMTYP_RGB24);

    if (!proto) {
        krabridge_free(&img);
        local->result = IPSTAT_FAILED;
        return AFUNC_OK;
    }

    LWIP_SETSIZE(proto, img.width, img.height);

    if (img.is_float && img.has_alpha) {
        LWPixelRGBAFP *row = (LWPixelRGBAFP *)malloc((size_t)img.width * sizeof(LWPixelRGBAFP));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const float *src = img.rgbaf + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0]; row[x].g = src[x * 4 + 1];
                row[x].b = src[x * 4 + 2]; row[x].a = src[x * 4 + 3];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else if (img.is_float) {
        LWPixelRGBFP *row = (LWPixelRGBFP *)malloc((size_t)img.width * sizeof(LWPixelRGBFP));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const float *src = img.rgbaf + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0]; row[x].g = src[x * 4 + 1]; row[x].b = src[x * 4 + 2];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else if (img.has_alpha) {
        LWPixelRGBA32 *row = (LWPixelRGBA32 *)malloc((size_t)img.width * sizeof(LWPixelRGBA32));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const unsigned char *src = img.rgba8 + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0]; row[x].g = src[x * 4 + 1];
                row[x].b = src[x * 4 + 2]; row[x].a = src[x * 4 + 3];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else {
        LWPixelRGB24 *row = (LWPixelRGB24 *)malloc((size_t)img.width * sizeof(LWPixelRGB24));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const unsigned char *src = img.rgba8 + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0]; row[x].g = src[x * 4 + 1]; row[x].b = src[x * 4 + 2];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    }

    local->result = LWIP_DONE(proto, aborted ? IPSTAT_FAILED : IPSTAT_OK);
    local->done(local->priv_data, proto);
    krabridge_free(&img);
    return AFUNC_OK;
}
