/*
 * crypto_matte.c - "AP_Cryptomatte": generate spec-compliant Cryptomatte from
 * a LightWave render.
 *
 * Two cooperating servers in this DLL sharing module-global state:
 *   - AP_Cryptomatte        (PixelFilter)  accumulates per-SAMPLE coverage.
 *   - AP_CryptomatteOutput  (ImageFilter)  at frame end, ranks + writes the EXR.
 *
 * Why a pixel filter: it fires per sample BEFORE antialiasing, exposing each
 * sample's Object_ID/Surface_ID and sub-pixel position -- exactly the data
 * Cryptomatte needs and that LightWave's reconstruction-averaged ID buffers
 * cannot give. (Confirmed in-engine: up to 8 spp, real jitter, final render.)
 *
 * Mapping (confirmed): Surface_ID buffer int == LWSurfaceFuncs.getIndexID; a
 * surface yields BOTH its material name (name) and owning object (sceneObject),
 * so Surface_ID alone drives CryptoObject and CryptoMaterial. Object_ID buffer
 * is not needed for naming.
 *
 * v1: single-threaded accumulation (pixel filter runs single-threaded -- no
 * LWPFF_MULTITHREADED), own dedicated EXR output. Merge-into-multilayer mode
 * and MT are follow-ups.
 *
 * Output path set by the "APImageIO_CryptomatteOutput <base>" command; the
 * output filter appends frame + ".cryptomatte.exr". Inert if unset.
 */
#include <lwserver.h>
#include <lwfilter.h>
#include <lwrender.h>
#include <lwsurf.h>
#include <lwgeneric.h>
#include <windows.h>          /* CRITICAL_SECTION: evaluate() is called on many render threads */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "crypto_encode.h"
#include "crypto_accum.h"
#include "crypto_matte.h"
#include "oiiobridge.h"
#include "ap_prefs.h"
#include "exr_session.h"
#include "ap_report.h"

#define CM_NTYPES 3                 /* 0 = CryptoObject, 1 = CryptoMaterial, 2 = CryptoAsset */
#define CM_NRANKS 6                 /* default ranks -> 3 EXR levels/type    */
#define CM_LEVELS ((CM_NRANKS + 1) / 2)

#define CM_MAXTHREADS 256           /* per-thread accumulators (index by render threadIndex) */

static LWCStringASCII CM_TYPENAME[CM_NTYPES] = { "CryptoObject", "CryptoMaterial", "CryptoAsset" };

/* ---- module-global shared state ---- */
static GlobalFunc  *g_cm_global   = NULL;
/* Per-thread accumulators: each render thread owns one, so evaluate() adds
 * LOCK-FREE. Merged into one at frame end (in the output filter). */
static cm_accum    *g_accum[CM_MAXTHREADS];   /* NULL until a thread first touches it */
static unsigned int *g_shared_total = NULL;   /* one per-pixel sample-count array, shared by all threads */
static int          g_frame_w = 0, g_frame_h = 0;

/* surface table: indexed by getIndexID, gives the float id per type */
static float       *g_surf_id[CM_NTYPES] = { NULL, NULL };  /* [g_surf_cap] */
static int          g_surf_cap    = 0;        /* highest getIndexID + 1      */

static cm_manifest  g_manifest[CM_NTYPES];    /* per-type name->hash JSON    */
static int          g_tables_built = 0;

/* evaluate() runs on multiple render threads; this guards the shared accumulator
 * (and one-time setup). Set up in the activation function. */
static CRITICAL_SECTION g_lock;
static int              g_lock_init = 0;

/* dedup helper: array of names already added to a manifest */
typedef struct { char **v; int n, cap; } cm_nameset;
static int nameset_add(cm_nameset *s, const char *name)   /* 1 if newly added */
{
    int i;
    for (i = 0; i < s->n; ++i) if (!strcmp(s->v[i], name)) return 0;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 32;
                          s->v = (char **)realloc(s->v, s->cap * sizeof(char *)); }
    s->v[s->n++] = _strdup(name);
    return 1;
}
static void nameset_free(cm_nameset *s)
{ int i; for (i = 0; i < s->n; ++i) free(s->v[i]); free(s->v); s->v = NULL; s->n = s->cap = 0; }

