#include "es8388.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ES8388";

static i2c_port_t s_i2c_port = I2C_NUM_0;
static uint8_t s_current_volume = 75;

/* Registradores do chip ES8388 */
#define ES8388_CONTROL1         0x00
#define ES8388_CONTROL2         0x01
#define ES8388_CHIPPOWER        0x02
#define ES8388_ADCPOWER         0x03
#define ES8388_DACPOWER         0x04
#define ES8388_CHIPLOPOW1       0x05
#define ES8388_CHIPLOPOW2       0x06
#define ES8388_ANAVOLMANAG      0x07
#define ES8388_MASTERMODE       0x08

/* Registradores do DAC */
#define ES8388_DACCONTROL1      0x17 /* Formato do DAC (16-bit I2S) */
#define ES8388_DACCONTROL2      0x18 /* Razão do Clock (256 * Fs) */
#define ES8388_DACCONTROL3      0x19 /* Mute / Soft ramp: 0x04 = Mute, 0x00 = Unmute */
#define ES8388_DACCONTROL4      0x1A /* Volume Digital Canal Esquerdo (0x00 = 0dB) */
#define ES8388_DACCONTROL5      0x1B /* Volume Digital Canal Direito (0x00 = 0dB) */

/* Registradores de Mixer e Saídas Analógicas */
#define ES8388_DACCONTROL16     0x26 /* Seleção de entrada no mixer */
#define ES8388_DACCONTROL17     0x27 /* Roteamento Left DAC -> Left Mixer (0x90 = 0dB) */
#define ES8388_DACCONTROL20     0x2A /* Roteamento Right DAC -> Right Mixer (0x90 = 0dB) */
#define ES8388_DACCONTROL21     0x2B /* Habilitação de clocks internos (0x80 = DAC ON) */
#define ES8388_DACCONTROL23     0x2D /* VROI = 0 */
#define ES8388_DACCONTROL24     0x2E /* Volume LOUT1 / ROUT1 (Saída P2 / Fone): 0 = mute, 33 = 0dB */
#define ES8388_DACCONTROL25     0x2F /* Volume LOUT2 / ROUT2 (Alto-falante PA): 0 = mute, 33 = 0dB */
#define ES8388_DACCONTROL26     0x30
#define ES8388_DACCONTROL27     0x31

