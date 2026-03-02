#include "lcd_ws28b.h"
#include "tca9554.h"
#include "pinmap_ws28b.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

static const char *TAG = "LCD_WS28B";

static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *framebuffer = NULL;
static uint8_t *mono_snapshot = NULL;
static size_t mono_snapshot_size = 0;

/* 2.8" is 480x640 Portrait physical */
#undef LCD_WIDTH_RES
#undef LCD_HEIGHT_RES
#define LCD_WIDTH_RES   480
#define LCD_HEIGHT_RES  640

/* 3-wire SPI bit-bang helpers for ST7701S initialization */
static void lcd_spi_write_bus(uint8_t data)
{
        for (int i = 0; i < 8; i++) {
                gpio_set_level(PM_LCD_SPI_SCL_GPIO, 0);
                gpio_set_level(PM_LCD_SPI_SDA_GPIO, (data & 0x80) ? 1 : 0);
                data <<= 1;
                gpio_set_level(PM_LCD_SPI_SCL_GPIO, 1);
        }
}

static void st7701_write_cmd(uint8_t cmd)
{
        gpio_set_level(PM_LCD_SPI_SCL_GPIO, 0);
        gpio_set_level(PM_LCD_SPI_SDA_GPIO, 0); // Command bit (D/C = 0)
        gpio_set_level(PM_LCD_SPI_SCL_GPIO, 1);
        lcd_spi_write_bus(cmd);
}

static void st7701_write_data(uint8_t data)
{
        gpio_set_level(PM_LCD_SPI_SCL_GPIO, 0);
        gpio_set_level(PM_LCD_SPI_SDA_GPIO, 1); // Data bit (D/C = 1)
        gpio_set_level(PM_LCD_SPI_SCL_GPIO, 1);
        lcd_spi_write_bus(data);
}

