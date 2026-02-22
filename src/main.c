/* pico-umac
 *
 * Main loop to initialise umac, and run main event loop (piping
 * keyboard/mouse events in).
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hw.h"
#include "platform.h"
#include "video.h"
#include "kbd.h"
#include "log.h"

#include "bsp/rp2040/board.h"
#include "tusb.h"

#include "umac.h"
#include "rom.h"

#if USE_SD
#include "f_util.h"
#include "ff.h"
#include "rtc.h"
#include "hw_config.h"
#endif

////////////////////////////////////////////////////////////////////////////////
// Imports and data

extern void     hid_app_task(void);
extern int cursor_x;
extern int cursor_y;
extern int cursor_button;

// Mac binary data:  disc and ROM images
static const uint8_t umac_disc[] = {
#include "umac-disc.h"
};
static const uint8_t umac_rom[] = {
#include "umac-rom.h"
};

static uint8_t umac_ram[RAM_SIZE];

////////////////////////////////////////////////////////////////////////////////

static void     io_init()
{
        platform_led_init();
}

static void     poll_led_etc()
{
        static bool led_on = false;
        static uint64_t last_us = 0;
        uint64_t now_us = platform_time_us();

        if ((now_us - last_us) > 500000) {
                last_us = now_us;

                led_on = !led_on;
                platform_led_set(led_on);
        }
}

static int umac_cursor_x = 0;
static int umac_cursor_y = 0;
static int umac_cursor_button = 0;

#if USE_SD
static bool sd_ready = false;
static sd_card_t *mounted_sd = NULL;
static const uint8_t *capture_fb_base = NULL;

#if USE_BOOTSEL_CAPTURE
static void capture_write_screenshot(void)
{
        if (!sd_ready || !capture_fb_base) {
                return;
        }

        char path[20];
        UINT wrote = 0;
        FRESULT fr = FR_OK;
        FIL fp = {0};
        unsigned int index = 0;
        bool opened = false;

        for (index = 0; index < 10000; index++) {
                snprintf(path, sizeof(path), "0:/CAP%04u.PBM", index);
                fr = f_open(&fp, path, FA_CREATE_NEW | FA_WRITE);
                if (fr == FR_OK) {
                        opened = true;
                        break;
                }
                if (fr != FR_EXIST) {
                        log_printf("capture: open %s failed: %s (%d)\n",
                                   path, FRESULT_str(fr), fr);
                        return;
                }
        }
        if (!opened) {
                log_printf("capture: no free CAPxxxx.PBM filename\n");
                return;
        }

        char header[24];
        int header_len = snprintf(header, sizeof(header), "P4\n%d %d\n", DISP_WIDTH, DISP_HEIGHT);
        fr = f_write(&fp, header, (UINT)header_len, &wrote);
        if (fr != FR_OK || wrote != (UINT)header_len) {
                log_printf("capture: header write failed: %s (%d)\n", FRESULT_str(fr), fr);
                (void)f_close(&fp);
                return;
        }

        const UINT row_bytes = DISP_WIDTH / 8;
        for (int y = 0; y < DISP_HEIGHT; y++) {
                const uint8_t *row = capture_fb_base + (y * row_bytes);
                fr = f_write(&fp, row, row_bytes, &wrote);
                if (fr != FR_OK || wrote != row_bytes) {
                        log_printf("capture: data write failed at row %d: %s (%d)\n",
                                   y, FRESULT_str(fr), fr);
                        (void)f_close(&fp);
                        return;
                }
        }

        fr = f_sync(&fp);
        (void)f_close(&fp);
        if (fr != FR_OK) {
                log_printf("capture: sync failed: %s (%d)\n", FRESULT_str(fr), fr);
                return;
        }

        log_printf("capture: wrote %s\n", path);
}

static void capture_button_task(void)
{
        static bool last_pressed = false;
        static uint64_t last_poll_us = 0;
        static uint64_t last_capture_us = 0;
        uint64_t now_us = platform_time_us();

        if ((now_us - last_poll_us) < (60 * 1000)) {
                return;
        }
        last_poll_us = now_us;

        bool pressed = platform_bootsel_pressed();
        if (pressed && !last_pressed &&
            (now_us - last_capture_us) > (700 * 1000)) {
                last_capture_us = now_us;
                capture_write_screenshot();
        }
        last_pressed = pressed;
}
#endif
#endif

static void     poll_umac()
{
        static uint64_t last_1hz_us = 0;
        static uint64_t last_vsync_us = 0;
        uint64_t now_us = platform_time_us();

        umac_loop();

        uint64_t p_1hz = now_us - last_1hz_us;
        uint64_t p_vsync = now_us - last_vsync_us;
        if (p_vsync >= 16667) {
                /* FIXME: Trigger this off actual vsync */
                umac_vsync_event();
                last_vsync_us = now_us;
        }
        if (p_1hz >= 1000000) {
                umac_1hz_event();
                last_1hz_us = now_us;
        }

        int update = 0;
        int dx = 0;
        int dy = 0;
        int b = umac_cursor_button;
        if (cursor_x != umac_cursor_x) {
                dx = cursor_x - umac_cursor_x;
                umac_cursor_x = cursor_x;
                update = 1;
        }
        if (cursor_y != umac_cursor_y) {
                dy = cursor_y - umac_cursor_y;
                umac_cursor_y = cursor_y;
                update = 1;
        }
        if (cursor_button != umac_cursor_button) {
                b = cursor_button;
                umac_cursor_button = cursor_button;
                update = 1;
        }
        if (update) {
                umac_mouse(dx, -dy, b);
        }

        if (!kbd_queue_empty()) {
                uint16_t k = kbd_queue_pop();
                umac_kbd_event(k & 0xff, !!(k & 0x8000));
        }
}

