/*
 * crypto_matte.h - shared access to the Cryptomatte accumulation, so more than
 * one output path can emit it (the dedicated .cryptomatte.exr writer, and the
 * multilayer EXR filter in "merge" mode).
 */
#ifndef AP_CRYPTO_MATTE_H
#define AP_CRYPTO_MATTE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Produced Cryptomatte data for one frame: the ranked Crypto* channels
 * (interleaved) plus the header manifest attributes. All arrays are owned by the
 * struct; release with cm_crypto_free. */
typedef struct {
    int          nchan;         /* number of Crypto channels                    */
    char       **names;         /* [nchan] channel names, e.g. "CryptoObject00.R" */
    float       *interleaved;   /* [w*h*nchan], channel-interleaved per pixel     */
    int          nattr;         /* number of header attributes                  */
    char       **attrN;         /* [nattr] attribute names                      */
    char       **attrV;         /* [nattr] attribute values (manifest JSON etc.) */
} CMCryptoData;

/* Merge the per-thread accumulators for a w x h frame, rank them, and produce
 * the Crypto channels + manifest attrs into `out`. Returns 1 if Cryptomatte data
 * was available (and *out is filled), 0 otherwise. CONSUMES the accumulators
 * (frees them) -- call at most once per frame. Thread-safe (takes the crypto
 * lock). Caller must cm_crypto_free(out) on success. */
int  cm_crypto_produce(int w, int h, CMCryptoData *out);
void cm_crypto_free(CMCryptoData *out);

#ifdef __cplusplus
}
#endif

#endif /* AP_CRYPTO_MATTE_H */
