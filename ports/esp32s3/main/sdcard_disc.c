#include "sdcard_disc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
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
static bool s_disc_read_only = true;

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

static int disc_file_write(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FILE *fp = (FILE *)ctx;
        if (!fp || !data) {
                return -1;
        }
        if (fseek(fp, (long)offset, SEEK_SET) != 0) {
                return -1;
        }
        if (fwrite(data, 1, len, fp) != len) {
                return -1;
        }
        fflush(fp);
        int fd = fileno(fp);
        if (fd >= 0) {
                fsync(fd);
        }
        return 0;
}

static bool open_file_disc(const char *path, disc_descr_t *out_disc)
{
        struct stat st = {0};
        if (!out_disc || stat(path, &st) != 0 || st.st_size <= 0) {
                return false;
        }

        if (s_disc_fp) {
                fclose(s_disc_fp);
                s_disc_fp = NULL;
        }

        s_disc_fp = fopen(path, "r+b");
        s_disc_read_only = false;
        if (!s_disc_fp) {
                s_disc_fp = fopen(path, "rb");
                s_disc_read_only = true;
        }
        if (!s_disc_fp) {
                ESP_LOGW(TAG, "open failed: %s (errno=%d)", path, errno);
                return false;
        }

        out_disc->base = NULL;
        out_disc->size = (unsigned int)st.st_size;
        out_disc->read_only = s_disc_read_only ? 1 : 0;
        out_disc->op_ctx = s_disc_fp;
        out_disc->op_read = disc_file_read;
        out_disc->op_write = s_disc_read_only ? NULL : disc_file_write;

        ESP_LOGI(TAG, "using image %s (%u bytes, ro=%d)", path, out_disc->size, out_disc->read_only);
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
                .pin_bit_mask = (1ULL << PM_LCD_CS_GPIO) |
                                (1ULL << PM_TOUCH_CS_GPIO) |
                                (1ULL << PM_SD_CS_GPIO),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&out) == ESP_OK) {
                gpio_set_level(PM_LCD_CS_GPIO, 1);
                gpio_set_level(PM_TOUCH_CS_GPIO, 1);
                gpio_set_level(PM_SD_CS_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(20));
        }
}

static bool sdcard_ensure_spi_bus_ready(void)
{
        spi_bus_config_t buscfg = {
                .mosi_io_num = PM_SPI_MOSI_GPIO,
                .miso_io_num = PM_SPI_MISO_GPIO,
                .sclk_io_num = PM_SPI_SCK_GPIO,
                .quadwp_io_num = -1,
                .quadhd_io_num = -1,
                .max_transfer_sz = 4096,
        };
        esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
        return (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
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
        if (!sdcard_ensure_spi_bus_ready()) {
                ESP_LOGE(TAG, "SPI2 init failed");
                return false;
        }

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
                ESP_LOGW(TAG, "mount failed (CS=%d): %s", cs, esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(120));
        }
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

        ESP_LOGW(TAG, "no .img/.dsk/.dc42 on SD root");
        return false;
}
