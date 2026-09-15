#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_kit.h"
#include "wifi_manager.h"

static const char *TAG = "WEB_RADIO_MAIN";

#define SAMPLE_RATE_HZ      44100
#define SINE_AMPLITUDE      12000 /* Amplitude alta e clara para fones P2 e alto-falantes */

/**
 * @brief Gera um sinal senoidal estéreo com frequências independentes para cada canal
 * @param left_freq Frequência do canal esquerdo em Hz (0 para silêncio)
 * @param right_freq Frequência do canal direito em Hz (0 para silêncio)
 * @param duration_ms Duração em milissegundos
 */
static void play_tone(uint32_t left_freq, uint32_t right_freq, uint32_t duration_ms)
{
    const size_t chunk_samples = 256;
    int16_t buffer[chunk_samples * 2]; // Amostras estéreo intercaladas [L, R, L, R...]
    size_t total_samples = (SAMPLE_RATE_HZ * duration_ms) / 1000;
    size_t samples_generated = 0;

    double phase_l = 0.0;
    double phase_r = 0.0;
    double phase_inc_l = left_freq  ? (2.0 * M_PI * left_freq  / SAMPLE_RATE_HZ) : 0.0;
    double phase_inc_r = right_freq ? (2.0 * M_PI * right_freq / SAMPLE_RATE_HZ) : 0.0;

    while (samples_generated < total_samples) {
        size_t to_write = (total_samples - samples_generated > chunk_samples) 
                          ? chunk_samples 
                          : (total_samples - samples_generated);

        for (size_t i = 0; i < to_write; i++) {
            buffer[i * 2]     = left_freq  ? (int16_t)(sin(phase_l) * SINE_AMPLITUDE) : 0;
            buffer[i * 2 + 1] = right_freq ? (int16_t)(sin(phase_r) * SINE_AMPLITUDE) : 0;

            phase_l += phase_inc_l;
            if (phase_l >= 2.0 * M_PI) phase_l -= 2.0 * M_PI;

            phase_r += phase_inc_r;
            if (phase_r >= 2.0 * M_PI) phase_r -= 2.0 * M_PI;
        }

        size_t bytes_written = 0;
        audio_kit_i2s_write(buffer, to_write * 2 * sizeof(int16_t), &bytes_written, 100);
        samples_generated += to_write;
    }
}

/**
 * @brief Gera intervalo de silêncio
 */
static void play_silence(uint32_t duration_ms)
{
    play_tone(0, 0, duration_ms);
}

/**
 * @brief Tarefa FreeRTOS contínua para teste da saída de áudio P2 (fone de ouvido)
 */
static void audio_p2_test_task(void *pvParameters)
{
    while (1) {
        /* 1. Canal Esquerdo (440 Hz - Nota Lá 4) */
        ESP_LOGI(TAG, "🔊 [P2 Teste] 1/3: Canal ESQUERDO (440 Hz)...");
        play_tone(440, 0, 700);
        play_silence(150);

        /* 2. Canal Direito (880 Hz - Nota Lá 5) */
        ESP_LOGI(TAG, "🔊 [P2 Teste] 2/3: Canal DIREITO (880 Hz)...");
        play_tone(0, 880, 700);
        play_silence(150);

        /* 3. Ambos os Canais - Acorde Estéreo (L: 554 Hz, R: 659 Hz) */
        ESP_LOGI(TAG, "🔊 [P2 Teste] 3/3: AMBOS OS CANAIS ESTÉREO (L: 554 Hz | R: 659 Hz)...");
        play_tone(554, 659, 1000);
        play_silence(1000);
    }
}

/**
 * @brief Rotina serial periódica a cada 1 segundo (Heartbeat)
 *        Envia a mensagem solicitada com os milissegundos decorridos desde o boot
 */
static void serial_heartbeat_task(void *pvParameters)
{
    while (1) {
        int64_t millis = esp_timer_get_time() / 1000;
        printf("Estou funcionando a  %lld milisengundos\n", (long long)millis);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "    Web Radio ESP32 - ESP32-Audio-Kit (ESP32-A1S) ");
    ESP_LOGI(TAG, "==================================================");

    /* 1. Inicializa NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Dispara imediatamente a rotina serial de 1 segundo solicitada */
    xTaskCreatePinnedToCore(serial_heartbeat_task, "heartbeat_task", 2048, NULL, 1, NULL, 0);

    /* 3. Diagnóstico de Memória */
    size_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Memória SRAM Livre : %d KB", (int)(free_sram / 1024));
    ESP_LOGI(TAG, "Memória PSRAM Livre: %d KB", (int)(free_psram / 1024));

    /* 4. Inicializa Hardware de Áudio (ESP32-A1S Audio-Kit) */
    ret = audio_kit_init(SAMPLE_RATE_HZ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro na inicialização do ESP32-Audio-Kit!");
        return;
    }

    ESP_LOGI(TAG, "ESP32-Audio-Kit inicializado com sucesso.");

    /* Configura volume alto e limpo para fones P2 e alto-falante (85%) */
    audio_kit_set_volume(85);

    /* 5. Dispara a tarefa de áudio estéreo para a saída P2 */
    xTaskCreatePinnedToCore(audio_p2_test_task, "audio_p2_test", 4096, NULL, 5, NULL, 1);

    /* 6. Inicializa o Gerenciador Wi-Fi (SoftAP 10.10.10.1 ou Conexão Station) */
    wifi_manager_init();
}