esp_err_t es8388_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    esp_err_t err = i2c_master_write_to_device(s_i2c_port, ES8388_I2C_ADDR, data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao gravar reg 0x%02X: %s", reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t es8388_read_reg(uint8_t reg, uint8_t *val)
{
    esp_err_t err = i2c_master_write_read_device(s_i2c_port, ES8388_I2C_ADDR, &reg, 1, val, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao ler reg 0x%02X: %s", reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t es8388_init(i2c_port_t i2c_port)
{
    s_i2c_port = i2c_port;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Inicializando Codec ES8388 no endereco 0x%02X...", ES8388_I2C_ADDR);

    /* 1. Mute DAC inicialmente */
    ret |= es8388_write_reg(ES8388_DACCONTROL3, 0x04);

    /* 2. Chip Control e Power Management */
    ret |= es8388_write_reg(ES8388_CONTROL2, 0x50);
    ret |= es8388_write_reg(ES8388_CHIPPOWER, 0x00);  // Liga toda a circuiteria do chip
    ret |= es8388_write_reg(ES8388_MASTERMODE, 0x00); // Modo escravo I2S (ESP32 gera clocks)

    /* 3. Configuração do DAC */
    ret |= es8388_write_reg(ES8388_DACPOWER, 0xC0);   // Desliga DAC temporariamente para ajuste
    ret |= es8388_write_reg(ES8388_CONTROL1, 0x12);   // Referência e polarização Play&Record
    ret |= es8388_write_reg(ES8388_DACCONTROL1, 0x18); // 16-bit I2S Standard
    ret |= es8388_write_reg(ES8388_DACCONTROL2, 0x02); // Fs ratio = 256 * MCLK

    /* 4. Roteamento de áudio analógico para o Mixer */
    ret |= es8388_write_reg(ES8388_DACCONTROL16, 0x00);
    ret |= es8388_write_reg(ES8388_DACCONTROL17, 0x90); // Left DAC -> Left Mixer (0dB)
    ret |= es8388_write_reg(ES8388_DACCONTROL20, 0x90); // Right DAC -> Right Mixer (0dB)
    ret |= es8388_write_reg(ES8388_DACCONTROL21, 0x80); // Enable DAC clock
    ret |= es8388_write_reg(ES8388_DACCONTROL23, 0x00); // VROI = 0

    /* 5. Volume digital do DAC (0x00 = 0dB, sem atenuação digital) */
    ret |= es8388_write_reg(ES8388_DACCONTROL4, 0x00);
    ret |= es8388_write_reg(ES8388_DACCONTROL5, 0x00);

    /* 6. Habilita saídas LOUT1/ROUT1 (Fone P2) e LOUT2/ROUT2 (Alto-falantes) */
    ret |= es8388_write_reg(ES8388_DACPOWER, 0x3C);

    /* 7. Ajusta volume analógico e remove mudo */
    ret |= es8388_set_voice_volume(s_current_volume);
    ret |= es8388_set_voice_mute(false);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ES8388 configurado com sucesso! (P2 LOUT1/ROUT1 e Alto-falantes LOUT2/ROUT2 ativos)");
    } else {
        ESP_LOGE(TAG, "Erro ao configurar registradores do ES8388");
    }

    return ret;
}

esp_err_t es8388_config_fmt(es8388_format_t fmt, es8388_bits_t bits)
{
    uint8_t reg = 0;
    reg = ((uint8_t)fmt & 0x03) | (((uint8_t)bits & 0x07) << 2);
    return es8388_write_reg(ES8388_DACCONTROL1, reg);
}

esp_err_t es8388_set_voice_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_current_volume = volume;

    // Converte escala 0..100 para o registrador do ES8388 (0 = mudo, 33 = 0dB ganho máximo)
    uint8_t reg_val = (uint8_t)((volume * 33) / 100);

    esp_err_t ret = ESP_OK;
    ret |= es8388_write_reg(ES8388_DACCONTROL24, reg_val); // LOUT1 / ROUT1 (Saída P2)
    ret |= es8388_write_reg(ES8388_DACCONTROL25, reg_val); // LOUT2 / ROUT2 (Alto-falante)
    ret |= es8388_write_reg(ES8388_DACCONTROL26, 0x00);
    ret |= es8388_write_reg(ES8388_DACCONTROL27, 0x00);

    ESP_LOGI(TAG, "Volume configurado para %d%% (reg 0x2E/0x2F: 0x%02X)", volume, reg_val);
    return ret;
}

esp_err_t es8388_get_voice_volume(uint8_t *volume)
{
    if (!volume) return ESP_ERR_INVALID_ARG;
    *volume = s_current_volume;
    return ESP_OK;
}

esp_err_t es8388_set_voice_mute(bool enable)
{
    // Bit 2 do DACCONTROL3 controla o mute do DAC (0x04 = Mute, 0x00 = Unmute)
    uint8_t val = enable ? 0x04 : 0x00;
    return es8388_write_reg(ES8388_DACCONTROL3, val);
}

esp_err_t es8388_set_output_channel(es8388_out_channel_t output)
{
    esp_err_t ret = ESP_OK;
    uint8_t dac_pwr = 0x00;

    if (output & ES8388_OUT_LINE1) {
        dac_pwr |= 0x30; // LOUT1 / ROUT1 (P2)
    }
    if (output & ES8388_OUT_LINE2) {
        dac_pwr |= 0x0C; // LOUT2 / ROUT2 (Speaker)
    }

    ret |= es8388_write_reg(ES8388_DACPOWER, dac_pwr);
    return ret;
}
