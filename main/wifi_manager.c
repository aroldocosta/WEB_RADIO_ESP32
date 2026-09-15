#include "wifi_manager.h"
#include <string.h>
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "dns_server.h"
#include "web_server.h"

static const char *TAG = "WIFI_MGR";

#define NVS_NAMESPACE       "wifi_cfg"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASSWORD    "pwd"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY_COUNT     3

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static wifi_mgr_state_t s_current_state = WIFI_MGR_STATE_IDLE;
static int s_retry_num = 0;
static bool s_is_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Conectando ao ponto de acesso configurado...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY_COUNT) {
            s_retry_num++;
            ESP_LOGW(TAG, "Tentativa %d de %d: Reconectando ao Wi-Fi...", s_retry_num, MAX_RETRY_COUNT);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Falha ao conectar no Wi-Fi após %d tentativas.", MAX_RETRY_COUNT);
            s_is_connected = false;
            s_current_state = WIFI_MGR_STATE_IDLE;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, "  🎉 CONECTADO À REDE WI-FI COM SUCESSO!");
        ESP_LOGI(TAG, "  Endereço IP Local: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "==================================================");
        s_retry_num = 0;
        s_is_connected = true;
        s_current_state = WIFI_MGR_STATE_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente conectou ao SoftAP (MAC: " MACSTR ")", MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente desconectou do SoftAP (MAC: " MACSTR ")", MAC2STR(event->mac));
    }
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS para gravacao: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK && password) {
        err = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password);
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Credenciais Wi-Fi salvas com sucesso na NVS (SSID: %s)", ssid);
    } else {
        ESP_LOGE(TAG, "Erro ao gravar dados na NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_manager_get_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err == ESP_OK && password) {
        nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, password, &password_len);
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    nvs_erase_all(nvs_handle);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Credenciais apagadas da NVS.");
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "Iniciando modo SoftAP no IP %s...", WIFI_AP_IP_STR);
    s_current_state = WIFI_MGR_STATE_AP_MODE;

    /* Para qualquer interface Station que esteja tentando conectar */
    esp_wifi_stop();

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    /* Configurar IP estático do SoftAP para 10.10.10.1 */
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton(WIFI_AP_IP_STR);
    ip_info.gw.addr = esp_ip4addr_aton(WIFI_AP_GW_STR);
    ip_info.netmask.addr = esp_ip4addr_aton(WIFI_AP_NETMASK_STR);

    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    /* Usa modo APSTA para permitir que o escaneamento de redes funcione enquanto o AP está ligado */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  🌐 PORTAL DE CONFIGURAÇÃO WI-FI ATIVO!");
    ESP_LOGI(TAG, "  Rede Wi-Fi (SSID): %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "  Acesse no navegador: http://%s/", WIFI_AP_IP_STR);
    ESP_LOGI(TAG, "==========================================================");

    /* Inicia o Servidor DNS Captive Portal e Servidor HTTP */
    uint8_t ip_bytes[4] = {10, 10, 10, 1};
    dns_server_start(ip_bytes);
    web_server_start();

    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Inicializando Gerenciador Wi-Fi...");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    /* Verifica se existem credenciais salvas na NVS */
    char saved_ssid[33] = {0};
    char saved_pwd[65] = {0};
    esp_err_t err = wifi_manager_get_credentials(saved_ssid, sizeof(saved_ssid), saved_pwd, sizeof(saved_pwd));

    if (err == ESP_OK && strlen(saved_ssid) > 0) {
        ESP_LOGI(TAG, "Credenciais encontradas na NVS! SSID: %s. Tentando conectar...", saved_ssid);
        s_current_state = WIFI_MGR_STATE_CONNECTING;

        wifi_config_t wifi_sta_config = {0};
        strncpy((char *)wifi_sta_config.sta.ssid, saved_ssid, sizeof(wifi_sta_config.sta.ssid));
        strncpy((char *)wifi_sta_config.sta.password, saved_pwd, sizeof(wifi_sta_config.sta.password));

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        /* Aguarda conexão com timeout de 12 segundos */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                              WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                              pdFALSE,
                                              pdFALSE,
                                              pdMS_TO_TICKS(12000));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Conexão Station bem-sucedida! Dispositivo pronto para streaming.");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Nao foi possivel conectar na rede '%s'. Ativando SoftAP de configuracao...", saved_ssid);
    } else {
        ESP_LOGI(TAG, "Nenhuma credencial configurada na NVS. Iniciando Portal em 10.10.10.1...");
    }

    /* Se não houver credenciais salvas ou se a conexão falhar, entra em modo SoftAP */
    return wifi_manager_start_ap();
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_current_state;
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}
