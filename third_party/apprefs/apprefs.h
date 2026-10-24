/*
 * apprefs.h - shared LightWave Preferences helper for the AP* plugins.
 *
 * Publishes a table of parameters into LW's unified Preferences panel via
 * LWPreferenceSystem, and reads them back, handling the gotchas once so each
 * plugin doesn't re-implement them:
 *   - publish is idempotent and latches only once it actually took (path-form
 *     rejections don't get cached as "done");
 *   - a '/' in a parameter name is auto-sanitised to '-' (a raw '/' is parsed as
 *     a path separator and spawns a phantom sub-branch - the "I/O Threads" bug);
 *   - idents are assigned automatically (ident_base + table index);
 *   - the settings LayoutGeneric is one macro.
 *
 * A plugin declares a static ApPrefParam[] table (order defines the read
 * indices) and a single ApPrefs instance, then calls apprefs_int/float/string
 * by index. See PROJECT plugins APImageIO / APAnimIO for worked examples.
 *
 * Compiled into each plugin's .p (self-contained /MT); no shared runtime DLL.
 */

#ifndef AP_SHARED_APPREFS_H
#define AP_SHARED_APPREFS_H

#include <lwserver.h>
#include <lwgeneric.h>            /* LWLAYOUTGENERIC_VERSION for the panel macro */
#include <lwpreferencesystem.h>  /* LWPreferenceSystemDataType */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ApPrefParam {
    const char                 *name;   /* display name; '/' -> '-' automatically */
    LWPreferenceSystemDataType  type;   /* LWPrefTypeInt / Float / String / ...   */
    const char                 *hints;  /* ';'-separated; ui.tooltip last & ';'-free */
    const LWPreferenceSystemCallbacks *callbacks; /* NULL, or a proxy/button handler */
} ApPrefParam;

typedef struct ApPrefs {
    const char        *branch;      /* plain path, e.g. "/APImageIO/" (publish==read form) */
    const ApPrefParam *params;
    int                count;
    unsigned int       ident_base;  /* idents = ident_base + index; give each plugin its own */
    int                _published;  /* internal latch; initialise to 0 */
} ApPrefs;

/* ---- hint-string builders (bake in the syntax rules) --------------------- *
 * `extra` = "" or extra tokens ending in ';' (e.g. "ui.divider=above;"). The
 * ui.tooltip= token is always emitted last so a stray ';' can't split it.     */
#define AP_STRINGIZE_(x) #x
#define AP_STR_(x) AP_STRINGIZE_(x)

#define AP_INT(name, dflt, lo, hi, extra, tip)                                  \
    { (name), LWPrefTypeInt,                                                    \
      "value.default=" AP_STR_(dflt) ";value.min=" AP_STR_(lo)                  \
      ";value.max=" AP_STR_(hi) ";ui.control=int;" extra "ui.tooltip=" tip }

/* dflt MUST be numeric 1 or 0 - LW ignores value.default=true/false and the
   checkbox silently defaults to 0 (verified 2026-07-11 via the inspector). */
#define AP_CHECK(name, dflt, extra, tip)                                        \
    { (name), LWPrefTypeInt,                                                    \
      "value.default=" AP_STR_(dflt) ";ui.control=checkbox;" extra "ui.tooltip=" tip }

#define AP_POPUP(name, dflt, choices, extra, tip)                               \
    { (name), LWPrefTypeInt,                                                    \
      "value.default=" AP_STR_(dflt) ";ui.control=popup;ui.choices=" choices    \
      ";" extra "ui.tooltip=" tip }

#define AP_STRING(name, extra, tip)                                             \
    { (name), LWPrefTypeString, extra "ui.tooltip=" tip }

/* A clickable proxy "button" in the prefs panel. `cbptr` is a pointer to a
 * static LWPreferenceSystemCallbacks whose buttonClick does the work (e.g. open
 * a file requester and setStringValue on a sibling string param). */
#define AP_BUTTON(name, extra, tip, cbptr)                                       \
    { (name), LWPrefTypeProxy, extra "ui.tooltip=" tip, (cbptr) }

/* Escape hatch for anything the typed builders don't cover. */
#define AP_RAW(name, type, hints) { (name), (type), (hints), 0 }

#define AP_COUNT(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

/* Publish every parameter (idempotent). Call freely before any read. */
void apprefs_publish(GlobalFunc *global, ApPrefs *p);

/* Read a value by param INDEX (matches params[] order; typo-proof). Publishes
   first if needed. Returns `fallback` when the pref system is unavailable or the
   index is out of range. String getter never returns NULL. */
int         apprefs_int   (GlobalFunc *global, ApPrefs *p, int index, int fallback);
double      apprefs_float (GlobalFunc *global, ApPrefs *p, int index, double fallback);
const char *apprefs_string(GlobalFunc *global, ApPrefs *p, int index, const char *fallback);

/* Publish then open the Preferences panel navigated to this branch. */
void apprefs_open_panel(GlobalFunc *global, ApPrefs *p);

/* Remove a (possibly stale/phantom) branch. Plain path, e.g. "/APImageIO/I/". */
void apprefs_remove_branch(GlobalFunc *global, const char *path);

/* Emit the boilerplate "... Settings" LayoutGeneric activation for `prefs_ptr`. */
#define AP_PREFS_PANEL_FN(fnname, prefs_ptr)                                    \
    XCALL_(int) fnname(int version, GlobalFunc *global, void *vlocal, void *sd) \
    {                                                                          \
        (void)vlocal; (void)sd;                                                \
        if (version != LWLAYOUTGENERIC_VERSION) return AFUNC_BADVERSION;       \
        apprefs_open_panel(global, (prefs_ptr));                               \
        return AFUNC_OK;                                                       \
    }

#ifdef __cplusplus
}
#endif

#endif /* AP_SHARED_APPREFS_H */
