/*
 * apprefs.c - see apprefs.h.
 */

#include "apprefs.h"

#include <lwhost.h>
#include <stdlib.h>
#include <string.h>

static LWPreferenceSystem *get_ps(GlobalFunc *global)
{
    return global ? (LWPreferenceSystem *)global(LWPREFERENCESYSTEM_GLOBAL, GFUSE_TRANSIENT) : NULL;
}

/* Copy `in` to `out`, replacing '/' with '-'. A '/' in a pref parameter name is
   parsed as a path separator and would spawn a phantom sub-branch. */
static const char *sanitize(const char *in, char *out, size_t n)
{
    size_t i = 0;
    if (!in || !out || n == 0) return in;
    for (; in[i] && i + 1 < n; i++) out[i] = (in[i] == '/') ? '-' : in[i];
    out[i] = '\0';
    return out;
}

/* 1 when the saved hints match ours. LW stores what it was given with a ';'
   appended, so compare ignoring trailing ';' and spaces. */
static int same_hints(const char *a, const char *b)
{
    size_t la = a ? strlen(a) : 0, lb = b ? strlen(b) : 0;
    while (la && (a[la - 1] == ';' || a[la - 1] == ' ')) la--;
    while (lb && (b[lb - 1] == ';' || b[lb - 1] == ' ')) lb--;
    return la == lb && (la == 0 || memcmp(a, b, la) == 0);
}

/* LW reloads a page's saved definitions (<branch>.lwpub) at startup, and a
   later publish does not replace them: a key whose hints changed (a new
   control, tooltip, default) keeps the old ones, and a key the code dropped
   stays on the page. Detect either and publish the page afresh, keeping every
   value. Once per session (the caller's latch). */
static void heal_saved_definitions(LWPreferenceSystem *ps, ApPrefs *p)
{
    enum { MAXP = 128 };
    int    ivals[MAXP];
    double fvals[MAXP];
    LWPreferenceSystemFloat3 f3[MAXP];
    char  *svals[MAXP];
    char nm[256];
    unsigned int n, k;
    size_t blen = strlen(p->branch);
    int i, stale = 0;

    if (!ps->getParameterInfo || !ps->getParameterCount || !ps->getParameterName || !ps->removeParameters ||
        p->count > MAXP)
        return;
    for (i = 0; i < p->count && !stale; i++) {
        const LWPreferenceSystemParameterInfo *pi;
        if (p->params[i].type == LWPrefTypeProxy) continue;
        pi = ps->getParameterInfo(p->branch, sanitize(p->params[i].name, nm, sizeof(nm)));
        if (pi && !same_hints(pi->hints, p->params[i].hints)) stale = 1;
    }
    n = ps->getParameterCount(p->branch);                   /* recursive, full paths */
    for (k = 0; k < n && !stale; k++) {
        const char *full = ps->getParameterName(p->branch, k), *leaf;
        int known = 0;
        if (!full || strncmp(full, p->branch, blen) != 0) continue;
        leaf = full + blen;
        if (strchr(leaf, '/')) continue;                    /* a sub-branch: not ours to judge */
        for (i = 0; i < p->count && !known; i++)
            known = strcmp(leaf, sanitize(p->params[i].name, nm, sizeof(nm))) == 0;
        if (!known) stale = 1;
    }
    if (!stale) return;

    for (i = 0; i < p->count; i++) {
        const char *key = sanitize(p->params[i].name, nm, sizeof(nm));
        svals[i] = NULL;
        switch (p->params[i].type) {
        case LWPrefTypeInt:    ivals[i] = ps->getIntValue ? ps->getIntValue(p->branch, key) : 0; break;
        case LWPrefTypeFloat:  fvals[i] = ps->getFloatValue ? ps->getFloatValue(p->branch, key) : 0.0; break;
        case LWPrefTypeFloat3: {
            const LWPreferenceSystemFloat3 *v = ps->getFloat3Value ? ps->getFloat3Value(p->branch, key) : NULL;
            if (v) f3[i] = *v; else memset(&f3[i], 0, sizeof f3[i]);
            break; }
        case LWPrefTypeString: {
            const char *s = ps->getStringValue ? ps->getStringValue(p->branch, key) : NULL;
            if (s && (svals[i] = (char *)malloc(strlen(s) + 1)) != NULL) strcpy(svals[i], s);
            break; }
        default: break;
        }
    }
    ps->removeParameters(p->branch);
    for (i = 0; i < p->count; i++) {
        const char *key = sanitize(p->params[i].name, nm, sizeof(nm));
        ps->publishParameter(p->branch, key, p->params[i].type, p->ident_base + (unsigned int)i,
                             NULL, p->params[i].hints);
        if (p->params[i].callbacks && ps->setCallbacks) ps->setCallbacks(p->branch, key, p->params[i].callbacks);
        switch (p->params[i].type) {
        case LWPrefTypeInt:    if (ps->setIntValue) ps->setIntValue(p->branch, key, ivals[i]); break;
        case LWPrefTypeFloat:  if (ps->setFloatValue) ps->setFloatValue(p->branch, key, fvals[i]); break;
        case LWPrefTypeFloat3: if (ps->setFloat3Value) ps->setFloat3Value(p->branch, key, &f3[i]); break;
        case LWPrefTypeString:
            if (ps->setStringValue) ps->setStringValue(p->branch, key, svals[i] ? svals[i] : "");
            free(svals[i]);
            break;
        default: break;
        }
    }
}

