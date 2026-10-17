#include "spi_bus.h"
#include "board_config.h"
#include "esp_log.h"

static const char *TAG = "SPI_BUS";

esp_err_t spi_bus_init(void)
{
    spi_bus_config_t cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t ret = spi_bus_initialize(APP_SPI_HOST, &cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao bus SPI: %s", esp_err_to_name(ret));
    }
    return ret;
}