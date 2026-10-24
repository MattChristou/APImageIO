/*
 * load_image_cmd.c - "APImageIO_LoadImage": load image files into LightWave's
 * image list from a command.
 *
 * LightWave has no LoadImage command (only IE_CreateImage, which makes a
 * blank image); files can only be loaded through LWImageList->load(). This
 * wraps that so macros, LScript, Python evaluate(), the Render Pass Manager
 * and importers can load images by command:
 *
 *   APImageIO_LoadImage C:\textures\wall.exr
 *
 * The path is the rest of the line, unquoted (like LoadObject); surrounding
 * quotes are tolerated. With no argument (the Utilities menu entry) it opens a
 * multi-select file requester instead - so never call it without a path from
 * an unattended script.
 *
 * Loading goes through LightWave's normal loader chain, so APImageIO's own
 * loaders (EXR, WebP, AVIF, SVG, PNG16, KRA, OIIO) apply exactly as they do in
 * the Image Editor. A file that is already in the image list is not loaded a
 * second time, which keeps scripts idempotent. The outcome goes to Layout's
 * message line; a command cannot return the new image to its caller.
 */
#include <lwserver.h>
#include <lwhost.h>
#include <lwgeneric.h>
#include <lwimage.h>
#include <lwdialog.h>
#include "ap_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_report.h"

#define LI_PATHLEN 1024
#define LI_MAXPICK 256

/* ---- multi-select requester: pickName() is called once per chosen file and
   reads the next name from the request structure, so both live at file scope.
   Names are only collected here; loading happens after the dialog closes. ---- */
static LWFileReqLocal g_fr;
static char  g_path[LI_PATHLEN], g_base[LI_PATHLEN], g_full[LI_PATHLEN];
static char *g_picked[LI_MAXPICK];
static int   g_npicked;

static int li_pick_name(void)
{
    if (g_npicked >= LI_MAXPICK) return 1;              /* stop: list full */
    if (g_fr.fullName && g_fr.fullName[0])
        g_picked[g_npicked++] = _strdup(g_fr.fullName);
    return 0;                                           /* continue */
}

static int li_request(GlobalFunc *global)
{
    LWFileActivateFunc *fa = (LWFileActivateFunc *)global(LWFILEACTIVATEFUNC_GLOBAL, GFUSE_TRANSIENT);
    LWDirInfoFunc *dir = (LWDirInfoFunc *)global(LWDIRINFOFUNC_GLOBAL, GFUSE_TRANSIENT);
    const char *imgdir = dir ? dir("Images") : NULL;

    g_npicked = 0;
    if (!fa) return 0;
    memset(&g_fr, 0, sizeof g_fr);
    g_path[0] = g_base[0] = g_full[0] = '\0';
    if (imgdir) { strncpy(g_path, imgdir, LI_PATHLEN - 1); g_path[LI_PATHLEN - 1] = '\0'; }
    g_fr.reqType  = FREQ_MULTILOAD;
    g_fr.title    = "Load Images (APImageIO)";
    g_fr.fileType = "Images";
    g_fr.path     = g_path;
    g_fr.baseName = g_base;
    g_fr.fullName = g_full;
    g_fr.bufLen   = LI_PATHLEN;
    g_fr.pickName = li_pick_name;
    if (fa(LWFILEREQ_VERSION, &g_fr) != AFUNC_OK || g_fr.result <= 0) return g_npicked;
    /* A requester that ignores pickName still fills fullName with the pick. */
    if (g_npicked == 0 && g_full[0]) g_picked[g_npicked++] = _strdup(g_full);
    return g_npicked;
}

/* Copy the command argument, trimming whitespace and one pair of quotes. */
static void li_clean_arg(const char *in, char *out, size_t cap)
{
    size_t n;
    while (*in == ' ' || *in == '\t') ++in;
    strncpy(out, in, cap - 1); out[cap - 1] = '\0';
    n = strlen(out);
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t' || out[n - 1] == '\r' || out[n - 1] == '\n')) out[--n] = '\0';
    if (n >= 2 && ((out[0] == '"' && out[n - 1] == '"') || (out[0] == '\'' && out[n - 1] == '\''))) {
        memmove(out, out + 1, n - 2);
        out[n - 2] = '\0';
    }
}

static const char *li_base(const char *p)
{
    const char *s, *b = p;
    for (s = p; *s; ++s) if (*s == '\\' || *s == '/' || *s == ':') b = s + 1;
    return b;
}

