#include "player.h"
#include "sd_storage.h"
#include "wav_file.h"
#include "i2s_audio.h"
#include "tft_display.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "PLAYER";

#define MAX_TRACKS              64
#define FEED_CHUNK_SIZE         4096
#define PROGRESS_UPDATE_BYTES   (FEED_CHUNK_SIZE * 20)   // cap nhat progress sau moi ~80KB da doc

static char s_track_names[MAX_TRACKS][64];
static int  s_track_count = 0;

typedef enum {
    PLAYER_CMD_PLAY_INDEX = 0,
    PLAYER_CMD_TOGGLE_PLAY,
    PLAYER_CMD_NEXT,
    PLAYER_CMD_PREV,
    PLAYER_CMD_SHUFFLE,
} player_cmd_type_t;

typedef struct {
    player_cmd_type_t type;
    int index;
} player_cmd_t;

static QueueHandle_t s_cmd_queue = NULL;

static int  s_cur_index  = -1;    // chi so bai dang/sap phat, -1 = chua chon bai nao
static bool s_is_playing = false; // da bat dau phat it nhat 1 bai (co the dang pause)
static bool s_is_paused  = false;

// ---------------------------------------------------------------------
// Quet thu muc nhac tren SD, luu vao s_track_names[]
// ---------------------------------------------------------------------
static void scan_music_dir(void)
{
    s_track_count = 0;
    DIR *dir = opendir(SD_MUSIC_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Khong mo duoc thu muc %s", SD_MUSIC_DIR);
        return;
    }

    struct dirent *e;
    while ((e = readdir(dir)) != NULL && s_track_count < MAX_TRACKS) {
        if (e->d_type == DT_DIR) continue;
        strncpy(s_track_names[s_track_count], e->d_name, sizeof(s_track_names[0]) - 1);
        s_track_names[s_track_count][sizeof(s_track_names[0]) - 1] = '\0';
        s_track_count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "Tim thay %d file trong %s", s_track_count, SD_MUSIC_DIR);
}

static int pick_random_index(void)
{
    if (s_track_count <= 0) return -1;
    if (s_track_count == 1) return 0;
    int idx;
    do {
        idx = (int)(esp_random() % (uint32_t)s_track_count);
    } while (idx == s_cur_index);
    return idx;
}

// Bo phan duoi ".wav" cho de nhin tren danh sach (khong doi ten file goc tren SD)
static void display_name(int index, char *buf, size_t buf_len)
{
    strncpy(buf, s_track_names[index], buf_len - 1);
    buf[buf_len - 1] = '\0';
    char *dot = strrchr(buf, '.');
    if (dot && strcasecmp(dot, ".wav") == 0) *dot = '\0';
}

