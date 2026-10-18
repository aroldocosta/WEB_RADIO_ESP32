#include "mqtt_client_app.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include <string.h>

static const char *TAG = "BENEDICTA_MQTT";

#define MQTT_BROKER_URI "mqtt://oficinabr.com:1883"
#define MQTT_DEFAULT_USER "microlet"
#define MQTT_DEFAULT_PASS "87654321"
#define TOPIC_CATALOG_UPDATE "benedictatu/catalog/update"

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    s_is_connected = true;
    ESP_LOGI(TAG, "Conectado com sucesso ao broker MQTT (%s)", MQTT_BROKER_URI);
    /* Inscreve-se no tópico de atualização de links do catálogo */
    esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_CATALOG_UPDATE, 1);
    ESP_LOGI(TAG, "Inscrito no topico: %s", TOPIC_CATALOG_UPDATE);
    break;

  case MQTT_EVENT_DISCONNECTED:
    s_is_connected = false;
    ESP_LOGW(TAG, "Desconectado do broker MQTT. Tentando reconectar...");
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "Notificacao MQTT recebida no topico: %.*s", event->topic_len,
             event->topic);
    ESP_LOGI(TAG, "Conteudo: %.*s", event->data_len, event->data);
    /* Aqui o firmware detecta atualizações de URLs do catálogo emitidas pela
     * plataforma */
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGW(TAG, "Erro no cliente MQTT");
    break;

  default:
    break;
  }
}

esp_err_t mqtt_client_app_init(void) {
  if (s_mqtt_client) {
    return ESP_OK;
  }

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char client_id[32];
  snprintf(client_id, sizeof(client_id), "benedictatu_%02x%02x%02x", mac[3],
           mac[4], mac[5]);

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = MQTT_BROKER_URI,
      .credentials.username = MQTT_DEFAULT_USER,
      .credentials.authentication.password = MQTT_DEFAULT_PASS,
      .credentials.client_id = client_id,
      .session.keepalive = 60,
      .task.stack_size = 3584,
  };

  s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (!s_mqtt_client) {
    ESP_LOGE(TAG, "Falha ao instanciar esp_mqtt_client!");
    return ESP_FAIL;
  }

  esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, NULL);
  esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Cliente MQTT iniciado para o aparelho [%s]", client_id);
  }
  return ret;
}

bool mqtt_client_app_is_connected(void) { return s_is_connected; }
