#include "volume_button.h"
#include "board_config.h"
#include "i2s_audio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "VOL_BTN";

#define VOLUME_STEP   0.10f   // moi lan nhan tang/giam 10%
#define DEBOUNCE_MS   300     // khoang thoi gian toi thieu giua 2 lan nhan duoc chap nhan (cho 1 nut)

static QueueHandle_t s_btn_queue = NULL;

// ISR: cuc ky nhe, chi day so GPIO vao queue roi thoat ngay.
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(uint32_t)arg;
    BaseType_t higher_prio_woken = pdFALSE;
    xQueueSendFromISR(s_btn_queue, &pin, &higher_prio_woken);
    if (higher_prio_woken) portYIELD_FROM_ISR();
}

static void button_task(void *arg)
{
    gpio_num_t pin;
    int64_t last_up_ms = 0, last_down_ms = 0;

    while (1) {
        if (xQueueReceive(s_btn_queue, &pin, portMAX_DELAY) != pdTRUE) continue;

        int64_t now_ms = esp_timer_get_time() / 1000;
        int64_t *last_ms = (pin == PIN_BTN_VOL_UP) ? &last_up_ms : &last_down_ms;

        // Loc bounce: neu lan nhan nay cach lan truoc (cung nut) chua du DEBOUNCE_MS
        // thi coi la bounce, bo qua ngay - khong vTaskDelay, khong chan task.
        if (now_ms - *last_ms < DEBOUNCE_MS) continue;
        *last_ms = now_ms;

        float vol = i2s_audio_get_volume();
        vol += (pin == PIN_BTN_VOL_UP) ? VOLUME_STEP : -VOLUME_STEP;
        i2s_audio_set_volume(vol);   

        ESP_LOGI(TAG, "%s -> volume = %.0f%%",
                 (pin == PIN_BTN_VOL_UP) ? "VOL_UP" : "VOL_DOWN",
                 i2s_audio_get_volume() * 100.0f);
    }
}

static void configure_button_gpio(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  
        .intr_type    = GPIO_INTR_POSEDGE,       // nhan = canh len (do dung pull-down)
    };
    gpio_config(&cfg);
    gpio_isr_handler_add(pin, btn_isr_handler, (void *)(uint32_t)pin);
}

esp_err_t volume_button_init(void)
{
    s_btn_queue = xQueueCreate(8, sizeof(gpio_num_t));
    if (!s_btn_queue) {
        ESP_LOGE(TAG, "Tao queue that bai");
        return ESP_ERR_NO_MEM;
    }

    // ESP_ERR_INVALID_STATE nghia la ISR service da duoc cai dat tu truoc (vi du module khac),
    // truong hop nay bo qua, khong coi la loi.
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Cai dat ISR service that bai: %s", esp_err_to_name(ret));
        return ret;
    }

    configure_button_gpio(PIN_BTN_VOL_UP);
    configure_button_gpio(PIN_BTN_VOL_DOWN);

    xTaskCreate(button_task, "vol_btn", 3072, NULL, 5, NULL);
    return ESP_OK;
}