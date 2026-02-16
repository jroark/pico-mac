/* SPI LCD output for Waveshare Pico-ResTouch-LCD-2.8.
 *
 * This backend renders the emulator's 1bpp framebuffer into RGB565 and pushes
 * it to a 320x240 panel over SPI.
 *
 * Copyright 2026
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "video.h"

#if USE_SD
#include "hw_config.h"
#include "spi.h"
#endif

extern int cursor_x;
extern int cursor_y;
extern int cursor_button;

#if !defined(LCD_SPI) || !defined(LCD_WIDTH) || !defined(LCD_HEIGHT)
#error "LCD_* compile-time settings are required for video_waveshare_lcd.c"
#endif

#if (LCD_SPI == 0)
#define LCD_SPI_INST spi0
#elif (LCD_SPI == 1)
#define LCD_SPI_INST spi1
#else
#error "LCD_SPI must be 0 or 1"
#endif

static spi_inst_t *const lcd_spi = LCD_SPI_INST;
static uint32_t *video_framebuffer;
static uint8_t *video_framebuffer_bytes;
static absolute_time_t last_frame;
static absolute_time_t last_flush;
static int view_x0;
static int view_y0;
static int view_w;
static int view_h;
static uint32_t lcd_spi_hz = LCD_MHZ * 1000 * 1000;
#if USE_TOUCH
static uint32_t touch_spi_hz = TOUCH_MHZ * 1000 * 1000;
#endif

/* Line buffer in big-endian RGB565 bytes. */
static uint8_t line_buf[LCD_WIDTH * 2];
static uint16_t x_src0[LCD_WIDTH];
static uint16_t x_src1[LCD_WIDTH];
static uint16_t y_src0[LCD_HEIGHT];
static uint16_t y_src1[LCD_HEIGHT];
static int next_flush_line;

static inline int map_coord(int pos, int in_max, int out_max);

static inline void lcd_cs(bool active)
{
        gpio_put(LCD_PIN_CS, active ? 0 : 1);
}

static inline void lcd_dc(bool data)
{
        gpio_put(LCD_PIN_DC, data ? 1 : 0);
}

#if USE_TOUCH
static inline void touch_cs(bool active)
{
        gpio_put(TOUCH_PIN_CS, active ? 0 : 1);
}
#endif

static void lcd_write_bytes(const uint8_t *buf, size_t len)
{
        spi_write_blocking(lcd_spi, buf, len);
}

static void lcd_write_cmd(uint8_t cmd)
{
        lcd_dc(false);
        lcd_cs(true);
        lcd_write_bytes(&cmd, 1);
        lcd_cs(false);
}

static void lcd_write_data(const uint8_t *data, size_t len)
{
        lcd_dc(true);
        lcd_cs(true);
        lcd_write_bytes(data, len);
        lcd_cs(false);
}

static void lcd_write_u16be(uint16_t v)
{
        uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
        lcd_write_data(b, sizeof(b));
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        lcd_write_cmd(0x2A); /* CASET */
        lcd_write_u16be(x0);
        lcd_write_u16be(x1);

        lcd_write_cmd(0x2B); /* RASET */
        lcd_write_u16be(y0);
        lcd_write_u16be(y1);

        lcd_write_cmd(0x2C); /* RAMWR */
}

