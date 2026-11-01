/*
 * lwf_version.h -- a plugin binary's id and version, for an update checker.
 *
 * One source file of the plugin says
 *
 *   #include "lwf/lwf_version.h"
 *   LWF_PLUGIN_INFO("AP_DeltaMush", "1.4.2");
 *
 * which exports, from the .p,
 *
 *   const LWFPluginInfo *lwf_plugin_info(void);
 *
 * The id is the key the plugin catalogue lists it under (one id per .p,
 * however many servers it holds); the version is dot-separated numbers,
 * optionally with a -prerelease tag ("1.5.0-beta.2"). The record also
 * carries the LWF build the plugin was linked with.
 *
 * The record is plain bytes behind a magic, with no pointers, so a checker
 * can read it two ways:
 *   - load the plugin and call lwf_plugin_info() (GetProcAddress / dlsym);
 *   - read it from the file without loading or running anything:
 *     lwf_plugin_info_read(). This also works for the other architecture's
 *     slice of a universal macOS binary and for a plugin the running host
 *     could not load.
 *
 * The macro needs no other part of lwf, so plugins built from a vcxproj
 * or an Xcode project can use it with just this header (copy it into the
 * plugin's repository). It works in C and C++; using it twice in one
 * plugin is a duplicate-symbol link error.
 *
 * A Python plugin (a .py with no binary) carries the same fields as one
 * line of text at the top level of its main .py, which a checker finds by
 * its start and reads with ast.literal_eval, never by importing the file:
 *
 *   LWF_PLUGIN_INFO = {"id": "RizomBridge", "version": "0.6.0"}
 */
#ifndef LWF_VERSION_H
#define LWF_VERSION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LWF_PLUGIN_INFO_ABI 1
/* 15 characters and a NUL. A binary can hold the magic outside a record
   (the reader's own copy, for one), so the reader also checks what follows. */
#define LWF_PLUGIN_INFO_MAGIC "LWF" "PLUGININFO" "\x01\x7e"

typedef struct LWFPluginInfo {
    char magic[16];     /* LWF_PLUGIN_INFO_MAGIC                            */
    int  abi;           /* LWF_PLUGIN_INFO_ABI                              */
    int  size;          /* sizeof(LWFPluginInfo)                            */
    char id[64];        /* catalogue key, NUL-terminated                    */
    char version[32];   /* "major.minor.patch[-pre]", NUL-terminated        */
    char lwf[64];       /* LWF_BUILD_STRING the plugin was built against,
                           "" for a plugin built without lwf               */
} LWFPluginInfo;

#ifdef LWF_BUILD_STRING
#define LWF_PLUGIN_INFO_LWF LWF_BUILD_STRING
#else
#define LWF_PLUGIN_INFO_LWF ""
#endif

#if defined(_WIN32)
#define LWF_PLUGIN_EXPORT __declspec(dllexport)
#else
#define LWF_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#define LWF_PLUGIN_EXTERN_C extern "C"
#else
#define LWF_PLUGIN_EXTERN_C
#endif

/* Used at file scope with a semicolon: LWF_PLUGIN_INFO("Id", "1.0.0");
   (the last declaration takes it). A too-long id or version fails to
   compile (initialiser string too long). */
#define LWF_PLUGIN_INFO(id_, version_)                                      \
    static const LWFPluginInfo lwf__plugin_info_ = {                       \
        LWF_PLUGIN_INFO_MAGIC, LWF_PLUGIN_INFO_ABI,                        \
        (int)sizeof(LWFPluginInfo), id_, version_, LWF_PLUGIN_INFO_LWF };  \
    LWF_PLUGIN_EXTERN_C LWF_PLUGIN_EXPORT                                  \
    const LWFPluginInfo *lwf_plugin_info(void);                            \
    LWF_PLUGIN_EXTERN_C LWF_PLUGIN_EXPORT                                  \
    const LWFPluginInfo *lwf_plugin_info(void) { return &lwf__plugin_info_; } \
    LWF_PLUGIN_EXTERN_C const LWFPluginInfo *lwf_plugin_info(void)

/* The name a checker looks up with GetProcAddress / dlsym. */
#define LWF_PLUGIN_INFO_SYMBOL "lwf_plugin_info"

typedef const LWFPluginInfo *(*LWFPluginInfoFunc)(void);

/* The following are in lwf (src/lwf_version.c). */

/* Reads the record from a plugin file without loading it. Returns 1 and
   fills *out, 0 if the file has no valid record, -1 if it can't be read.
   The first valid record wins (a universal binary has one per slice). */
int lwf_plugin_info_read(const char *path, LWFPluginInfo *out);

/* Checks a record found in memory: magic, abi, size, and NUL-terminated,
   non-empty id and version. 1 if valid. */
int lwf_plugin_info_valid(const LWFPluginInfo *info);

/* Compares two versions: <0, 0, >0 like strcmp. Numeric fields compare as
   numbers ("1.10" > "1.9"), missing fields are 0 ("1.2" == "1.2.0"), and a
   prerelease sorts before its release ("1.0-rc.1" < "1.0"), prerelease
   fields comparing numerically when both are numbers, else as text. A
   leading 'v' is ignored, and so is "+build" metadata. */
int lwf_version_compare(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* LWF_VERSION_H */