static void st7701_init_sequence(void)
{
        ESP_LOGI(TAG, "Sending ST7701S 480x640 init sequence");
        /* Keep CS active across the whole sequence, same as vendor driver. */
        tca9554_set_level(PM_EXIO_LCD_CS, 0);
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Sequence matched to Waveshare ESP32-S3-Touch-LCD-2.8B reference. */
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x13);
        st7701_write_cmd(0xEF); st7701_write_data(0x08);
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x10);
        st7701_write_cmd(0xC0); st7701_write_data(0x4F); st7701_write_data(0x00);
        st7701_write_cmd(0xC1); st7701_write_data(0x10); st7701_write_data(0x02);
        st7701_write_cmd(0xC2); st7701_write_data(0x07); st7701_write_data(0x02);
        st7701_write_cmd(0xCC); st7701_write_data(0x10);
        st7701_write_cmd(0xB0); st7701_write_data(0x00); st7701_write_data(0x10); st7701_write_data(0x17); st7701_write_data(0x0D); st7701_write_data(0x11); st7701_write_data(0x06); st7701_write_data(0x05); st7701_write_data(0x08); st7701_write_data(0x07); st7701_write_data(0x1F); st7701_write_data(0x04); st7701_write_data(0x11); st7701_write_data(0x0E); st7701_write_data(0x29); st7701_write_data(0x30); st7701_write_data(0x1F);
        st7701_write_cmd(0xB1); st7701_write_data(0x00); st7701_write_data(0x0D); st7701_write_data(0x14); st7701_write_data(0x0E); st7701_write_data(0x11); st7701_write_data(0x06); st7701_write_data(0x04); st7701_write_data(0x08); st7701_write_data(0x08); st7701_write_data(0x20); st7701_write_data(0x05); st7701_write_data(0x13); st7701_write_data(0x13); st7701_write_data(0x26); st7701_write_data(0x30); st7701_write_data(0x1F);
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x11);
        st7701_write_cmd(0xB0); st7701_write_data(0x65);
        st7701_write_cmd(0xB1); st7701_write_data(0x71);
        st7701_write_cmd(0xB2); st7701_write_data(0x82);
        st7701_write_cmd(0xB3); st7701_write_data(0x80);
        st7701_write_cmd(0xB5); st7701_write_data(0x42);
        st7701_write_cmd(0xB7); st7701_write_data(0x85);
        st7701_write_cmd(0xB8); st7701_write_data(0x20);
        st7701_write_cmd(0xC0); st7701_write_data(0x09);
        st7701_write_cmd(0xC1); st7701_write_data(0x78);
        st7701_write_cmd(0xC2); st7701_write_data(0x78);
        st7701_write_cmd(0xD0); st7701_write_data(0x88);
        st7701_write_cmd(0xEE); st7701_write_data(0x42);
        st7701_write_cmd(0xE0); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x02);
        st7701_write_cmd(0xE1); st7701_write_data(0x04); st7701_write_data(0xA0); st7701_write_data(0x06); st7701_write_data(0xA0); st7701_write_data(0x05); st7701_write_data(0xA0); st7701_write_data(0x07); st7701_write_data(0xA0); st7701_write_data(0x00); st7701_write_data(0x44); st7701_write_data(0x44);
        st7701_write_cmd(0xE2); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00);
        st7701_write_cmd(0xE3); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x22); st7701_write_data(0x22);
        st7701_write_cmd(0xE4); st7701_write_data(0x44); st7701_write_data(0x44);
        st7701_write_cmd(0xE5); st7701_write_data(0x0C); st7701_write_data(0x90); st7701_write_data(0xA0); st7701_write_data(0xA0); st7701_write_data(0x0E); st7701_write_data(0x92); st7701_write_data(0xA0); st7701_write_data(0xA0); st7701_write_data(0x08); st7701_write_data(0x8C); st7701_write_data(0xA0); st7701_write_data(0xA0); st7701_write_data(0x0A); st7701_write_data(0x8E); st7701_write_data(0xA0); st7701_write_data(0xA0);
        st7701_write_cmd(0xE6); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x22); st7701_write_data(0x22);
        st7701_write_cmd(0xE7); st7701_write_data(0x44); st7701_write_data(0x44);
        st7701_write_cmd(0xE8); st7701_write_data(0x0D); st7701_write_data(0x91); st7701_write_data(0xA0); st7701_write_data(0xA0); st7701_write_data(0x0F); st7701_write_data(0x93); st7701_write_data(0xA0); st7701_write_data(0xA0); st7701_write_data(0x09); st7701_write_data(0x8D); st7701_write_data(0xA0); st7701_write_data(0xA0); st7701_write_data(0x0B); st7701_write_data(0x8F); st7701_write_data(0xA0); st7701_write_data(0xA0);
        st7701_write_cmd(0xEB); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0xE4); st7701_write_data(0xE4); st7701_write_data(0x44); st7701_write_data(0x00); st7701_write_data(0x40);
        st7701_write_cmd(0xED); st7701_write_data(0xFF); st7701_write_data(0xF5); st7701_write_data(0x47); st7701_write_data(0x6F); st7701_write_data(0x0B); st7701_write_data(0xA1); st7701_write_data(0xAB); st7701_write_data(0xFF); st7701_write_data(0xFF); st7701_write_data(0xBA); st7701_write_data(0x1A); st7701_write_data(0xB0); st7701_write_data(0xF6); st7701_write_data(0x74); st7701_write_data(0x5F); st7701_write_data(0xFF);
        st7701_write_cmd(0xEF); st7701_write_data(0x08); st7701_write_data(0x08); st7701_write_data(0x08); st7701_write_data(0x40); st7701_write_data(0x3F); st7701_write_data(0x64);
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00);
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x13);
        st7701_write_cmd(0xE6); st7701_write_data(0x16); st7701_write_data(0x7C);
        st7701_write_cmd(0xE8); st7701_write_data(0x00); st7701_write_data(0x0E);
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00);
        st7701_write_cmd(0x11); st7701_write_data(0x00);
        vTaskDelay(pdMS_TO_TICKS(200));
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x13);
        st7701_write_cmd(0xE8); st7701_write_data(0x00); st7701_write_data(0x0C);
        vTaskDelay(pdMS_TO_TICKS(150));
        st7701_write_cmd(0xE8); st7701_write_data(0x00); st7701_write_data(0x00);
        st7701_write_cmd(0xFF); st7701_write_data(0x77); st7701_write_data(0x01); st7701_write_data(0x00); st7701_write_data(0x00); st7701_write_data(0x00);
        st7701_write_cmd(0x29); st7701_write_data(0x00);
        st7701_write_cmd(0x35); st7701_write_data(0x00);
        st7701_write_cmd(0x11); st7701_write_data(0x00);
        vTaskDelay(pdMS_TO_TICKS(200));
        st7701_write_cmd(0x29); st7701_write_data(0x00);
        st7701_write_cmd(0x29); st7701_write_data(0x00);
        vTaskDelay(pdMS_TO_TICKS(100));
        tca9554_set_level(PM_EXIO_LCD_CS, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t lcd_ws28b_init(void)
{
        /* 1. Init TCA9554 if not already done */
        esp_err_t ret = tca9554_init(PM_I2C_SDA_GPIO, PM_I2C_SCL_GPIO);
        if (ret != ESP_OK) {
                ESP_LOGE(TAG, "TCA9554 init failed: %d", ret);
                return ret;
        }

        /* 2. Setup SPI pins for ST7701S bit-bang */
        gpio_config_t io_conf = {
                .pin_bit_mask = (1ULL << PM_LCD_SPI_SCL_GPIO) | (1ULL << PM_LCD_SPI_SDA_GPIO) | (1ULL << PM_LCD_BL_GPIO),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(PM_LCD_BL_GPIO, 0);

        /* 3. Reset LCD via expander */
        tca9554_set_mode(PM_EXIO_LCD_RST, false);
        tca9554_set_mode(PM_EXIO_LCD_CS, false);
        tca9554_set_level(PM_EXIO_LCD_CS, 1);
        tca9554_set_level(PM_EXIO_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        tca9554_set_level(PM_EXIO_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(100));

        /* 4. Init ST7701S registers */
        st7701_init_sequence();

        /* 5. Setup RGB Panel - Using SKU 30103 specific GPIOs */
        esp_lcd_rgb_panel_config_t panel_config = {
                .clk_src = LCD_CLK_SRC_DEFAULT,
                .num_fbs = 1,
                .bounce_buffer_size_px = 10 * LCD_WIDTH_RES,
                .psram_trans_align = 64,
                .data_width = 16,
                .bits_per_pixel = 16,
                .de_gpio_num = PM_LCD_RGB_DE_GPIO,
                .pclk_gpio_num = PM_LCD_RGB_PCLK_GPIO,
                .vsync_gpio_num = PM_LCD_RGB_VSYNC_GPIO,
                .hsync_gpio_num = PM_LCD_RGB_HSYNC_GPIO,
                .data_gpio_nums = {
                        PM_LCD_RGB_B0_GPIO, PM_LCD_RGB_B1_GPIO, PM_LCD_RGB_B2_GPIO, PM_LCD_RGB_B3_GPIO, PM_LCD_RGB_B4_GPIO,
                        PM_LCD_RGB_G0_GPIO, PM_LCD_RGB_G1_GPIO, PM_LCD_RGB_G2_GPIO, PM_LCD_RGB_G3_GPIO, PM_LCD_RGB_G4_GPIO, PM_LCD_RGB_G5_GPIO,
                        PM_LCD_RGB_R0_GPIO, PM_LCD_RGB_R1_GPIO, PM_LCD_RGB_R2_GPIO, PM_LCD_RGB_R3_GPIO, PM_LCD_RGB_R4_GPIO
                },
                .timings = {
                        .pclk_hz = 16 * 1000 * 1000,
                        .h_res = LCD_WIDTH_RES,
                        .v_res = LCD_HEIGHT_RES,
                        .hsync_back_porch = 10,
                        .hsync_front_porch = 50,
                        .hsync_pulse_width = 8,
                        .vsync_back_porch = 18,
                        .vsync_front_porch = 8,
                        .vsync_pulse_width = 2,
                        .flags.pclk_active_neg = false,
                },
                .disp_gpio_num = -1,
                .flags.fb_in_psram = true,
        };

        ret = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
        if (ret != ESP_OK) return ret;

        ret = esp_lcd_panel_reset(panel_handle);
        if (ret != ESP_OK) return ret;
        ret = esp_lcd_panel_init(panel_handle);
        if (ret != ESP_OK) return ret;

        /* Get the address of the framebuffer */
        uint16_t *fb0;
        ret = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 1, (void **)&fb0);
        if (ret != ESP_OK) return ret;
        framebuffer = fb0;

        /* Clear to Black */
        memset(framebuffer, 0, LCD_WIDTH_RES * LCD_HEIGHT_RES * sizeof(uint16_t));

        /* Turn on backlight */
        gpio_set_level(PM_LCD_BL_GPIO, 1);

        ESP_LOGI(TAG, "RGB panel initialized successfully");
        return ESP_OK;
}

void lcd_ws28b_set_backlight(uint8_t level)
{
        gpio_set_level(PM_LCD_BL_GPIO, level ? 1 : 0);
}

/* Blit Macintosh mono to 480x640 RGB565 with 90-degree rotation.
 * For non-native source sizes (e.g. 512x342), keep 1:1 pixels and center.
 */
esp_err_t lcd_ws28b_blit_mono(const uint8_t *src, int src_w, int src_h)
{
        if (!framebuffer || !src || src_w <= 0 || src_h <= 0) return ESP_ERR_INVALID_ARG;

        size_t src_stride = (size_t)(src_w + 7) / 8;
        size_t src_bytes = src_stride * (size_t)src_h;
        if (src_bytes > mono_snapshot_size) {
                uint8_t *new_buf = heap_caps_realloc(mono_snapshot, src_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!new_buf) {
                        return ESP_ERR_NO_MEM;
                }
                mono_snapshot = new_buf;
                mono_snapshot_size = src_bytes;
        }
        memcpy(mono_snapshot, src, src_bytes);

        int rot_w = src_h;
        int rot_h = src_w;
        int off_x = (LCD_WIDTH_RES - rot_w) / 2;
        int off_y = (LCD_HEIGHT_RES - rot_h) / 2;

        for (int ly = 0; ly < src_h; ly++) {
                const uint8_t *row = mono_snapshot + ((size_t)ly * src_stride);
                int dx = off_x + (src_h - 1 - ly);
                if ((unsigned)dx >= LCD_WIDTH_RES) continue;

                for (int lx = 0; lx < src_w; lx++) {
                        uint8_t b = row[lx >> 3];

                        bool on = (b & (0x80 >> (lx & 7))) != 0;
                        uint16_t color = on ? 0x0000 : 0xFFFF;

                        int dy = off_y + lx;
                        if ((unsigned)dy < LCD_HEIGHT_RES) {
                                framebuffer[dy * LCD_WIDTH_RES + dx] = color;
                        }
                }
        }

        return ESP_OK;
}
