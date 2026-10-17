#include "tft_display.h"
#include "LGFX_Config.hpp"
#include "esp_log.h"

static const char *TAG = "TFT";
static LGFX s_lcd;
static bool s_inited = false;

extern "C" esp_err_t tft_display_init(void)
{
    if (s_inited) return ESP_OK;

    if (!s_lcd.init()) {
        ESP_LOGE(TAG, "Khoi tao man hinh that bai");
        return ESP_FAIL;
    }

    s_lcd.setRotation(1);     // neu hinh bi xoay/lat sai thi doi so 0-3 cho dung
    s_lcd.setColorDepth(16);
    s_lcd.fillScreen(TFT_BLACK);
    s_lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    s_lcd.setTextSize(2);
    s_lcd.setCursor(10, 10);
    s_lcd.print("San sang...");

    s_inited = true;
    ESP_LOGI(TAG, "Khoi tao man hinh thanh cong");
    return ESP_OK;
}

extern "C" void tft_display_show_track(const char *filename)
{
    if (!s_inited) return;

    s_lcd.fillScreen(TFT_BLACK);

    s_lcd.setCursor(10, 10);
    s_lcd.setTextSize(2);
    s_lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    s_lcd.print("Dang phat:");

    s_lcd.setCursor(10, 50);
    s_lcd.setTextSize(1);
    s_lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    s_lcd.println(filename);
}