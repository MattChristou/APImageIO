/*
 * crypto_accum.c - per-pixel Cryptomatte coverage accumulator (see header).
 */
#include "crypto_accum.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define CM_ATOMIC_INC(p) InterlockedIncrement((volatile LONG *)(p))
#else
#define CM_ATOMIC_INC(p) __atomic_add_fetch((p), 1, __ATOMIC_RELAXED)
#endif

/* Open-addressing hash of (pixel, idbits) -> accumulated weight. One per type.
 * Empty slot marker: key_pixel == CM_EMPTY. Valid pixel indices are < w*h,
 * always well below CM_EMPTY. */
#define CM_EMPTY 0xFFFFFFFFu

typedef struct {
    unsigned int *key_pixel;   /* [cap] */
    unsigned int *key_id;      /* [cap] idbits (float reinterpreted) */
    float        *val;         /* [cap] accumulated weight (sample count) */
    size_t        cap;         /* power of two */
    size_t        count;
} cm_hash;

struct cm_accum {
    int            w, h, ntypes, nranks;
    unsigned int  *total;      /* [w*h] sample count per pixel */
    int            total_shared; /* 1 => total is shared+atomic, not owned */
    cm_hash       *hash;       /* [ntypes] live during accumulation */
    /* finalized output: per type, [w*h*nranks] id and coverage */
    float        **rank_id;    /* [ntypes] -> [w*h*nranks] */
    float        **rank_cov;   /* [ntypes] -> [w*h*nranks] */
    int            finalized;
};

/* -------- hash helpers -------- */

static int hash_init(cm_hash *ht, size_t cap)
{
    size_t i;
    ht->key_pixel = (unsigned int *)malloc(cap * sizeof(unsigned int));
    ht->key_id    = (unsigned int *)malloc(cap * sizeof(unsigned int));
    ht->val       = (float *)malloc(cap * sizeof(float));
    if (!ht->key_pixel || !ht->key_id || !ht->val) {
        free(ht->key_pixel); free(ht->key_id); free(ht->val);
        ht->key_pixel = ht->key_id = NULL; ht->val = NULL; return 0;
    }
    for (i = 0; i < cap; ++i) ht->key_pixel[i] = CM_EMPTY;
    ht->cap = cap; ht->count = 0;
    return 1;
}

static void hash_freebuf(cm_hash *ht)
{
    free(ht->key_pixel); free(ht->key_id); free(ht->val);
    ht->key_pixel = ht->key_id = NULL; ht->val = NULL; ht->cap = ht->count = 0;
}

static size_t hash_slot(size_t cap, unsigned int pixel, unsigned int idbits)
{
    /* mix pixel and idbits */
    unsigned int h = pixel * 2654435761u ^ (idbits + 0x9e3779b9u + (pixel << 6) + (pixel >> 2));
    return (size_t)h & (cap - 1);
}

static void hash_insert(cm_hash *ht, unsigned int pixel, unsigned int idbits, float w);

static int hash_grow(cm_hash *ht)
{
    cm_hash n; size_t i;
    if (!hash_init(&n, ht->cap * 2)) return 0;
    for (i = 0; i < ht->cap; ++i)
        if (ht->key_pixel[i] != CM_EMPTY)
            hash_insert(&n, ht->key_pixel[i], ht->key_id[i], ht->val[i]);
    hash_freebuf(ht);
    *ht = n;
    return 1;
}

static void hash_insert(cm_hash *ht, unsigned int pixel, unsigned int idbits, float w)
{
    size_t s;
    if ((ht->count + 1) * 10 >= ht->cap * 7) {   /* load factor > 0.7 */
        if (!hash_grow(ht)) return;              /* on OOM, keep going (may collide) */
    }
    s = hash_slot(ht->cap, pixel, idbits);
    for (;;) {
        if (ht->key_pixel[s] == CM_EMPTY) {
            ht->key_pixel[s] = pixel; ht->key_id[s] = idbits; ht->val[s] = w;
            ht->count++; return;
        }
        if (ht->key_pixel[s] == pixel && ht->key_id[s] == idbits) {
            ht->val[s] += w; return;
        }
        s = (s + 1) & (ht->cap - 1);
    }
}