void apprefs_publish(GlobalFunc *global, ApPrefs *p)
{
    LWPreferenceSystem *ps;
    char nm[256];
    int i;

    if (!p || p->_published) return;
    ps = get_ps(global);
    if (!ps) return;

    for (i = 0; i < p->count; i++) {
        const char *pnm = sanitize(p->params[i].name, nm, sizeof(nm));
        ps->publishParameter(p->branch, pnm, p->params[i].type,
                             p->ident_base + (unsigned int)i,
                             NULL, p->params[i].hints);
        /* proxy/button params carry a callbacks table (buttonClick etc.) */
        if (p->params[i].callbacks && ps->setCallbacks)
            ps->setCallbacks(p->branch, pnm, p->params[i].callbacks);
    }

    /* Latch only if the publishes actually landed; otherwise a rejected path
       form can be retried on a later call. */
    if (ps->getParameterCount(p->branch) > 0) {
        p->_published = 1;
        heal_saved_definitions(ps, p);
    }
}

int apprefs_int(GlobalFunc *global, ApPrefs *p, int index, int fallback)
{
    LWPreferenceSystem *ps;
    char nm[256];

    if (!p || index < 0 || index >= p->count) return fallback;
    apprefs_publish(global, p);
    ps = get_ps(global);
    if (!ps || ps->getParameterCount(p->branch) == 0) return fallback;
    return ps->getIntValue(p->branch, sanitize(p->params[index].name, nm, sizeof(nm)));
}

double apprefs_float(GlobalFunc *global, ApPrefs *p, int index, double fallback)
{
    LWPreferenceSystem *ps;
    char nm[256];

    if (!p || index < 0 || index >= p->count) return fallback;
    apprefs_publish(global, p);
    ps = get_ps(global);
    if (!ps || ps->getParameterCount(p->branch) == 0) return fallback;
    return ps->getFloatValue(p->branch, sanitize(p->params[index].name, nm, sizeof(nm)));
}

const char *apprefs_string(GlobalFunc *global, ApPrefs *p, int index, const char *fallback)
{
    LWPreferenceSystem *ps;
    LWCStringUTF8 s;
    char nm[256];

    if (!fallback) fallback = "";
    if (!p || index < 0 || index >= p->count) return fallback;
    apprefs_publish(global, p);
    ps = get_ps(global);
    if (!ps || ps->getParameterCount(p->branch) == 0) return fallback;
    s = ps->getStringValue(p->branch, sanitize(p->params[index].name, nm, sizeof(nm)));
    return s ? s : fallback;
}

void apprefs_open_panel(GlobalFunc *global, ApPrefs *p)
{
    LWPreferenceSystem *ps;
    if (!p) return;
    apprefs_publish(global, p);
    ps = get_ps(global);
    if (ps)
        ps->openPanel(p->branch, NULL, 0);
}

void apprefs_remove_branch(GlobalFunc *global, const char *path)
{
    LWPreferenceSystem *ps = get_ps(global);
    if (ps && path)
        ps->removeParameters(path);
}
