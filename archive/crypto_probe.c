/*
 * crypto_probe.c - "AP_CryptoProbe" PixelFilter: a throwaway diagnostic to
 * decide whether a LightWave 2025 pixel filter can drive Cryptomatte.
 *
 * v1 finding: pa->sx/sy always report the PIXEL CENTRE (.5), Coverage and
 * getSamplerRange read 0, and the 4000-sample cap was consumed entirely by the
 * coarse adaptive preview pass of a large frame -- so the make-or-break
 * question (does evaluate() fire once PER SAMPLE with differing per-sample IDs
 * at edges?) went unanswered.
 *
 * v2 pulls the ground truth from the render state reachable via pa->ray:
 *   ray->renderState->subPixelXY   -> the true jittered sub-pixel position
 *   ray->renderState->sampleIndex  -> current sample index within the pixel
 *   ray->renderState->numSamples   -> total samples budgeted for the pixel
 *   ray->renderState->samplePass   -> adaptive pass number
 *   ray->flags (LWRT_PREVIEW etc.) -> whether this is a preview/VPR context
 *
 * DECISIVE READS after a render:
 *   - If a pixel (px,py) appears with sampleIndex 0,1,2,... (numSamples>1),
 *     evaluate() IS per-sample -> full multi-rank Cryptomatte is viable.
 *   - If every (px,py) appears exactly once even when numSamples>1, the filter
 *     is per-PIXEL (front id only) -> pixel-filter route is dead; fall back to
 *     the surface-shader route.
 *   - If Object_ID differs across a pixel's samples at an object silhouette,
 *     overlapping-coverage capture is confirmed.
 *
 * Logs to %TEMP%\ap_crypto_probe.txt (per-sample TSV) and rewrites a running
 * digest to %TEMP%\ap_crypto_probe_summary.txt so results are visible without
 * closing the scene. Single-threaded (renderFlags omits LWPFF_MULTITHREADED)
 * for a race-free log. Throwaway -- remove once findings are recorded.
 *
 * TEST: render a SMALL final frame (e.g. 320x240, F9) with antialiasing ON
 * (camera samples > 1) of a 2-object scene sharing an anti-aliased silhouette.
 */
#include <lwserver.h>
#include <lwfilter.h>
#include <lwrender.h>
#include <lwsurf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CRYPTO_PROBE_MAXLOG   20000   /* cap raw log lines */
#define CRYPTO_PROBE_HEADROWS 400     /* always log the first N samples (baseline) */
#define CRYPTO_PROBE_BUFROWS  8       /* full all-buffer dumps for the first N samples */

static GlobalFunc     *g_global = NULL;
static LWFilterGlobal *g_fg     = NULL;

#define CRYPTO_PROBE_MAXPAIRS 256  /* distinct (Object_ID,Surface_ID) combos tracked */

typedef struct st_ProbePair {
    int          obj, surf;   /* integer buffer values */
    long         count;       /* samples with this pair */
    unsigned int px, py;      /* a pixel where first seen */
} ProbePair;

typedef struct st_ProbeInst {
    FILE  *log;
    FILE  *buf;            /* full per-buffer dump */
    char   sumpath[1024];
    ProbePair pairs[CRYPTO_PROBE_MAXPAIRS];
    int    npairs;
    long   count;          /* total evaluate() calls (samples) */
    long   logged;
    /* summary accumulators */
    unsigned int max_numSamples;
    unsigned int max_sampleIndex;
    long   with_sampleIndex_gt0;   /* # calls where sampleIndex>0  => per-sample proof */
    long   with_subpixel_jitter;   /* # calls where subPixel frac != .5 */
    long   null_ray;               /* # calls with no ray / renderState */
    long   preview_flag;           /* # calls with LWRT_PREVIEW set */
    int    obj_lo, obj_hi;         /* Object_ID range seen */
} ProbeInst;

/* v3: declare ONLY these three. v2 requested every buffersAvailable() name and
 * getVal()'d them, which crashed (NULL read inside Layout) -- some advertised
 * buffers aren't backed in a pixel-filter pass, so getVal derefs NULL. */
static LWCStringUTF8 PROBE_FLAGS[] = {
    LWBUFFER_OBJECT_ID, LWBUFFER_SURFACE_ID, LWBUFFER_COVERAGE, 0
};
static LWCStringUTF8 *probe_flags(LWInstance inst) { (void)inst; return PROBE_FLAGS; }

