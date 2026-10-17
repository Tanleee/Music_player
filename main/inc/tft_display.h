#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao man hinh LGFX. Goi 1 lan trong app_main, SAU spi_bus_init()/sd_storage_init()
// (man hinh dung chung bus SPI2 voi SD card).
esp_err_t tft_display_init(void);

// Hien thi ten file dang phat len man hinh.
void tft_display_show_track(const char *filename);

#ifdef __cplusplus
}
#endif