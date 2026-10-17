#include "wav_file.h"
#include <string.h>
#include <stdbool.h>

esp_err_t wav_read_info(FILE *f, wav_info_t *info)
{
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        return ESP_ERR_INVALID_ARG;          // khong phai file WAV
    }

    bool got_fmt = false;
    while (1) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) return ESP_ERR_NOT_FOUND;
        uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);

        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (size < 16 || fread(fmt, 1, 16, f) != 16) return ESP_ERR_INVALID_SIZE;
            uint16_t tag = fmt[0] | (fmt[1] << 8);
            info->channels    = fmt[2] | (fmt[3] << 8);
            info->sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            info->bits        = fmt[14] | (fmt[15] << 8);
            if (tag != 1) return ESP_ERR_NOT_SUPPORTED;   // chi ho tro PCM
            got_fmt = true;
            fseek(f, (long)(size - 16) + (size & 1), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            if (!got_fmt) return ESP_ERR_INVALID_STATE;
            info->data_size = size;
            info->data_offset = (uint32_t)ftell(f);
            return ESP_OK;
        } else {
            fseek(f, (long)size + (size & 1), SEEK_CUR);  // bo qua chunk khac (LIST...)
        }
    }
}