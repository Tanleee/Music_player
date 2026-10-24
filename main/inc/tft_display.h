#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tft_track_click_cb_t)(int index);
typedef void (*tft_control_cb_t)(void);

// Khoi tao man hinh LGFX + LVGL. Goi 1 lan trong app_main, SAU spi_bus_init()/sd_storage_init()
// (man hinh dung chung bus SPI2 voi SD card).
esp_err_t tft_display_init(void);

// Nap danh sach ten bai hat cho man hinh Library (goi lai moi khi danh sach thay doi).
// names[] chi can song trong luc goi ham nay, noi dung se duoc copy vao cac label cua list.
void tft_display_set_track_list(const char * const *names, int count);

// Dang ky cac callback: UI goi xuong lop logic phat nhac khi nguoi dung thao tac.
// Chi can goi 1 lan, ngay sau tft_display_init().
void tft_display_set_callbacks(tft_track_click_cb_t on_track_click,
                                tft_control_cb_t on_shuffle,
                                tft_control_cb_t on_play_pause,
                                tft_control_cb_t on_next,
                                tft_control_cb_t on_prev);

// Cap nhat ten bai dang phat (hien o thanh mini-player day man hinh Library
// va o tieu de man hinh Player). Goi ham nay se tu dong hien thanh mini-player.
void tft_display_show_track(const char *filename);

// true = dang phat (hien icon pause), false = dang tam dung (hien icon play)
void tft_display_set_playing(bool is_playing);

// Cap nhat thanh tien trinh + nhan thoi gian o man hinh Player (don vi: giay)
void tft_display_set_progress(uint32_t pos_sec, uint32_t dur_sec);

// Chuyen qua lai giua 2 man hinh
void tft_display_show_player_screen(void);
void tft_display_show_library_screen(void);

#ifdef __cplusplus
}
#endif