/* ---- build / free the surface->id table + manifests (at render init) ---- */
static void cm_build_tables(void)
{
    LWSurfaceFuncs *sf; LWItemInfo *ii; LWObjectInfo *oi;
    LWSurfaceID s; int t, maxid = 0;
    cm_nameset seen_obj = {0}, seen_mat = {0}, seen_asset = {0};
    char **omap_name = NULL, **omap_file = NULL, **omap_root = NULL;
    int omap_n = 0, omap_cap = 0, m;

    if (g_tables_built || !g_cm_global) return;
    sf = (LWSurfaceFuncs *)g_cm_global(LWSURFACEFUNCS_GLOBAL, GFUSE_TRANSIENT);
    ii = (LWItemInfo *)g_cm_global(LWITEMINFO_GLOBAL, GFUSE_TRANSIENT);
    oi = (LWObjectInfo *)g_cm_global(LWOBJECTINFO_GLOBAL, GFUSE_TRANSIENT);
    if (!sf || !sf->first) return;
    /* No surfaces yet (filter instantiated during scene load, before surfaces
     * populate)? Don't latch -- let a later call (first sample) build the table. */
    if (!sf->first()) return;

    for (t = 0; t < CM_NTYPES; ++t) cm_manifest_init(&g_manifest[t]);

    /* pass 1: find highest getIndexID */
    for (s = sf->first(); s; s = sf->next(s)) {
        unsigned int idx = sf->getIndexID ? sf->getIndexID(s) : 0;
        if ((int)idx > maxid) maxid = (int)idx;
    }
    g_surf_cap = maxid + 1;
    for (t = 0; t < CM_NTYPES; ++t)
        g_surf_id[t] = (float *)calloc((size_t)g_surf_cap, sizeof(float));

    /* object -> asset name (top-most object ancestor), from the item hierarchy
     * -- so CryptoAsset groups a parented rig/group under its root. Each object
     * is keyed by BOTH its item name and its file path, because sceneObject()
     * (the per-pixel key below) returns the path for a loaded .lwo and the
     * name for an unsaved one. */
    if (ii && ii->first) {
        LWItemID it;
        for (it = ii->first(LWI_OBJECT, NULL); it; it = ii->next(it)) {
            LWItemID root = it, p;
            const char *onm, *ofn, *rnm;
            while (ii->parent && (p = ii->parent(root)) &&
                   (!ii->type || ii->type(p) == LWI_OBJECT)) root = p;
            onm = ii->name ? ii->name(it)   : "";
            ofn = (oi && oi->filename) ? oi->filename(it) : "";
            rnm = ii->name ? ii->name(root) : "";
            if (omap_n == omap_cap) {
                int ncap = omap_cap ? omap_cap * 2 : 32;
                char **nn = (char **)realloc(omap_name, ncap * sizeof(char *));
                char **nf, **nr;
                if (nn) omap_name = nn;
                nf = nn ? (char **)realloc(omap_file, ncap * sizeof(char *)) : NULL;
                if (nf) omap_file = nf;
                nr = nf ? (char **)realloc(omap_root, ncap * sizeof(char *)) : NULL;
                if (nr) omap_root = nr;
                if (!nr) break;                          /* OOM: asset falls back to object */
                omap_cap = ncap;
            }
            omap_name[omap_n] = _strdup(onm ? onm : "");
            omap_file[omap_n] = _strdup(ofn ? ofn : "");
            omap_root[omap_n] = _strdup(rnm ? rnm : "");
            ++omap_n;
        }
    }

    /* pass 2: fill the id tables + manifests. Critical: the manifest name for each
     * type must be the SAME string used to derive the per-pixel id, or Nuke's
     * hash(name) won't match the pixel. Object/asset names come from sceneObject()
     * / the hierarchy root (what the pixel id uses), NOT LWItemInfo.name() where
     * they'd differ (path vs short name) for loaded .lwo objects. */
    for (s = sf->first(); s; s = sf->next(s)) {
        unsigned int idx = sf->getIndexID ? sf->getIndexID(s) : 0;
        const char *mat = sf->name ? sf->name(s) : "";
        const char *obj = sf->sceneObject ? sf->sceneObject(s) : "";
        const char *asset = obj;               /* fallback: asset = object */
        if ((int)idx >= g_surf_cap) continue;
        for (m = 0; m < omap_n; ++m)
            if ((omap_file[m] && omap_file[m][0] && !_stricmp(omap_file[m], obj)) ||
                (omap_name[m] && !strcmp(omap_name[m], obj))) {
                if (omap_root[m]) asset = omap_root[m];
                break;
            }
        if (g_surf_id[0]) g_surf_id[0][idx] = cm_name_to_id(obj);    /* CryptoObject   */
        if (g_surf_id[1]) g_surf_id[1][idx] = cm_name_to_id(mat);    /* CryptoMaterial */
        if (g_surf_id[2]) g_surf_id[2][idx] = cm_name_to_id(asset);  /* CryptoAsset    */
        if (nameset_add(&seen_obj,   obj))   cm_manifest_add(&g_manifest[0], obj,   cm_name_hash(obj));
        if (nameset_add(&seen_mat,   mat))   cm_manifest_add(&g_manifest[1], mat,   cm_name_hash(mat));
        if (nameset_add(&seen_asset, asset)) cm_manifest_add(&g_manifest[2], asset, cm_name_hash(asset));
    }

    for (m = 0; m < omap_n; ++m) { free(omap_name[m]); free(omap_file[m]); free(omap_root[m]); }
    free(omap_name); free(omap_file); free(omap_root);
    nameset_free(&seen_obj); nameset_free(&seen_mat); nameset_free(&seen_asset);
    g_tables_built = 1;
}

