#include "web_server.h"
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "cJSON.h"
#include "setup_page.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t s_server = NULL;

static void restart_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Reiniciando sistema em 1.5 segundos para conectar à rede configurada...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* Handler da Página Principal (GET /) */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, SETUP_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Handler da API de Escaneamento Wi-Fi (GET /api/scan) */
static esp_err_t api_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Iniciando varredura Wi-Fi para o portal...");
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };
    esp_wifi_scan_start(&scan_config, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * (ap_count ? ap_count : 1));
    if (ap_count > 0 && ap_records) {
        esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        if (strlen((char *)ap_records[i].ssid) == 0) continue;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(item, "auth", ap_records[i].authmode != WIFI_AUTH_OPEN ? 1 : 0);
        cJSON_AddItemToArray(root, item);
    }
    if (ap_records) free(ap_records);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    return ESP_OK;
}

/* Handler da API para Salvar Credenciais (POST /api/save) */
static esp_err_t api_save_handler(httpd_req_t *req)
{
    char buf[384];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON *pwd_item = cJSON_GetObjectItem(json, "password");

    if (cJSON_IsString(ssid_item) && (ssid_item->valuestring != NULL)) {
        const char *ssid = ssid_item->valuestring;
        const char *pwd = (cJSON_IsString(pwd_item) && pwd_item->valuestring) ? pwd_item->valuestring : "";

        ESP_LOGI(TAG, "Gravando credenciais na NVS para SSID: '%s'", ssid);
        wifi_manager_save_credentials(ssid, pwd);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
        cJSON_Delete(json);

        /* Dispara tarefa de reinício para conectar na rede configurada */
        xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
        return ESP_OK;
    }

    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campos invalidos");
    return ESP_FAIL;
}

/* Handler para Redirecionamento de Portal Cativo */
static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://10.10.10.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK; // Já está rodando
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Iniciando servidor HTTP do Portal em 10.10.10.1:80...");
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Rota Principal */
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(s_server, &root_uri);

    /* Rota API Scan */
    httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_GET,
        .handler = api_scan_handler,
    };
    httpd_register_uri_handler(s_server, &scan_uri);

    /* Rota API Save */
    httpd_uri_t save_uri = {
        .uri = "/api/save",
        .method = HTTP_POST,
        .handler = api_save_handler,
    };
    httpd_register_uri_handler(s_server, &save_uri);

    /* Rotas Captive Portal de Sistemas Operacionais (Android / iOS / Windows) */
    const char *captive_urls[] = {
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/ncsi.txt",
        "/connecttest.txt",
        "/canonical.html",
        "/success.txt",
    };

    for (size_t i = 0; i < sizeof(captive_urls) / sizeof(captive_urls[0]); i++) {
        httpd_uri_t cap_uri = {
            .uri = captive_urls[i],
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
        };
        httpd_register_uri_handler(s_server, &cap_uri);
    }

    ESP_LOGI(TAG, "Servidor HTTP ativo com rotas de API e Portal Cativo configuradas.");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
