#include "sdcard_disc.h"

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
#include "pinmap.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";
static const char *kMountPoint = "/sdcard";

static bool s_sd_mounted = false;
static bool s_mount_tried = false;
static sdmmc_card_t *s_card = NULL;
static FILE *s_disc_fp = NULL;

static int disc_file_read(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FILE *fp = (FILE *)ctx;
        if (!fp || !data) {
                return -1;
        }
        if (fseek(fp, (long)offset, SEEK_SET) != 0) {
                return -1;
        }
        return (fread(data, 1, len, fp) == len) ? 0 : -1;
}

static bool has_image_ext(const char *name)
{
        const char *dot = strrchr(name, '.');
        if (!dot) {
                return false;
        }
        return strcasecmp(dot, ".img") == 0 ||
               strcasecmp(dot, ".dsk") == 0 ||
               strcasecmp(dot, ".dc42") == 0;
}

static bool open_file_disc(const char *path, disc_descr_t *out_disc)
{
        struct stat st = {0};
        if (!out_disc || stat(path, &st) != 0 || st.st_size <= 0) {
                return false;
        }

        FILE *fp = fopen(path, "rb");
        if (!fp) {
                return false;
        }

        size_t size = (size_t)st.st_size;
        uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
                buf = (uint8_t *)malloc(size);
        }
        if (!buf) {
                if (s_disc_fp) {
                        fclose(s_disc_fp);
                        s_disc_fp = NULL;
                }
                s_disc_fp = fopen(path, "rb");
                fclose(fp);
                if (!s_disc_fp) {
                        ESP_LOGE(TAG, "alloc failed and file reopen failed for %s", path);
                        return false;
                }

                out_disc->base = NULL;
                out_disc->size = (unsigned int)size;
                out_disc->read_only = 1;
                out_disc->op_ctx = s_disc_fp;
                out_disc->op_read = disc_file_read;
                out_disc->op_write = NULL;
                ESP_LOGW(TAG, "low heap: using file-backed image %s (%u bytes)", path, out_disc->size);
                return true;
        }

        size_t got = fread(buf, 1, size, fp);
        fclose(fp);
        if (got != size) {
                ESP_LOGE(TAG, "short read for %s (%u/%u)", path, (unsigned)got, (unsigned)size);
                free(buf);
                return false;
        }

        out_disc->base = buf;
        out_disc->size = (unsigned int)size;
        out_disc->read_only = 0;
        out_disc->op_ctx = NULL;
        out_disc->op_read = NULL;
        out_disc->op_write = NULL;

        ESP_LOGI(TAG, "loaded image %s into RAM (%u bytes)", path, out_disc->size);
        return true;
}

static bool try_known_names(disc_descr_t *out_disc)
{
        static const char *kCandidates[] = {
                "/sdcard/umac0.img",
                "/sdcard/UMAC0.IMG",
                "/sdcard/disc.img",
                "/sdcard/disc.dsk",
                "/sdcard/system.img",
                "/sdcard/system.dsk",
                "/sdcard/System Disk.img",
        };
        for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++) {
                if (open_file_disc(kCandidates[i], out_disc)) {
                        return true;
                }
        }
        return false;
}

static bool try_scan_root(disc_descr_t *out_disc)
{
        DIR *dir = opendir(kMountPoint);
        if (!dir) {
                return false;
        }

        bool ok = false;
        struct dirent *de = NULL;
        while ((de = readdir(dir)) != NULL) {
                if (de->d_type == DT_DIR || !has_image_ext(de->d_name)) {
                        continue;
                }
                char path[256];
                size_t name_len = strnlen(de->d_name, 200);
                snprintf(path, sizeof(path), "%s/%.*s", kMountPoint, (int)name_len, de->d_name);
                if (open_file_disc(path, out_disc)) {
                        ok = true;
                        break;
                }
        }
        closedir(dir);
        return ok;
}

static void sdcard_prepare_shared_spi_lines(void)
{
        gpio_config_t out = {
                /* Do not reconfigure LCD CS here; LCD SPI device already owns it. */
                .pin_bit_mask = (1ULL << PM_TOUCH_CS_GPIO) |
                                (1ULL << PM_SD_CS_GPIO),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&out) == ESP_OK) {
                gpio_set_level(PM_TOUCH_CS_GPIO, 1);
                gpio_set_level(PM_SD_CS_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(20));
        }
}

static bool sdcard_mount_if_needed(void)
{
        if (s_sd_mounted) {
                return true;
        }
        if (s_mount_tried) {
                return false;
        }
        s_mount_tried = true;

        sdcard_prepare_shared_spi_lines();

        static const int cs_candidates[] = {
                PM_SD_CS_GPIO,
                15,
        };
        for (size_t i = 0; i < sizeof(cs_candidates) / sizeof(cs_candidates[0]); i++) {
                int cs = cs_candidates[i];
                sdmmc_host_t host = SDSPI_HOST_DEFAULT();
                host.slot = SPI2_HOST;
                host.max_freq_khz = 1000;

                sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
                slot_config.gpio_cs = cs;
                slot_config.host_id = SPI2_HOST;

                esp_vfs_fat_sdmmc_mount_config_t mount_config = {
                        .format_if_mount_failed = false,
                        .max_files = 4,
                        .allocation_unit_size = 16 * 1024,
                        .disk_status_check_enable = false,
                        .use_one_fat = false,
                };

                esp_err_t err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &s_card);
                if (err == ESP_OK) {
                        s_sd_mounted = true;
                        ESP_LOGI(TAG, "mounted at %s (CS=%d)", kMountPoint, cs);
                        return true;
                }
                ESP_LOGI(TAG, "mount attempt failed (CS=%d): %s", cs, esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(120));
        }
        ESP_LOGW(TAG, "SD mount failed on all CS candidates; continuing with built-in image");
        return false;
}

bool sdcard_disc_try_load(disc_descr_t *out_disc)
{
        if (!out_disc || !sdcard_mount_if_needed()) {
                return false;
        }
        if (try_known_names(out_disc)) {
                return true;
        }
        if (try_scan_root(out_disc)) {
                return true;
        }

        ESP_LOGI(TAG, "no .img/.dsk/.dc42 on SD root; using built-in image");
        return false;
}
