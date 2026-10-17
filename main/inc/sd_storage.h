#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_MUSIC_DIR   SD_MOUNT_POINT "/music"

esp_err_t sd_storage_init(void);     // mount the SD (can goi spi_bus_init truoc)
void      sd_storage_deinit(void);   // unmount
bool      sd_storage_is_mounted(void);
void      sd_storage_list_dir(const char *path);