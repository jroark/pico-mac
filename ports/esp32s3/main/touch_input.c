#include "touch_input.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pinmap.h"

#define TOUCH_HOST SPI2_HOST
#define TOUCH_HZ (1 * 1000 * 1000)

#define LCD_WIDTH 320
#define LCD_HEIGHT 240

#define TOUCH_RAW_MIN_X 220
#define TOUCH_RAW_MAX_X 3880
#define TOUCH_RAW_MIN_Y 220
#define TOUCH_RAW_MAX_Y 3880
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 1
#define TOUCH_INVERT_Y 0
#define TOUCH_EDGE_SNAP_X 8
#define TOUCH_EDGE_SNAP_Y 8

static const char *TAG = "touch";
static spi_device_handle_t s_touch_spi;
static bool s_touch_ready = false;
static bool s_last_valid = false;
static int s_last_x = 0;
static int s_last_y = 0;
static int s_last_button = 0;
static int s_raw_min_x = TOUCH_RAW_MIN_X;
static int s_raw_max_x = TOUCH_RAW_MAX_X;
static int s_raw_min_y = TOUCH_RAW_MIN_Y;
static int s_raw_max_y = TOUCH_RAW_MAX_Y;
static int s_obs_min_x = 4095;
static int s_obs_max_x = 0;
static int s_obs_min_y = 4095;
static int s_obs_max_y = 0;
static int s_obs_count = 0;
static int s_valid_streak = 0;
static int s_invalid_streak = 0;
static int s_hist_x[3] = {0};
static int s_hist_y[3] = {0};
static int s_hist_count = 0;
static int s_hist_head = 0;

static int clampi(int v, int lo, int hi)
{
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
}

static int map_axis(int raw, int in_min, int in_max, int out_max)
{
        if (in_max <= in_min) {
                return out_max / 2;
        }
        int v = raw;
        if (v < in_min) v = in_min;
        if (v > in_max) v = in_max;
        return ((v - in_min) * out_max) / (in_max - in_min);
}

static int median3(int a, int b, int c)
{
        if (a > b) { int t = a; a = b; b = t; }
        if (b > c) { int t = b; b = c; c = t; }
        if (a > b) { int t = a; a = b; b = t; }
        return b;
}

static bool touch_pressed_irq(void)
{
        return gpio_get_level(PM_TOUCH_IRQ_GPIO) == 0;
}

static uint16_t touch_read_axis(uint8_t cmd)
{
        uint8_t tx[3] = {cmd, 0, 0};
        uint8_t rx[3] = {0, 0, 0};
        spi_transaction_t t = {
                .length = 24,
                .tx_buffer = tx,
                .rx_buffer = rx,
        };
        if (spi_device_transmit(s_touch_spi, &t) != ESP_OK) {
                return 0;
        }
        return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
}

static bool touch_read_raw(int *raw_x, int *raw_y)
{
        /* Median over 5 samples + pressure gate for stable XPT2046 reads. */
        uint16_t xs[5];
        uint16_t ys[5];
        for (int i = 0; i < 5; i++) {
                xs[i] = touch_read_axis(0xD0);
                ys[i] = touch_read_axis(0x90);
        }
        uint16_t z1 = touch_read_axis(0xB0);
        uint16_t z2 = touch_read_axis(0xC0);
        for (int i = 0; i < 4; i++) {
                for (int j = i + 1; j < 5; j++) {
                        if (xs[j] < xs[i]) { uint16_t t = xs[i]; xs[i] = xs[j]; xs[j] = t; }
                        if (ys[j] < ys[i]) { uint16_t t = ys[i]; ys[i] = ys[j]; ys[j] = t; }
                }
        }
        *raw_x = xs[2];
        *raw_y = ys[2];
        bool irq_pressed = touch_pressed_irq();
        bool z_valid = (z1 > 20 && z1 < 4090 && z2 > 20 && z2 < 4090);
        bool pressure_pressed = (z2 > (z1 + 25));
        bool xy_valid = (*raw_x > 20 && *raw_x < 4090 && *raw_y > 20 && *raw_y < 4090);
        bool spread_ok = ((xs[4] - xs[0]) <= 250) && ((ys[4] - ys[0]) <= 250);
        /* Accept press if either hardware IRQ or pressure looks valid. */
        bool pressed = irq_pressed || pressure_pressed;
        return (z_valid && xy_valid && spread_ok && pressed);
}