#if USE_SD
static int      disc_do_read(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FIL *fp = (FIL *)ctx;
        f_lseek(fp, offset);
        unsigned int did_read = 0;
        FRESULT fr = f_read(fp, data, len, &did_read);
        if (fr != FR_OK || len != did_read) {
                log_printf("disc: f_read returned %d, read %u (of %u)\n", fr, did_read, len);
                return -1;
        }
        return 0;
}

static int      disc_do_write(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FIL *fp = (FIL *)ctx;
        f_lseek(fp, offset);
        unsigned int did_write = 0;
        FRESULT fr = f_write(fp, data, len, &did_write);
        if (fr != FR_OK || len != did_write) {
                log_printf("disc: f_write returned %d, read %u (of %u)\n", fr, did_write, len);
                return -1;
        }
        return 0;
}

static FIL discfp;
static FIL logfp;

static void     setup_sd_logging(sd_card_t *pSD)
{
        static const char *const log_names[] = {
                "umac.log",
                "UMAC.LOG",
                "0:umac.log",
                "0:UMAC.LOG",
                "0:/umac.log",
                "0:/UMAC.LOG",
        };
        static const BYTE open_modes[] = {
                FA_OPEN_APPEND | FA_WRITE,
                FA_OPEN_ALWAYS | FA_WRITE,
        };
        FRESULT fr = f_chdrive(pSD->pcName);
        if (fr != FR_OK) {
                log_printf("  warning: chdrive %s failed: %s (%d)\n",
                           pSD->pcName, FRESULT_str(fr), fr);
        }

        const char *chosen = NULL;
        for (unsigned int m = 0; m < (sizeof(open_modes) / sizeof(open_modes[0])); m++) {
                for (unsigned int i = 0; i < (sizeof(log_names) / sizeof(log_names[0])); i++) {
                        fr = f_open(&logfp, log_names[i], open_modes[m]);
                        if (fr == FR_OK) {
                                chosen = log_names[i];
                                goto opened;
                        }
                        log_printf("  warning: can't open %s: %s (%d)\n",
                                   log_names[i], FRESULT_str(fr), fr);
                }
        }
opened:
        if (!chosen) {
                return;
        }

        fr = f_lseek(&logfp, f_size(&logfp));
        if (fr != FR_OK) {
                log_printf("  warning: can't seek %s: %s (%d)\n",
                           chosen, FRESULT_str(fr), fr);
                (void)f_close(&logfp);
                return;
        }

        static const char probe[] = "[pico-umac] log file opened\n";
        UINT wrote = 0;
        fr = f_write(&logfp, probe, sizeof(probe) - 1, &wrote);
        if (fr != FR_OK || wrote != sizeof(probe) - 1) {
                log_printf("  warning: can't write %s: %s (%d), wrote=%u\n",
                           chosen, FRESULT_str(fr), fr, wrote);
                (void)f_close(&logfp);
                return;
        }
        fr = f_sync(&logfp);
        if (fr != FR_OK) {
                log_printf("  warning: can't sync %s: %s (%d)\n",
                           chosen, FRESULT_str(fr), fr);
                (void)f_close(&logfp);
                return;
        }

        log_set_sd_file(&logfp);
        log_printf("  logging to %s\n", chosen);
}

