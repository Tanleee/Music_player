#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include "spi_bus.h"
#include "sd_storage.h"
#include "wav_file.h"
#include "i2s_audio.h"
#include "tft_display.h"
#include "volume_button.h"

static const char *TAG = "MAIN";

#define MAX_MUSIC_FILES 64

static char s_file_names[MAX_MUSIC_FILES][64];
static int  s_file_count = 0;

// Liet ke ten (khong kem duong dan) cac file trong SD_MUSIC_DIR vao s_file_names
static void scan_music_dir(void)
{
    s_file_count = 0;
    DIR *dir = opendir(SD_MUSIC_DIR);
    if (!dir) return;

    struct dirent *e;
    while ((e = readdir(dir)) != NULL && s_file_count < MAX_MUSIC_FILES) {
        if (e->d_type == DT_DIR) continue;
        strncpy(s_file_names[s_file_count], e->d_name, sizeof(s_file_names[0]) - 1);
        s_file_names[s_file_count][sizeof(s_file_names[0]) - 1] = '\0';
        s_file_count++;
    }
    closedir(dir);
}

// Xao tron danh sach file (thuat toan Fisher-Yates), dung esp_random()
// (bo tao so ngau nhien phan cung co san tren ESP32, khong can seed truoc)
static void shuffle_file_list(void)
{
    for (int i = s_file_count - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        char tmp[sizeof(s_file_names[0])];
		
		// swap
        memcpy(tmp, s_file_names[i], sizeof(tmp));
        memcpy(s_file_names[i], s_file_names[j], sizeof(tmp));
        memcpy(s_file_names[j], tmp, sizeof(tmp));
    }
}

// Task phat nhac: quet thu muc, xao tron thu tu, roi phat lan luot tung file WAV
static void audio_play_task(void *arg)
{
    scan_music_dir();
    shuffle_file_list();

    ESP_LOGI(TAG, "Tim thay %d file trong %s, phat theo thu tu ngau nhien:", s_file_count, SD_MUSIC_DIR);

    for (int i = 0; i < s_file_count; i++) {
		
        char path[300];		
        snprintf(path, sizeof(path), "%s/%s", SD_MUSIC_DIR, s_file_names[i]);
		
        FILE *f = fopen(path, "rb");
		
        if (!f) continue;

        wav_info_t info;
        if (wav_read_info(f, &info) == ESP_OK) {
            ESP_LOGI(TAG, "Dang phat %s: %u Hz, %u kenh, %u bit, %u bytes",
                     s_file_names[i], (unsigned)info.sample_rate, info.channels,
                     info.bits, (unsigned)info.data_size);
            tft_display_show_track(s_file_names[i]);

            if (i2s_audio_begin_track(&info) == ESP_OK) {
                fseek(f, (long)info.data_offset, SEEK_SET);

                uint8_t buf[4096];
                size_t bytes_left = info.data_size;
                while (bytes_left > 0) {
                    size_t to_read = (bytes_left < sizeof(buf)) ? bytes_left : sizeof(buf);
                    size_t n = fread(buf, 1, to_read, f);
                    if (n == 0) break;   // het file / loi doc the SD

                    i2s_audio_feed(buf, n);   // day vao hang doi, task rieng se ghi ra I2S
                    bytes_left -= n;
                }

                i2s_audio_wait_drain();   // cho phat het truoc khi doi file ke tiep
            }
        } else {
            ESP_LOGW(TAG, "%s: khong phai WAV PCM hop le", s_file_names[i]);
        }
        fclose(f);
    }

    ESP_LOGI(TAG, "Da phat het cac file trong thu muc music");
	
    vTaskDelete(NULL);   
}
void app_main(void)
{
    tft_display_init();   // khong chan chuong trinh neu that bai, nhung can bus da san sang cho SD

    if (sd_storage_init() != ESP_OK) return;
    if (i2s_audio_init() != ESP_OK) return;
    
	i2s_audio_set_volume(1.0f);
	if (volume_button_init() != ESP_OK) {
	    ESP_LOGW(TAG, "Khoi tao nut am luong that bai");
	}
	
    xTaskCreate(audio_play_task, "audio_play", 8192, NULL, 5, NULL);
}