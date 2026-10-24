/*
 * exr_session.c - shared per-render EXR session state (see exr_session.h).
 */
#include "exr_session.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int is_dir_utf8(const char *path)
{
    wchar_t w[1024];
    DWORD a;
    if (!path || !path[0] || !MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return 0;
    a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void ap_exr_resolve_base(const char *in, const char *scene_name, char *out, size_t cap)
{
    char dir[1024], stem[256];
    const char *s, *b;
    size_t n, i;

    if (!out || cap == 0) return;
    if (!in) in = "";
    n = strlen(in);
    if (!(n && (in[n - 1] == '\\' || in[n - 1] == '/')) && !is_dir_utf8(in)) {
        ap_exr_strip_exr_ext(in, out, cap);              /* filename-prefix form */
        return;
    }

    /* folder form: drop any trailing slash, then add one back */
    strncpy(dir, in, sizeof dir - 1); dir[sizeof dir - 1] = '\0';
    n = strlen(dir);
    while (n && (dir[n - 1] == '\\' || dir[n - 1] == '/')) dir[--n] = '\0';

    /* scene stem: last path component, extension dropped, unsafe chars -> _ */
    b = scene_name ? scene_name : "";
    for (s = b; *s; ++s) if (*s == '\\' || *s == '/' || *s == ':') b = s + 1;
    strncpy(stem, b, sizeof stem - 1); stem[sizeof stem - 1] = '\0';
    { char *dot = strrchr(stem, '.'); if (dot && dot != stem) *dot = '\0'; }
    for (i = 0; stem[i]; ++i)
        if (strchr("<>\"|?*", stem[i]) || (unsigned char)stem[i] < 32) stem[i] = '_';
    if (!stem[0]) strcpy(stem, "render");

    snprintf(out, cap, "%s\\%s_", dir, stem);
}

static char g_compression[32]   = "";
static char g_ml_output[1024]    = "";
static char g_crypto_output[1024] = "";

static void set_str(char *dst, size_t cap, const char *src)
{
    if (!src) src = "";
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void        ap_exr_set_compression(const char *c) { set_str(g_compression, sizeof g_compression, c); }
const char *ap_exr_compression(void)              { return g_compression; }

void        ap_exr_set_multilayer_output(const char *p) { set_str(g_ml_output, sizeof g_ml_output, p); }
const char *ap_exr_multilayer_output(void)              { return g_ml_output; }

void        ap_crypto_set_output(const char *p) { set_str(g_crypto_output, sizeof g_crypto_output, p); }
const char *ap_crypto_output(void)              { return g_crypto_output; }

static int g_exr_half = -1;   /* -1 = unset (use pref) */
void        ap_exr_set_half(int h) { g_exr_half = (h < 0) ? -1 : (h ? 1 : 0); }
int         ap_exr_half(void)      { return g_exr_half; }

static int g_crypto_mode = -1;  /* -1 = unset (use pref); 0 = own EXR; 1 = merge */
void        ap_crypto_set_mode(int m) { g_crypto_mode = (m == 1) ? 1 : 0; }
int         ap_crypto_mode(void)      { return g_crypto_mode; }   /* -1 if unset */

void ap_exr_strip_exr_ext(const char *in, char *out, size_t cap)
{
    size_t len;
    if (!out || cap == 0) return;
    if (!in) in = "";
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
    len = strlen(out);
    if (len >= 4 && out[len-4] == '.' &&
        (out[len-3] == 'e' || out[len-3] == 'E') &&
        (out[len-2] == 'x' || out[len-2] == 'X') &&
        (out[len-1] == 'r' || out[len-1] == 'R'))
        out[len-4] = '\0';
}

const char *ap_exr_lossless_compression(const char *comp)
{
    static const char *const lossy[] = { "pxr24", "dwaa", "dwab", "b44a", "b44", 0 };
    int i;
    if (!comp || !comp[0]) return "zip";
    for (i = 0; lossy[i]; ++i)
        if (!_strnicmp(comp, lossy[i], strlen(lossy[i]))) return "zip";
    return comp;
}
