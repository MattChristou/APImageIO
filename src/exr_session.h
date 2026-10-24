/*
 * exr_session.h - per-render EXR settings shared across APImageIO's EXR writers.
 *
 * A tiny same-DLL session state set by APImageIO commands (e.g. by the Render
 * Pass Manager before each pass) and read by both the single-image EXR saver
 * and the multilayer EXR image filter. Empty strings mean "unset".
 */
#ifndef AP_EXR_SESSION_H
#define AP_EXR_SESSION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copy `in` -> `out` (cap bytes) dropping a single trailing ".exr"/".EXR", so an
 * output base picked via a Save requester ("name.exr") becomes "name" before the
 * writers append the frame number + ".exr". No-op when there's no .exr suffix. */
void ap_exr_strip_exr_ext(const char *in, char *out, size_t cap);

/* Turn the output setting into the base the writers append "NNNN.exr" to.
 * A FOLDER (an existing directory, or a path ending in a slash) becomes
 * "<folder>\<scene>_", named after the scene (`scene_name`, path and
 * extension dropped; "render" if empty). Anything else is a filename prefix
 * (the older form, still used by the Render Pass Manager) and only loses a
 * trailing ".exr" as in ap_exr_strip_exr_ext. Paths are UTF-8. */
void ap_exr_resolve_base(const char *in, const char *scene_name, char *out, size_t cap);

/* EXR compression override (OIIO name: "zip"/"piz"/"dwaa"/...). "" => use pref. */
/* `comp` if it stores 32-bit float data exactly, else "zip". PXR24 rounds
 * float32 to 24 bits, DWAA/DWAB apply lossy DCT to channels named .R/.G/.B/.Y
 * and B44/B44A are lossy for half data -- any of them breaks bit-exact data
 * such as Cryptomatte ids (picks stop matching the manifest). Accepts OIIO
 * forms like "dwaa:45". */
const char *ap_exr_lossless_compression(const char *comp);

void        ap_exr_set_compression(const char *c);
const char *ap_exr_compression(void);            /* never NULL; "" if unset */

/* Multilayer EXR output base path. The filter appends the frame + ".exr".
 * "" => the multilayer filter is inert (writes nothing). */
void        ap_exr_set_multilayer_output(const char *path);
const char *ap_exr_multilayer_output(void);      /* never NULL; "" if unset */

/* Cryptomatte output base path. AP_CryptomatteOutput appends frame +
 * ".cryptomatte.exr". "" => the Cryptomatte output filter is inert. */
void        ap_crypto_set_output(const char *path);
const char *ap_crypto_output(void);              /* never NULL; "" if unset */

/* EXR bit-depth override: -1 = unset (use pref), 0 = 32-bit float, 1 = half. */
void        ap_exr_set_half(int half);
int         ap_exr_half(void);                   /* -1 if unset, else 0/1 */

/* Cryptomatte output mode override: -1 = unset (use the pref), 0 = own dedicated
 * .cryptomatte.exr, 1 = merge the Crypto channels into the AP_MultilayerEXR output. */
void        ap_crypto_set_mode(int mode);
int         ap_crypto_mode(void);                /* -1 unset, 0 own, 1 merge */

#ifdef __cplusplus
}
#endif

#endif /* AP_EXR_SESSION_H */
