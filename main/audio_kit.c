#include "audio_kit.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_KIT";

static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_pa_enabled = false;
static uint32_t s_current_rate = 0;

esp_err_t audio_kit_pa_enable(bool enable)
{
    s_pa_enabled = enable;
    esp_err_t err = gpio_set_level(AUDIO_KIT_PA_PIN, enable ? 1 : 0);
    ESP_LOGI(TAG, "Amplificador PA (GPIO %d) %s", AUDIO_KIT_PA_PIN, enable ? "LIGADO (Ativo)" : "DESLIGADO (Mudo)");
    return err;
}

esp_err_t audio_kit_init(uint32_t sample_rate)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Iniciando configuracao da placa ESP32-Audio-Kit (ESP32-A1S)...");

    /* 1. Configurar o pino do amplificador (PA) em nível baixo para evitar cliques/ruídos durante o boot */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << AUDIO_KIT_PA_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(AUDIO_KIT_PA_PIN, 0);

    /* 2. Inicializar o Barramento I2C Master para controle do Codec */
    ESP_LOGI(TAG, "Configurando I2C (SCL: GPIO %d, SDA: GPIO %d)...", AUDIO_KIT_I2C_SCL_PIN, AUDIO_KIT_I2C_SDA_PIN);
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AUDIO_KIT_I2C_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = AUDIO_KIT_I2C_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AUDIO_KIT_I2C_FREQ_HZ,
    };
    ret = i2c_param_config(AUDIO_KIT_I2C_PORT, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha na configuracao dos parametros I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(AUDIO_KIT_I2C_PORT, i2c_conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao instalar driver I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. Inicializar Barramento I2S com fornecimento de MCLK em GPIO 0 */
    ESP_LOGI(TAG, "Configurando canal I2S TX com MCLK em GPIO %d a %lu Hz...", AUDIO_KIT_I2S_MCLK_PIN, (unsigned long)sample_rate);
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_KIT_I2S_PORT, I2S_ROLE_MASTER);
    /* 8 descritores de 512 frames = 4096 amostras (~92,8 ms de tolerância a jitter a 44,1 kHz) */
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;

    ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar canal I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_KIT_I2S_MCLK_PIN,
            .bclk = AUDIO_KIT_I2S_BCLK_PIN,
            .ws   = AUDIO_KIT_I2S_WS_PIN,
            .dout = AUDIO_KIT_I2S_DOUT_PIN,
            .din  = AUDIO_KIT_I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar modo padrao I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao habilitar canal I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    s_current_rate = sample_rate;

    /* 4. Breve delay para estabilização do sinal MCLK antes de configurar o codec */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 5. Inicializar o Codec de Áudio ES8388 */
    ret = es8388_init(AUDIO_KIT_I2C_PORT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Atencao: Nao foi possivel comunicar com o Codec ES8388 no endereco 0x%02X.", ES8388_I2C_ADDR);
    }

    /* 6. Habilitar o amplificador de potência (PA) após a estabilização do codec */
    vTaskDelay(pdMS_TO_TICKS(50));
    audio_kit_pa_enable(true);

    ESP_LOGI(TAG, "ESP32-Audio-Kit pronto para reproducao!");
    return ESP_OK;
}

esp_err_t audio_kit_i2s_write(const void *src, size_t size, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!s_tx_handle) return ESP_ERR_INVALID_STATE;
    return i2s_channel_write(s_tx_handle, src, size, bytes_written, timeout_ms);
}

esp_err_t audio_kit_set_sample_rate(uint32_t sample_rate)
{
    if (!s_tx_handle) return ESP_ERR_INVALID_STATE;
    if (s_current_rate == sample_rate) return ESP_OK;

    /* Filtra valores anômalos que não sejam taxas padrão de áudio */
    if (sample_rate != 8000  && sample_rate != 11025 && sample_rate != 12000 &&
        sample_rate != 16000 && sample_rate != 22050 && sample_rate != 24000 &&
        sample_rate != 32000 && sample_rate != 44100 && sample_rate != 48000) {
        ESP_LOGW(TAG, "Taxa de amostragem invalida rejeitada: %lu Hz", (unsigned long)sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Reconfigurando taxa de amostragem I2S de %lu Hz para %lu Hz...",
             (unsigned long)s_current_rate, (unsigned long)sample_rate);
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t ret = i2s_channel_disable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao desabilitar I2S: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao reconfigurar clock I2S: %s", esp_err_to_name(ret));
    }
    i2s_channel_enable(s_tx_handle);
    s_current_rate = sample_rate;
    return ret;
}

esp_err_t audio_kit_set_volume(uint8_t volume)
{
    return es8388_set_voice_volume(volume);
}

i2s_chan_handle_t audio_kit_get_i2s_handle(void)
{
    return s_tx_handle;
}
