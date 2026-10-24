/*
 * exr_multilayer.c - "AP_MultilayerEXR" ImageFilter: write all of a frame's
 * render buffers (AOVs) into ONE multilayer OpenEXR, plus the command that
 * points it at an output path.
 *
 * An ImageFilter's process() gets an LWFilterAccess with getLine(bufferName,
 * layer, y) -- read access to every named render buffer at once, which is the
 * only place all a pass's AOVs are visible together. We read them, name each
 * channel "<Buffer>.<R/G/B/A|Z|n>", and write a single multichannel EXR via OIIO.
 *
 * PHASE B (minimal): captures Final_Render (RGBA) + Depth to validate the
 * getLine/layers mechanics and the multichannel write. Generalise to all
 * buffersInUse() once confirmed in-app.
 *
 * Output path + compression come from the shared EXR session (set by the
 * APImageIO commands, e.g. by the Render Pass Manager per pass). If no
 * multilayer output path is set, the filter is inert.
 */
#include <lwserver.h>
#include <lwfilter.h>
#include <lwrender.h>      /* LWSceneInfo: scene name for folder-style output */
#include <lwgeneric.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "oiiobridge.h"
#include "ap_prefs.h"
#include "exr_session.h"
#include "crypto_matte.h"      /* merge-mode: append Cryptomatte channels */
#include "ap_report.h"

static GlobalFunc    *g_ml_global     = NULL;
static LWFilterGlobal *g_filterGlobal = NULL;

#define ML_MAXBUF 256

/* We request nothing extra: capture Final_Render (always present) plus every
 * buffer the render already has in use (i.e. whatever the buffer set / AOV
 * sheet enabled -- those are computed and getLine-accessible). */
static LWCStringASCII ML_FLAGS[] = { 0 };
static LWCStringASCII *ml_flags(LWInstance inst) { (void)inst; return ML_FLAGS; }

/* Buffers holding measurements rather than colour. These are always stored
 * 32-bit float, even when the half-float pref is on: half rounds integer ids
 * above 2048, turns background depth beyond 65504 into inf, and loses
 * world-space position precision. Matched on the buffer-name prefix so the
 * "UV_Tangent_Space_*" family and "Motion_X/_Y" are covered. */
static int is_data_buffer(const char *name)
{
    static const char *const k[] = {
        "Depth", "Geometry", "Motion", "Normals", "Surface_ID", "Object_ID",
        "UV", "Camera_Tangent_Space", "World_Position", "Object_Position",
        "Texture_Position", "dPdu", "dPdv", "Sampling_Weight", "Special", 0 };
    int i;
    if (!name) return 0;
    for (i = 0; k[i]; ++i) if (!strncmp(name, k[i], strlen(k[i]))) return 1;
    return 0;
}

/* component suffix for a layer given the layer count of a buffer */
static const char *layer_suffix(int nlayers, int i)
{
    static const char *rgba[4] = { "R", "G", "B", "A" };
    static char num[8];
    if (nlayers == 1) return "Z";
    if (nlayers <= 4) return rgba[i];
    snprintf(num, sizeof num, "%d", i);
    return num;
}

