/*
 * crypto_accum.h - per-pixel Cryptomatte coverage accumulator.
 *
 * Collects, for each pixel, a coverage-ranked list of id-hashes across every
 * render sample -- the core data structure a Cryptomatte generator needs and
 * that LightWave will not build for you (its ID buffers are reconstruction-
 * averaged). No LightWave / OpenEXR dependency: pure C, unit-testable.
 *
 * Model: `ntypes` independent id spaces (e.g. type 0 = CryptoObject, type 1 =
 * CryptoMaterial). For each rendered sample you call cm_accum_add() once with
 * that sample's pixel and its float id per type (0.0f = background/no-id for
 * that type). Coverage is normalized by the ACTUAL number of samples the pixel
 * received (box filter) -- robust to adaptive sampling; the background is the
 * unstored remainder. After the frame, cm_accum_finalize() ranks each pixel's
 * ids by coverage and keeps the top `nranks`.
 *
 * Single-threaded: call add() from one thread (the pixel filter runs
 * single-threaded when LWPFF_MULTITHREADED is not set). MT is a later concern.
 */
#ifndef AP_CRYPTO_ACCUM_H
#define AP_CRYPTO_ACCUM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cm_accum cm_accum;

/* Create an accumulator for a width x height frame, `ntypes` id spaces, keeping
 * `nranks` ranks per pixel. Returns NULL on allocation failure. */
cm_accum *cm_accum_create(int width, int height, int ntypes, int nranks);

/* As cm_accum_create, but the per-pixel sample-total array is SHARED (not owned)
 * and incremented atomically -- so many per-thread accumulators can share one
 * `shared_total` (width*height uint32, caller-allocated & zeroed, caller-freed),
 * saving N-1 copies of the total array. `shared_total` must outlive the accum. */
cm_accum *cm_accum_create_shared(int width, int height, int ntypes, int nranks,
                                 unsigned int *shared_total);

/* Drop all accumulated data (start a new frame) without reallocating the frame. */
void cm_accum_reset(cm_accum *a);

/* Record one sample at pixel (x,y). `ids` has `ntypes` floats; ids[t] is the
 * float id for type t for this sample (0.0f => background/no id for that type).
 * Increments the pixel's total sample count regardless of ids. Out-of-range
 * pixels are ignored. */
void cm_accum_add(cm_accum *a, int x, int y, const float *ids);

/* Merge src's raw (pre-finalize) counts into dst: adds per-pixel sample totals
 * and per-(pixel,id) weights. Both must be same width/height/ntypes and NOT yet
 * finalized. Used to combine per-thread accumulators before ranking. */
void cm_accum_merge(cm_accum *dst, const cm_accum *src);

/* Rank each pixel's ids by coverage and keep the top nranks. Call once, after
 * the last add()/merge(). Frees the working hash tables. Idempotent. */
void cm_accum_finalize(cm_accum *a);

/* After finalize(): the ranked id / coverage for a pixel. rank in [0,nranks).
 * Returns 0.0f for empty ranks or out-of-range args. Coverage is in [0,1]. */
float cm_accum_id (const cm_accum *a, int type, int x, int y, int rank);
float cm_accum_cov(const cm_accum *a, int type, int x, int y, int rank);

/* Accessors. */
int cm_accum_width (const cm_accum *a);
int cm_accum_height(const cm_accum *a);
int cm_accum_nranks(const cm_accum *a);

void cm_accum_free(cm_accum *a);

#ifdef __cplusplus
}
#endif

#endif /* AP_CRYPTO_ACCUM_H */