static void lcd_init_panel(void)
{
        /* Ensure other SPI slaves are de-selected before LCD traffic starts. */
#if USE_SD
#if (SD_SPI == LCD_SPI)
        gpio_init(SD_CS);
        gpio_set_dir(SD_CS, GPIO_OUT);
        gpio_put(SD_CS, 1);
#endif
#endif
#if USE_TOUCH
        gpio_init(TOUCH_PIN_CS);
        gpio_set_dir(TOUCH_PIN_CS, GPIO_OUT);
        gpio_put(TOUCH_PIN_CS, 1);
#endif

        gpio_init(LCD_PIN_CS);
        gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
        gpio_put(LCD_PIN_CS, 1);

        gpio_init(LCD_PIN_DC);
        gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
        gpio_put(LCD_PIN_DC, 0);

        gpio_init(LCD_PIN_RST);
        gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
        gpio_put(LCD_PIN_RST, 1);

        gpio_init(LCD_PIN_BL);
        gpio_set_dir(LCD_PIN_BL, GPIO_OUT);
        gpio_put(LCD_PIN_BL, 1);

        spi_init(lcd_spi, lcd_spi_hz);
        spi_set_format(lcd_spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
        gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
        gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SPI);

        sleep_ms(10);
        gpio_put(LCD_PIN_RST, 0);
        sleep_ms(20);
        gpio_put(LCD_PIN_RST, 1);
        sleep_ms(120);

        lcd_write_cmd(0x01); /* SWRESET */
        sleep_ms(120);

        lcd_write_cmd(0x11); /* SLPOUT */
        sleep_ms(120);

        {
                const uint8_t pixfmt = 0x55; /* 16bpp RGB565 */
                lcd_write_cmd(0x3A);
                lcd_write_data(&pixfmt, 1);
        }

        {
                /* Landscape, RGB */
                const uint8_t madctl = 0x28;
                lcd_write_cmd(0x36);
                lcd_write_data(&madctl, 1);
        }

        lcd_write_cmd(0x21); /* INVON */
        lcd_write_cmd(0x13); /* NORON */
        lcd_write_cmd(0x29); /* DISPON */
        sleep_ms(20);

        lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
}

#if USE_TOUCH
static void touch_init(void)
{
        gpio_init(TOUCH_PIN_CS);
        gpio_set_dir(TOUCH_PIN_CS, GPIO_OUT);
        gpio_put(TOUCH_PIN_CS, 1);

        gpio_init(TOUCH_PIN_IRQ);
        gpio_set_dir(TOUCH_PIN_IRQ, GPIO_IN);
        gpio_pull_up(TOUCH_PIN_IRQ);
}

static bool touch_is_pressed(void)
{
        return gpio_get(TOUCH_PIN_IRQ) == 0;
}

static uint16_t touch_read_axis(uint8_t cmd)
{
        uint8_t tx[3] = {cmd, 0, 0};
        uint8_t rx[3] = {0};

        touch_cs(true);
        spi_write_read_blocking(lcd_spi, tx, rx, sizeof(tx));
        touch_cs(false);
        return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
}

static bool touch_read_raw(int *raw_x, int *raw_y)
{
        spi_set_baudrate(lcd_spi, touch_spi_hz);
        bool irq_pressed = touch_is_pressed();
        if (TOUCH_USE_IRQ && !irq_pressed) {
                spi_set_baudrate(lcd_spi, lcd_spi_hz);
                return false;
        }

        /* Median-of-3 sample reject helps tame noisy raw readings. */
        uint16_t xs[3];
        uint16_t ys[3];
        for (int i = 0; i < 3; i++) {
                xs[i] = touch_read_axis(0xD0); /* X position */
                ys[i] = touch_read_axis(0x90); /* Y position */
        }
        uint16_t z1 = touch_read_axis(0xB0);
        uint16_t z2 = touch_read_axis(0xC0);
        for (int i = 0; i < 2; i++) {
                for (int j = i + 1; j < 3; j++) {
                        if (xs[j] < xs[i]) {
                                uint16_t t = xs[i];
                                xs[i] = xs[j];
                                xs[j] = t;
                        }
                        if (ys[j] < ys[i]) {
                                uint16_t t = ys[i];
                                ys[i] = ys[j];
                                ys[j] = t;
                        }
                }
        }
        *raw_x = xs[1];
        *raw_y = ys[1];

        bool z_valid = (z1 > 20 && z1 < 4090 && z2 > 20 && z2 < 4090);
        bool xy_valid = (*raw_x > 20 && *raw_x < 4090 && *raw_y > 20 && *raw_y < 4090);
        bool pressed;
#if TOUCH_USE_IRQ
        /* IRQ-gated mode: IRQ must assert, with plausible sampled data. */
        pressed = irq_pressed && (z_valid || xy_valid);
#else
        /* No-IRQ mode: rely on pressure validity to avoid false corner-locks. */
        pressed = z_valid;
#endif
        spi_set_baudrate(lcd_spi, lcd_spi_hz);
        return pressed;
}

static int map_axis(int raw, int raw_min, int raw_max, int out_max)
{
        if (raw_max <= raw_min) {
                return out_max / 2;
        }
        if (raw < raw_min) {
                raw = raw_min;
        } else if (raw > raw_max) {
                raw = raw_max;
        }
        return (raw - raw_min) * out_max / (raw_max - raw_min);
}

