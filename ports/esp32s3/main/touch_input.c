#include "touch_input.h"
#include "pinmap_ws28b.h"
#include "tca9554.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lcd_touch_gt911.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef DISP_WIDTH
#define DISP_WIDTH 640
#endif
#ifndef DISP_HEIGHT
#define DISP_HEIGHT 480
#endif

static const char *TAG = "TOUCH_GT911";
static esp_lcd_touch_handle_t touch_handle = NULL;

/* Board-specific GT911 reset/address-select sequence (Waveshare 2.8B). */
static void gt911_board_reset_select_addr(bool addr_0x5d)
{
        gpio_config_t irq_cfg = {
                .pin_bit_mask = (1ULL << PM_TOUCH_IRQ_GPIO),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&irq_cfg);
        gpio_set_level(PM_TOUCH_IRQ_GPIO, addr_0x5d ? 0 : 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        tca9554_set_mode(PM_EXIO_TP_RST, false);
        tca9554_set_level(PM_EXIO_TP_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        tca9554_set_level(PM_EXIO_TP_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(60));

        gpio_set_level(PM_TOUCH_IRQ_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_direction(PM_TOUCH_IRQ_GPIO, GPIO_MODE_INPUT);
}

bool touch_input_init(void)
{
        ESP_LOGI(TAG, "Initializing GT911 touch controller");

        /* Match Waveshare bring-up: INT level during reset selects I2C address. */
        gt911_board_reset_select_addr(true);

        esp_lcd_panel_io_handle_t io_handle = NULL;
        /* Primary address selected by reset sequence above. */
        esp_lcd_panel_io_i2c_config_t i2c_conf = {
                .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, /* 0x5D */
                .control_phase_bytes = 1,
                .dc_bit_offset = 0,
                .lcd_cmd_bits = 16,
                .lcd_param_bits = 0,
                .flags = {
                        .disable_control_phase = 1,
                }
        };
        esp_lcd_touch_io_gt911_config_t gt911_cfg = {
                .dev_addr = i2c_conf.dev_addr,
        };

        // Note: I2C bus is already initialized by tca9554_init in lcd_ws28b_init
        esp_err_t ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &i2c_conf, &io_handle);
        if (ret != ESP_OK) return false;

        esp_lcd_touch_config_t touch_config = {
                /* Raw GT911 axes on this board: X=0..639, Y=0..479 */
                .x_max = 640,
                .y_max = 480,
                .rst_gpio_num = -1, // Handled via expander
                .int_gpio_num = PM_TOUCH_IRQ_GPIO,
                .levels = {
                        .reset = 0,
                        .interrupt = 0,
                },
                .flags = {
                        .swap_xy = 0,
                        .mirror_x = 0,
                        .mirror_y = 0,
                },
                .driver_data = &gt911_cfg,
        };

        ret = esp_lcd_touch_new_i2c_gt911(io_handle, &touch_config, &touch_handle);
        if (ret != ESP_OK) {
                ESP_LOGW(TAG, "GT911 init at 0x5D failed, trying 0x14");
                gt911_board_reset_select_addr(false);
                i2c_conf.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP; /* 0x14 */
                gt911_cfg.dev_addr = i2c_conf.dev_addr;
                ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &i2c_conf, &io_handle);
                if (ret == ESP_OK) {
                        ret = esp_lcd_touch_new_i2c_gt911(io_handle, &touch_config, &touch_handle);
                }
        }

        if (ret != ESP_OK) return false;

        ESP_LOGI(TAG, "GT911 initialized");
        return true;
}

bool touch_input_poll(int *dx, int *dy, int *button)
{
        if (!touch_handle) return false;

        esp_lcd_touch_point_data_t point;
        uint8_t count;
        
        esp_lcd_touch_read_data(touch_handle);
        esp_err_t err = esp_lcd_touch_get_data(touch_handle, &point, &count, 1);

        static int last_out_x = 0;
        static int last_out_y = 0;
        static int last_raw_x = 0;
        static int last_raw_y = 0;
        static int filt_x_q8 = 0;
        static int filt_y_q8 = 0;
        static uint8_t last_track_id = 0xff;
        static bool last_pressed = false;

        if (err == ESP_OK && count > 0) {
                /* Map controller range (~640x480) into current emulated framebuffer size. */
                int sx = ((int)point.x * DISP_WIDTH) / 640;
                int sy = ((int)point.y * DISP_HEIGHT) / 480;

                if (sx < 0) sx = 0;
                if (sx >= DISP_WIDTH) sx = DISP_WIDTH - 1;
                if (sy < 0) sy = 0;
                if (sy >= DISP_HEIGHT) sy = DISP_HEIGHT - 1;

                if (!last_pressed) {
                        last_raw_x = sx;
                        last_raw_y = sy;
                        filt_x_q8 = sx << 8;
                        filt_y_q8 = sy << 8;
                        last_out_x = sx;
                        last_out_y = sy;
                        last_track_id = point.track_id;
                        *dx = 0;
                        *dy = 0;
                } else {
                        int raw_dx = sx - last_raw_x;
                        int raw_dy = sy - last_raw_y;
                        last_raw_x = sx;
                        last_raw_y = sy;

                        /* Ignore sporadic outlier samples that cause large jumps. */
                        if (point.track_id == last_track_id &&
                            (abs(raw_dx) > 120 || abs(raw_dy) > 120)) {
                                *dx = 0;
                                *dy = 0;
                                *button = 1;
                                return true;
                        }
                        last_track_id = point.track_id;

                        /* Exponential moving average (alpha = 0.5) on mapped coordinates. */
                        int target_x_q8 = sx << 8;
                        int target_y_q8 = sy << 8;
                        filt_x_q8 += (target_x_q8 - filt_x_q8) >> 1;
                        filt_y_q8 += (target_y_q8 - filt_y_q8) >> 1;

                        int out_x = filt_x_q8 >> 8;
                        int out_y = filt_y_q8 >> 8;
                        int ddx = out_x - last_out_x;
                        int ddy = out_y - last_out_y;
                        last_out_x = out_x;
                        last_out_y = out_y;

                        /* Jitter deadzone. */
                        if (abs(ddx) <= 1) ddx = 0;
                        if (abs(ddy) <= 1) ddy = 0;

                        /* Cap per-sample delta to suppress noisy bursts. */
                        if (ddx > 20) ddx = 20;
                        if (ddx < -20) ddx = -20;
                        if (ddy > 20) ddy = 20;
                        if (ddy < -20) ddy = -20;

                        *dx = ddx;
                        *dy = ddy;
                }
                last_pressed = true;
                *button = 1;
                return true;
        } else if (last_pressed) {
                *dx = 0;
                *dy = 0;
                *button = 0;
                last_pressed = false;
                last_track_id = 0xff;
                return true;
        }

        return false;
}
