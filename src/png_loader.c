/*
 * png_loader.c - LightWave "ImageLoader" server that intercepts ONLY 16-bit
 * PNGs (which the built-in loader truncates to 8-bit) and delivers them as
 * float to preserve precision. All other PNGs return NOREC so the built-in
 * loader keeps handling them unchanged.
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "pngbridge.h"
#include "ap_report.h"

XCALL_(int)
PngLoader(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageLoaderLocal *local = (LWImageLoaderLocal *)vlocal;
    PngImage img;
    LWImageProtocolID proto;
    char err[256];
    int x, y, aborted = 0;

    (void)serverData;

    if (version != LWIMAGELOADER_VERSION)
        return AFUNC_BADVERSION;

    /* Only claim 16-bit PNGs; let the built-in loader handle everything else. */
    if (!pngbridge_is_png16(local->filename)) {
        local->result = IPSTAT_NOREC;
        return AFUNC_OK;
    }

    memset(&img, 0, sizeof(img));
    err[0] = '\0';
    if (!pngbridge_load16(local->filename, &img, err, sizeof(err)) || !img.rgba) {
        ap_report_load_error(global, "PNG 16-bit", local->filename, err);
        local->result = IPSTAT_BADFILE;
        pngbridge_free(&img);
        return AFUNC_OK;
    }

    proto = local->begin(local->priv_data,
                         img.has_alpha ? LWIMTYP_RGBAFP : LWIMTYP_RGBFP);
    if (!proto) {
        pngbridge_free(&img);
        local->result = IPSTAT_FAILED;
        return AFUNC_OK;
    }

    LWIP_SETSIZE(proto, img.width, img.height);

    if (img.has_alpha) {
        LWPixelRGBAFP *row = (LWPixelRGBAFP *)malloc((size_t)img.width * sizeof(LWPixelRGBAFP));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const float *src = img.rgba + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0]; row[x].g = src[x * 4 + 1];
                row[x].b = src[x * 4 + 2]; row[x].a = src[x * 4 + 3];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    } else {
        LWPixelRGBFP *row = (LWPixelRGBFP *)malloc((size_t)img.width * sizeof(LWPixelRGBFP));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const float *src = img.rgba + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0]; row[x].g = src[x * 4 + 1]; row[x].b = src[x * 4 + 2];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    }

    local->result = LWIP_DONE(proto, aborted ? IPSTAT_FAILED : IPSTAT_OK);
    local->done(local->priv_data, proto);
    pngbridge_free(&img);
    return AFUNC_OK;
}
