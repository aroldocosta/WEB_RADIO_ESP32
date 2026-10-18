#include "audio_kit.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "radio_storage.h"
#include "web_radio.h"
#include "wifi_manager.h"
#include <math.h>
#include <stdio.h>

#include "boot_sound.h"

static const char *TAG = "WEB_RADIO_MAIN";

#define SAMPLE_RATE_HZ 44100

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

  /* Aguarda a vinheta sacra terminar antes de iniciar a reprodução da rádio */
  while (boot_sound_is_playing()) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  boot_sound_stop();

  vTaskDelay(pdMS_TO_TICKS(100));

  /* Inicializa e conecta ao stream da rádio configurada */
  web_radio_init();

  radio_station_t initial_station;
  if (radio_storage_get_by_index(0, &initial_station) == ESP_OK) {
    ESP_LOGI(TAG, "Iniciando com primeira estacao salva: %s",
             initial_station.name);
    web_radio_play(initial_station.name, initial_station.url);
  } else {
    web_radio_play("Bossa Nova Brazil", RADIO_DEFAULT_URL_BOSSA);
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

  /* Configura volume da placa para 80% (audibilidade clara da vinheta e rádio)
   */
  audio_kit_set_volume(60);

  /* 5. Inicia abertura sacra de Nossa Senhora (Ave Maria) */
  boot_sound_start();

  /* 6. Dispara o supervisor para comutar para a Web Radio quando o Wi-Fi
   * conectar */
  xTaskCreatePinnedToCore(radio_supervisor_task, "radio_sup", 4096, NULL, 4,
                          NULL, 0);

  /* 7. Inicializa o Gerenciador Wi-Fi (SoftAP 10.10.10.1 ou Conexão Station) */
  wifi_manager_init();
}
