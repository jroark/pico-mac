#include "lcd_bringup.h"

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pinmap.h"

#define LCD_HOST SPI2_HOST
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LCD_BL_ON_LEVEL 1
#define LCD_BL_OFF_LEVEL 0

static const char *TAG = "lcd";
static spi_device_handle_t lcd_spi;
static bool spi_ready = false;

static esp_err_t lcd_send(bool data_mode, const void *buf, size_t len)
{
        gpio_set_level(PM_LCD_DC_GPIO, data_mode ? 1 : 0);
        spi_transaction_t t = {0};
        t.length = len * 8;
        t.tx_buffer = buf;
        return spi_device_transmit(lcd_spi, &t);
}

static esp_err_t lcd_cmd(uint8_t cmd)
{
        return lcd_send(false, &cmd, 1);
}

static esp_err_t lcd_data(const void *data, size_t len)
{
        return lcd_send(true, data, len);
}

static esp_err_t lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        uint8_t col[4] = { x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff };
        uint8_t row[4] = { y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff };

        esp_err_t err = lcd_cmd(0x2A); /* CASET */
        if (err != ESP_OK) return err;
        err = lcd_data(col, sizeof(col));
        if (err != ESP_OK) return err;
        err = lcd_cmd(0x2B); /* PASET */
        if (err != ESP_OK) return err;
        err = lcd_data(row, sizeof(row));
        if (err != ESP_OK) return err;
        return lcd_cmd(0x2C); /* RAMWR */
}

static esp_err_t lcd_fill_rgb565(uint16_t color)
{
        esp_err_t err = lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
        if (err != ESP_OK) return err;

        uint16_t line[64];
        for (size_t i = 0; i < 64; i++) {
                line[i] = (uint16_t)((color << 8) | (color >> 8));
        }

        const int total_pixels = LCD_WIDTH * LCD_HEIGHT;
        int sent = 0;
        while (sent < total_pixels) {
            int chunk = total_pixels - sent;
            if (chunk > (int)(sizeof(line) / sizeof(line[0]))) {
                chunk = (int)(sizeof(line) / sizeof(line[0]));
            }
            err = lcd_data(line, chunk * sizeof(uint16_t));
            if (err != ESP_OK) return err;
            sent += chunk;
        }
        return ESP_OK;
}

static esp_err_t lcd_blit_mono_internal(const uint8_t *fb, int fb_width, int fb_height)
{
        if (!fb || fb_width <= 0 || fb_height <= 0) {
                return ESP_ERR_INVALID_ARG;
        }

        esp_err_t err = lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
        if (err != ESP_OK) {
                return err;
        }

        uint16_t line[LCD_WIDTH];
        for (int y = 0; y < LCD_HEIGHT; y++) {
                int src_y = (y * fb_height) / LCD_HEIGHT;
                const uint8_t *row = fb + (src_y * ((fb_width + 7) / 8));
                for (int x = 0; x < LCD_WIDTH; x++) {
                        int src_x = (x * fb_width) / LCD_WIDTH;
                        uint8_t b = row[src_x >> 3];
                        int bit = 7 - (src_x & 7);
                        bool on = ((b >> bit) & 1) != 0;
                        /* Mac framebuffer bit=1 should render black, bit=0 white. */
                        uint16_t px = on ? 0x0000 : 0xffff;
                        line[x] = (uint16_t)((px << 8) | (px >> 8));
                }
                err = lcd_data(line, sizeof(line));
                if (err != ESP_OK) {
                        return err;
                }
        }

        return ESP_OK;
}

