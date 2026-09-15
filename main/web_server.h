#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia o servidor HTTP embutido para o portal de configuração Wi-Fi
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t web_server_start(void);

/**
 * @brief Para o servidor HTTP
 */
void web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