static void cm_free_tables(void)
{
    int t;
    for (t = 0; t < CM_NTYPES; ++t) {
        free(g_surf_id[t]); g_surf_id[t] = NULL;
        if (g_manifest[t].json) cm_manifest_free(&g_manifest[t]);
    }
    g_surf_cap = 0; g_tables_built = 0;
}

/* ================= PixelFilter: AP_Cryptomatte ================= */

static void cm_rend_cleanup(LWInstance inst)
{ (void)inst;
  int i;
  if (!g_lock_init) return;
  EnterCriticalSection(&g_lock);
  cm_free_tables();
  for (i = 0; i < CM_MAXTHREADS; ++i)
      if (g_accum[i]) { cm_accum_free(g_accum[i]); g_accum[i] = NULL; }
  free(g_shared_total); g_shared_total = NULL;
  g_frame_w = g_frame_h = 0;
  LeaveCriticalSection(&g_lock); }

/* preprocess: the pre-render, single-threaded hook (NOT called for pixel filters
 * in this LW build -- setup happens in create()/evaluate() instead). Kept for
 * forward-compat: build tables + capture frame dimensions. */
static void cm_pf_preprocess(LWInstance inst, LWPreprocessAccess *ppa,
                             LWRenderGlobals *rg, LWRenderState *rs, LWRenderStateID *rsid)
{
    (void)inst; (void)ppa; (void)rs; (void)rsid;
    if (!g_lock_init) { InitializeCriticalSection(&g_lock); g_lock_init = 1; }
    EnterCriticalSection(&g_lock);
    cm_build_tables();
    if (rg && rg->frameWidth > 0 && rg->frameHeight > 0) {
        g_frame_w = (int)rg->frameWidth; g_frame_h = (int)rg->frameHeight;
    }
    LeaveCriticalSection(&g_lock);
}

static LWCStringUTF8 CM_PF_FLAGS[] = { LWBUFFER_SURFACE_ID, 0 };
static LWCStringUTF8 *cm_pf_flags(LWInstance inst) { (void)inst; return CM_PF_FLAGS; }
static unsigned int   cm_pf_renderFlags(LWInstance inst) { (void)inst; return LWPFF_EVERYPIXEL; }

