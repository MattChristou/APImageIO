/*
 * ap_report.h - surface a loader's decode error (or a writer's failure) to
 * the user.
 *
 * Without this, a file a loader claims but cannot decode (IPSTAT_BADFILE)
 * produces only LightWave's generic load failure, and the bridge's reason
 * ("no mergedimage.png inside", "no RGB layer found", ...) is thrown away.
 *
 * Uses LWMessageFuncs->error, which writes to Layout's one-line message area
 * rather than raising a modal, so it never blocks an unattended job. Only call
 * it for BADFILE: a NOREC loader (OIIO) must stay silent, because another
 * loader may still succeed with the same file.
 */

#ifndef AP_REPORT_H
#define AP_REPORT_H

#include <lwserver.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "<format>: can't load <file name> - <err>". `err` may be empty or NULL. */
void ap_report_load_error(GlobalFunc *global, const char *format,
                          const char *filename, const char *err);

/* "<format>: can't save <file name> - <err>" - for writers whose failure would
 * otherwise leave no file and no message (e.g. a whole sequence written to a
 * missing folder). Same non-modal message line. */
void ap_report_save_error(GlobalFunc *global, const char *format,
                          const char *filename, const char *err);

/* Number of reports issued so far. A caller can compare it before and after
   an operation to tell whether a loader already explained a failure, and so
   avoid overwriting that specific message with a generic one. */
unsigned ap_report_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AP_REPORT_H */
