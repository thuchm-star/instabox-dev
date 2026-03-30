#include <stdint.h>
#include <stdbool.h>
#include "driver/i2s.h"
#include "board_config.h"

#define I2S_SAMPLE_RATE  16000
#define I2S_DMA_BUF_COUNT  4
#define I2S_DMA_BUF_LEN    1024

static bool s_initialized;

void audio_i2s_init(void) {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };
    esp_err_t err = i2s_driver_install(BOARD_I2S_NUM, &i2s_config, 0, NULL);
    if (err != ESP_OK) return;

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = BOARD_GPIO_I2S_BCLK,
        .ws_io_num = BOARD_GPIO_I2S_LRCK,
        .data_out_num = BOARD_GPIO_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    err = i2s_set_pin(BOARD_I2S_NUM, &pin_config);
    if (err != ESP_OK) {
        i2s_driver_uninstall(BOARD_I2S_NUM);
        return;
    }
    s_initialized = true;
}

void audio_i2s_play_attention(void) {
    if (!s_initialized) return;
    /* TODO: phát mẫu âm thanh attention qua I2S (MAX98357A) */
}