/* -------- accumulator -------- */

static cm_accum *cm_accum_create_impl(int width, int height, int ntypes, int nranks,
                                      unsigned int *shared_total)
{
    cm_accum *a;
    int t;
    size_t npix;
    if (width <= 0 || height <= 0 || ntypes <= 0 || nranks <= 0) return NULL;
    a = (cm_accum *)calloc(1, sizeof *a);
    if (!a) return NULL;
    a->w = width; a->h = height; a->ntypes = ntypes; a->nranks = nranks;
    npix = (size_t)width * height;

    if (shared_total) { a->total = shared_total; a->total_shared = 1; }
    else              { a->total = (unsigned int *)calloc(npix, sizeof(unsigned int)); }
    a->hash     = (cm_hash *)calloc((size_t)ntypes, sizeof(cm_hash));
    a->rank_id  = (float **)calloc((size_t)ntypes, sizeof(float *));
    a->rank_cov = (float **)calloc((size_t)ntypes, sizeof(float *));
    if (!a->total || !a->hash || !a->rank_id || !a->rank_cov) { cm_accum_free(a); return NULL; }

    for (t = 0; t < ntypes; ++t)
        if (!hash_init(&a->hash[t], 1024)) { cm_accum_free(a); return NULL; }
    return a;
}

cm_accum *cm_accum_create(int width, int height, int ntypes, int nranks)
{
    return cm_accum_create_impl(width, height, ntypes, nranks, NULL);
}

cm_accum *cm_accum_create_shared(int width, int height, int ntypes, int nranks,
                                 unsigned int *shared_total)
{
    return cm_accum_create_impl(width, height, ntypes, nranks, shared_total);
}

void cm_accum_reset(cm_accum *a)
{
    int t;
    if (!a) return;
    if (!a->total_shared)     /* shared total is reset by its owner, not per-accum */
        memset(a->total, 0, (size_t)a->w * a->h * sizeof(unsigned int));
    for (t = 0; t < a->ntypes; ++t) {
        hash_freebuf(&a->hash[t]);
        hash_init(&a->hash[t], 1024);
        free(a->rank_id[t]);  a->rank_id[t]  = NULL;
        free(a->rank_cov[t]); a->rank_cov[t] = NULL;
    }
    a->finalized = 0;
}

void cm_accum_add(cm_accum *a, int x, int y, const float *ids)
{
    size_t pix; int t;
    if (!a || a->finalized || x < 0 || y < 0 || x >= a->w || y >= a->h || !ids) return;
    pix = (size_t)y * a->w + x;
    if (a->total_shared) CM_ATOMIC_INC(&a->total[pix]);   /* shared across threads */
    else                 a->total[pix]++;
    for (t = 0; t < a->ntypes; ++t) {
        float id = ids[t];
        unsigned int idbits;
        if (id == 0.0f) continue;               /* background/no id for this type */
        memcpy(&idbits, &id, sizeof idbits);
        hash_insert(&a->hash[t], (unsigned int)pix, idbits, 1.0f);
    }
}

void cm_accum_merge(cm_accum *dst, const cm_accum *src)
{
    size_t npix, i; int t;
    if (!dst || !src || dst->finalized || src->finalized) return;
    if (dst->w != src->w || dst->h != src->h || dst->ntypes != src->ntypes) return;
    npix = (size_t)dst->w * dst->h;
    /* If both share the same total array it's already complete -- don't double it. */
    if (dst->total != src->total)
        for (i = 0; i < npix; ++i) dst->total[i] += src->total[i];
    for (t = 0; t < dst->ntypes; ++t) {
        const cm_hash *s = &src->hash[t];
        for (i = 0; i < s->cap; ++i)
            if (s->key_pixel[i] != CM_EMPTY)
                hash_insert(&dst->hash[t], s->key_pixel[i], s->key_id[i], s->val[i]);
    }
}

