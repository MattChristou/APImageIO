/*
 * ap_report.c - see ap_report.h.
 */

#include "ap_report.h"

#include <lwhost.h>
#include <stdio.h>
#include <string.h>

static const char *base_name(const char *path)
{
    const char *s, *b = path;
    if (!path) return "";
    for (s = path; *s; s++)
        if (*s == '\\' || *s == '/' || *s == ':') b = s + 1;
    return b;
}

static unsigned g_count = 0;

unsigned ap_report_count(void) { return g_count; }

static void report(GlobalFunc *global, const char *verb, const char *format,
                   const char *filename, const char *err)
{
    LWMessageFuncs *msg;
    char line[512];

    ++g_count;
    if (!global) return;
    msg = (LWMessageFuncs *)global(LWMESSAGEFUNCS_GLOBAL, GFUSE_TRANSIENT);
    if (!msg || !msg->error) return;   /* absent under ScreamerNet */

    /* The message area shows one line, so the reason goes on line_a rather
       than in line_b where it would be clipped. */
    snprintf(line, sizeof(line), "%s: can't %s %s - %s",
             format ? format : "APImageIO", verb, base_name(filename),
             (err && err[0]) ? err : "failed");
    msg->error(line, NULL);
}

void ap_report_load_error(GlobalFunc *global, const char *format,
                          const char *filename, const char *err)
{
    report(global, "load", format, filename, (err && err[0]) ? err : "decode failed");
}

void ap_report_save_error(GlobalFunc *global, const char *format,
                          const char *filename, const char *err)
{
    report(global, "save", format, filename, (err && err[0]) ? err : "write failed");
}
