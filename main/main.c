#include "audio_kit.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client_app.h"
#include "nvs_flash.h"
#include "radio_storage.h"
#include "web_radio.h"
#include "wifi_manager.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "WEB_RADIO_MAIN";

#define SAMPLE_RATE_HZ 44100
#define SINE_AMPLITUDE                                                         \
  12000 /* Amplitude alta e clara para fones P2 e alto-falantes */

static TaskHandle_t s_audio_test_task_hdl = NULL;

/**
 * @brief Gera um sinal senoidal estéreo com frequências independentes para cada
 * canal
 * @param left_freq Frequência do canal esquerdo em Hz (0 para silêncio)
 * @param right_freq Frequência do canal direito em Hz (0 para silêncio)
 * @param duration_ms Duração em milissegundos
 */
static void play_tone(uint32_t left_freq, uint32_t right_freq,
                      uint32_t duration_ms) {
  const size_t chunk_samples = 256;
  int16_t buffer[chunk_samples *
                 2]; // Amostras estéreo intercaladas [L, R, L, R...]
  size_t total_samples = (SAMPLE_RATE_HZ * duration_ms) / 1000;
  size_t samples_generated = 0;

  double phase_l = 0.0;
  double phase_r = 0.0;
  double phase_inc_l =
      left_freq ? (2.0 * M_PI * left_freq / SAMPLE_RATE_HZ) : 0.0;
  double phase_inc_r =
      right_freq ? (2.0 * M_PI * right_freq / SAMPLE_RATE_HZ) : 0.0;

  while (samples_generated < total_samples) {
    size_t to_write = (total_samples - samples_generated > chunk_samples)
                          ? chunk_samples
                          : (total_samples - samples_generated);

    for (size_t i = 0; i < to_write; i++) {
      buffer[i * 2] = left_freq ? (int16_t)(sin(phase_l) * SINE_AMPLITUDE) : 0;
      buffer[i * 2 + 1] =
          right_freq ? (int16_t)(sin(phase_r) * SINE_AMPLITUDE) : 0;

      phase_l += phase_inc_l;
      if (phase_l >= 2.0 * M_PI)
        phase_l -= 2.0 * M_PI;

      phase_r += phase_inc_r;
      if (phase_r >= 2.0 * M_PI)
        phase_r -= 2.0 * M_PI;
    }

    size_t bytes_written = 0;
    audio_kit_i2s_write(buffer, to_write * 2 * sizeof(int16_t), &bytes_written,
                        100);
    samples_generated += to_write;
  }
}

/**
 * @brief Gera intervalo de silêncio
 */
static void play_silence(uint32_t duration_ms) { play_tone(0, 0, duration_ms); }

static volatile bool s_audio_test_running = true;

/**
 * @brief Tarefa FreeRTOS inicial para teste da saída de áudio até o Wi-Fi
 * conectar
 */
static void audio_p2_test_task(void *pvParameters) {
  while (s_audio_test_running && !wifi_manager_is_connected()) {
    /* 1. Canal Esquerdo (440 Hz - Nota Lá 4) */
    ESP_LOGI(TAG, "🔊 [P2 Teste] 1/3: Canal ESQUERDO (440 Hz)...");
    play_tone(440, 0, 700);
    if (!s_audio_test_running || wifi_manager_is_connected())
      break;
    play_silence(150);

    /* 2. Canal Direito (880 Hz - Nota Lá 5) */
    if (!s_audio_test_running || wifi_manager_is_connected())
      break;
    ESP_LOGI(TAG, "🔊 [P2 Teste] 2/3: Canal DIREITO (880 Hz)...");
    play_tone(0, 880, 700);
    if (!s_audio_test_running || wifi_manager_is_connected())
      break;
    play_silence(150);

    /* 3. Ambos os Canais - Acorde Estéreo (L: 554 Hz, R: 659 Hz) */
    if (!s_audio_test_running || wifi_manager_is_connected())
      break;
    ESP_LOGI(TAG, "🔊 [P2 Teste] 3/3: AMBOS OS CANAIS ESTÉREO (L: 554 Hz | R: "
                  "659 Hz)...");
    play_tone(554, 659, 1000);
    play_silence(1000);
  }

  ESP_LOGI(
      TAG,
      "Tarefa de teste de tons encerrada com seguranca (driver I2S liberado).");
  s_audio_test_task_hdl = NULL;
  vTaskDelete(NULL);
}

/**
 * @brief Rotina serial periódica a cada 1 segundo (Heartbeat)
 *        Envia a mensagem solicitada com os milissegundos decorridos desde o
 * boot
 */
static void serial_heartbeat_task(void *pvParameters) {
  while (1) {
    int64_t millis = esp_timer_get_time() / 1000;
    printf("Estou funcionando a  %lld milisengundos\n", (long long)millis);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
 * @brief Tarefa supervisora: aguarda conexao Wi-Fi e inicializa a reproducao da
 * Web Radio
 */
static void radio_supervisor_task(void *pvParameters) {
  ESP_LOGI(TAG, "Supervisor aguardando conexao Wi-Fi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  ESP_LOGI(TAG, "==================================================");
  ESP_LOGI(TAG, "  🌐 Wi-Fi Conectado! Iniciando Web Rádio Pública");
  ESP_LOGI(TAG, "==================================================");

  /* Sinaliza encerramento da tarefa de teste e aguarda término limpo para não
   * bloquear mutex do I2S */
  s_audio_test_running = false;
  while (s_audio_test_task_hdl != NULL) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  vTaskDelay(pdMS_TO_TICKS(100));

  /* Conecta ao broker MQTT para notificacoes de catalogo */
  mqtt_client_app_init();

  /* Inicializa e conecta ao stream da rádio configurada */
  web_radio_init();

  radio_station_t initial_station;
  if (radio_storage_get_by_index(0, &initial_station) == ESP_OK) {
    ESP_LOGI(TAG, "Iniciando com primeira estacao salva: %s",
             initial_station.name);
    web_radio_play(initial_station.name, initial_station.url);
  } else {
    web_radio_play("Rádio Aparecida FM",
                   "https://aparecida.jmvstream.com/stream");
  }

  vTaskDelete(NULL);
}

void app_main(void) {
  ESP_LOGI(TAG, "==================================================");
  ESP_LOGI(TAG, "    Web Radio ESP32 - ESP32-Audio-Kit (ESP32-A1S) ");
  ESP_LOGI(TAG, "==================================================");

  /* 1. Inicializa NVS */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  /* Inicializa armazenamento persistente de rádios (com presets de fábrica na
   * 1ª vez) */
  radio_storage_init();

  /* 2. Dispara imediatamente a rotina serial de 1 segundo solicitada */
  xTaskCreatePinnedToCore(serial_heartbeat_task, "heartbeat_task", 2048, NULL,
                          1, NULL, 0);

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

  /* Configura volume da placa para 30% */
  audio_kit_set_volume(30);

  /* 5. Dispara a tarefa inicial de teste P2 (ativa ate o Wi-Fi conectar) */
  xTaskCreatePinnedToCore(audio_p2_test_task, "audio_p2_test", 4096, NULL, 5,
                          &s_audio_test_task_hdl, 1);

  /* 6. Dispara o supervisor para comutar para a Web Radio quando o Wi-Fi
   * conectar */
  xTaskCreatePinnedToCore(radio_supervisor_task, "radio_sup", 4096, NULL, 4,
                          NULL, 0);

  /* 7. Inicializa o Gerenciador Wi-Fi (SoftAP 10.10.10.1 ou Conexão Station) */
  wifi_manager_init();
}
