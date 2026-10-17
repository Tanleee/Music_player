#pragma once
#include "driver/spi_common.h"

#define APP_SPI_HOST   SPI2_HOST

// Bus SPI dung chung: TFT + Touch + SD
#define PIN_SPI_SCK    6
#define PIN_SPI_MOSI   7
#define PIN_SPI_MISO   10   // dung chung: SDO man hinh + T_DO cam ung + SD_MISO

#define PIN_TFT_CS     4
#define PIN_TFT_DC     3
#define PIN_TFT_RST    8
#define PIN_TOUCH_CS   0
#define PIN_SD_CS      9

// I2S - MAX98357A
#define PIN_I2S_BCLK   2
#define PIN_I2S_LRC    1
#define PIN_I2S_DIN    5

// Ghi chu: GPIO8, GPIO9 la strapping pin tren ESP32-C3 (LOG/BOOT) nhung da
// duoc kiem chung chay on dinh trong thuc nghiem truoc do, khong can doi.
// GPIO18, GPIO19 danh cho USB, GPIO20, GPIO21 danh cho UART debug -> khong dung toi nua,
// nen Serial Monitor hoat dong binh thuong, khong bi nhieu nhu lo ngai truoc do.