// ---------------------------------------------------------------------
// Cac callback man hinh cham goi xuong - CHAY TRONG lvgl_task, chi duoc
// gui lenh vao hang doi (khong chan, khong lam gi nang) roi thoat ngay,
// de khong lam treo/giat thao tac cham man hinh.
// ---------------------------------------------------------------------
static void ui_on_track_click(int index)
{
    player_cmd_t cmd = { .type = PLAYER_CMD_PLAY_INDEX, .index = index };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

static void ui_on_shuffle(void)
{
    player_cmd_t cmd = { .type = PLAYER_CMD_SHUFFLE, .index = 0 };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

static void ui_on_play_pause(void)
{
    player_cmd_t cmd = { .type = PLAYER_CMD_TOGGLE_PLAY, .index = 0 };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

static void ui_on_next(void)
{
    player_cmd_t cmd = { .type = PLAYER_CMD_NEXT, .index = 0 };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

static void ui_on_prev(void)
{
    player_cmd_t cmd = { .type = PLAYER_CMD_PREV, .index = 0 };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

// ---------------------------------------------------------------------
// Phat 1 file WAV, doc tung chunk va day vao i2s_audio_feed().
// Giua chung co kiem tra hang doi lenh (khong chan):
//  - Play/Pause -> xu ly tai cho (tam dung/tiep tuc), KHONG ngat bai dang phat.
//  - Lenh khac (chon bai khac/Next/Prev/Shuffle) -> ngat bai dang phat, lenh
//    duoc GIU NGUYEN trong hang doi (chi "peek") de vong lap ben ngoai tu lay.
// Tra ve true neu phat het file mot cach tu nhien, false neu bi ngat.
// ---------------------------------------------------------------------
static bool play_file_blocking(int index)
{
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", SD_MUSIC_DIR, s_track_names[index]);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Khong mo duoc file %s", path);
        return true;
    }

    wav_info_t info;
    if (wav_read_info(f, &info) != ESP_OK) {
        ESP_LOGW(TAG, "%s: khong phai WAV PCM hop le", s_track_names[index]);
        fclose(f);
        return true;
    }
    if (i2s_audio_begin_track(&info) != ESP_OK) {
        fclose(f);
        return true;
    }
    fseek(f, (long)info.data_offset, SEEK_SET);

    uint32_t bytes_per_sec = info.sample_rate * info.channels * (info.bits / 8);
    uint32_t duration_sec  = bytes_per_sec ? (info.data_size / bytes_per_sec) : 0;

    char name_buf[64];
    display_name(index, name_buf, sizeof(name_buf));
    tft_display_show_track(name_buf);
    tft_display_set_playing(true);
    tft_display_set_progress(0, duration_sec);

    static uint8_t buf[FEED_CHUNK_SIZE];   // static de khong chiem stack cua task
    size_t   bytes_left         = info.data_size;
    uint32_t bytes_fed          = 0;
    uint32_t bytes_since_update = 0;
    bool     interrupted        = false;

    while (bytes_left > 0) {
        player_cmd_t cmd;

        if (xQueuePeek(s_cmd_queue, &cmd, 0) == pdTRUE) {
            if (cmd.type == PLAYER_CMD_TOGGLE_PLAY) {
                xQueueReceive(s_cmd_queue, &cmd, 0);   // tieu thu lenh nay tai day
                s_is_paused = !s_is_paused;
                if (s_is_paused) i2s_audio_pause(); else i2s_audio_resume();
                tft_display_set_playing(!s_is_paused);
            } else {
                interrupted = true;
                break;
            }
        }

        while (s_is_paused) {
            if (xQueuePeek(s_cmd_queue, &cmd, 0) == pdTRUE) {
                if (cmd.type == PLAYER_CMD_TOGGLE_PLAY) {
                    xQueueReceive(s_cmd_queue, &cmd, 0);
                    s_is_paused = false;
                    i2s_audio_resume();
                    tft_display_set_playing(true);
                } else {
                    interrupted = true;
                    break;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        if (interrupted) break;

        size_t to_read = (bytes_left < sizeof(buf)) ? bytes_left : sizeof(buf);
        size_t n = fread(buf, 1, to_read, f);
        if (n == 0) break;   // het file / loi doc the SD

        i2s_audio_feed(buf, n);
        bytes_left -= n;
        bytes_fed  += n;
        bytes_since_update += n;

        if (bytes_per_sec && bytes_since_update >= PROGRESS_UPDATE_BYTES) {
            tft_display_set_progress(bytes_fed / bytes_per_sec, duration_sec);
            bytes_since_update = 0;
        }
    }

    fclose(f);
    if (!interrupted) {
        i2s_audio_wait_drain();
        tft_display_set_progress(duration_sec, duration_sec);
    }
    return !interrupted;
}

// ---------------------------------------------------------------------
// Task chinh: cho lenh tu man hinh cham, cap nhat s_cur_index roi phat.
// ---------------------------------------------------------------------
static void player_task(void *arg)
{
    scan_music_dir();

    const char *names[MAX_TRACKS];
    for (int i = 0; i < s_track_count; i++) names[i] = s_track_names[i];
    tft_display_set_track_list(names, s_track_count);

    player_cmd_t cmd;
    for (;;) {
        bool got_cmd;
        if (!s_is_playing) {
            // Chua phat gi ca -> cho vo han, khong ton CPU
            got_cmd = (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE);
        } else {
            // Vua ket thuc 1 bai mot cach tu nhien -> lenh tu-dong-sang-bai-ke
            // da nam san trong hang doi, se lay duoc gan nhu ngay lap tuc.
            got_cmd = (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE);
        }
        if (!got_cmd) continue;

        switch (cmd.type) {
            case PLAYER_CMD_PLAY_INDEX:
                if (cmd.index >= 0 && cmd.index < s_track_count) s_cur_index = cmd.index;
                break;
            case PLAYER_CMD_SHUFFLE:
                s_cur_index = pick_random_index();
                break;
            case PLAYER_CMD_NEXT:
                if (s_track_count > 0) s_cur_index = (s_cur_index + 1 + s_track_count) % s_track_count;
                break;
            case PLAYER_CMD_PREV:
                if (s_track_count > 0) s_cur_index = (s_cur_index - 1 + s_track_count) % s_track_count;
                break;
            case PLAYER_CMD_TOGGLE_PLAY:
                // Chua phat bai nao thi bam Play/Pause cung khong lam gi
                continue;
        }

        if (s_cur_index < 0) continue;

        s_is_playing = true;
        s_is_paused  = false;

        bool finished_naturally = play_file_blocking(s_cur_index);

        if (finished_naturally && s_track_count > 0) {
            s_cur_index = (s_cur_index + 1) % s_track_count;
            player_cmd_t auto_next = { .type = PLAYER_CMD_PLAY_INDEX, .index = s_cur_index };
            xQueueSend(s_cmd_queue, &auto_next, 0);
        } else if (finished_naturally) {
            s_is_playing = false;
            tft_display_set_playing(false);
        }
        // Neu bi ngat giua chung, lenh moi da nam san trong hang doi (chi
        // "peek" chu chua "receive" trong play_file_blocking) nen vong lap
        // tiep theo se tu dong lay va xu ly, khong can lam gi them o day.
    }
}

esp_err_t player_init(void)
{
    s_cmd_queue = xQueueCreate(4, sizeof(player_cmd_t));
    if (!s_cmd_queue) return ESP_ERR_NO_MEM;

    tft_display_set_callbacks(ui_on_track_click, ui_on_shuffle,
                               ui_on_play_pause, ui_on_next, ui_on_prev);

    BaseType_t ok = xTaskCreate(player_task, "player_task", 8192, NULL, 5, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}