static void cm_pf_evaluate(LWInstance inst, const LWPixelAccess *pa)
{
    const LWRay *ray; const LWRenderState *rs; LWRenderStateID rsid;
    unsigned int px = 0, py = 0; float surfid = 0.0f; float ids[CM_NTYPES];
    int si, ti;
    cm_accum *acc;
    (void)inst;
    if (!pa) return;

    ray = pa->ray;
    rs  = ray ? ray->renderState   : NULL;
    rsid= ray ? ray->renderStateID : NULL;
    if (!rs || !rsid || !rs->pixelXY) return;

    /* this render thread's index -> its own accumulator (lock-free adds) */
    ti = rs->threadIndex ? (int)rs->threadIndex(rsid) : 0;
    if (ti < 0 || ti >= CM_MAXTHREADS) ti = CM_MAXTHREADS - 1;   /* clamp (shared slot) */

    /* preprocess isn't called for pixel filters here -> lazily build tables and
     * this thread's accumulator on first use (double-checked, locked). */
    if (!g_accum[ti]) {
        EnterCriticalSection(&g_lock);
        if (!g_tables_built) cm_build_tables();
        if (g_frame_w == 0 && ray && ray->renderGlobals &&
            ray->renderGlobals->frameWidth > 0 && ray->renderGlobals->frameHeight > 0) {
            g_frame_w = (int)ray->renderGlobals->frameWidth;
            g_frame_h = (int)ray->renderGlobals->frameHeight;
        }
        if (g_frame_w > 0 && !g_shared_total)      /* one shared per-pixel total for all threads */
            g_shared_total = (unsigned int *)calloc((size_t)g_frame_w * g_frame_h, sizeof(unsigned int));
        if (!g_accum[ti] && g_frame_w > 0 && g_shared_total)
            g_accum[ti] = cm_accum_create_shared(g_frame_w, g_frame_h, CM_NTYPES, CM_NRANKS, g_shared_total);
        LeaveCriticalSection(&g_lock);
    }
    acc = g_accum[ti];
    if (!acc) return;

    rs->pixelXY(rsid, &px, &py);
    if (px == 0xFFFFFFFFu || py == 0xFFFFFFFFu) return;      /* skip apron */

    if (pa->getVal) pa->getVal(LWBUFFER_SURFACE_ID, 1, &surfid);
    si = (int)(surfid + 0.5f);
    /* background (si<=0) contributes to the pixel total but no id */
    ids[0] = (si > 0 && si < g_surf_cap && g_surf_id[0]) ? g_surf_id[0][si] : 0.0f;
    ids[1] = (si > 0 && si < g_surf_cap && g_surf_id[1]) ? g_surf_id[1][si] : 0.0f;
    ids[2] = (si > 0 && si < g_surf_cap && g_surf_id[2]) ? g_surf_id[2][si] : 0.0f;

    /* lock-free: this thread is the sole writer of acc */
    cm_accum_add(acc, (int)px, (int)py, ids);
}

static LWInstance cm_pf_create(void *p, void *c, LWError *e)
{
    (void)p; (void)c; (void)e;
    if (!g_lock_init) { InitializeCriticalSection(&g_lock); g_lock_init = 1; }
    /* Deliberately NOT building the surface table here: create() runs when the
     * filter is added (or the scene loads), and anything edited after that --
     * objects added, surfaces renamed, another scene loaded -- would render
     * with a stale table. The first sample of each frame builds it instead,
     * and cm_crypto_produce / cleanup free it, so every frame is current. */
    return (LWInstance)1;
}
static void       cm_pf_destroy(LWInstance i) { (void)i; }
static LWError    cm_pf_copy(LWInstance a, LWInstance b) { (void)a;(void)b; return NULL; }

XCALL_(int)
CryptoMatte(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWPixelFilterHandler *local = (LWPixelFilterHandler *)vlocal;
    (void)serverData;
    if (version != LWPIXELFILTER_VERSION) return AFUNC_BADVERSION;
    g_cm_global = global;
    if (!g_lock_init) { InitializeCriticalSection(&g_lock); g_lock_init = 1; }
    local->inst->create  = cm_pf_create;
    local->inst->destroy = cm_pf_destroy;
    local->inst->copy    = cm_pf_copy;
    local->inst->load    = NULL;
    local->inst->save    = NULL;
    if (local->rend) local->rend->cleanup = cm_rend_cleanup;
    local->evaluate    = cm_pf_evaluate;
    local->flags       = cm_pf_flags;
    local->renderFlags = cm_pf_renderFlags;
    local->preprocess  = cm_pf_preprocess;
    return AFUNC_OK;
}

static const char *cm_suffix(int c) { static const char *s[4] = {"R","G","B","A"}; return s[c&3]; }

/* Stable pseudo-random preview colour from a float id's bits (Cryptomatte
 * preview convention: low 3 bytes of the id float bits -> RGB). */
static void cm_preview_color(float idf, float *r, float *g, float *b)
{
    unsigned int bits;
    memcpy(&bits, &idf, sizeof bits);
    *r = (float)( bits        & 0xFF) / 255.0f;
    *g = (float)((bits >>  8) & 0xFF) / 255.0f;
    *b = (float)((bits >> 16) & 0xFF) / 255.0f;
}

