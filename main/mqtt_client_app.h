#ifndef MQTT_CLIENT_APP_H
#define MQTT_CLIENT_APP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa e conecta ao broker MQTT (mqtt.oficinabr.com) com credenciais
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t mqtt_client_app_init(void);

/**
 * @brief Informa se o cliente MQTT está atualmente conectado ao broker
 */
bool mqtt_client_app_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_APP_H
