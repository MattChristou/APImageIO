/*
 * crypto_encode.h - self-contained Cryptomatte encoding primitives.
 *
 * No LightWave / OpenEXR dependency -- pure C, so it can be unit-tested on its
 * own. Implements the pieces the Cryptomatte spec fixes:
 *   - MurmurHash3 x86_32 of a name  (the canonical id hash)
 *   - uint32 -> float32 "id" conversion with clamped exponent (no NaN/Inf)
 *   - the 7-hex-char metadata key derived from a type name ("CryptoObject")
 *   - a growable manifest JSON builder  {"name":"hexhash",...}
 */
#ifndef AP_CRYPTO_ENCODE_H
#define AP_CRYPTO_ENCODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MurmurHash3 x86_32 (seed 0 is the Cryptomatte convention). */
uint32_t cm_murmur3_32(const void *key, size_t len, uint32_t seed);

/* Convenience: MurmurHash3_32 of a NUL-terminated name, seed 0. */
uint32_t cm_name_hash(const char *name);

/* uint32 hash -> float32 id per spec: reinterpret bits but clamp the exponent
 * into [1,254] so the result is never NaN/Inf/denormal. */
float    cm_hash_to_float(uint32_t h);

/* One-shot: name -> float id (cm_hash_to_float(cm_name_hash(name))). */
float    cm_name_to_id(const char *name);

/* Metadata key for a type name: lowercase 7 hex chars of MurmurHash3_32(type).
 * out must hold >= 8 bytes. e.g. "CryptoObject" -> "f834066" (verify in-engine). */
void     cm_type_key(const char *type_name, char out[8]);

/* Growable manifest: maps object/surface names -> 8-hex uint32 hash. */
typedef struct cm_manifest {
    char  *json;     /* NUL-terminated JSON object, always valid */
    size_t len;      /* strlen(json) */
    size_t cap;      /* allocated */
    int    count;    /* entries added */
} cm_manifest;

void cm_manifest_init(cm_manifest *m);
/* Add "name":"hexhash". De-dup is the caller's job. name is JSON-escaped. */
void cm_manifest_add(cm_manifest *m, const char *name, uint32_t hash);
void cm_manifest_free(cm_manifest *m);

#ifdef __cplusplus
}
#endif

#endif /* AP_CRYPTO_ENCODE_H */
