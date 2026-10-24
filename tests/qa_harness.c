/* APImageIO regression harness. Links the real bridge sources (no LightWave
   needed) and checks the behaviour fixed or added in 0.3.0 / 0.4.0:
     - krabridge: .kra/.ora decode, byte-exact vs pypng; claim/reject rules
     - exrbridge: layer choice, no-RGB files, display window, no leak on throw
     - svgbridge: aspect-preserving size clamp
     - crypto_encode: Cryptomatte metadata key
   Test files come from mk_kra.py / mk_exr.py (run_tests.bat runs them).
   Exit code = number of failed checks. */
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "exrbridge.h"
#include "svgbridge.h"
#include "krabridge.h"
#include "crypto_encode.h"
#include "exr_session.h"

#define D(f) "data/" f

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { g_pass++; printf("  PASS  "); } \
                              else { g_fail++; printf("  FAIL  "); } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

static int approx(float a, float b) { return fabsf(a - b) < 1e-3f; }

/* ------------------------------------------------------------------ KRA */

static unsigned char *slurp(const char *f, size_t *n)
{
    FILE *fp = fopen(f, "rb"); unsigned char *b; long len;
    *n = 0;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
    b = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
    if (b) *n = fread(b, 1, (size_t)len, fp);
    fclose(fp);
    return b;
}

/* Decode and compare with <file>.expect (RGBA8, or big-endian RGBA16). */
static void kra_ok(const char *f, int w, int h, int alpha, int isfloat)
{
    KraImage im; char err[256] = "", ex[300]; size_t n, i, cnt, bad = 0;
    unsigned char *want;
    memset(&im, 0, sizeof im);
    if (!krabridge_is_kra(f)) { CHECK(0, "%s claimed", f); return; }
    if (!krabridge_load(f, &im, err, sizeof err)) { CHECK(0, "%s loads (%s)", f, err); return; }
    snprintf(ex, sizeof ex, "%s.expect", f);
    want = slurp(ex, &n);
    cnt = (size_t)im.width * im.height * 4;
    if (!want || n != cnt * (im.is_float ? 2 : 1)) bad = (size_t)-1;
    else if (im.is_float) {
        for (i = 0; i < cnt; ++i) {
            unsigned v = (unsigned)lrintf(im.rgbaf[i] * 65535.0f);
            if (v != ((unsigned)want[i * 2] << 8 | want[i * 2 + 1])) bad++;
        }
    } else bad = memcmp(im.rgba8, want, cnt) ? 1 : 0;
    CHECK(im.width == w && im.height == h && im.has_alpha == alpha && im.is_float == isfloat && bad == 0,
          "%-24s %dx%d alpha=%d float=%d pixels %s", f, im.width, im.height, im.has_alpha,
          im.is_float, bad == 0 ? "exact" : "DIFFER");
    free(want);
    krabridge_free(&im);
}

static void kra_reject(const char *f, int expect_claim, const char *errpart)
{
    KraImage im; char err[256] = "";
    int claimed = krabridge_is_kra(f), loaded = 0;
    memset(&im, 0, sizeof im);
    if (claimed) loaded = krabridge_load(f, &im, err, sizeof err);
    krabridge_free(&im);
    if (!expect_claim) CHECK(!claimed, "%-24s NOREC (left for other loaders)", f);
    else CHECK(claimed && !loaded && strstr(err, errpart),
               "%-24s BADFILE \"%s\"", f, err);
}

/* ------------------------------------------------------------------ EXR */

static const float *px(const ExrImage *im, int x, int y) { return im->rgba + ((size_t)y * im->width + x) * 4; }
static int load(const char *f, ExrImage *im, char *err)
{
    memset(im, 0, sizeof *im); err[0] = 0;
    return exrbridge_load(f, im, err, 256);
}
static size_t private_bytes(void)
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc);
    return pmc.PrivateUsage;
}

