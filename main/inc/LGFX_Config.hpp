#pragma once
#include <LovyanGFX.hpp>
#include "board_config.h"

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789    _panel_instance;
  lgfx::Bus_SPI         _bus_instance;
  lgfx::Touch_XPT2046   _touch_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host   = APP_SPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_SPI_SCK;
      cfg.pin_mosi = PIN_SPI_MOSI;
      cfg.pin_miso = PIN_SPI_MISO;
      cfg.pin_dc   = PIN_TFT_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs  = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;   // GPIO8 - dung GPIO that, KHONG phai -1
      cfg.pin_busy = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.readable  = false;
      cfg.invert    = false;
      cfg.rgb_order = true;
      cfg.bus_shared = true;   // bus dung chung voi SD
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;    cfg.x_max = 4095;
      cfg.y_min = 0;    cfg.y_max = 4095;
      cfg.pin_int    = -1;
      cfg.bus_shared = true;
      cfg.spi_host   = APP_SPI_HOST;
      cfg.freq       = 1000000;
      cfg.pin_sclk   = PIN_SPI_SCK;
      cfg.pin_mosi   = PIN_SPI_MOSI;
      cfg.pin_miso   = PIN_SPI_MISO;
      cfg.pin_cs     = PIN_TOUCH_CS;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};