#include "tft_display.h"
#include "LGFX_Config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

// LUU Y VE PHIEN BAN LVGL:
// Code nay viet theo API LVGL v9 (lv_button_create, lv_list_add_button, ...)
// vi idf_component.yml dang khai bao "lvgl/lvgl: ^9".

static const char *TAG = "TFT";
static LGFX s_lcd;
static bool s_inited = false;

static lv_display_t *s_disp = nullptr;

#define LV_BUF_LINES   30   // so dong man hinh moi lan ve - giu nho de tiet kiem RAM tren C3
static uint16_t *s_lv_buf1 = nullptr;
static uint16_t *s_lv_buf2 = nullptr;

// --- font chu dung trong UI - giam con 1 nua, khop co chu o man hinh Player.
#if LV_FONT_MONTSERRAT_10
  #define UI_FONT_SMALL   &lv_font_montserrat_10
#else
  #define UI_FONT_SMALL   LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_14
  #define UI_FONT_NORMAL  &lv_font_montserrat_14
#else
  #define UI_FONT_NORMAL  LV_FONT_DEFAULT
#endif

// --- 2 man hinh: danh sach bai hat (Library) va dang phat (Player) ---
static lv_obj_t *s_scr_library = nullptr;
static lv_obj_t *s_scr_player  = nullptr;

// --- cac control o man hinh Library ---
static lv_obj_t *s_list              = nullptr;
static lv_obj_t *s_track_count_label = nullptr;
static lv_obj_t *s_miniplay_bar      = nullptr;
static lv_obj_t *s_miniplay_label    = nullptr;
static lv_obj_t *s_miniplay_btn_icon = nullptr;
static lv_obj_t *s_shuffle_icon       = nullptr;
static lv_obj_t *s_shuffle_mode_label = nullptr;   // chu "Shuffle"/"Loop" canh nut tron do
static bool      s_shuffle_mode_on    = true;      // true = dang o che do Shuffle, false = Loop

// --- cac control o man hinh Player ---
static lv_obj_t *s_player_title_label  = nullptr;
static lv_obj_t *s_player_progress_bar = nullptr;
static lv_obj_t *s_player_pos_label    = nullptr;
static lv_obj_t *s_player_dur_label    = nullptr;
static lv_obj_t *s_player_play_icon    = nullptr;
static lv_obj_t *s_disc_rings          = nullptr;   // nhom vien trang quanh dia, se xoay khi phat nhac

static char s_current_track_name[64] = "";
static int32_t s_disc_angle    = 0;       // goc xoay hien tai cua dia, don vi 0.1 do (0..3599)
static bool    s_disc_spinning = false;   // true khi dang phat nhac -> dia quay