static int clampi(int v, int lo, int hi)
{
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
}

static void touch_update_mouse(void)
{
#if USE_SD
        /* Touch shares SPI with LCD/SD on this board; serialize when SD is enabled. */
        spi_t *sd_spi = spi_get_by_num(0);
        if (sd_spi && ((LCD_SPI == 0 && sd_spi->hw_inst == spi0) ||
                       (LCD_SPI == 1 && sd_spi->hw_inst == spi1))) {
                spi_lock(sd_spi);
        } else {
                sd_spi = NULL;
        }
#endif

        int raw_x = 0;
        int raw_y = 0;
        if (touch_read_raw(&raw_x, &raw_y)) {
                int tx = raw_x;
                int ty = raw_y;
#if TOUCH_SWAP_XY
                int t = tx;
                tx = ty;
                ty = t;
#endif
                /* First map raw touch to full LCD coordinates. */
                int lx = map_axis(tx, TOUCH_RAW_MIN_X, TOUCH_RAW_MAX_X, LCD_WIDTH - 1);
                int ly = map_axis(ty, TOUCH_RAW_MIN_Y, TOUCH_RAW_MAX_Y, LCD_HEIGHT - 1);
#if TOUCH_INVERT_X
                lx = (LCD_WIDTH - 1) - lx;
#endif
#if TOUCH_INVERT_Y
                ly = (LCD_HEIGHT - 1) - ly;
#endif
                /* Then map from displayed viewport (letterboxed region) to Mac space. */
                int vx = clampi(lx - view_x0, 0, view_w - 1);
                int vy = clampi(ly - view_y0, 0, view_h - 1);
                int mx = map_coord(vx, DISP_WIDTH, view_w);
                int my = map_coord(vy, DISP_HEIGHT, view_h);
                cursor_x = mx;
                cursor_y = my;
                cursor_button = 1;
        } else {
                cursor_button = 0;
        }

#if USE_SD
        if (sd_spi) {
                spi_unlock(sd_spi);
        }
#endif
}
#endif

static inline bool fb_is_black(int src_x, int src_y)
{
        unsigned int row_bytes = DISP_WIDTH / 8;
        unsigned int byte_index = (src_y * row_bytes) + (src_x >> 3);
        uint8_t b = video_framebuffer_bytes[byte_index];
        return (b & (0x80u >> (src_x & 7))) != 0;
}

static inline uint16_t gray_to_rgb565(uint8_t gray)
{
        return (uint16_t)(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3));
}

static inline uint16_t fb_sample_nearest_rgb565(int src_x, int src_y)
{
        return fb_is_black(src_x, src_y) ? 0x0000 : 0xFFFF;
}

static inline uint16_t fb_sample_area_rgb565(int x, int y)
{
        int sx0 = x_src0[x];
        int sx1 = x_src1[x];
        int sy0 = y_src0[y];
        int sy1 = y_src1[y];
        int black = 0;
        int total = (sx1 - sx0) * (sy1 - sy0);

        for (int yy = sy0; yy < sy1; yy++) {
                for (int xx = sx0; xx < sx1; xx++) {
                        black += fb_is_black(xx, yy) ? 1 : 0;
                }
        }

        uint8_t gray = (uint8_t)(255 - ((black * 255) / total));
        return gray_to_rgb565(gray);
}

static inline int map_coord(int pos, int in_max, int out_max)
{
        if (out_max <= 1) {
                return 0;
        }
        return (pos * (in_max - 1)) / (out_max - 1);
}

