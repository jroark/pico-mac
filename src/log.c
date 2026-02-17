/*
 * Logging helper for stdout and optional SD file mirroring.
 */

#include <stdio.h>
#include <string.h>

#include "pico/sync.h"
#include "log.h"

#if USE_SD
static FIL *g_log_fp;
#endif
static mutex_t g_log_lock;
static bool g_log_lock_inited;

void log_init(void)
{
        if (!g_log_lock_inited) {
                mutex_init(&g_log_lock);
                g_log_lock_inited = true;
        }
}

#if USE_SD
void log_set_sd_file(FIL *fp)
{
        log_init();
        mutex_enter_blocking(&g_log_lock);
        g_log_fp = fp;
        mutex_exit(&g_log_lock);
}
#endif

int log_vprintf(const char *fmt, va_list args)
{
        log_init();

        va_list console_args;
        va_copy(console_args, args);
        int ret = vprintf(fmt, console_args);
        va_end(console_args);

#if USE_SD
        FIL *fp = NULL;
        mutex_enter_blocking(&g_log_lock);
        fp = g_log_fp;
        mutex_exit(&g_log_lock);
        if (fp) {
                char line[256];
                va_list file_args;
                va_copy(file_args, args);
                int needed = vsnprintf(line, sizeof(line), fmt, file_args);
                va_end(file_args);
                if (needed > 0) {
                        UINT wrote = 0;
                        int to_write = needed;
                        if (to_write > (int)sizeof(line) - 1) {
                                to_write = (int)sizeof(line) - 1;
                        }
                        mutex_enter_blocking(&g_log_lock);
                        FRESULT fr = f_write(fp, line, (UINT)to_write, &wrote);
                        if (fr == FR_OK && wrote == (UINT)to_write) {
                                f_sync(fp);
                        }
                        mutex_exit(&g_log_lock);
                }
        }
#endif
        return ret;
}

int log_printf(const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        int ret = log_vprintf(fmt, args);
        va_end(args);
        return ret;
}
