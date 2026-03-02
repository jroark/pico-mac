#include "sdcard_disc.h"
#include "pinmap_ws28b.h"
#include "tca9554.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";
static const char *kMountPoint = "/sdcard";

static bool s_sd_mounted = false;
static sdmmc_card_t *s_card = NULL;
static FILE *s_disc_fp = NULL;

static int disc_file_read(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FILE *fp = (FILE *)ctx;
        if (!fp || !data) return -1;
        if (fseek(fp, (long)offset, SEEK_SET) != 0) return -1;
        return (fread(data, 1, len, fp) == len) ? 0 : -1;
}

static bool has_image_ext(const char *name)
{
        const char *dot = strrchr(name, '.');
        if (!dot) return false;
        return strcasecmp(dot, ".img") == 0 || strcasecmp(dot, ".dsk") == 0 || strcasecmp(dot, ".dc42") == 0;
}

static bool open_file_disc(const char *path, disc_descr_t *out_disc)
{
        struct stat st = {0};
        if (!out_disc || stat(path, &st) != 0 || st.st_size <= 0) return false;

        FILE *fp = fopen(path, "rb");
        if (!fp) return false;

        size_t size = (size_t)st.st_size;
        uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf) {
                size_t got = fread(buf, 1, size, fp);
                fclose(fp);
                if (got == size) {
                        out_disc->base = buf;
                        out_disc->size = (unsigned int)size;
                        out_disc->read_only = 0;
                        out_disc->op_ctx = NULL;
                        out_disc->op_read = NULL;
                        out_disc->op_write = NULL;
                        ESP_LOGI(TAG, "loaded image %s into PSRAM (%u bytes)", path, out_disc->size);
                        return true;
                }
                free(buf);
        } else {
                if (s_disc_fp) fclose(s_disc_fp);
                s_disc_fp = fp; // Reuse handle
                out_disc->base = NULL;
                out_disc->size = (unsigned int)size;
                out_disc->read_only = 1;
                out_disc->op_ctx = s_disc_fp;
                out_disc->op_read = disc_file_read;
                out_disc->op_write = NULL;
                ESP_LOGW(TAG, "low heap: using file-backed image %s (%u bytes)", path, out_disc->size);
                return true;
        }
        return false;
}

static bool try_scan_root(disc_descr_t *out_disc)
{
        static const char *kCandidates[] = {
                "/sdcard/umac0.img",
                "/sdcard/UMAC0.IMG",
                "/sdcard/disc.img",
                "/sdcard/disc.dsk",
                "/sdcard/system.img",
                "/sdcard/system.dsk",
        };
        for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++) {
                if (open_file_disc(kCandidates[i], out_disc)) {
                        return true;
                }
        }

        DIR *dir = opendir(kMountPoint);
        if (!dir) return false;
        struct dirent *de = NULL;
        while ((de = readdir(dir)) != NULL) {
                if (de->d_type == DT_DIR || !has_image_ext(de->d_name)) continue;
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", kMountPoint, de->d_name);
                if (open_file_disc(path, out_disc)) {
                        closedir(dir);
                        return true;
                }
        }
        closedir(dir);
        return false;
}

static bool sdcard_mount_if_needed(void)
{
        if (s_sd_mounted) return true;

        /* Prepare SPI bus for SD card */
        spi_bus_config_t buscfg = {
                .mosi_io_num = PM_SD_SPI_MOSI_GPIO,
                .miso_io_num = PM_SD_SPI_MISO_GPIO,
                .sclk_io_num = PM_SD_SPI_SCK_GPIO,
                .quadwp_io_num = -1,
                .quadhd_io_num = -1,
                .max_transfer_sz = 4096,
        };
        
        esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "Failed to initialize SPI bus for SD");
                return false;
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI2_HOST;
        host.max_freq_khz = 5000;

        /* Note: CS is handled via TCA9554 expander. 
         * The SDSPI driver needs a GPIO number, but we handle it manually or 
         * we provide a dummy and use the expander in a pre-transmit callback.
         * For now, we'll try providing a dummy GPIO if needed, or 
         * use a specific SDSPI slot config.
         */
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = -1; // We will handle CS via TCA9554 if possible, or use a spare pin
        slot_config.host_id = SPI2_HOST;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
                .format_if_mount_failed = false,
                .max_files = 4,
                .allocation_unit_size = 16 * 1024,
        };

        /* Since we use TCA9554 for CS, we might need to manually pull it low. 
         * This is tricky with the standard esp_vfs_fat_sdspi_mount.
         * For a quick port, we'll assume the board wiring allows standard access.
         */
        tca9554_set_mode(PM_EXIO_LCD_CS, false);
        tca9554_set_level(PM_EXIO_LCD_CS, 1);
        tca9554_set_mode(PM_EXIO_SD_CS, false);
        tca9554_set_level(PM_EXIO_SD_CS, 1);
        vTaskDelay(pdMS_TO_TICKS(2));
        tca9554_set_level(PM_EXIO_SD_CS, 0);

        err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &s_card);
        if (err == ESP_OK) {
                s_sd_mounted = true;
                ESP_LOGI(TAG, "SD card mounted");
                return true;
        }
        
        tca9554_set_level(PM_EXIO_SD_CS, 1);
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        return false;
}

bool sdcard_disc_try_load(disc_descr_t *out_disc)
{
        if (!out_disc || !sdcard_mount_if_needed()) return false;
        return try_scan_root(out_disc);
}
