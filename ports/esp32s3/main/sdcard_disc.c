#include "sdcard_disc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "pinmap.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";
static const char *kMountPoint = "/sdcard";

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

static bool load_file_to_memory(const char *path, disc_descr_t *out_disc)
{
        struct stat st = {0};
        if (stat(path, &st) != 0 || st.st_size <= 0) {
                return false;
        }

        size_t size = (size_t)st.st_size;
        uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
                buf = (uint8_t *)malloc(size);
        }
        if (!buf) {
                ESP_LOGE(TAG, "failed to allocate %u bytes for disk image", (unsigned)size);
                return false;
        }

        FILE *fp = fopen(path, "rb");
        if (!fp) {
                ESP_LOGE(TAG, "failed to open %s", path);
                free(buf);
                return false;
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
        ESP_LOGI(TAG, "loaded disk image %s (%u bytes)", path, (unsigned)size);
        return true;
}

static bool try_known_names(disc_descr_t *out_disc)
{
        static const char *kCandidates[] = {
                "/sdcard/disc.img",
                "/sdcard/disc.dsk",
                "/sdcard/system.img",
                "/sdcard/system.dsk",
                "/sdcard/System Disk.img",
        };
        for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++) {
                if (load_file_to_memory(kCandidates[i], out_disc)) {
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
                if (de->d_type == DT_DIR) {
                        continue;
                }
                if (!has_image_ext(de->d_name)) {
                        continue;
                }
                char path[256];
                size_t name_len = strnlen(de->d_name, 200);
                snprintf(path, sizeof(path), "%s/%.*s", kMountPoint, (int)name_len, de->d_name);
                if (load_file_to_memory(path, out_disc)) {
                        ok = true;
                        break;
                }
        }

        closedir(dir);
        return ok;
}

bool sdcard_disc_try_load(disc_descr_t *out_disc)
{
        if (!out_disc) {
                return false;
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI2_HOST;
        host.max_freq_khz = 10000;

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = PM_SD_CS_GPIO;
        slot_config.host_id = SPI2_HOST;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
                .format_if_mount_failed = false,
                .max_files = 4,
                .allocation_unit_size = 16 * 1024,
                .disk_status_check_enable = false,
                .use_one_fat = false,
        };

        sdmmc_card_t *card = NULL;
        esp_err_t err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
        if (err != ESP_OK) {
                ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
                return false;
        }
        ESP_LOGI(TAG, "SD mounted at %s", kMountPoint);

        if (try_known_names(out_disc)) {
                return true;
        }
        if (try_scan_root(out_disc)) {
                return true;
        }

        ESP_LOGW(TAG, "no .img/.dsk/.dc42 found on SD root; using built-in image");
        return false;
}