/* ---- shared crypto emission (used by own-EXR writer AND multilayer merge) ---- */

int cm_crypto_produce(int w, int h, CMCryptoData *out)
{
    cm_accum *acc = NULL;
    int i, t, L, c, ci, x, y, nc, a;
    if (!out || w <= 0 || h <= 0 || !g_lock_init) return 0;
    memset(out, 0, sizeof *out);

    EnterCriticalSection(&g_lock);
    for (i = 0; i < CM_MAXTHREADS; ++i)
        if (g_accum[i]) { if (!acc) acc = g_accum[i]; else cm_accum_merge(acc, g_accum[i]); }
    if (!acc) { LeaveCriticalSection(&g_lock); return 0; }
    cm_accum_finalize(acc);

    /* per type: 3 preview RGB channels + CM_LEVELS*4 ranked (id,coverage) channels */
    nc = CM_NTYPES * (3 + CM_LEVELS * 4);
    out->names       = (char **)calloc((size_t)nc, sizeof(char *));
    out->interleaved = (float *)malloc((size_t)w * h * nc * sizeof(float));
    if (!out->names || !out->interleaved) {
        free(out->names); free(out->interleaved); memset(out, 0, sizeof *out);
        goto consume;    /* still free the accumulators */
    }
    out->nchan = nc;

    ci = 0;
    for (t = 0; t < CM_NTYPES; ++t) {
        char nm[64];
        /* preview channels: "<Type>.R/.G/.B" (hash-coloured flat preview) */
        for (c = 0; c < 3; ++c) {
            snprintf(nm, sizeof nm, "%s.%s", CM_TYPENAME[t], cm_suffix(c));
            out->names[ci++] = _strdup(nm);
        }
        for (L = 0; L < CM_LEVELS; ++L)
            for (c = 0; c < 4; ++c) {
                snprintf(nm, sizeof nm, "%s%02d.%s", CM_TYPENAME[t], L, cm_suffix(c));
                out->names[ci++] = _strdup(nm);
            }
    }

    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            float *px = out->interleaved + ((size_t)y * w + x) * nc;
            int k = 0;
            for (t = 0; t < CM_NTYPES; ++t) {
                float pr = 0.0f, pg = 0.0f, pb = 0.0f;
                int r;
                /* preview = coverage-weighted sum of each rank's hash colour */
                for (r = 0; r < CM_NRANKS; ++r) {
                    float cr, cg, cb, cov = cm_accum_cov(acc, t, x, y, r);
                    if (cov <= 0.0f) continue;
                    cm_preview_color(cm_accum_id(acc, t, x, y, r), &cr, &cg, &cb);
                    pr += cr * cov; pg += cg * cov; pb += cb * cov;
                }
                px[k++] = pr; px[k++] = pg; px[k++] = pb;
                for (L = 0; L < CM_LEVELS; ++L) {
                    int r0 = 2*L, r1 = 2*L + 1;
                    px[k++] = cm_accum_id (acc, t, x, y, r0);
                    px[k++] = cm_accum_cov(acc, t, x, y, r0);
                    px[k++] = cm_accum_id (acc, t, x, y, r1);
                    px[k++] = cm_accum_cov(acc, t, x, y, r1);
                }
            }
        }

    out->attrN = (char **)calloc((size_t)CM_NTYPES * 4, sizeof(char *));
    out->attrV = (char **)calloc((size_t)CM_NTYPES * 4, sizeof(char *));
    a = 0;
    if (out->attrN && out->attrV)
        for (t = 0; t < CM_NTYPES; ++t) {
            char key[8], nm[64];
            cm_type_key(CM_TYPENAME[t], key);
            snprintf(nm, 64, "cryptomatte/%s/name", key);
            out->attrN[a] = _strdup(nm); out->attrV[a] = _strdup(CM_TYPENAME[t]); ++a;
            snprintf(nm, 64, "cryptomatte/%s/hash", key);
            out->attrN[a] = _strdup(nm); out->attrV[a] = _strdup("MurmurHash3_32"); ++a;
            snprintf(nm, 64, "cryptomatte/%s/conversion", key);
            out->attrN[a] = _strdup(nm); out->attrV[a] = _strdup("uint32_to_float32"); ++a;
            snprintf(nm, 64, "cryptomatte/%s/manifest", key);
            out->attrN[a] = _strdup(nm);
            out->attrV[a] = _strdup(g_manifest[t].json ? g_manifest[t].json : "{}"); ++a;
        }
    out->nattr = a;