/* insert (id,cov) into a pixel's descending-by-cov rank arrays of length nranks */
static void rank_insert(float *ids, float *covs, int nranks, float id, float cov)
{
    int r, pos = -1;
    for (r = 0; r < nranks; ++r) {
        if (covs[r] <= 0.0f) { pos = r; break; }     /* empty slot */
        if (cov > covs[r])   { pos = r; break; }     /* outranks slot r */
    }
    if (pos < 0) return;                              /* not in the top nranks */
    for (r = nranks - 1; r > pos; --r) { ids[r] = ids[r-1]; covs[r] = covs[r-1]; }
    ids[pos] = id; covs[pos] = cov;
}

void cm_accum_finalize(cm_accum *a)
{
    int t;
    size_t npix, i;
    if (!a || a->finalized) return;
    npix = (size_t)a->w * a->h;

    for (t = 0; t < a->ntypes; ++t) {
        cm_hash *ht = &a->hash[t];
        float *rid = (float *)calloc(npix * a->nranks, sizeof(float));
        float *rcv = (float *)calloc(npix * a->nranks, sizeof(float));
        a->rank_id[t] = rid; a->rank_cov[t] = rcv;
        if (!rid || !rcv) continue;
        for (i = 0; i < ht->cap; ++i) {
            unsigned int pixel = ht->key_pixel[i];
            float id, cov; unsigned int idbits;
            if (pixel == CM_EMPTY) continue;
            if (a->total[pixel] == 0) continue;
            idbits = ht->key_id[i];
            memcpy(&id, &idbits, sizeof id);
            cov = ht->val[i] / (float)a->total[pixel];
            rank_insert(rid + (size_t)pixel * a->nranks,
                        rcv + (size_t)pixel * a->nranks, a->nranks, id, cov);
        }
        hash_freebuf(ht);                              /* done with working data */
    }
    a->finalized = 1;
}

static int rank_ok(const cm_accum *a, int type, int x, int y, int rank)
{
    return a && a->finalized && type >= 0 && type < a->ntypes &&
           x >= 0 && y >= 0 && x < a->w && y < a->h &&
           rank >= 0 && rank < a->nranks && a->rank_id[type] && a->rank_cov[type];
}

float cm_accum_id(const cm_accum *a, int type, int x, int y, int rank)
{
    if (!rank_ok(a, type, x, y, rank)) return 0.0f;
    return a->rank_id[type][((size_t)y * a->w + x) * a->nranks + rank];
}

float cm_accum_cov(const cm_accum *a, int type, int x, int y, int rank)
{
    if (!rank_ok(a, type, x, y, rank)) return 0.0f;
    return a->rank_cov[type][((size_t)y * a->w + x) * a->nranks + rank];
}

int cm_accum_width (const cm_accum *a) { return a ? a->w : 0; }
int cm_accum_height(const cm_accum *a) { return a ? a->h : 0; }
int cm_accum_nranks(const cm_accum *a) { return a ? a->nranks : 0; }

void cm_accum_free(cm_accum *a)
{
    int t;
    if (!a) return;
    if (a->hash)     for (t = 0; t < a->ntypes; ++t) hash_freebuf(&a->hash[t]);
    if (a->rank_id)  for (t = 0; t < a->ntypes; ++t) free(a->rank_id[t]);
    if (a->rank_cov) for (t = 0; t < a->ntypes; ++t) free(a->rank_cov[t]);
    free(a->hash); free(a->rank_id); free(a->rank_cov);
    if (!a->total_shared) free(a->total);      /* shared total is owned by the caller */
    free(a);
}