// --- callback dang ky tu lop logic phat nhac (player.c) ---
static tft_track_click_cb_t s_cb_track_click = nullptr;
static tft_control_cb_t     s_cb_shuffle     = nullptr;
static tft_control_cb_t     s_cb_play_pause  = nullptr;
static tft_control_cb_t     s_cb_next        = nullptr;
static tft_control_cb_t     s_cb_prev        = nullptr;
static tft_mode_cb_t        s_cb_mode_change = nullptr;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    s_lcd.startWrite();
    s_lcd.setAddrWindow(area->x1, area->y1, w, h);
    s_lcd.pushPixelsDMA((uint16_t *)px_map, (uint32_t)w * h);
    s_lcd.endWrite();

    lv_display_flush_ready(disp);
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int32_t x, y;
    if (s_lcd.getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        // Man hinh xoay doc (setRotation(2)) lam truc X cua cam ung bi nguoc so
        // voi truc X cua hinh anh hien thi -> dao lai cho khop.
        data->point.x = s_lcd.width() - 1 - x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void lvgl_task(void *arg)
{
    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void format_mmss(uint32_t sec, char *buf, size_t len)
{
    snprintf(buf, len, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

// Dia nhac tu xoay khi dang phat. LVGL v9 cho phep ap style transform_rotation
// len ca mot nhom widget (khong chi rieng anh) - engine ve se tu dong render
// nhom do ra 1 layer roi xoay layer, nen cac duong vien trang ben trong van
// xoay deu quanh tam cung voi dia.
static void disc_spin_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_disc_spinning || !s_disc_rings) return;
    s_disc_angle += 15;              // ~1.5 do moi lan, timer chay moi 33ms (~7s/vong)
    if (s_disc_angle >= 3600) s_disc_angle -= 3600;
    lv_obj_set_style_transform_rotation(s_disc_rings, s_disc_angle, 0);
}

// ================== SU KIEN NGUOI DUNG ==================

static void event_track_click(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_cb_track_click) s_cb_track_click(index);
    tft_display_show_player_screen();
}

static void event_shuffle_click(lv_event_t *e)
{
    (void)e;
    // Nut nay vua kich hoat phat ngau nhien, vua la cong tac doi hien thi giua
    // 2 che do Shuffle/Loop (doi ca icon lan chu moi lan bam).
    s_shuffle_mode_on = !s_shuffle_mode_on;
    if (s_shuffle_mode_on) {
        lv_label_set_text(s_shuffle_icon, LV_SYMBOL_SHUFFLE);
        lv_label_set_text(s_shuffle_mode_label, "Shuffle");
    } else {
        lv_label_set_text(s_shuffle_icon, LV_SYMBOL_LOOP);
        lv_label_set_text(s_shuffle_mode_label, "Loop");
    }

    // Luon bao cho player.c biet che do hien tai, de no biet chon bai ke tiep
    // the nao (ngau nhien hay tuan tu) khi 1 bai phat xong.
    if (s_cb_mode_change) s_cb_mode_change(s_shuffle_mode_on);

    // Chi phat ngay 1 bai ngau nhien khi dang CHUYEN SANG Shuffle. Chuyen sang
    // Loop thi KHONG doi bai - de nguyen bai dang phat tiep tuc.
    if (s_shuffle_mode_on && s_cb_shuffle) s_cb_shuffle();
    // Khong chuyen man hinh - nguoi dung van dang o Library.
}

static void event_play_pause_click(lv_event_t *e)
{
    (void)e;
    if (s_cb_play_pause) s_cb_play_pause();
}

static void event_next_click(lv_event_t *e)
{
    (void)e;
    if (s_cb_next) s_cb_next();
}

static void event_prev_click(lv_event_t *e)
{
    (void)e;
    if (s_cb_prev) s_cb_prev();
}

static void event_player_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    // Vuot xuong (tu tren xuong) o man hinh Player -> quay ve Library,
    // thay cho nut back o goc tren trai (da bo).
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        tft_display_show_library_screen();
    }
}

static void event_miniplay_bar_click(lv_event_t *e)
{
    (void)e;
    // Bam vao thanh mini-player (ngoai 2 nut play/next) -> mo man hinh Player day du.
    tft_display_show_player_screen();
}

// ================== TAO MAN HINH LIBRARY ==================