static void touch_raw_normalize_axes(int raw_x, int raw_y, int *tx, int *ty)
{
        int nx = raw_x;
        int ny = raw_y;
#if TOUCH_SWAP_XY
        int t = nx;
        nx = ny;
        ny = t;
#endif
        *tx = nx;
        *ty = ny;
}

static void touch_raw_to_lcd(int raw_x, int raw_y, int *lx, int *ly)
{
        int tx = 0;
        int ty = 0;
        touch_raw_normalize_axes(raw_x, raw_y, &tx, &ty);
        *lx = map_axis(tx, s_raw_min_x, s_raw_max_x, LCD_WIDTH - 1);
        *ly = map_axis(ty, s_raw_min_y, s_raw_max_y, LCD_HEIGHT - 1);
#if TOUCH_INVERT_X
        *lx = (LCD_WIDTH - 1) - *lx;
#endif
#if TOUCH_INVERT_Y
        *ly = (LCD_HEIGHT - 1) - *ly;
#endif
        *lx = clampi(*lx, 0, LCD_WIDTH - 1);
        *ly = clampi(*ly, 0, LCD_HEIGHT - 1);
}

static void touch_cal_observe(int raw_x, int raw_y)
{
        int tx = 0;
        int ty = 0;
        touch_raw_normalize_axes(raw_x, raw_y, &tx, &ty);

        if (tx < s_obs_min_x) s_obs_min_x = tx;
        if (tx > s_obs_max_x) s_obs_max_x = tx;
        if (ty < s_obs_min_y) s_obs_min_y = ty;
        if (ty > s_obs_max_y) s_obs_max_y = ty;
        s_obs_count++;
        if (s_obs_count < 120) {
                return;
        }

        int target_min_x = clampi(s_obs_min_x - 40, 20, 3800);
        int target_max_x = clampi(s_obs_max_x + 40, 300, 4090);
        int target_min_y = clampi(s_obs_min_y - 40, 20, 3800);
        int target_max_y = clampi(s_obs_max_y + 40, 300, 4090);
        if ((target_max_x - target_min_x) < 1200 || (target_max_y - target_min_y) < 1200) {
                return;
        }

        /* Slow adaptation reduces drift while still converging to real panel bounds. */
        s_raw_min_x = (s_raw_min_x * 15 + target_min_x) / 16;
        s_raw_max_x = (s_raw_max_x * 15 + target_max_x) / 16;
        s_raw_min_y = (s_raw_min_y * 15 + target_min_y) / 16;
        s_raw_max_y = (s_raw_max_y * 15 + target_max_y) / 16;
}

bool touch_input_init(void)
{
        gpio_config_t in = {
                .pin_bit_mask = (1ULL << PM_TOUCH_IRQ_GPIO),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&in) != ESP_OK) {
                return false;
        }

        spi_device_interface_config_t devcfg = {
                .clock_speed_hz = TOUCH_HZ,
                .mode = 0,
                .spics_io_num = PM_TOUCH_CS_GPIO,
                .queue_size = 1,
        };
        esp_err_t err = spi_bus_add_device(TOUCH_HOST, &devcfg, &s_touch_spi);
        if (err != ESP_OK) {
                ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
                return false;
        }

        s_last_valid = false;
        s_last_button = 0;
        s_obs_min_x = 4095;
        s_obs_max_x = 0;
        s_obs_min_y = 4095;
        s_obs_max_y = 0;
        s_obs_count = 0;
        s_valid_streak = 0;
        s_invalid_streak = 0;
        s_hist_count = 0;
        s_hist_head = 0;
        s_touch_ready = true;
        ESP_LOGI(TAG, "touch ready (XPT2046)");
        return true;
}

