#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pinmap_ws28b.h"
#include "lcd_ws28b.h"
#include "sdcard_disc.h"
#include "touch_input.h"
#include "umac.h"
#include "rom.h"

static const char *TAG = "pico-mac";

static const uint8_t umac_disc[] = {
#include "../../../incbin/umac-disc.h"
};

static const uint8_t umac_rom_flash[] = {
#include "../../../incbin/umac-rom.h"
};

static uint8_t umac_ram[RAM_SIZE] EXT_RAM_BSS_ATTR;
static uint8_t umac_rom[sizeof(umac_rom_flash)] EXT_RAM_BSS_ATTR;
static volatile uint32_t g_frame_seq = 0;

static void emu_task(void *arg)
{
        (void)arg;
        int64_t last_vsync_us = 0;
        uint32_t spin_count = 0;

        while (true) {
                int64_t now = esp_timer_get_time();
                umac_loop();

                if ((now - last_vsync_us) >= 16667) {
                        umac_vsync_event();
                        last_vsync_us = now;
                        g_frame_seq++;
                }

                if ((++spin_count & 0x3F) == 0) {
                        vTaskDelay(1);
                }
        }
}

static void blit_task(void *arg)
{
        (void)arg;
        uint32_t last_seq = 0;
        int64_t last_blit_us = 0;

        while (true) {
                uint32_t seq = g_frame_seq;
                int64_t now = esp_timer_get_time();
                if (seq != last_seq && (now - last_blit_us) >= 33333) {
                        const uint8_t *fb = umac_ram + umac_get_fb_offset();
                        (void)lcd_ws28b_blit_mono(fb, DISP_WIDTH, DISP_HEIGHT);
                        last_seq = seq;
                        last_blit_us = now;
                        vTaskDelay(1);
                } else {
                        vTaskDelay(pdMS_TO_TICKS(1));
                }
        }
}

static void touch_task(void *arg)
{
        (void)arg;
        while (true) {
                int dx = 0, dy = 0, button = 0;
                if (touch_input_poll(&dx, &dy, &button)) {
                        /* touch_input_poll uses screen-space deltas (+Y is down). */
                        umac_mouse(dx, -dy, button);
                }
                vTaskDelay(pdMS_TO_TICKS(4));
        }
}

void app_main(void)
{
        disc_descr_t discs[DISC_NUM_DRIVES] = {0};

        ESP_LOGI(TAG, "Waveshare ESP32-S3-Touch-LCD-2.8B port starting");
#if (DISP_WIDTH == 512) && (DISP_HEIGHT == 342)
        ESP_LOGW(TAG, "display mode is native 512x342; ROM resolution patch is disabled");
#else
        ESP_LOGI(TAG, "display mode is %dx%d; ROM resolution patch is enabled", DISP_WIDTH, DISP_HEIGHT);
#endif

        /* Copy ROM to PSRAM to allow patching */
        memcpy(umac_rom, umac_rom_flash, sizeof(umac_rom_flash));
        if (rom_patch(umac_rom) != 0) {
                ESP_LOGW(TAG, "rom_patch failed or not needed");
        }

        if (lcd_ws28b_init() != ESP_OK) {
                ESP_LOGE(TAG, "LCD init failed");
                while (true) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                }
        }

        discs[0].base = (uint8_t *)umac_disc;
        discs[0].size = sizeof(umac_disc);
        discs[0].read_only = 1;
        if (sdcard_disc_try_load(&discs[0])) {
                ESP_LOGI(TAG, "using disk image from SD card");
        } else {
                ESP_LOGI(TAG, "using built-in disk image");
        }

        if (umac_init(umac_ram, (void *)umac_rom, discs) != 0) {
                ESP_LOGE(TAG, "umac_init failed");
                while (true) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                }
        }
        ESP_LOGI(TAG, "umac started, source framebuffer %dx%d", DISP_WIDTH, DISP_HEIGHT);

        if (touch_input_init()) {
                xTaskCreate(touch_task, "touch", 4096, NULL, 4, NULL);
        } else {
                ESP_LOGW(TAG, "touch init failed; continuing without touch");
        }

        xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 8, NULL, 1);
        xTaskCreatePinnedToCore(blit_task, "blit", 8192, NULL, 6, NULL, 0);
        vTaskDelete(NULL);
}
