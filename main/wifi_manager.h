#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AP_SSID            "WebRadio-Setup"
#define WIFI_AP_IP_STR          "10.10.10.1"
#define WIFI_AP_GW_STR          "10.10.10.1"
#define WIFI_AP_NETMASK_STR     "255.255.255.0"

typedef enum {
    WIFI_MGR_STATE_IDLE = 0,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_AP_MODE,
} wifi_mgr_state_t;

/**
 * @brief Inicializa o subsistema de rede e executa o fluxo de boot Wi-Fi:
 *        - Se houver rede salva na NVS: tenta conectar como Station (STA).
 *        - Se não houver ou se a conexão falhar: ativa o SoftAP em 10.10.10.1.
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Grava o SSID e Senha do Wi-Fi na memória NVS
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Lê as credenciais armazenadas na NVS
 */
esp_err_t wifi_manager_get_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len);

/**
 * @brief Apaga as credenciais da NVS (forçando o modo de configuração no próximo boot)
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * @brief Inicia o modo Ponto de Acesso (SoftAP) no IP 10.10.10.1 com Portal Cativo
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief Retorna o estado atual da conexão Wi-Fi
 */
wifi_mgr_state_t wifi_manager_get_state(void);

/**
 * @brief Retorna true se estiver conectado a uma rede Wi-Fi Station com IP atribuído
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
