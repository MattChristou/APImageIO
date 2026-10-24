/*
 * svg_loader.c - LightWave "ImageLoader" server for SVG, via lunasvg.
 *
 * Rasterizes the SVG to 8-bit RGBA (with alpha) at its intrinsic size and
 * delivers RGBA32. Loader only.
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "svgbridge.h"
#include "ap_report.h"

XCALL_(int)
SvgLoader(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageLoaderLocal *local = (LWImageLoaderLocal *)vlocal;
    SvgImage img;
    LWImageProtocolID proto;
    char err[256];
    int x, y, aborted = 0;

    (void)serverData;

    if (version != LWIMAGELOADER_VERSION)
        return AFUNC_BADVERSION;

    if (!svgbridge_is_svg(local->filename)) {
        local->result = IPSTAT_NOREC;
        return AFUNC_OK;
    }

    memset(&img, 0, sizeof(img));
    err[0] = '\0';
    if (!svgbridge_load(local->filename, &img, err, sizeof(err)) || !img.rgba) {
        ap_report_load_error(global, "SVG", local->filename, err);
        local->result = IPSTAT_BADFILE;
        svgbridge_free(&img);
        return AFUNC_OK;
    }

    proto = local->begin(local->priv_data, LWIMTYP_RGBA32);
    if (!proto) {
        svgbridge_free(&img);
        local->result = IPSTAT_FAILED;
        return AFUNC_OK;
    }

    LWIP_SETSIZE(proto, img.width, img.height);

    {
        LWPixelRGBA32 *row = (LWPixelRGBA32 *)malloc((size_t)img.width * sizeof(LWPixelRGBA32));
        if (!row) aborted = 1;
        for (y = 0; !aborted && y < img.height; y++) {
            const unsigned char *src = img.rgba + (size_t)y * img.width * 4;
            for (x = 0; x < img.width; x++) {
                row[x].r = src[x * 4 + 0];
                row[x].g = src[x * 4 + 1];
                row[x].b = src[x * 4 + 2];
                row[x].a = src[x * 4 + 3];
            }
            if (LWIP_SENDLINE(proto, y, row)) aborted = 1;
        }
        if (row) free(row);
    }

    local->result = LWIP_DONE(proto, aborted ? IPSTAT_FAILED : IPSTAT_OK);
    local->done(local->priv_data, proto);
    svgbridge_free(&img);
    return AFUNC_OK;
}
