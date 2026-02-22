#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pinmap.h"
#include "lcd_bringup.h"
#include "umac.h"
#include "rom.h"

static const char *TAG = "pico-mac";

static const uint8_t umac_disc[] = {
#include "../../../incbin/umac-disc.h"
};

static const uint8_t umac_rom[] = {
#include "../../../incbin/umac-rom.h"
};

static uint8_t umac_ram[RAM_SIZE];

void app_main(void)
{
        disc_descr_t discs[DISC_NUM_DRIVES] = {0};
        int64_t last_vsync_us = 0;
        int64_t last_blit_us = 0;

        ESP_LOGI(TAG, "ESP32-S3 port scaffold started");
        ESP_LOGI(TAG, "Pin map: SCK=%d MOSI=%d MISO=%d", PM_SPI_SCK_GPIO, PM_SPI_MOSI_GPIO, PM_SPI_MISO_GPIO);
        ESP_LOGI(TAG, "Pin map: LCD CS=%d DC=%d RST=%d BL=%d", PM_LCD_CS_GPIO, PM_LCD_DC_GPIO, PM_LCD_RST_GPIO, PM_LCD_BL_GPIO);
        if (!lcd_panel_init()) {
                ESP_LOGE(TAG, "LCD bring-up failed");
                while (true) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                }
        }

        discs[0].base = (uint8_t *)umac_disc;
        discs[0].size = sizeof(umac_disc);
        discs[0].read_only = 1;

        if (umac_init(umac_ram, (void *)umac_rom, discs) != 0) {
                ESP_LOGE(TAG, "umac_init failed");
                while (true) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                }
        }
        ESP_LOGI(TAG, "umac started, framebuffer %dx%d", DISP_WIDTH, DISP_HEIGHT);

        while (true) {
                int64_t now = esp_timer_get_time();
                umac_loop();

                if ((now - last_vsync_us) >= 16667) {
                        umac_vsync_event();
                        last_vsync_us = now;
                }
                if ((now - last_blit_us) >= 33333) {
                        const uint8_t *fb = umac_ram + umac_get_fb_offset();
                        (void)lcd_blit_mono(fb, DISP_WIDTH, DISP_HEIGHT);
                        last_blit_us = now;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
        }
}
