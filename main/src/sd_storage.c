#include "sd_storage.h"
#include "board_config.h"
#include <dirent.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"

static const char *TAG = "SD";
static sdmmc_card_t *s_card = NULL;

esp_err_t sd_storage_init(void)
{
    if (s_card) return ESP_OK;   // da mount roi

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,   // khong tu dong format
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = APP_SPI_HOST;
    host.max_freq_khz = 20000;   // test on dinh roi tang len 20000

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_SD_CS;
    slot_cfg.host_id = APP_SPI_HOST;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg,
                                            &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount that bai: %s", esp_err_to_name(ret));
        s_card = NULL;
        return ret;
    }
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

void sd_storage_deinit(void)
{
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
}

bool sd_storage_is_mounted(void)
{
    return s_card != NULL;
}

void sd_storage_list_dir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Khong mo duoc thu muc %s", path);
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "  %s %s", (e->d_type == DT_DIR) ? "[DIR] " : "[FILE]", e->d_name);
    }
    closedir(dir);
}