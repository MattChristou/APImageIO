/*
 * crypto_encode.c - Cryptomatte encoding primitives (see crypto_encode.h).
 */
#include "crypto_encode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- MurmurHash3 x86_32 (public-domain reference, by Austin Appleby) ---- */

static uint32_t rotl32(uint32_t x, int8_t r)
{
    return (x << r) | (x >> (32 - r));
}

uint32_t cm_murmur3_32(const void *key, size_t len, uint32_t seed)
{
    const uint8_t *data = (const uint8_t *)key;
    const int nblocks = (int)(len / 4);
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51, c2 = 0x1b873593;
    const uint8_t *tail;
    uint32_t k1;
    int i;

    for (i = 0; i < nblocks; i++) {
        uint32_t k = (uint32_t)data[i*4+0]        | ((uint32_t)data[i*4+1] << 8)
                   | ((uint32_t)data[i*4+2] << 16) | ((uint32_t)data[i*4+3] << 24);
        k *= c1; k = rotl32(k, 15); k *= c2;
        h1 ^= k; h1 = rotl32(h1, 13); h1 = h1 * 5 + 0xe6546b64;
    }

    tail = data + nblocks * 4;
    k1 = 0;
    switch (len & 3) {
        case 3: k1 ^= (uint32_t)tail[2] << 16; /* fallthrough */
        case 2: k1 ^= (uint32_t)tail[1] << 8;  /* fallthrough */
        case 1: k1 ^= (uint32_t)tail[0];
                k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
    }

    h1 ^= (uint32_t)len;
    h1 ^= h1 >> 16; h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13; h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    return h1;
}

uint32_t cm_name_hash(const char *name)
{
    return cm_murmur3_32(name, name ? strlen(name) : 0, 0);
}

float cm_hash_to_float(uint32_t h)
{
    uint32_t mantissa = h & 0x007FFFFFu;
    uint32_t exponent = (h >> 23) & 0xFFu;
    uint32_t sign     = h & 0x80000000u;
    uint32_t bits;
    float f;
    if (exponent < 1u)   exponent = 1u;
    if (exponent > 254u) exponent = 254u;
    bits = sign | (exponent << 23) | mantissa;
    memcpy(&f, &bits, sizeof f);
    return f;
}

float cm_name_to_id(const char *name)
{
    return cm_hash_to_float(cm_name_hash(name));
}

void cm_type_key(const char *type_name, char out[8])
{
    uint32_t h = cm_name_hash(type_name);
    /* Spec / reference implementations: the FIRST 7 chars of "%08x", i.e. the
       top 28 bits. (Masking the low 28 bits gave the last 7 - readable by Nuke,
       which keys by whatever is present, but not by tools that recompute it.) */
    snprintf(out, 8, "%07x", (unsigned)(h >> 4));
}

/* ---- manifest builder ---- */

void cm_manifest_init(cm_manifest *m)
{
    m->cap = 64;
    m->json = (char *)malloc(m->cap);
    m->json[0] = '{'; m->json[1] = '}'; m->json[2] = '\0';
    m->len = 2;
    m->count = 0;
}

static void cm_ensure(cm_manifest *m, size_t extra)
{
    if (m->len + extra + 1 > m->cap) {
        while (m->len + extra + 1 > m->cap) m->cap *= 2;
        m->json = (char *)realloc(m->json, m->cap);
    }
}

void cm_manifest_add(cm_manifest *m, const char *name, uint32_t hash)
{
    /* JSON-escape name into a temp (handle " and \) */
    char esc[512]; size_t e = 0; const char *s = name ? name : "";
    char frag[600];
    int n;
    for (; *s && e < sizeof esc - 2; ++s) {
        if (*s == '"' || *s == '\\') esc[e++] = '\\';
        esc[e++] = *s;
    }
    esc[e] = '\0';

    /* build  "name":"hexhash"  with a leading comma if not first */
    n = snprintf(frag, sizeof frag, "%s\"%s\":\"%08x\"",
                 m->count ? "," : "", esc, (unsigned)hash);
    if (n < 0) return;

    cm_ensure(m, (size_t)n);
    /* insert before the closing '}' */
    memmove(m->json + m->len - 1 + (size_t)n, m->json + m->len - 1, 2); /* move '}' + NUL */
    memcpy(m->json + m->len - 1, frag, (size_t)n);
    m->len += (size_t)n;
    m->count++;
}

void cm_manifest_free(cm_manifest *m)
{
    free(m->json);
    m->json = NULL; m->len = m->cap = 0; m->count = 0;
}