bool touch_input_poll(int *dx, int *dy, int *button)
{
        if (!s_touch_ready || !dx || !dy || !button) {
                return false;
        }

        int raw_x = 0, raw_y = 0;
        if (touch_read_raw(&raw_x, &raw_y)) {
                s_invalid_streak = 0;
                if (s_valid_streak < 3) {
                        s_valid_streak++;
                }
                if (s_valid_streak < 2) {
                        *dx = 0;
                        *dy = 0;
                        *button = 0;
                        return false;
                }

                touch_cal_observe(raw_x, raw_y);
                int was_pressed = s_last_button;
                int raw_x2 = 0, raw_y2 = 0;
                int lx = 0, ly = 0;
                if (s_last_valid) {
                        const int max_step = 70;
                        touch_raw_to_lcd(raw_x, raw_y, &lx, &ly);
                        if (abs(lx - s_last_x) > max_step || abs(ly - s_last_y) > max_step) {
                                if (touch_read_raw(&raw_x2, &raw_y2)) {
                                        int lx2 = 0, ly2 = 0;
                                        touch_raw_to_lcd(raw_x2, raw_y2, &lx2, &ly2);
                                        if (abs(lx2 - s_last_x) > max_step || abs(ly2 - s_last_y) > max_step) {
                                                lx = s_last_x;
                                                ly = s_last_y;
                                        } else {
                                                lx = lx2;
                                                ly = ly2;
                                        }
                                } else {
                                        lx = s_last_x;
                                        ly = s_last_y;
                                }
                        }
                } else {
                        touch_raw_to_lcd(raw_x, raw_y, &lx, &ly);
                }

                if (lx <= TOUCH_EDGE_SNAP_X) {
                        lx = 0;
                } else if (lx >= (LCD_WIDTH - 1 - TOUCH_EDGE_SNAP_X)) {
                        lx = LCD_WIDTH - 1;
                }
                if (ly <= TOUCH_EDGE_SNAP_Y) {
                        ly = 0;
                } else if (ly >= (LCD_HEIGHT - 1 - TOUCH_EDGE_SNAP_Y)) {
                        ly = LCD_HEIGHT - 1;
                }

                s_hist_x[s_hist_head] = lx;
                s_hist_y[s_hist_head] = ly;
                s_hist_head = (s_hist_head + 1) % 3;
                if (s_hist_count < 3) {
                        s_hist_count++;
                }
                if (s_hist_count == 3) {
                        lx = median3(s_hist_x[0], s_hist_x[1], s_hist_x[2]);
                        ly = median3(s_hist_y[0], s_hist_y[1], s_hist_y[2]);
                }

                int ndx = 0;
                int ndy = 0;
                if (s_last_valid) {
                        ndx = lx - s_last_x;
                        ndy = ly - s_last_y;
                        /* Small deadzone to reduce pointer jitter when held still. */
                        if (abs(ndx) <= 1) ndx = 0;
                        if (abs(ndy) <= 1) ndy = 0;
                }
                s_last_x = lx;
                s_last_y = ly;
                s_last_valid = true;
                *dx = ndx;
                *dy = ndy;
                *button = 1;
                s_last_button = 1;
                return (ndx != 0 || ndy != 0 || was_pressed == 0);
        }

        s_valid_streak = 0;
        if (s_invalid_streak < 3) {
                s_invalid_streak++;
        }
        if (s_invalid_streak < 2) {
                return false;
        }

        if (s_last_button) {
                s_last_valid = false;
                s_hist_count = 0;
                s_hist_head = 0;
                s_last_button = 0;
                *dx = 0;
                *dy = 0;
                *button = 0;
                return true;
        }

        return false;
}