static void lcd_push_lines(int start_y, int line_count)
{
#if USE_SD
        /* If SD and LCD share a controller, serialize access with the SD SPI mutex. */
        spi_t *sd_spi = spi_get_by_num(0);
        if (sd_spi && ((LCD_SPI == 0 && sd_spi->hw_inst == spi0) ||
                       (LCD_SPI == 1 && sd_spi->hw_inst == spi1))) {
                spi_lock(sd_spi);
        } else {
                sd_spi = NULL;
        }
#endif
        int end_y = start_y + line_count - 1;
        if (start_y < 0) {
                start_y = 0;
        }
        if (end_y >= LCD_HEIGHT) {
                end_y = LCD_HEIGHT - 1;
        }
        if (start_y > end_y) {
#if USE_SD
                if (sd_spi) {
                        spi_unlock(sd_spi);
                }
#endif
                return;
        }

        lcd_set_window(0, start_y, LCD_WIDTH - 1, end_y);
        lcd_dc(true);
        lcd_cs(true);
        for (int y = start_y; y <= end_y; y++) {
                bool in_y = (y >= view_y0) && (y < (view_y0 + view_h));
#if (LCD_FILTER_MODE == 0)
                int src_y = in_y ? map_coord(y - view_y0, DISP_HEIGHT, view_h) : 0;
#endif
                for (int x = 0; x < LCD_WIDTH; x++) {
                        bool in_x = (x >= view_x0) && (x < (view_x0 + view_w));
                        uint16_t px = 0xFFFF; /* Border color */
                        if (in_x && in_y) {
#if (LCD_FILTER_MODE == 0)
                                int src_x = map_coord(x - view_x0, DISP_WIDTH, view_w);
                                px = fb_sample_nearest_rgb565(src_x, src_y);
#else
                                px = fb_sample_area_rgb565(x, y);
#endif
                        }
                        line_buf[2 * x] = (uint8_t)(px >> 8);
                        line_buf[2 * x + 1] = (uint8_t)px;
                }
                lcd_write_bytes(line_buf, sizeof(line_buf));
        }
        lcd_cs(false);
#if USE_SD
        if (sd_spi) {
                spi_unlock(sd_spi);
        }
#endif
}

void video_init(uint32_t *framebuffer)
{
        printf("Video init (Waveshare LCD)\n");
        video_framebuffer = framebuffer;
        video_framebuffer_bytes = (uint8_t *)framebuffer;
        /* Preserve source aspect ratio instead of stretching. */
        view_w = LCD_WIDTH;
        view_h = (LCD_WIDTH * DISP_HEIGHT) / DISP_WIDTH;
        if (view_h > LCD_HEIGHT) {
                view_h = LCD_HEIGHT;
                view_w = (LCD_HEIGHT * DISP_WIDTH) / DISP_HEIGHT;
        }
        view_x0 = (LCD_WIDTH - view_w) / 2;
        view_y0 = (LCD_HEIGHT - view_h) / 2;
        for (int x = 0; x < LCD_WIDTH; x++) {
                if (x < view_x0 || x >= (view_x0 + view_w)) {
                        x_src0[x] = 0;
                        x_src1[x] = 1;
                        continue;
                }
                int vx = x - view_x0;
                int sx0 = (vx * DISP_WIDTH) / view_w;
                int sx1 = ((vx + 1) * DISP_WIDTH) / view_w;
                if (sx1 <= sx0) {
                        sx1 = sx0 + 1;
                }
                x_src0[x] = sx0;
                x_src1[x] = sx1;
        }
        for (int y = 0; y < LCD_HEIGHT; y++) {
                if (y < view_y0 || y >= (view_y0 + view_h)) {
                        y_src0[y] = 0;
                        y_src1[y] = 1;
                        continue;
                }
                int vy = y - view_y0;
                int sy0 = (vy * DISP_HEIGHT) / view_h;
                int sy1 = ((vy + 1) * DISP_HEIGHT) / view_h;
                if (sy1 <= sy0) {
                        sy1 = sy0 + 1;
                }
                y_src0[y] = sy0;
                y_src1[y] = sy1;
        }
        lcd_init_panel();
#if USE_TOUCH
        touch_init();
#endif
        last_frame = get_absolute_time();
        last_flush = last_frame;
        next_flush_line = 0;
        lcd_push_lines(0, LCD_HEIGHT);
}

void video_task(void)
{
#if USE_TOUCH
        touch_update_mouse();
#endif
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(last_flush, now) < 3000) {
                return;
        }
        last_flush = now;

        enum { LCD_FLUSH_LINES = 16 };
        lcd_push_lines(next_flush_line, LCD_FLUSH_LINES);
        next_flush_line += LCD_FLUSH_LINES;
        if (next_flush_line >= LCD_HEIGHT) {
                next_flush_line = 0;
                last_frame = now;
        }
}