#if USE_WAVESHARE_LCD && USE_TOUCH
static bool load_touch_calibration(touch_calibration_t *cal)
{
        if (!cal) {
                return false;
        }
        static const char *const paths[] = {
                "0:/touch.cal",
                "touch.cal",
                "0:/TOUCH.CAL",
        };
        char buf[96] = {0};
        for (unsigned int i = 0; i < (sizeof(paths) / sizeof(paths[0])); i++) {
                FIL fp = {0};
                FRESULT fr = f_open(&fp, paths[i], FA_READ);
                if (fr != FR_OK) {
                        continue;
                }
                UINT did = 0;
                fr = f_read(&fp, buf, sizeof(buf) - 1, &did);
                (void)f_close(&fp);
                if (fr != FR_OK || did == 0) {
                        continue;
                }
                buf[did] = '\0';
                int a, b, c, d;
                if (sscanf(buf, "%d %d %d %d", &a, &b, &c, &d) == 4) {
                        cal->raw_min_x = a;
                        cal->raw_max_x = b;
                        cal->raw_min_y = c;
                        cal->raw_max_y = d;
                        return true;
                }
        }
        return false;
}

static void save_touch_calibration(const touch_calibration_t *cal)
{
        if (!cal) {
                return;
        }
        FIL fp = {0};
        FRESULT fr = f_open(&fp, "0:/touch.cal", FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) {
                log_printf("touch calib: open touch.cal failed: %s (%d)\n", FRESULT_str(fr), fr);
                return;
        }
        char out[96];
        int n = snprintf(out, sizeof(out), "%d %d %d %d\n",
                         cal->raw_min_x, cal->raw_max_x,
                         cal->raw_min_y, cal->raw_max_y);
        UINT wrote = 0;
        fr = f_write(&fp, out, (UINT)n, &wrote);
        if (fr == FR_OK && wrote == (UINT)n) {
                fr = f_sync(&fp);
        }
        (void)f_close(&fp);
        if (fr != FR_OK) {
                log_printf("touch calib: save touch.cal failed: %s (%d)\n", FRESULT_str(fr), fr);
        } else {
                log_printf("touch calib: saved to touch.cal\n");
        }
}

static void maybe_run_touch_calibration(void)
{
        if (!sd_ready || !mounted_sd) {
                return;
        }
        touch_calibration_t cal = {0};
        if (load_touch_calibration(&cal)) {
                video_touch_set_calibration(&cal);
                log_printf("touch calib: loaded x[%d..%d] y[%d..%d]\n",
                           cal.raw_min_x, cal.raw_max_x, cal.raw_min_y, cal.raw_max_y);
                return;
        }

        log_printf("touch calib: no touch.cal, starting calibration\n");
        if (video_touch_calibrate(&cal)) {
                video_touch_set_calibration(&cal);
                save_touch_calibration(&cal);
        } else {
                log_printf("touch calib: calibration failed, using defaults\n");
        }
}
#else
static void maybe_run_touch_calibration(void)
{
}
#endif
#endif

