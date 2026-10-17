#include "i2s_audio.h"
#include "board_config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "I2S_AUDIO";
static i2s_chan_handle_t s_tx_chan = NULL;
static float s_volume = 1.0f;

#define CHUNK_SIZE_MAX  4096   // gioi han an toan cho moi lan feed
#define QUEUE_DEPTH     6

typedef struct {
    uint8_t *data;   // cap phat tren HEAP, khong phai stack
    size_t   len;
} audio_chunk_t;

static QueueHandle_t s_audio_queue = NULL;

void i2s_audio_set_volume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    s_volume = volume;
}

// Task rieng: chi lo lay chunk tu hang doi va ghi ra I2S
static void i2s_writer_task(void *arg)
{
    audio_chunk_t chunk;   // gio chi la 1 con tro + 1 size_t (~8-12 byte) - khong con nang stack nua
    while (1) {
        if (xQueueReceive(s_audio_queue, &chunk, portMAX_DELAY) == pdTRUE) {
            size_t written = 0;
            i2s_channel_write(s_tx_chan, chunk.data, chunk.len, &written, portMAX_DELAY);
            free(chunk.data);   // giai phong sau khi ghi xong
        }
    }
}

esp_err_t i2s_audio_init(void)
{
    if (s_tx_chan) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 16;
    chan_cfg.dma_frame_num = 960;
    chan_cfg.auto_clear    = true;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tao I2S channel that bai: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_LRC,
            .dout = PIN_I2S_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao I2S std mode that bai: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) return ret;

    s_audio_queue = xQueueCreate(QUEUE_DEPTH, sizeof(audio_chunk_t));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "Tao queue that bai");
        return ESP_ERR_NO_MEM;
    }

    // Stack 4096 la du, vi gio chunk tren stack chi con vai byte (con tro), khong phai 4096 byte nua
    xTaskCreate(i2s_writer_task, "i2s_writer", 4096, NULL, 6, NULL);

    return ESP_OK;
}

esp_err_t i2s_audio_begin_track(const wav_info_t *info)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    if (info->bits != 8 && info->bits != 16 && info->bits != 24 && info->bits != 32) {
        ESP_LOGW(TAG, "So bit %u khong duoc ho tro", info->bits);
        return ESP_ERR_NOT_SUPPORTED;
    }

    i2s_channel_disable(s_tx_chan);

    i2s_data_bit_width_t bit_width =
        (info->bits == 8)  ? I2S_DATA_BIT_WIDTH_8BIT  :
        (info->bits == 16) ? I2S_DATA_BIT_WIDTH_16BIT :
        (info->bits == 24) ? I2S_DATA_BIT_WIDTH_24BIT : I2S_DATA_BIT_WIDTH_32BIT;

    i2s_slot_mode_t slot_mode = (info->channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

    i2s_std_clk_config_t  clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(info->sample_rate);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bit_width, slot_mode);

    esp_err_t ret = i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
    if (ret != ESP_OK) return ret;
    ret = i2s_channel_reconfig_std_slot(s_tx_chan, &slot_cfg);
    if (ret != ESP_OK) return ret;

    return i2s_channel_enable(s_tx_chan);
}

void i2s_audio_feed(const uint8_t *data, size_t len)
{
    if (!s_audio_queue || len == 0 || len > CHUNK_SIZE_MAX) return;

    uint8_t *buf = malloc(len);
    if (!buf) {
        ESP_LOGW(TAG, "Het heap, bo qua 1 chunk am thanh");
        return;
    }
    memcpy(buf, data, len);

    if (s_volume < 1.0f) {
        int16_t *samples = (int16_t *)buf;
        size_t sample_count = len / sizeof(int16_t);
        for (size_t i = 0; i < sample_count; i++) {
            samples[i] = (int16_t)((float)samples[i] * s_volume);
        }
    }

    audio_chunk_t chunk = { .data = buf, .len = len };
    if (xQueueSend(s_audio_queue, &chunk, portMAX_DELAY) != pdTRUE) {
        free(buf);   // phong truong hop gui that bai, tranh ri bo nho
    }
}

void i2s_audio_wait_drain(void)
{
    if (!s_audio_queue) return;
    while (uxQueueMessagesWaiting(s_audio_queue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}