consume:
    for (i = 0; i < CM_MAXTHREADS; ++i)
        if (g_accum[i]) { cm_accum_free(g_accum[i]); g_accum[i] = NULL; }
    free(g_shared_total); g_shared_total = NULL;
    g_frame_w = g_frame_h = 0;
    cm_free_tables();      /* manifests were copied into attrV above; rebuilt next frame */
    LeaveCriticalSection(&g_lock);
    return out->nchan > 0;
}

void cm_crypto_free(CMCryptoData *out)
{
    int i;
    if (!out) return;
    if (out->names) { for (i = 0; i < out->nchan; ++i) free(out->names[i]); free(out->names); }
    free(out->interleaved);
    if (out->attrN) { for (i = 0; i < out->nattr; ++i) free(out->attrN[i]); free(out->attrN); }
    if (out->attrV) { for (i = 0; i < out->nattr; ++i) free(out->attrV[i]); free(out->attrV); }
    memset(out, 0, sizeof *out);
}

/* ================= ImageFilter: AP_CryptomatteOutput ================= */

static LWCStringASCII CM_IF_FLAGS[] = { 0 };
static LWCStringASCII *cm_if_flags(LWInstance inst) { (void)inst; return CM_IF_FLAGS; }

static LWError cm_if_process(LWInstance inst, const LWFilterAccess *fa)
{
    const char *base = ap_crypto_output();
    char defbase[1024];
    APImageIOPrefs prefs;
    int w, h, x, y, ci, nchan;
    char **names; float *inter;
    CMCryptoData cd;
    char fname[1200], comp[32], errbuf[256];
    (void)inst;

    if (!fa) return NULL;
    apprefs_read(g_cm_global, &prefs);
    { int mode = ap_crypto_mode();               /* session override > pref */
      if (mode < 0) mode = prefs.crypto_mode;
      if (mode != 0) return NULL; }              /* merge mode: the multilayer filter emits it */

    /* Path: session command > the "Multilayer/Cryptomatte Output Path" pref
     * (the only route a user can set by hand - LayoutGeneric commands take no
     * typed argument) > %TEMP%\ap_cryptomatte_, so the filter still works when
     * just added. Own mode appends ".cryptomatte.exr", so sharing the base with
     * the multilayer file ("<base>NNNN.exr") does not collide. */
    if (!base || !base[0]) base = prefs.ml_output;
    if (!base || !base[0]) {
        const char *tmp = getenv("TEMP");
        if (!tmp || !tmp[0]) tmp = getenv("TMP");
        if (!tmp || !tmp[0]) tmp = "C:\\tmp";
        snprintf(defbase, sizeof defbase, "%s\\ap_cryptomatte_", tmp);
        base = defbase;
    }
    w = fa->width; h = fa->height;
    if (w <= 0 || h <= 0 || !fa->getLine) return NULL;

    if (!cm_crypto_produce(w, h, &cd)) return NULL;   /* no accumulation -> nothing to write */

    nchan = 4 + cd.nchan;                             /* beauty RGBA + crypto */
    names = (char **)calloc((size_t)nchan, sizeof(char *));
    inter = (float *)malloc((size_t)w * h * nchan * sizeof(float));
    if (!names || !inter) {
        free(names); free(inter); cm_crypto_free(&cd);
        ap_report_save_error(g_cm_global, "Cryptomatte", base, "out of memory");
        return NULL;
    }

    names[0] = _strdup("R"); names[1] = _strdup("G"); names[2] = _strdup("B"); names[3] = _strdup("A");
    for (ci = 0; ci < cd.nchan; ++ci) names[4 + ci] = _strdup(cd.names[ci]);

    for (y = 0; y < h; ++y) {
        const float *br = fa->getLine(LWBUFFER_FINAL_RENDER_RGBA, 0, y);
        const float *bg = fa->getLine(LWBUFFER_FINAL_RENDER_RGBA, 1, y);
        const float *bb = fa->getLine(LWBUFFER_FINAL_RENDER_RGBA, 2, y);
        const float *ba = fa->getLine(LWBUFFER_FINAL_RENDER_RGBA, 3, y);
        for (x = 0; x < w; ++x) {
            float *px = inter + ((size_t)y * w + x) * nchan;
            const float *cp = cd.interleaved + ((size_t)y * w + x) * cd.nchan;
            px[0] = br ? br[x] : 0.0f;  px[1] = bg ? bg[x] : 0.0f;
            px[2] = bb ? bb[x] : 0.0f;  px[3] = ba ? ba[x] : 1.0f;
            memcpy(px + 4, cp, (size_t)cd.nchan * sizeof(float));
        }
    }

    /* compression: session override > pref (prefs already read above) */
    if (ap_exr_compression()[0]) strncpy(comp, ap_exr_compression(), sizeof comp - 1);
    else strncpy(comp, apprefs_exr_compression_name(prefs.exr_compression), sizeof comp - 1);
    comp[sizeof comp - 1] = '\0';
    {   /* a lossy codec (pxr24/dwa/b44) would alter the id bits -> zip */
        const char *safe = ap_exr_lossless_compression(comp);
        if (safe != comp) { strncpy(comp, safe, sizeof comp - 1); comp[sizeof comp - 1] = '\0'; }
    }

    { char cbase[1024];
      LWSceneInfo *si = g_cm_global ? (LWSceneInfo *)g_cm_global(LWSCENEINFO_GLOBAL, GFUSE_TRANSIENT) : NULL;
      ap_exr_resolve_base(base, si ? si->name : NULL, cbase, sizeof cbase);
      snprintf(fname, sizeof fname, "%s%04d.cryptomatte.exr", cbase, (int)fa->frame); }
    errbuf[0] = '\0';
    if (!oiiobridge_save_exr_multi_meta(fname, w, h, nchan, (const char *const *)names, inter,
                                        0 /* 32-bit float required */, comp,
                                        (const char *const *)cd.attrN, (const char *const *)cd.attrV,
                                        cd.nattr, errbuf, sizeof errbuf))
        ap_report_save_error(g_cm_global, "Cryptomatte", fname, errbuf);

    for (ci = 0; ci < nchan; ++ci) free(names[ci]);
    free(names); free(inter);
    cm_crypto_free(&cd);
    return NULL;
}