static esp_err_t lcd_init_gpio(void)
{
        gpio_config_t out = {
                .pin_bit_mask = (1ULL << PM_LCD_CS_GPIO) |
                                (1ULL << PM_LCD_DC_GPIO) |
                                (1ULL << PM_LCD_RST_GPIO) |
                                (1ULL << PM_LCD_BL_GPIO),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "gpio_config");

        gpio_set_level(PM_LCD_CS_GPIO, 1);
        gpio_set_level(PM_LCD_DC_GPIO, 0);
        gpio_set_level(PM_LCD_BL_GPIO, LCD_BL_OFF_LEVEL);

        gpio_set_level(PM_LCD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(PM_LCD_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(PM_LCD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
        return ESP_OK;
}

static esp_err_t lcd_init_spi(void)
{
        if (!spi_ready) {
                spi_bus_config_t buscfg = {
                        .mosi_io_num = PM_SPI_MOSI_GPIO,
                        .miso_io_num = PM_SPI_MISO_GPIO,
                        .sclk_io_num = PM_SPI_SCK_GPIO,
                        .quadwp_io_num = -1,
                        .quadhd_io_num = -1,
                        .max_transfer_sz = 4096,
                };
                ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO),
                                    TAG, "spi_bus_initialize");

                spi_device_interface_config_t devcfg = {
                        .clock_speed_hz = 26 * 1000 * 1000,
                        .mode = 0,
                        .spics_io_num = PM_LCD_CS_GPIO,
                        .queue_size = 1,
                };
                ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_HOST, &devcfg, &lcd_spi),
                                    TAG, "spi_bus_add_device");
                spi_ready = true;
        }
        return ESP_OK;
}

static esp_err_t lcd_init_panel(void)
{
        uint8_t data;

        ESP_RETURN_ON_ERROR(lcd_cmd(0x01), TAG, "SWRESET");
        vTaskDelay(pdMS_TO_TICKS(120));

        ESP_RETURN_ON_ERROR(lcd_cmd(0x11), TAG, "SLPOUT");
        vTaskDelay(pdMS_TO_TICKS(120));

        data = 0x55; /* 16-bit/pixel */
        ESP_RETURN_ON_ERROR(lcd_cmd(0x3A), TAG, "COLMOD");
        ESP_RETURN_ON_ERROR(lcd_data(&data, 1), TAG, "COLMOD data");

        /* ILI9341 MADCTL: MX + MV + BGR for landscape without mirrored text */
        data = 0x68;
        ESP_RETURN_ON_ERROR(lcd_cmd(0x36), TAG, "MADCTL");
        ESP_RETURN_ON_ERROR(lcd_data(&data, 1), TAG, "MADCTL data");

        ESP_RETURN_ON_ERROR(lcd_cmd(0x21), TAG, "INVON");
        ESP_RETURN_ON_ERROR(lcd_cmd(0x13), TAG, "NORON");
        ESP_RETURN_ON_ERROR(lcd_cmd(0x29), TAG, "DISPON");
        vTaskDelay(pdMS_TO_TICKS(20));

        return ESP_OK;
}

bool lcd_bringup_test(void)
{
        if (lcd_init_gpio() != ESP_OK) {
                return false;
        }
        if (lcd_init_spi() != ESP_OK) {
                return false;
        }
        if (lcd_init_panel() != ESP_OK) {
                return false;
        }
        if (lcd_fill_rgb565(0x07E0) != ESP_OK) { /* solid green */
                return false;
        }

        gpio_set_level(PM_LCD_BL_GPIO, LCD_BL_ON_LEVEL);
        ESP_LOGI(TAG, "LCD bring-up complete (green test fill)");
        return true;
}

bool lcd_panel_init(void)
{
        if (lcd_init_gpio() != ESP_OK) {
                return false;
        }
        if (lcd_init_spi() != ESP_OK) {
                return false;
        }
        if (lcd_init_panel() != ESP_OK) {
                return false;
        }
        gpio_set_level(PM_LCD_BL_GPIO, LCD_BL_ON_LEVEL);
        return true;
}

bool lcd_blit_mono(const uint8_t *fb, int fb_width, int fb_height)
{
        return lcd_blit_mono_internal(fb, fb_width, fb_height) == ESP_OK;
}
