#include <stdio.h>
#include "esp_log.h"

#include "spi_bus.h"
#include "sd_storage.h"
#include "i2s_audio.h"
#include "tft_display.h"
#include "volume_button.h"
#include "player.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    tft_display_init();   // khong chan chuong trinh neu that bai, nhung can bus da san sang cho SD

    if (sd_storage_init() != ESP_OK) return;
    if (i2s_audio_init() != ESP_OK) return;

    i2s_audio_set_volume(1.0f);
    if (volume_button_init() != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao nut am luong that bai");
    }

    // player_init() se tu quet thu muc nhac, nap danh sach len man hinh Library
    // va tao task rieng xu ly phat nhac theo thao tac cham tren man hinh.
    if (player_init() != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao player that bai");
    }
}