#ifndef AUDIO_KIT_H
#define AUDIO_KIT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "es8388.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
   Mapeamento de Pinos da Placa ESP32-Audio-Kit (Módulo Ai-Thinker ESP32-A1S)
   ========================================================================= */

/* Barramento I2C (Controle dos registradores do Codec ES8388) */
#define AUDIO_KIT_I2C_PORT          I2C_NUM_0
#define AUDIO_KIT_I2C_SCL_PIN       GPIO_NUM_32
#define AUDIO_KIT_I2C_SDA_PIN       GPIO_NUM_33
#define AUDIO_KIT_I2C_FREQ_HZ       100000

/* Barramento I2S (Áudio Digital PCM) */
#define AUDIO_KIT_I2S_PORT          I2S_NUM_0
#define AUDIO_KIT_I2S_MCLK_PIN      GPIO_NUM_0   /* Clock Mestre para o Codec */
#define AUDIO_KIT_I2S_BCLK_PIN      GPIO_NUM_27  /* Bit Clock */
#define AUDIO_KIT_I2S_WS_PIN        GPIO_NUM_25  /* Word Select / LRCK */
#define AUDIO_KIT_I2S_DOUT_PIN      GPIO_NUM_26  /* Dados ESP32 -> DAC */
#define AUDIO_KIT_I2S_DIN_PIN       GPIO_NUM_35  /* Dados ADC -> ESP32 */

/* Habilitação do Amplificador de Potência (Power Amplifier - NS4150) */
#define AUDIO_KIT_PA_PIN            GPIO_NUM_21

/* Botões físicos onboard (Audio Kit Keys) */
#define AUDIO_KIT_KEY1_PIN          GPIO_NUM_36  /* KEY1 */
#define AUDIO_KIT_KEY2_PIN          GPIO_NUM_13  /* KEY2 */
#define AUDIO_KIT_KEY3_PIN          GPIO_NUM_19  /* KEY3 */
#define AUDIO_KIT_KEY4_PIN          GPIO_NUM_23  /* KEY4 */
#define AUDIO_KIT_KEY5_PIN          GPIO_NUM_18  /* KEY5 */
#define AUDIO_KIT_KEY6_PIN          GPIO_NUM_5   /* KEY6 */

/* Detecção de fone de ouvido */
#define AUDIO_KIT_HP_DETECT_PIN     GPIO_NUM_39

/**
 * @brief Inicializa todo o hardware de áudio da placa ESP32-Audio-Kit (ESP32-A1S):
 *        1. Inicializa o barramento I2C (GPIOs 32 e 33)
 *        2. Detecta e inicializa o codec ES8388
 *        3. Inicializa o canal transmissor I2S com MCLK em GPIO 0
 *        4. Configura o GPIO 21 e liga o amplificador PA onboard
 * @param sample_rate Taxa de amostragem padrão em Hz (ex: 44100 ou 48000)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t audio_kit_init(uint32_t sample_rate);

/**
 * @brief Envia bloco de dados de áudio PCM via DMA I2S
 * @param src Ponteiro para os dados PCM
 * @param size Tamanho dos dados em bytes
 * @param bytes_written Ponteiro para armazenar bytes efetivamente transmitidos
 * @param timeout_ms Tempo limite em milissegundos
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t audio_kit_i2s_write(const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Reconfigura dinamicamente a taxa de amostragem do barramento I2S
 * @param sample_rate Nova frequencia de amostragem em Hz (ex: 44100, 48000, 32000)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t audio_kit_set_sample_rate(uint32_t sample_rate);

/**
 * @brief Ajusta o volume geral da placa
 * @param volume Valor de volume entre 0 e 100
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t audio_kit_set_volume(uint8_t volume);

/**
 * @brief Habilita ou desabilita o amplificador de alto-falantes (PA)
 * @param enable true para ligar o amplificador, false para desligar/mutar
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t audio_kit_pa_enable(bool enable);

/**
 * @brief Retorna o handle do canal I2S inicializado
 */
i2s_chan_handle_t audio_kit_get_i2s_handle(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_KIT_H