/* Breadcrumb: overwrite a stage file + flush so a crash leaves the last-reached
 * point on disk. Called only on the very first evaluate() to keep it cheap. */
static void probe_stage(const char *stage)
{
    const char *tmp = getenv("TEMP");
    char path[1024]; FILE *f;
    if (!tmp || !tmp[0]) tmp = getenv("TMP");
    if (!tmp || !tmp[0]) tmp = ".";
    snprintf(path, sizeof path, "%s\\ap_crypto_probe_stage.txt", tmp);
    f = fopen(path, "w");
    if (f) { fprintf(f, "last stage reached: %s\n", stage); fclose(f); }
}

/* Single-threaded, every sample (no MULTITHREADED: race-free diagnostic log). */
static unsigned int probe_render_flags(LWInstance inst) { (void)inst; return LWPFF_EVERYPIXEL; }

static void probe_write_summary(ProbeInst *p)
{
    FILE *s = fopen(p->sumpath, "w");
    if (!s) return;
    fprintf(s,
        "AP_CryptoProbe summary\n"
        "----------------------\n"
        "total samples seen      : %ld\n"
        "max numSamples/pixel    : %u\n"
        "max sampleIndex seen    : %u\n"
        "calls w/ sampleIndex>0  : %ld   <== >0 means evaluate() is PER-SAMPLE\n"
        "calls w/ subpixel jitter: %ld   <== >0 means true sub-pixel positions\n"
        "calls w/ no ray/rstate  : %ld\n"
        "calls w/ LWRT_PREVIEW   : %ld   <== if ~all, this was a preview/VPR pass\n"
        "Object_ID range         : %d .. %d\n"
        "\nVERDICT HINT: sampleIndex>0 AND numSamples>1 => per-sample pixel filter\n"
        "=> full multi-rank Cryptomatte viable. All-zero sampleIndex with\n"
        "numSamples>1 => per-pixel only => use surface-shader route instead.\n",
        p->count, p->max_numSamples, p->max_sampleIndex,
        p->with_sampleIndex_gt0, p->with_subpixel_jitter,
        p->null_ray, p->preview_flag, p->obj_lo, p->obj_hi);
    fprintf(s, "\n== distinct (Object_ID, Surface_ID) pairs seen  (%d) ==\n"
               "   correlate Surface_ID with getIndexID in ap_crypto_probe_names.txt\n",
            p->npairs);
    { int i; for (i = 0; i < p->npairs; ++i)
        fprintf(s, "   Object_ID=%d  Surface_ID=%d   count=%ld   (@ px=%u py=%u)\n",
                p->pairs[i].obj, p->pairs[i].surf, p->pairs[i].count,
                p->pairs[i].px, p->pairs[i].py); }
    fclose(s);
}

/* Dump the surface/object NAME tables so we can correlate the integer
 * Object_ID / Surface_ID buffer values to names (needed for the manifest).
 * Hypothesis: Surface_ID == LWSurfaceFuncs.getIndexID; Object_ID == object
 * enumeration order. Written to %TEMP%\ap_crypto_probe_names.txt. */
static void probe_dump_names(void)
{
    const char *tmp = getenv("TEMP");
    char path[1024]; FILE *f;
    LWSurfaceFuncs *sf; LWItemInfo *ii;
    if (!tmp || !tmp[0]) tmp = getenv("TMP");
    if (!tmp || !tmp[0]) tmp = ".";
    snprintf(path, sizeof path, "%s\\ap_crypto_probe_names.txt", tmp);
    f = fopen(path, "w");
    if (!f) return;
    if (g_global) {
        sf = (LWSurfaceFuncs *)g_global(LWSURFACEFUNCS_GLOBAL, GFUSE_TRANSIENT);
        ii = (LWItemInfo *)g_global(LWITEMINFO_GLOBAL, GFUSE_TRANSIENT);
        fprintf(f, "== SURFACES:  getIndexID | name | scene object ==\n");
        if (sf && sf->first) {
            LWSurfaceID s;
            for (s = sf->first(); s; s = sf->next(s))
                fprintf(f, "  %u\t%s\t[%s]\n",
                        sf->getIndexID ? sf->getIndexID(s) : 0u,
                        sf->name ? sf->name(s) : "?",
                        sf->sceneObject ? sf->sceneObject(s) : "?");
        } else fprintf(f, "  (no Surface Functions global)\n");
        fprintf(f, "\n== OBJECTS:  enum# | name ==\n");
        if (ii && ii->first) {
            LWItemID it; int k = 0;
            for (it = ii->first(LWI_OBJECT, NULL); it; it = ii->next(it))
                fprintf(f, "  %d\t%s\n", k++, ii->name ? ii->name(it) : "?");
        } else fprintf(f, "  (no Item Info global)\n");
    }
    fclose(f);
}

