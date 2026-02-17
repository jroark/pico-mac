/*
 * Logging helper for console and optional SD file output.
 */

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

#if USE_SD
#include "ff.h"
#endif

void log_init(void);
int log_printf(const char *fmt, ...);
int log_vprintf(const char *fmt, va_list args);

#if USE_SD
void log_set_sd_file(FIL *fp);
#endif

#endif
