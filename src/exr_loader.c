/*
 * exr_loader.c - LightWave "ImageLoader" server backed by OpenEXRCore.
 *
 * Reads EXR files (including DWAA/DWAB-compressed and multi-layer files that
 * LightWave's stock loader rejects) via the exrbridge boundary, then streams
 * the resolved RGBA(F) image to LightWave through the ImageLoader protocol.
 *
 * Float delivery (LWIMTYP_RGBAFP / RGBFP) preserves the full HDR range; EXR
 * data is scene-linear and handed over unmodified.
 */

#include <lwserver.h>
#include <lwimageio.h>
#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

#include "exrbridge.h"
#include "ap_report.h"

XCALL_(int)
ExrLoader(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageLoaderLocal *local = (LWImageLoaderLocal *)vlocal;
    ExrImage img;
    LWImageProtocolID proto;
    char err[256];
    int x, y;

    (void)serverData;

    if (version != LWIMAGELOADER_VERSION)
        return AFUNC_BADVERSION;

    /* Claim only EXRs; let other loaders handle everything else. */
    if (!exrbridge_is_exr(local->filename)) {
        local->result = IPSTAT_NOREC;
        return AFUNC_OK;
    }

    memset(&img, 0, sizeof(img));
    err[0] = '\0';
    if (!exrbridge_load(local->filename, &img, err, sizeof(err)) || !img.rgba) {
        /* Recognised as EXR but could not be decoded. */
        ap_report_load_error(global, "OpenEXR", local->filename, err);
        local->result = IPSTAT_BADFILE;
        exrbridge_free(&img);
        return AFUNC_OK;
    }

    proto = local->begin(local->priv_data,
                         img.has_alpha ? LWIMTYP_RGBAFP : LWIMTYP_RGBFP);
    if (!proto) {
        exrbridge_free(&img);
        local->result = IPSTAT_FAILED;
        return AFUNC_OK;
    }

    LWIP_SETSIZE(proto, img.width, img.height);

    /* Note: deliberately NOT signalling LWIMPAR_GAMMA here. LightWave already
       treats float EXR data as scene-linear; an explicit gamma setParam on the
       loader protocol made LW reject the image (load error). EXR colour reads
       correctly without it. */

    {
        int aborted = 0;
        if (img.has_alpha) {
            LWPixelRGBAFP *row = (LWPixelRGBAFP *)malloc((size_t)img.width * sizeof(LWPixelRGBAFP));
            if (!row) { aborted = 1; }
            for (y = 0; !aborted && y < img.height; y++) {
                const float *src = img.rgba + (size_t)y * img.width * 4;
                for (x = 0; x < img.width; x++) {
                    row[x].r = src[x * 4 + 0];
                    row[x].g = src[x * 4 + 1];
                    row[x].b = src[x * 4 + 2];
                    row[x].a = src[x * 4 + 3];
                }
                if (LWIP_SENDLINE(proto, y, row)) { aborted = 1; }  /* non-zero = abort */
            }
            if (row) free(row);
        } else {
            LWPixelRGBFP *row = (LWPixelRGBFP *)malloc((size_t)img.width * sizeof(LWPixelRGBFP));
            if (!row) { aborted = 1; }
            for (y = 0; !aborted && y < img.height; y++) {
                const float *src = img.rgba + (size_t)y * img.width * 4;
                for (x = 0; x < img.width; x++) {
                    row[x].r = src[x * 4 + 0];
                    row[x].g = src[x * 4 + 1];
                    row[x].b = src[x * 4 + 2];
                }
                if (LWIP_SENDLINE(proto, y, row)) { aborted = 1; }
            }
            if (row) free(row);
        }

        /* Finalize the image through the protocol's own done(), then release. */
        local->result = LWIP_DONE(proto, aborted ? IPSTAT_FAILED : IPSTAT_OK);
        local->done(local->priv_data, proto);
        exrbridge_free(&img);

        /* TODO (Phase 3): signal scene-linear via LWIMPAR_GAMMA once verified
           against LW's color management. */
    }
    return AFUNC_OK;
}
