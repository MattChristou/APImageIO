/*
 * ap_platform.h - the few OS services APImageIO needs, behind one header so the
 * rest of the code builds unchanged on Windows and macOS.
 *
 *   AP_PATH_SEP / AP_PATH_SEP_STR   separator used when JOINING a folder and a
 *                                   name (both '/' and '\\' are still accepted
 *                                   when PARSING a path on either platform)
 *   ap_is_dir / ap_is_file          UTF-8 path tests (LightWave paths are UTF-8)
 *   ap_temp_dir                     per-user temp folder, no trailing separator
 *   ap_mutex_*                      a plain non-recursive lock
 *   _strdup / _stricmp              the MSVC names, mapped to POSIX off Windows
 */
#ifndef AP_PLATFORM_H
#define AP_PLATFORM_H

#include <stdlib.h>
#include <string.h>

/* header-only helpers: not every file uses every one */
#if defined(__GNUC__) || defined(__clang__)
#define AP_UNUSED __attribute__((unused))
#else
#define AP_UNUSED
#endif

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define AP_PATH_SEP     '\\'
#define AP_PATH_SEP_STR "\\"

AP_UNUSED static int ap_path_attr_(const char *path, int wantDir)
{
    wchar_t w[1024];
    DWORD a;
    if (!path || !path[0] || !MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return 0;
    a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    return wantDir ? (a & FILE_ATTRIBUTE_DIRECTORY) != 0 : (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

AP_UNUSED static const char *ap_temp_dir(void)
{
    const char *t = getenv("TEMP");
    if (!t || !t[0]) t = getenv("TMP");
    if (!t || !t[0]) t = "C:\\tmp";
    return t;
}

typedef CRITICAL_SECTION ap_mutex;
#define ap_mutex_init(m)    InitializeCriticalSection(m)
#define ap_mutex_lock(m)    EnterCriticalSection(m)
#define ap_mutex_unlock(m)  LeaveCriticalSection(m)

#else /* POSIX (macOS) */

#include <sys/stat.h>
#include <strings.h>
#include <pthread.h>

#define AP_PATH_SEP     '/'
#define AP_PATH_SEP_STR "/"

AP_UNUSED static int ap_path_attr_(const char *path, int wantDir)
{
    struct stat st;
    if (!path || !path[0] || stat(path, &st) != 0) return 0;
    return wantDir ? S_ISDIR(st.st_mode) : !S_ISDIR(st.st_mode);
}

AP_UNUSED static const char *ap_temp_dir(void)
{
    static char buf[1024];
    const char *t = getenv("TMPDIR");               /* per-user, ends in '/' on macOS */
    size_t n;
    if (!t || !t[0]) t = "/tmp";
    strncpy(buf, t, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    n = strlen(buf);
    while (n > 1 && buf[n - 1] == '/') buf[--n] = '\0';
    return buf;
}

typedef pthread_mutex_t ap_mutex;
#define ap_mutex_init(m)    pthread_mutex_init((m), NULL)
#define ap_mutex_lock(m)    pthread_mutex_lock(m)
#define ap_mutex_unlock(m)  pthread_mutex_unlock(m)

#ifndef _strdup
#define _strdup strdup
#endif
#ifndef _stricmp
#define _stricmp strcasecmp
#endif

#endif /* _WIN32 */

AP_UNUSED static int ap_is_dir(const char *path)  { return ap_path_attr_(path, 1); }
AP_UNUSED static int ap_is_file(const char *path) { return ap_path_attr_(path, 0); }

#endif /* AP_PLATFORM_H */