static void build_library_screen(void)
{
    s_scr_library = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_library, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_scr_library, 8, 0);
    lv_obj_set_scrollbar_mode(s_scr_library, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(s_scr_library, false);

    // Tieu de "Library" - thay cho thanh trang thai (gio/wifi/tin nhan/nguoi dung
    // khong can thiet cho may phat nhac nen bo het, chi giu tieu de).
    lv_obj_t *title = lv_label_create(s_scr_library);
    lv_label_set_text(title, "Library");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, UI_FONT_NORMAL, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // --- hang Shuffle: nut tron do (hinh tam giac play) + "Shuffle" + "n track(s)" ---
    lv_obj_t *shuffle_btn = lv_button_create(s_scr_library);
    lv_obj_set_size(shuffle_btn, 42, 42);
    lv_obj_set_style_radius(shuffle_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shuffle_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(shuffle_btn, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_add_event_cb(shuffle_btn, event_shuffle_click, LV_EVENT_CLICKED, nullptr);

    s_shuffle_icon = lv_label_create(shuffle_btn);
    lv_label_set_text(s_shuffle_icon, LV_SYMBOL_SHUFFLE);
    lv_obj_center(s_shuffle_icon);

    s_shuffle_mode_label = lv_label_create(s_scr_library);
    lv_label_set_text(s_shuffle_mode_label, "Shuffle");
    lv_obj_set_style_text_color(s_shuffle_mode_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_shuffle_mode_label, UI_FONT_SMALL, 0);
    lv_obj_align_to(s_shuffle_mode_label, shuffle_btn, LV_ALIGN_OUT_RIGHT_TOP, 10, 0);

    s_track_count_label = lv_label_create(s_scr_library);
    lv_label_set_text(s_track_count_label, "0 track(s)");
    lv_obj_set_style_text_color(s_track_count_label, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_font(s_track_count_label, UI_FONT_SMALL, 0);
    lv_obj_align_to(s_track_count_label, s_shuffle_mode_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    // --- danh sach bai hat: lv_list da bi deprecated tu LVGL 9.6, nen dung 1
    // container flex-column cuon duoc, tu tao tung hang (icon + ten bai) ---
    s_list = lv_obj_create(s_scr_library);
    lv_obj_set_size(s_list, lv_pct(100), 226);   // lap day het phan con lai, khong chua cho mini-player
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 0, 78);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 2, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    // --- thanh mini-player day man hinh, chi hien khi co bai dang phat ---
    s_miniplay_bar = lv_obj_create(s_scr_library);
    lv_obj_set_size(s_miniplay_bar, lv_pct(100), 48);
    lv_obj_align(s_miniplay_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_miniplay_bar, lv_color_make(40, 40, 40), 0);
    lv_obj_set_style_radius(s_miniplay_bar, 8, 0);
    lv_obj_set_style_border_width(s_miniplay_bar, 0, 0);
    lv_obj_set_style_pad_all(s_miniplay_bar, 0, 0);
    lv_obj_set_scrollable(s_miniplay_bar, false);
    lv_obj_set_hidden(s_miniplay_bar, true);   // an cho den khi co bai dang phat
    lv_obj_add_event_cb(s_miniplay_bar, event_miniplay_bar_click, LV_EVENT_CLICKED, nullptr);

    s_miniplay_label = lv_label_create(s_miniplay_bar);
    lv_label_set_text(s_miniplay_label, "");
    lv_label_set_long_mode(s_miniplay_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_miniplay_label, 110);
    lv_obj_set_height(s_miniplay_label, 18);
    lv_obj_set_style_text_color(s_miniplay_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_miniplay_label, UI_FONT_SMALL, 0);
    lv_obj_align(s_miniplay_label, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *miniplay_btn = lv_button_create(s_miniplay_bar);
    lv_obj_set_size(miniplay_btn, 32, 32);
    lv_obj_set_style_radius(miniplay_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(miniplay_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(miniplay_btn, 0, 0);
    lv_obj_align(miniplay_btn, LV_ALIGN_RIGHT_MID, -42, 0);
    lv_obj_add_event_cb(miniplay_btn, event_play_pause_click, LV_EVENT_CLICKED, nullptr);
    s_miniplay_btn_icon = lv_label_create(miniplay_btn);
    lv_label_set_text(s_miniplay_btn_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(s_miniplay_btn_icon, lv_color_white(), 0);
    lv_obj_center(s_miniplay_btn_icon);

    lv_obj_t *miniplay_next_btn = lv_button_create(s_miniplay_bar);
    lv_obj_set_size(miniplay_next_btn, 32, 32);
    lv_obj_set_style_radius(miniplay_next_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(miniplay_next_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(miniplay_next_btn, 0, 0);
    lv_obj_align(miniplay_next_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(miniplay_next_btn, event_next_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *next_icon = lv_label_create(miniplay_next_btn);
    lv_label_set_text(next_icon, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(next_icon, lv_color_white(), 0);
    lv_obj_center(next_icon);
}

// ================== TAO MAN HINH PLAYER ==================

static void build_player_screen(void)
{
    s_scr_player = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_player, lv_color_make(60, 50, 80), 0);
    lv_obj_set_style_pad_all(s_scr_player, 10, 0);
    lv_obj_set_scrollbar_mode(s_scr_player, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(s_scr_player, false);
    // Khong con nut back - vuot tu tren xuong de quay ve Library (xem event_player_gesture)
    lv_obj_add_event_cb(s_scr_player, event_player_gesture, LV_EVENT_GESTURE, nullptr);

    // khung bia dia - don gian hoa thanh cac hinh tron long nhau (dia nhac)
    lv_obj_t *art_bg = lv_obj_create(s_scr_player);
    lv_obj_set_size(art_bg, 140, 140);
    lv_obj_align(art_bg, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_radius(art_bg, 16, 0);
    lv_obj_set_style_bg_color(art_bg, lv_color_make(210, 200, 225), 0);
    lv_obj_set_style_border_width(art_bg, 0, 0);
    lv_obj_set_scrollable(art_bg, false);

    lv_obj_t *disc = lv_obj_create(art_bg);
    lv_obj_set_size(disc, 100, 100);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_black(), 0);
    lv_obj_set_style_border_width(disc, 0, 0);
    lv_obj_set_scrollable(disc, false);

    // --- nhom cac vien/duong trang bo khuyet (hieu ung anh sang) - nam trong
    // 1 container rieng de co the xoay ca nhom bang 1 style transform duy nhat ---
    s_disc_rings = lv_obj_create(disc);
    lv_obj_remove_style_all(s_disc_rings);   // bo style mac dinh TRUOC - de khong xoa mat
                                              // kich thuoc/vi tri dat ben duoi (size/center
                                              // cung la local style trong LVGL, remove_style_all
                                              // goi SAU set_size/center se xoa luon ca 2 dong do,
                                              // lam tam xoay (50,50) khong con trung tam thuc => dia
                                              // "le" quanh 1 diem sai thay vi dung yen o chinh giua)
    lv_obj_set_size(s_disc_rings, 100, 100);
    lv_obj_center(s_disc_rings);
    lv_obj_set_scrollable(s_disc_rings, false);
    lv_obj_set_clickable(s_disc_rings, false);
    lv_obj_set_style_transform_pivot_x(s_disc_rings, 50, 0);   // xoay quanh tam cua nhom 100x100
    lv_obj_set_style_transform_pivot_y(s_disc_rings, 50, 0);

    struct ring_def { int32_t size; int32_t start_deg; int32_t end_deg; };
    static const ring_def rings[] = {
        { 96,  15, 165 },   // vien ngoai, khuyet 1 doan phia tren-phai
        { 72, 195, 335 },   // vien giua, khuyet o goc khac
        { 48,  55, 250 },   // vien trong cung
    };
    for (int i = 0; i < (int)(sizeof(rings) / sizeof(rings[0])); i++) {
        lv_obj_t *arc = lv_arc_create(s_disc_rings);
        lv_obj_set_size(arc, rings[i].size, rings[i].size);
        lv_obj_center(arc);
        lv_obj_set_clickable(arc, false);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arc, 0, 0);
        lv_arc_set_bg_angles(arc, rings[i].start_deg, rings[i].end_deg);
        lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);   // khong dung indicator
        lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);        // an nut keo cua arc
    }

    lv_obj_t *disc_center = lv_obj_create(disc);
    lv_obj_set_size(disc_center, 24, 24);
    lv_obj_center(disc_center);
    lv_obj_set_style_radius(disc_center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc_center, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(disc_center, 0, 0);

    // ten bai + "Unknown Artist" co dinh (file WAV khong co san ten nghe si trong header)
    s_player_title_label = lv_label_create(s_scr_player);
    lv_label_set_text(s_player_title_label, "");
    lv_label_set_long_mode(s_player_title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_player_title_label, 220);
    lv_obj_set_height(s_player_title_label, 20);
    lv_obj_set_style_text_color(s_player_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_player_title_label, UI_FONT_NORMAL, 0);
    lv_obj_align(s_player_title_label, LV_ALIGN_TOP_LEFT, 0, 176);

    lv_obj_t *artist_label = lv_label_create(s_scr_player);
    lv_label_set_text(artist_label, "Unknown Artist");
    lv_obj_set_style_text_color(artist_label, lv_color_make(190, 190, 190), 0);
    lv_obj_set_style_text_font(artist_label, UI_FONT_SMALL, 0);
    lv_obj_align(artist_label, LV_ALIGN_TOP_LEFT, 0, 196);

    // thanh tien trinh + nhan thoi gian (chi hien thi, khong keo tua)
    s_player_progress_bar = lv_bar_create(s_scr_player);
    lv_obj_set_size(s_player_progress_bar, 220, 6);
    lv_obj_align(s_player_progress_bar, LV_ALIGN_TOP_LEFT, 0, 218);
    lv_bar_set_range(s_player_progress_bar, 0, 100);
    lv_bar_set_value(s_player_progress_bar, 0, LV_ANIM_OFF);

    s_player_pos_label = lv_label_create(s_scr_player);
    lv_label_set_text(s_player_pos_label, "0:00");
    lv_obj_set_style_text_color(s_player_pos_label, lv_color_make(190, 190, 190), 0);
    lv_obj_set_style_text_font(s_player_pos_label, UI_FONT_SMALL, 0);
    lv_obj_align(s_player_pos_label, LV_ALIGN_TOP_LEFT, 0, 226);

    s_player_dur_label = lv_label_create(s_scr_player);
    lv_label_set_text(s_player_dur_label, "0:00");
    lv_obj_set_style_text_color(s_player_dur_label, lv_color_make(190, 190, 190), 0);
    lv_obj_set_style_text_font(s_player_dur_label, UI_FONT_SMALL, 0);
    lv_obj_align(s_player_dur_label, LV_ALIGN_TOP_RIGHT, 0, 226);

    // hang nut dieu khien: Prev - Play/Pause - Next
    lv_obj_t *prev_btn = lv_button_create(s_scr_player);
    lv_obj_set_size(prev_btn, 40, 40);
    lv_obj_set_style_bg_opa(prev_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(prev_btn, 0, 0);
    lv_obj_align(prev_btn, LV_ALIGN_TOP_LEFT, 12, 244);
    lv_obj_add_event_cb(prev_btn, event_prev_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *prev_icon = lv_label_create(prev_btn);
    lv_label_set_text(prev_icon, LV_SYMBOL_PREV);
    lv_obj_set_style_text_color(prev_icon, lv_color_white(), 0);
    lv_obj_center(prev_icon);

    lv_obj_t *play_btn = lv_button_create(s_scr_player);
    lv_obj_set_size(play_btn, 52, 52);
    lv_obj_set_style_radius(play_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_white(), 0);
    lv_obj_align(play_btn, LV_ALIGN_TOP_MID, 0, 238);
    lv_obj_add_event_cb(play_btn, event_play_pause_click, LV_EVENT_CLICKED, nullptr);
    s_player_play_icon = lv_label_create(play_btn);
    lv_label_set_text(s_player_play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(s_player_play_icon, lv_color_black(), 0);
    lv_obj_center(s_player_play_icon);

    lv_obj_t *next_btn = lv_button_create(s_scr_player);
    lv_obj_set_size(next_btn, 40, 40);
    lv_obj_set_style_bg_opa(next_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(next_btn, 0, 0);
    lv_obj_align(next_btn, LV_ALIGN_TOP_RIGHT, -12, 244);
    lv_obj_add_event_cb(next_btn, event_next_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *next_icon = lv_label_create(next_btn);
    lv_label_set_text(next_icon, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(next_icon, lv_color_white(), 0);
    lv_obj_center(next_icon);
}

// ================== HAM CONG KHAI (tft_display.h) ==================

extern "C" esp_err_t tft_display_init(void)
{
    if (s_inited) return ESP_OK;

    if (!s_lcd.init()) {
        ESP_LOGE(TAG, "Khoi tao man hinh that bai");
        return ESP_FAIL;
    }
    s_lcd.setRotation(2);   // man hinh doc 240 x 320
    s_lcd.setColorDepth(16);
    s_lcd.setSwapBytes(true);   // panel nay can dao byte RGB565 (da kiem chung bang pushImage truoc do)

    int32_t w = s_lcd.width();
    int32_t h = s_lcd.height();

    size_t buf_bytes = (size_t)w * LV_BUF_LINES * sizeof(uint16_t);
    s_lv_buf1 = (uint16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    s_lv_buf2 = (uint16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    if (!s_lv_buf1 || !s_lv_buf2) {
        ESP_LOGE(TAG, "Khong du RAM cho LVGL draw buffer");
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    lv_tick_set_cb(lvgl_tick_get_cb);

    s_disp = lv_display_create(w, h);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, s_lv_buf1, s_lv_buf2, buf_bytes,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);

    build_library_screen();
    build_player_screen();
    lv_screen_load(s_scr_library);

    xTaskCreate(lvgl_task, "lvgl", 6144, NULL, 4, NULL);
    lv_timer_create(disc_spin_timer_cb, 33, nullptr);   // quay dia khi dang phat nhac

    s_inited = true;
    ESP_LOGI(TAG, "Khoi tao man hinh (LVGL) thanh cong (%" PRId32 "x%" PRId32 ")", w, h);
    return ESP_OK;
}

extern "C" void tft_display_set_track_list(const char * const *names, int count)
{
    if (!s_inited || !s_list) return;

    lv_obj_clean(s_list);   // xoa toan bo item cu truoc khi nap lai

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_button_create(s_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 26);   // co dinh chieu cao - tranh ten bai dai lam hang bi gian ra
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_make(20, 20, 20), 0);
        lv_obj_set_style_pad_all(btn, 6, 0);
        lv_obj_set_style_pad_column(btn, 8, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_text_font(btn, UI_FONT_SMALL, 0);
        lv_obj_add_event_cb(btn, event_track_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);

        lv_obj_t *name_label = lv_label_create(btn);
        lv_label_set_text(name_label, names[i]);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_CLIP);   // 1 dong, cat bot neu qua dai
        lv_obj_set_height(name_label, 16);
        lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
        lv_obj_set_flex_grow(name_label, 1);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%d track(s)", count);
    lv_label_set_text(s_track_count_label, buf);
}

extern "C" void tft_display_set_callbacks(tft_track_click_cb_t on_track_click,
                                           tft_control_cb_t on_shuffle,
                                           tft_mode_cb_t on_mode_change,
                                           tft_control_cb_t on_play_pause,
                                           tft_control_cb_t on_next,
                                           tft_control_cb_t on_prev)
{
    s_cb_track_click = on_track_click;
    s_cb_shuffle     = on_shuffle;
    s_cb_mode_change = on_mode_change;
    s_cb_play_pause  = on_play_pause;
    s_cb_next        = on_next;
    s_cb_prev        = on_prev;
}

extern "C" void tft_display_show_track(const char *filename)
{
    if (!s_inited) return;
    strncpy(s_current_track_name, filename, sizeof(s_current_track_name) - 1);
    s_current_track_name[sizeof(s_current_track_name) - 1] = '\0';

    lv_label_set_text(s_miniplay_label, s_current_track_name);
    lv_label_set_text(s_player_title_label, s_current_track_name);
    lv_obj_set_hidden(s_miniplay_bar, false);   // co bai dang phat -> hien thanh mini-player
}

extern "C" void tft_display_set_playing(bool is_playing)
{
    if (!s_inited) return;
    const char *icon = is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    lv_label_set_text(s_miniplay_btn_icon, icon);
    lv_label_set_text(s_player_play_icon, icon);
    // KHONG dong bo icon nut Shuffle nua - nut do gio la cong tac che do
    // Shuffle/Loop rieng, khong con phan anh trang thai dang-phat/tam-dung.
    s_disc_spinning = is_playing;   // dia chi quay khi dang phat nhac
}

extern "C" void tft_display_set_progress(uint32_t pos_sec, uint32_t dur_sec)
{
    if (!s_inited) return;
    int percent = (dur_sec > 0) ? (int)((pos_sec * 100) / dur_sec) : 0;
    if (percent > 100) percent = 100;
    lv_bar_set_value(s_player_progress_bar, percent, LV_ANIM_OFF);

    char pos_buf[8], dur_buf[8];
    format_mmss(pos_sec, pos_buf, sizeof(pos_buf));
    format_mmss(dur_sec, dur_buf, sizeof(dur_buf));
    lv_label_set_text(s_player_pos_label, pos_buf);
    lv_label_set_text(s_player_dur_label, dur_buf);
}

extern "C" void tft_display_show_player_screen(void)
{
    if (!s_inited) return;
    lv_screen_load(s_scr_player);
}

extern "C" void tft_display_show_library_screen(void)
{
    if (!s_inited) return;
    lv_screen_load(s_scr_library);
}