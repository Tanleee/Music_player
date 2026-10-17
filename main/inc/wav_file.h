#pragma once
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint32_t data_offset;   // vi tri bat dau du lieu am thanh trong file
    uint32_t data_size;     // so byte du lieu am thanh
} wav_info_t;

esp_err_t wav_read_info(FILE *f, wav_info_t *info);