static void     disc_setup(disc_descr_t discs[DISC_NUM_DRIVES])
{
#if USE_SD
        const char *disc0_name = NULL;
        const char *disc0_ro_name = "umac0ro.img";
        const char *disc0_pattern = "umac0*.img";

#if USE_WAVESHARE_LCD
        /* On Waveshare boards, LCD/touch share SPI with SD.
         * Ensure both other slaves are de-selected before SD init/mount.
         */
        gpio_init(LCD_PIN_CS);
        gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
        gpio_put(LCD_PIN_CS, 1);
#if USE_TOUCH
        gpio_init(TOUCH_PIN_CS);
        gpio_set_dir(TOUCH_PIN_CS, GPIO_OUT);
        gpio_put(TOUCH_PIN_CS, 1);
#endif
#endif

        /* Mount SD filesystem */
        log_printf("Starting SPI/FatFS:\n");
        set_spi_dma_irq_channel(true, false);
        sd_card_t *pSD = sd_get_by_num(0);
        if (!pSD) {
                log_printf("  no SD device config found\n");
                goto no_sd;
        }
        FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
        log_printf("  mount: %d\n", fr);
        if (fr != FR_OK) {
                log_printf("  error mounting disc: %s (%d)\n", FRESULT_str(fr), fr);
                goto no_sd;
        }
        sd_ready = true;
        mounted_sd = pSD;

        /* If available, also mirror console logs to a file on SD. */
        setup_sd_logging(pSD);

        /* Look for a disc image */
        DIR di = {0};
        FILINFO fi = {0};
        fr = f_findfirst(&di, &fi, "/", disc0_pattern);
        if (fr != FR_OK || fi.fname[0] == '\0') {
                log_printf("  Can't find images %s: %s (%d)\n", disc0_pattern, FRESULT_str(fr), fr);
                goto no_sd;
        }
        disc0_name = fi.fname;
        f_closedir(&di);

        int read_only = !strcmp(disc0_name, disc0_ro_name);
        log_printf("  Opening %s (R%c)\n", disc0_name, read_only ? 'O' : 'W');

        /* Open image, set up disc info: */
        BYTE open_mode = FA_OPEN_EXISTING | FA_READ | (read_only ? 0 : FA_WRITE);
        fr = f_open(&discfp, disc0_name, open_mode);
        if (fr != FR_OK && fr != FR_EXIST) {
                log_printf("  *** Can't open %s: %s (%d)!\n", disc0_name, FRESULT_str(fr), fr);
                goto no_sd;
        } else {
                unsigned int disc_size = (unsigned int)f_size(&discfp);
                if (disc_size == 0 && fi.fsize > 0) {
                        /* Work around f_size() formatting/type oddities on some builds. */
                        disc_size = fi.fsize;
                }
                log_printf("  Opened, size %u bytes\n", disc_size);
                if (read_only)
                        log_printf("  (disc is read-only)\n");
                discs[0].base = 0; // Means use R/W ops
                discs[0].read_only = read_only;
                discs[0].size = disc_size;
                discs[0].op_ctx = &discfp;
                discs[0].op_read = disc_do_read;
                discs[0].op_write = disc_do_write;
        }

        /* FIXME: NVRAM state could be stored on SD too.
         * We could also implement a menu here to select an image,
         * writing text to the framebuffer and checking kbd_queue_*()
         * for user input.
         */
        return;

no_sd:
        sd_ready = false;
        mounted_sd = NULL;
#endif
        /* If we don't find (or look for) an SD-based image, attempt
         * to use in-flash disc image:
         */
        discs[0].base = (void *)umac_disc;
        discs[0].read_only = 1;
        discs[0].size = sizeof(umac_disc);
}

static void     core1_main()
{
        disc_descr_t discs[DISC_NUM_DRIVES] = {0};

        log_printf("Core 1 started\n");
        log_printf("ROM bytes: %u, DISC bytes: %u\n", (unsigned int)sizeof(umac_rom), (unsigned int)sizeof(umac_disc));
        if (sizeof(umac_rom) < ROM_SIZE) {
                log_printf("*** WARNING: ROM image smaller than expected (%u < %u)\n",
                       (unsigned int)sizeof(umac_rom), (unsigned int)ROM_SIZE);
        }
        disc_setup(discs);

        /* Video runs on core 1, i.e. IRQs/DMA are unaffected by
         * core 0's USB activity.
         */
        uint32_t *fb = (uint32_t *)(umac_ram + umac_get_fb_offset());
        video_init(fb);
#if USE_SD
        capture_fb_base = (const uint8_t *)fb;
        maybe_run_touch_calibration();
#endif
        umac_init(umac_ram, (void *)umac_rom, discs);

        log_printf("Enjoyable Mac times now begin:\n\n");

        while (true) {
                poll_umac();
                video_task();
#if USE_SD && USE_BOOTSEL_CAPTURE
                capture_button_task();
#endif
        }
}

int     main()
{
        set_sys_clock_khz(250*1000, true);

	stdio_init_all();
        log_init();
        io_init();

        multicore_launch_core1(core1_main);

	log_printf("Starting, init usb\n");
        tusb_init();

        /* This happens on core 0: */
	while (true) {
                tuh_task();
                hid_app_task();
                poll_led_etc();
	}

	return 0;
}
