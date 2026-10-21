#pragma once
#include <stdio.h>
#include "esp_err.h"
#include "wav_file.h"

esp_err_t i2s_audio_init(void);
void i2s_audio_set_volume(float volume);

// Cau hinh lai clock/slot cho dung dinh dang file WAV sap phat.
// Goi 1 lan khi bat dau phat 1 file moi (TRUOC khi goi i2s_audio_feed lan dau).
esp_err_t i2s_audio_begin_track(const wav_info_t *info);

// Day 1 khoi du lieu PCM vao hang doi de task rieng ghi ra I2S.
// Ham nay CHAN (block) neu hang doi dang day - dung de dieu tiet toc do doc SD.
void i2s_audio_feed(const uint8_t *data, size_t len);

// Cho den khi hang doi phat het sach (dung truoc khi doi dinh dang cho file ke tiep).
void i2s_audio_wait_drain(void);

// Lay muc am luong hien tai (0.0 - 1.0)
float i2s_audio_get_volume(void);