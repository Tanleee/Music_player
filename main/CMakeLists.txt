idf_component_register(SRCS "main.c" 
							"src/spi_bus.c" 
							"src/sd_storage.c" 
							"src/wav_file.c"
							"src/i2s_audio.c"
							"src/tft_display.cpp"
                       INCLUDE_DIRS "inc"
                       REQUIRES fatfs sdmmc esp_driver_spi esp_driver_sdspi esp_driver_i2s LovyanGFX)