#ifndef ES8388_H
#define ES8388_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Endereço padrão I2C do chip ES8388 na placa ESP32-A1S (AD0 em nível baixo) */
#define ES8388_I2C_ADDR             0x10

/* Modo do Codec */
typedef enum {
    ES8388_MODE_SLAVE = 0x00,
    ES8388_MODE_MASTER = 0x80,
} es8388_mode_t;

/* Formato I2S */
typedef enum {
    ES8388_FMT_I2S = 0x00,
    ES8388_FMT_LEFT = 0x01,
    ES8388_FMT_RIGHT = 0x02,
    ES8388_FMT_DSP = 0x03,
} es8388_format_t;

/* Resolução em bits */
typedef enum {
    ES8388_BITS_24 = 0x00,
    ES8388_BITS_20 = 0x01,
    ES8388_BITS_18 = 0x02,
    ES8388_BITS_16 = 0x03,
    ES8388_BITS_32 = 0x04,
} es8388_bits_t;

/* Saídas de áudio */
typedef enum {
    ES8388_OUT_NONE = 0x00,
    ES8388_OUT_LINE1 = 0x01, // Fone de Ouvido (LOUT1 / ROUT1)
    ES8388_OUT_LINE2 = 0x02, // Alto-Falantes / Amplificador PA (LOUT2 / ROUT2)
    ES8388_OUT_ALL   = 0x03, // Fone e Alto-Falante simultâneos
} es8388_out_channel_t;

/**
 * @brief Inicializa o codec ES8388 via barramento I2C
 * @param i2c_port Porta I2C utilizada (ex: I2C_NUM_0)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t es8388_init(i2c_port_t i2c_port);

/**
 * @brief Escreve em um registrador do chip ES8388
 */
esp_err_t es8388_write_reg(uint8_t reg, uint8_t val);

/**
 * @brief Lê o conteúdo de um registrador do chip ES8388
 */
esp_err_t es8388_read_reg(uint8_t reg, uint8_t *val);

/**
 * @brief Configura formato e resolução de bits do DAC
 * @param fmt Formato do barramento de áudio digital (padrão: ES8388_FMT_I2S)
 * @param bits Quantidade de bits por amostra (padrão: ES8388_BITS_16)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t es8388_config_fmt(es8388_format_t fmt, es8388_bits_t bits);

/**
 * @brief Ajusta o volume de saída do DAC
 * @param volume Valor de volume de 0 a 100
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t es8388_set_voice_volume(uint8_t volume);

/**
 * @brief Obtém o volume atual configurado
 * @param volume Ponteiro para receber o valor (0 a 100)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t es8388_get_voice_volume(uint8_t *volume);

/**
 * @brief Habilita ou desabilita mudo (mute) nas saídas de áudio
 * @param enable true para mutar, false para desmutar
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t es8388_set_voice_mute(bool enable);

/**
 * @brief Configura quais saídas analógicas estarão ativas
 * @param output Combinação de canais de saída (ES8388_OUT_LINE1, ES8388_OUT_LINE2 ou ES8388_OUT_ALL)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t es8388_set_output_channel(es8388_out_channel_t output);

#ifdef __cplusplus
}
#endif

#endif // ES8388_H