static LWInstance cm_if_create(void *p, void *c, LWError *e) { (void)p;(void)c;(void)e; return (LWInstance)1; }
static void       cm_if_destroy(LWInstance i) { (void)i; }
static LWError    cm_if_copy(LWInstance a, LWInstance b) { (void)a;(void)b; return NULL; }

XCALL_(int)
CryptoMatteOutput(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWImageFilterHandler *local = (LWImageFilterHandler *)vlocal;
    (void)serverData;
    if (version != LWIMAGEFILTER_VERSION) return AFUNC_BADVERSION;
    if (!g_cm_global) g_cm_global = global;
    local->inst->create  = cm_if_create;
    local->inst->destroy = cm_if_destroy;
    local->inst->copy    = cm_if_copy;
    local->inst->load    = NULL;
    local->inst->save    = NULL;
    local->process = cm_if_process;
    local->flags   = cm_if_flags;
    return AFUNC_OK;
}

/* ================= command: set output base path ================= */

XCALL_(int)
CryptomatteOutputCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWLayoutGeneric *lg = (LWLayoutGeneric *)vlocal;
    (void)global; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    if (lg && lg->commandArguments) ap_crypto_set_output(lg->commandArguments(lg->data));
    else                            ap_crypto_set_output("");
    return AFUNC_OK;
}

/* "APImageIO_CryptomatteMode <own|merge>" -- select the output mode.
 * own   = dedicated .cryptomatte.exr (AP Cryptomatte Output filter)
 * merge = fold Crypto channels into the AP MultilayerEXR output. */
XCALL_(int)
CryptomatteModeCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWLayoutGeneric *lg = (LWLayoutGeneric *)vlocal;
    (void)global; (void)serverData;
    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    if (lg && lg->commandArguments) {
        const char *a = lg->commandArguments(lg->data);
        ap_crypto_set_mode((a && (a[0] == 'm' || a[0] == 'M')) ? 1 : 0);   /* "merge" -> 1 */
    }
    return AFUNC_OK;
}
