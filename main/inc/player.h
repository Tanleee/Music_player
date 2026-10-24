#pragma once
#include "esp_err.h"

// Khoi tao module phat nhac:
//  - dang ky callback voi tft_display (nut Shuffle/Play-Pause/Next/Prev, bam vao 1 bai trong danh sach)
//  - tao 1 task rieng (player_task) de quet thu muc nhac tren SD, nap danh sach
//    len man hinh Library, va xu ly phat/dung/next/prev/shuffle theo lenh tu man hinh cham.
// Goi 1 lan trong app_main, SAU sd_storage_init(), i2s_audio_init() va tft_display_init().
esp_err_t player_init(void);