int main(void)
{
    ExrImage im; char err[256]; const float *p;

    printf("[KRA] Krita / OpenRaster loader\n");
    kra_ok(D("k_rgba8.kra"),         64, 40, 1, 0);
    kra_ok(D("k_rgba8_deflate.kra"), 64, 40, 1, 0);
    kra_ok(D("k_rgb8.kra"),          64, 40, 0, 0);
    kra_ok(D("k_rgba16.kra"),        64, 40, 1, 1);
    kra_ok(D("k_rgb16.kra"),         64, 40, 0, 1);
    kra_ok(D("k_gray8.kra"),         64, 40, 0, 0);
    kra_ok(D("k_graya8.kra"),        64, 40, 1, 0);
    kra_ok(D("k_palette.kra"),       64, 40, 0, 0);
    kra_ok(D("k_rgba8.ora"),         64, 40, 1, 0);
    kra_ok(D("K_UPPER.KRA"),         64, 40, 0, 0);
    kra_ok(D("k_case.kra"),          64, 40, 0, 0);
    kra_reject(D("k_nomerged.kra"),  1, "no mergedimage.png");
    kra_reject(D("k_notpng.kra"),    1, "not a PNG");
    kra_reject(D("k_fake.kra"),      0, "");
    kra_reject(D("k_empty.kra"),     0, "");
    kra_reject(D("k_zipnotkra.zip"), 0, "");
    kra_reject(D("missing.kra"),     0, "");

    printf("[EXR] layer choice\n");
    CHECK(load(D("blender_ao_combined.exr"), &im, err) && approx(px(&im, 5, 5)[0], 0.5f) && im.has_alpha,
          "Blender AO+Combined -> Combined (layer '%s')", im.layer);
    exrbridge_free(&im);
    CHECK(load(D("multipart_beauty.exr"), &im, err) && approx(px(&im, 5, 5)[0], 0.5f),
          "multipart AO|beauty -> beauty part");
    exrbridge_free(&im);
    CHECK(load(D("multipart_rgb_second.exr"), &im, err) && approx(px(&im, 5, 5)[0], 0.5f),
          "multipart Z|RGB -> RGB in part 1 (%s)", err);
    exrbridge_free(&im);
    CHECK(load(D("diffuse_specular.exr"), &im, err) && approx(px(&im, 5, 5)[0], 0.75f),
          "matcap diffuse+specular summed (layer '%s')", im.layer);
    exrbridge_free(&im);
    CHECK(load(D("plain_rgba.exr"), &im, err) && approx(px(&im, 5, 5)[0], 0.5f) && approx(px(&im, 5, 5)[3], 0.75f) && im.has_alpha,
          "plain RGBA unchanged");
    exrbridge_free(&im);

    printf("[EXR] files with no RGB layer\n");
    CHECK(load(D("y_only.exr"), &im, err) && approx(px(&im,3,3)[0], 0.3f) && approx(px(&im,3,3)[2], 0.3f) && !im.has_alpha,
          "Y-only -> grey 0.3, no alpha (%s)", err);
    exrbridge_free(&im);
    CHECK(load(D("y_alpha.exr"), &im, err) && approx(px(&im,3,3)[0], 0.3f) && approx(px(&im,3,3)[3], 0.6f) && im.has_alpha,
          "Y+A -> grey 0.3, alpha 0.6 (%s)", err);
    exrbridge_free(&im);
    CHECK(load(D("z_only.exr"), &im, err) && approx(px(&im,3,3)[0], 7.0f) && approx(px(&im,3,3)[2], 7.0f),
          "Z-only -> grey 7.0 (%s)", err);
    exrbridge_free(&im);

    printf("[EXR] display window\n");
    if (load(D("bbox_crop.exr"), &im, err)) {
        CHECK(im.width == 64 && im.height == 32, "bbox crop -> full 64x32 format (got %dx%d)", im.width, im.height);
        p = px(&im, 10, 4);  CHECK(approx(p[0], 1.0f), "inside box (10,4) = 1");
        p = px(&im, 25, 11); CHECK(approx(p[0], 1.0f), "inside box (25,11) = 1");
        p = px(&im, 9, 4);   CHECK(approx(p[0], 0.0f), "left of box = 0");
        p = px(&im, 26, 12); CHECK(approx(p[0], 0.0f), "past box = 0");
    } else CHECK(0, "bbox_crop loads (%s)", err);
    exrbridge_free(&im);
    if (load(D("overscan.exr"), &im, err)) {
        int x, y, bad = 0;
        CHECK(im.width == 64 && im.height == 32, "overscan -> cropped to 64x32 (got %dx%d)", im.width, im.height);
        for (y = 0; y < im.height; ++y) for (x = 0; x < im.width; ++x) if (!approx(px(&im,x,y)[0], 0.5f)) bad++;
        CHECK(bad == 0, "overscan ring removed (%d wrong pixels)", bad);
    } else CHECK(0, "overscan loads (%s)", err);
    exrbridge_free(&im);

    printf("[EXR] corrupt file: error, no leak\n");
    {
        int i, ok = 1; size_t before, after;
        CHECK(!load(D("big_truncated.exr"), &im, err), "truncated 2048^2 EXR rejected");
        exrbridge_free(&im);
        before = private_bytes();
        for (i = 0; i < 40; ++i) { if (load(D("big_truncated.exr"), &im, err)) ok = 0; exrbridge_free(&im); }
        after = private_bytes();
        CHECK(ok && after < before + 64u * 1024 * 1024,
              "40 failed loads: memory grew %.1f MB (a leak would be ~2560 MB)",
              ((double)after - (double)before) / (1024.0 * 1024.0));
        CHECK(load(D("big.exr"), &im, err) && im.width == 2048, "intact 2048^2 EXR loads");
        exrbridge_free(&im);
    }

    printf("[SVG] size clamp\n");
    {
        SvgImage s; memset(&s, 0, sizeof s);
        CHECK(svgbridge_load(D("wide.svg"), &s, err, 256) && s.width == 8192 && s.height == 410,
              "20000x1000 -> 8192x410 (got %dx%d)", s.width, s.height);
        svgbridge_free(&s); memset(&s, 0, sizeof s);
        CHECK(svgbridge_load(D("tall.svg"), &s, err, 256) && s.width == 205 && s.height == 8192,
              "500x20000 -> 205x8192 (got %dx%d)", s.width, s.height);
        svgbridge_free(&s); memset(&s, 0, sizeof s);
        CHECK(svgbridge_load(D("normal.svg"), &s, err, 256) && s.width == 300 && s.height == 150,
              "300x150 unchanged (got %dx%d)", s.width, s.height);
        svgbridge_free(&s); memset(&s, 0, sizeof s);
        CHECK(svgbridge_load(D("huge.svg"), &s, err, 256) && s.width == 8192 && s.height == 4096,
              "1e20 x 5e19 -> 8192x4096 (got %dx%d)", s.width, s.height);
        svgbridge_free(&s);
    }

    printf("[CRYPTO] metadata key\n");
    {
        char k[8], full[9];
        snprintf(full, sizeof full, "%08x", (unsigned)cm_name_hash("CryptoObject"));
        cm_type_key("CryptoObject", k);
        CHECK(strncmp(k, full, 7) == 0 && strlen(k) == 7, "key = first 7 hex of hash (%s -> %s)", full, k);
    }

    printf("[OUTPUT] folder / prefix resolution\n");
    {
        char out[1024];
        struct { const char *in, *scene, *want, *what; } c[] = {
            { "data",              "C:\\proj\\Scenes\\Shot010.lws", "data\\Shot010_",       "existing folder -> <folder>\\<scene>_" },
            { "Z:\\no\\such\\",    "Shot010.lws",                   "Z:\\no\\such\\Shot010_", "trailing slash = folder even if missing" },
            { "Z:\\renders/",      "a/b/Shot.v2.lws",               "Z:\\renders\\Shot.v2_", "mixed slashes, dotted name keeps all but extension" },
            { "data\\",            "",                              "data\\render_",        "no scene name -> render_" },
            { "data",              "Untitled",                      "data\\Untitled_",      "scene without extension" },
            { "data",              "bad|name?.lws",                 "data\\bad_name__",     "unsafe filename chars replaced" },
            { "C:\\out\\shot_",    "Shot010.lws",                   "C:\\out\\shot_",       "prefix form unchanged (Render Pass Manager)" },
            { "C:\\out\\cm.EXR",   "Shot010.lws",                   "C:\\out\\cm",          "prefix form still drops .exr" },
        };
        int i;
        for (i = 0; i < (int)(sizeof c / sizeof c[0]); ++i) {
            ap_exr_resolve_base(c[i].in, c[i].scene, out, sizeof out);
            CHECK(strcmp(out, c[i].want) == 0, "%-48s '%s'", c[i].what, out);
        }
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