/* Same file on disk? Case-insensitive, either slash direction. */
static int li_same_path(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char ca = (*a == '/') ? '\\' : *a, cb = (*b == '/') ? '\\' : *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static int li_exists(const char *path) { return ap_is_file(path); }   /* LW paths are UTF-8 */

typedef enum { LI_LOADED, LI_ALREADY, LI_MISSING, LI_FAILED_EXPLAINED, LI_FAILED } LiResult;

static LiResult li_load_one(LWImageList *il, const char *path, LWImageID *out)
{
    LWImageID img;
    unsigned before;
    *out = NULL;
    for (img = il->first(); img; img = il->next(img)) {
        const char *fn = il->filename ? il->filename(img, 0) : NULL;
        if (fn && li_same_path(fn, path)) { *out = img; return LI_ALREADY; }
    }
    if (!li_exists(path)) return LI_MISSING;
    before = ap_report_count();
    img = il->load(path);
    if (img) { *out = img; return LI_LOADED; }
    /* An APImageIO loader that rejected the file has already put the precise
       reason on the message line; don't overwrite it with a generic one. */
    return ap_report_count() != before ? LI_FAILED_EXPLAINED : LI_FAILED;
}

static void li_message(GlobalFunc *global, int is_error, const char *line)
{
    LWMessageFuncs *msg = (LWMessageFuncs *)global(LWMESSAGEFUNCS_GLOBAL, GFUSE_TRANSIENT);
    if (!msg) return;
    if (is_error) { if (msg->error) msg->error(line, NULL); }
    else if (msg->info) msg->info(line, NULL);
}

XCALL_(int)
ApLoadImageCmd(int version, GlobalFunc *global, void *vlocal, void *serverData)
{
    LWLayoutGeneric *lg = (LWLayoutGeneric *)vlocal;
    LWImageList *il;
    char arg[LI_PATHLEN], line[600];
    const char *raw;
    int i, n, loaded = 0, already = 0, failed = 0, explained = 0;
    LWImageID last = NULL;
    const char *lastName = "", *firstFail = NULL;
    LiResult lastRes = LI_LOADED;
    (void)serverData;

    if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;
    il = (LWImageList *)global(LWIMAGELIST_GLOBAL, GFUSE_TRANSIENT);
    if (!il || !il->load) return AFUNC_OK;

    raw = (lg && lg->commandArguments) ? lg->commandArguments(lg->data) : NULL;
    li_clean_arg(raw ? raw : "", arg, sizeof arg);
    g_npicked = 0;
    if (arg[0]) g_picked[g_npicked++] = _strdup(arg);
    else if (!li_request(global)) return AFUNC_OK;       /* cancelled */
    n = g_npicked;

    for (i = 0; i < n; ++i) {
        LWImageID img;
        if (!g_picked[i]) continue;
        lastRes = li_load_one(il, g_picked[i], &img);
        switch (lastRes) {
        case LI_LOADED:           ++loaded;  last = img; lastName = g_picked[i]; break;
        case LI_ALREADY:          ++already; last = img; lastName = g_picked[i]; break;
        case LI_FAILED_EXPLAINED: ++failed;  ++explained; if (!firstFail) firstFail = g_picked[i]; break;
        default:                  ++failed;  if (!firstFail) firstFail = g_picked[i];
                                  if (lastRes == LI_MISSING)
                                      ap_report_load_error(global, "Load Image", g_picked[i], "file not found");
                                  else
                                      ap_report_load_error(global, "Load Image", g_picked[i],
                                                           "no loader could read it");
                                  break;
        }
    }

    /* Summary line. A single failure leaves its own message in place. */
    if (n == 1 && failed == 0 && last) {
        int w = 0, h = 0;
        if (il->size) il->size(last, &w, &h);
        snprintf(line, sizeof line, "APImageIO: %s %s (%d x %d%s)",
                 lastRes == LI_ALREADY ? "already loaded" : "loaded", li_base(lastName),
                 w, h, (il->hasAlpha && il->hasAlpha(last)) ? ", alpha" : "");
        li_message(global, 0, line);
    } else if (n > 1) {
        if (failed == 0) {
            snprintf(line, sizeof line, "APImageIO: loaded %d image%s%s", loaded, loaded == 1 ? "" : "s",
                     already ? " (others already loaded)" : "");
            li_message(global, 0, line);
        } else if (failed > 1 || explained == 0) {
            snprintf(line, sizeof line, "APImageIO: loaded %d, already loaded %d, failed %d (first: %s)",
                     loaded, already, failed, firstFail ? li_base(firstFail) : "?");
            li_message(global, 1, line);
        }
    }

    for (i = 0; i < n; ++i) { free(g_picked[i]); g_picked[i] = NULL; }
    g_npicked = 0;
    return AFUNC_OK;
}