static LWInstance probe_create(void *priv, void *context, LWError *err)
{
    ProbeInst *p;
    const char *tmp;
    char path[1024];
    (void)priv; (void)context; (void)err;

    p = (ProbeInst *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->obj_lo = 1<<30; p->obj_hi = -(1<<30);

    tmp = getenv("TEMP");
    if (!tmp || !tmp[0]) tmp = getenv("TMP");
    if (!tmp || !tmp[0]) tmp = ".";
    snprintf(path, sizeof path, "%s\\ap_crypto_probe.txt", tmp);
    snprintf(p->sumpath, sizeof p->sumpath, "%s\\ap_crypto_probe_summary.txt", tmp);
    p->log = fopen(path, "w");
    snprintf(path, sizeof path, "%s\\ap_crypto_probe_buffers.txt", tmp);
    p->buf = fopen(path, "w");
    if (p->buf) {
        fprintf(p->buf, "# AP_CryptoProbe per-sample buffer dump (first %d samples)\n"
                        "# Object_ID/Surface_ID are integers; see ap_crypto_probe_names.txt for the name map\n",
                CRYPTO_PROBE_BUFROWS);
        fflush(p->buf);
    }
    probe_dump_names();
    if (p->log) {
        fprintf(p->log,
            "# AP_CryptoProbe v2 per-sample dump (head %d rows + all sampleIndex>0/pass>0, cap %d)\n"
            "# call\tpx\tpy\tspx\tspy\tsIdx\tnS\tpass\tthr\trayflags\tObjID\tSurfID\n",
            CRYPTO_PROBE_HEADROWS, CRYPTO_PROBE_MAXLOG);
        fflush(p->log);
    }
    return (LWInstance)p;
}

static void probe_destroy(LWInstance inst)
{
    ProbeInst *p = (ProbeInst *)inst;
    if (!p) return;
    probe_write_summary(p);
    if (p->log) {
        fprintf(p->log, "# total samples seen: %ld (logged %ld)\n", p->count, p->logged);
        fclose(p->log);
    }
    if (p->buf) fclose(p->buf);
    free(p);
}

static LWError probe_copy(LWInstance a, LWInstance b) { (void)a; (void)b; return NULL; }

static void probe_evaluate(LWInstance inst, const LWPixelAccess *pa)
{
    ProbeInst *p = (ProbeInst *)inst;
    const LWRay *ray;
    const LWRenderState *rs;
    LWRenderStateID rsid;
    unsigned int px = 0, py = 0, sIdx = 0, nS = 0, pass = 0, thr = 0, rflags = 0;
    double spx = 0.0, spy = 0.0, fx;
    float objid = 0.0f, surfid = 0.0f;
    int want_log;

    if (!p || !pa) return;
    ++p->count;
    { int first = (p->count == 1);

    if (first) probe_stage("A: entered evaluate");
    if (pa->getVal) {
        pa->getVal(LWBUFFER_OBJECT_ID,  1, &objid);
        pa->getVal(LWBUFFER_SURFACE_ID, 1, &surfid);
    }
    if (first) probe_stage("B: getVal(Object/Surface) ok");
    if ((int)objid < p->obj_lo) p->obj_lo = (int)objid;
    if ((int)objid > p->obj_hi) p->obj_hi = (int)objid;

    ray  = pa->ray;
    rs   = ray ? ray->renderState   : NULL;
    rsid = ray ? ray->renderStateID : NULL;
    if (ray) rflags = ray->flags;
    if (!ray || !rs) ++p->null_ray;
    if (first) { char s[80]; snprintf(s, sizeof s, "C: ray=%p rs=%p rsid=%p", (void*)ray, (void*)rs, (void*)rsid); probe_stage(s); }

    if (rs && rsid) {
        unsigned int *ip;
        if (first) probe_stage("D: before pixelXY");
        if (rs->pixelXY)     rs->pixelXY(rsid, &px, &py);
        if (first) probe_stage("E: before subPixelXY");
        if (rs->subPixelXY)  rs->subPixelXY(rsid, &spx, &spy);
        if (first) probe_stage("F: before sampleIndex");
        if (rs->sampleIndex && (ip = rs->sampleIndex(rsid))) sIdx = *ip;
        if (first) probe_stage("G: before numSamples");
        if (rs->numSamples  && (ip = rs->numSamples(rsid)))  nS   = *ip;
        if (first) probe_stage("H: before samplePass");
        if (rs->samplePass  && (ip = rs->samplePass(rsid)))  pass = *ip;
        if (first) probe_stage("I: before threadIndex");
        if (rs->threadIndex) thr = rs->threadIndex(rsid);
        if (first) probe_stage("J: renderState calls done");
    }

    /* track distinct (Object_ID, Surface_ID) integer pairs (skip apron) */
    if (px != 0xFFFFFFFFu && py != 0xFFFFFFFFu) {
        int oi = (int)objid, si = (int)surfid, j, found = 0;
        for (j = 0; j < p->npairs; ++j)
            if (p->pairs[j].obj == oi && p->pairs[j].surf == si) { ++p->pairs[j].count; found = 1; break; }
        if (!found && p->npairs < CRYPTO_PROBE_MAXPAIRS) {
            p->pairs[p->npairs].obj = oi; p->pairs[p->npairs].surf = si;
            p->pairs[p->npairs].count = 1; p->pairs[p->npairs].px = px; p->pairs[p->npairs].py = py;
            ++p->npairs;
        }
    }

    if (nS > p->max_numSamples)    p->max_numSamples  = nS;
    if (sIdx > p->max_sampleIndex) p->max_sampleIndex = sIdx;
    if (sIdx > 0)                  ++p->with_sampleIndex_gt0;
    if (rflags & LWRT_PREVIEW)     ++p->preview_flag;
    fx = spx - (double)(long)spx;
    if (fx < 0.499 || fx > 0.501)  ++p->with_subpixel_jitter;

    /* dump ONLY the three declared buffers (safe) for the first few samples */
    if (p->buf && p->count <= CRYPTO_PROBE_BUFROWS && pa->getVal) {
        float cov = 0.0f;
        pa->getVal(LWBUFFER_COVERAGE, 1, &cov);
        fprintf(p->buf, "sample %ld  px=%u py=%u spx=%.4f spy=%.4f sIdx=%u nS=%u pass=%u | "
                        "Object_ID=%.6g Surface_ID=%.6g Coverage=%.6g\n",
                p->count, px, py, spx, spy, sIdx, nS, pass, objid, surfid, cov);
        fflush(p->buf);
    }
    if (first) probe_stage("Z: first evaluate complete");
    } /* first scope */

    /* log the baseline head, plus every refinement sample (the interesting ones) */
    want_log = (p->logged < CRYPTO_PROBE_MAXLOG) &&
               (p->count <= CRYPTO_PROBE_HEADROWS || sIdx > 0 || pass > 0);
    if (p->log && want_log) {
        fprintf(p->log, "%ld\t%u\t%u\t%.4f\t%.4f\t%u\t%u\t%u\t%u\t0x%x\t%.6g\t%.6g\n",
                p->count, px, py, spx, spy, sIdx, nS, pass, thr, rflags, objid, surfid);
        ++p->logged;
        if ((p->logged & 63) == 0) fflush(p->log);
    }
    if ((p->count % 5000) == 0) probe_write_summary(p);   /* live digest */
    /* Probe must not alter the image. */
}

XCALL_(int)
CryptoProbe(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWPixelFilterHandler *local = (LWPixelFilterHandler *)vlocal;
    (void)global; (void)serverData;
    if (version != LWPIXELFILTER_VERSION) return AFUNC_BADVERSION;
    g_global = global;
    if (!g_fg) g_fg = (LWFilterGlobal *)global(LWFILTERINFO_GLOBAL, GFUSE_TRANSIENT);
    local->inst->create  = probe_create;
    local->inst->destroy = probe_destroy;
    local->inst->copy    = probe_copy;
    local->inst->load    = NULL;
    local->inst->save    = NULL;
    local->evaluate    = probe_evaluate;
    local->flags       = probe_flags;
    local->renderFlags = probe_render_flags;
    local->preprocess  = NULL;
    return AFUNC_OK;
}