static LWError ml_process(LWInstance inst, const LWFilterAccess *fa)
{
    const char *base = ap_exr_multilayer_output();
    APImageIOPrefs prefs;
    int w, h, b, nbuf = 0, nchan = 0, chan, y, x, total, crypto_n = 0;
    LWCStringASCII bufs[ML_MAXBUF];
    int nlayer[ML_MAXBUF];
    float *interleaved;
    char **names;
    unsigned char *chan_half;
    CMCryptoData cd;
    char fname[1200], comp[32], errbuf[256];
    (void)inst;

    if (!fa) return NULL;
    w = fa->width; h = fa->height;
    if (w <= 0 || h <= 0 || !fa->getLine || !fa->layers) return NULL;

    apprefs_read(g_ml_global, &prefs);
    if (!base || !base[0]) base = prefs.ml_output;   /* session path > pref path */
    if (!base || !base[0]) return NULL;              /* inert unless a path is set */

    /* buffer list: Final_Render (beauty) + everything in use, de-duplicated */
    bufs[nbuf++] = LWBUFFER_FINAL_RENDER_RGBA;
    if (g_filterGlobal && g_filterGlobal->buffersInUse) {
        LWCStringUTF8 *u = g_filterGlobal->buffersInUse();
        int k;
        if (u) for (k = 0; u[k] && nbuf < ML_MAXBUF; ++k) {
            int dup = 0, j;
            for (j = 0; j < nbuf; ++j) if (!strcmp(bufs[j], u[k])) { dup = 1; break; }
            if (!dup) bufs[nbuf++] = u[k];
        }
    }

    /* render-buffer channel count */
    for (b = 0; b < nbuf; ++b) {
        nlayer[b] = fa->layers(bufs[b]);
        if (nlayer[b] < 0) nlayer[b] = 0;
        nchan += nlayer[b];
    }
    if (nchan <= 0) return NULL;

    /* merge mode (session override > pref): pull Crypto channels to append */
    { int mode = ap_crypto_mode();
      if (mode < 0) mode = prefs.crypto_mode;
      if (mode == 1 && cm_crypto_produce(w, h, &cd)) crypto_n = cd.nchan; }
    total = nchan + crypto_n;

    { char cbase[1024];
      LWSceneInfo *si = g_ml_global ? (LWSceneInfo *)g_ml_global(LWSCENEINFO_GLOBAL, GFUSE_TRANSIENT) : NULL;
      ap_exr_resolve_base(base, si ? si->name : NULL, cbase, sizeof cbase);
      snprintf(fname, sizeof fname, "%s%04d.exr", cbase, (int)fa->frame); }

    interleaved = (float *)malloc((size_t)w * h * total * sizeof(float));
    names       = (char **)calloc((size_t)total, sizeof(char *));
    chan_half   = (unsigned char *)calloc((size_t)total, 1);
    if (!interleaved || !names || !chan_half) {
        free(interleaved); free(names); free(chan_half);
        if (crypto_n) cm_crypto_free(&cd);
        ap_report_save_error(g_ml_global, "Multilayer EXR", fname, "out of memory");
        return NULL;
    }

    /* channel names: "<BufferName>.<suffix>" then the Crypto* channels.
     * Precision: colour buffers follow the half pref; data buffers and the
     * Crypto ids (bit patterns) are always 32-bit float. */
    {
        int half = (ap_exr_half() >= 0) ? ap_exr_half() : prefs.exr_half;
        chan = 0;
        for (b = 0; b < nbuf; ++b) {
            int i, data = is_data_buffer(bufs[b]);
            for (i = 0; i < nlayer[b]; ++i) {
                char nm[128];
                snprintf(nm, sizeof nm, "%s.%s", bufs[b], layer_suffix(nlayer[b], i));
                chan_half[chan] = (unsigned char)(half && !data);
                names[chan++] = _strdup(nm);
            }
        }
    }
    { int i; for (i = 0; i < crypto_n; ++i) names[chan++] = _strdup(cd.names[i]); }

    /* gather pixels (stride = total): render buffers, then Crypto channels */
    for (y = 0; y < h; ++y) {
        chan = 0;
        for (b = 0; b < nbuf; ++b) {
            int i;
            for (i = 0; i < nlayer[b]; ++i) {
                const float *line = fa->getLine(bufs[b], i, y);
                float *dst = interleaved + ((size_t)y * w) * total + chan;
                if (line) for (x = 0; x < w; ++x) dst[x * total] = line[x];
                else      for (x = 0; x < w; ++x) dst[x * total] = 0.0f;
                ++chan;
            }
        }
        if (crypto_n)
            for (x = 0; x < w; ++x)
                memcpy(interleaved + ((size_t)y * w + x) * total + nchan,
                       cd.interleaved + ((size_t)y * w + x) * crypto_n,
                       (size_t)crypto_n * sizeof(float));
    }

    /* compression: session override wins over the pref default (prefs read above).
     * With Crypto channels present a lossy codec would alter the id bits, so
     * fall back to zip (the whole file shares one compression). */
    if (ap_exr_compression()[0]) strncpy(comp, ap_exr_compression(), sizeof comp - 1);
    else strncpy(comp, apprefs_exr_compression_name(prefs.exr_compression), sizeof comp - 1);
    comp[sizeof comp - 1] = '\0';
    if (crypto_n) {
        const char *safe = ap_exr_lossless_compression(comp);
        if (safe != comp) { strncpy(comp, safe, sizeof comp - 1); comp[sizeof comp - 1] = '\0'; }
    }

    errbuf[0] = '\0';
    if (!oiiobridge_save_exr_layers(fname, w, h, total, (const char *const *)names,
                                    interleaved, chan_half, comp,
                                    crypto_n ? (const char *const *)cd.attrN : NULL,
                                    crypto_n ? (const char *const *)cd.attrV : NULL,
                                    crypto_n ? cd.nattr : 0, errbuf, sizeof errbuf))
        ap_report_save_error(g_ml_global, "Multilayer EXR", fname, errbuf);

    for (chan = 0; chan < total; ++chan) free(names[chan]);
    free(names);
    free(chan_half);
    free(interleaved);
    if (crypto_n) cm_crypto_free(&cd);
    return NULL;
}

static LWInstance ml_create(void *priv, void *context, LWError *err)
{ (void)priv; (void)context; (void)err; return (LWInstance)1; }   /* stateless */
static void       ml_destroy(LWInstance inst) { (void)inst; }
static LWError    ml_copy(LWInstance a, LWInstance b) { (void)a; (void)b; return NULL; }

XCALL_(int)
OiioMultilayerExr(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageFilterHandler *local = (LWImageFilterHandler *)vlocal;
    (void)serverData;
    if (version != LWIMAGEFILTER_VERSION) return AFUNC_BADVERSION;
    g_ml_global = global;
    if (!g_filterGlobal) g_filterGlobal = (LWFilterGlobal *)global(LWFILTERINFO_GLOBAL, GFUSE_TRANSIENT);
    local->inst->create  = ml_create;
    local->inst->destroy = ml_destroy;
    local->inst->copy    = ml_copy;
    local->inst->load    = NULL;
    local->inst->save    = NULL;
    local->process = ml_process;
    local->flags   = ml_flags;
    return AFUNC_OK;
}

/* "APImageIO_MultilayerOutput <path>" command -- set the base output path the
 * multilayer filter writes to (frame + .exr appended). Empty arg disables it. */
XCALL_(int)
ApMultilayerOutputCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWLayoutGeneric *lg = (LWLayoutGeneric *)vlocal;
    (void)global; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    if (lg && lg->commandArguments) ap_exr_set_multilayer_output(lg->commandArguments(lg->data));
    else                            ap_exr_set_multilayer_output("");
    return AFUNC_OK;
}
