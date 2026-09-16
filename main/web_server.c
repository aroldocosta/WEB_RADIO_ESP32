#include "web_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "cJSON.h"
#include "setup_page.h"
#include "radio_page.h"
#include "radio_storage.h"
#include "web_radio.h"
#include "audio_kit.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t s_server = NULL;

static void restart_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Reiniciando sistema em 1.5 segundos para conectar a rede configurada...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* Handler da Página Principal (GET /) */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (wifi_manager_is_connected()) {
        httpd_resp_send(req, RADIO_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, SETUP_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

/* Handler explícito da Página de Configuração Wi-Fi (GET /wifi) */
static esp_err_t wifi_get_handler(httpd_req_t *req)
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

/* Handler da API para Salvar Credenciais Wi-Fi (POST /api/save) */
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

/* Handler da API para Listar Estações (GET /api/stations) */
static esp_err_t api_stations_get_handler(httpd_req_t *req)
{
    cJSON *root = radio_storage_get_all_json();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    return ESP_OK;
}

/* Handler da API para Cadastrar Nova Rádio (POST /api/stations) */
static esp_err_t api_stations_post_handler(httpd_req_t *req)
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

    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    cJSON *url_item = cJSON_GetObjectItem(json, "url");

    if (cJSON_IsString(name_item) && cJSON_IsString(url_item)) {
        int new_id = radio_storage_add(name_item->valuestring, url_item->valuestring);
        cJSON_Delete(json);
        if (new_id >= 0) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Limite de radios atingido ou erro ao salvar");
            return ESP_FAIL;
        }
    }

    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campos nome ou URL ausentes");
    return ESP_FAIL;
}

/* Handler da API para Excluir Rádio (DELETE /api/stations?id=X) */
static esp_err_t api_stations_delete_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char id_str[16];
        if (httpd_query_key_value(query, "id", id_str, sizeof(id_str)) == ESP_OK) {
            int id = atoi(id_str);
            if (radio_storage_delete(id) == ESP_OK) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
        }
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Estacao nao encontrada");
    return ESP_FAIL;
}

/* Handler da API para Sintonizar Estação (POST /api/play) */
static esp_err_t api_play_handler(httpd_req_t *req)
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

    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    cJSON *url_item = cJSON_GetObjectItem(json, "url");

    const char *name = (cJSON_IsString(name_item)) ? name_item->valuestring : "Web Radio";
    const char *url = (cJSON_IsString(url_item)) ? url_item->valuestring : NULL;

    if (url && strlen(url) > 0) {
        ESP_LOGI(TAG, "Comando de sintonia via Web: '%s' (%s)", name, url);
        web_radio_play(name, url);
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "URL invalida");
    return ESP_FAIL;
}

/* Handler da API para Pausar / Parar Reprodução (POST /api/stop) */
static esp_err_t api_stop_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Comando de parada recebido via Web.");
    web_radio_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Handler da API para Ajuste de Volume (POST /api/volume) */
static esp_err_t api_volume_handler(httpd_req_t *req)
{
    char buf[128];
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

    cJSON *vol_item = cJSON_GetObjectItem(json, "volume");
    if (cJSON_IsNumber(vol_item)) {
        int vol = vol_item->valueint;
        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;
        audio_kit_set_volume((uint8_t)vol);
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campo volume ausente ou invalido");
    return ESP_FAIL;
}

/* Handler da API de Status Completo (GET /api/status) */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    web_radio_status_t status;
    web_radio_get_status(&status);

    uint8_t vol = 60;
    es8388_get_voice_volume(&vol);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "playing", status.is_playing);
    cJSON_AddStringToObject(root, "name", status.current_name);
    cJSON_AddStringToObject(root, "url", status.current_url);
    cJSON_AddNumberToObject(root, "hz", status.sample_rate_hz);
    cJSON_AddNumberToObject(root, "bitrate", status.bitrate_kbps);
    cJSON_AddNumberToObject(root, "buffer", status.buffer_bytes);
    cJSON_AddNumberToObject(root, "frames", status.frames_decoded);
    cJSON_AddNumberToObject(root, "volume", vol);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    return ESP_OK;
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
    config.max_uri_handlers = 24;
    config.stack_size = 10240;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Iniciando servidor HTTP (porta 80)...");
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Rota Principal: Serve Player (Station) ou Setup Wi-Fi (SoftAP) */
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(s_server, &root_uri);

    /* Rota Explícita para Setup Wi-Fi */
    httpd_uri_t wifi_uri = {
        .uri = "/wifi",
        .method = HTTP_GET,
        .handler = wifi_get_handler,
    };
    httpd_register_uri_handler(s_server, &wifi_uri);

    /* APIs Wi-Fi */
    httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_GET,
        .handler = api_scan_handler,
    };
    httpd_register_uri_handler(s_server, &scan_uri);

    httpd_uri_t save_uri = {
        .uri = "/api/save",
        .method = HTTP_POST,
        .handler = api_save_handler,
    };
    httpd_register_uri_handler(s_server, &save_uri);

    /* APIs de Rádios e Sintonia */
    httpd_uri_t stations_get_uri = {
        .uri = "/api/stations",
        .method = HTTP_GET,
        .handler = api_stations_get_handler,
    };
    httpd_register_uri_handler(s_server, &stations_get_uri);

    httpd_uri_t stations_post_uri = {
        .uri = "/api/stations",
        .method = HTTP_POST,
        .handler = api_stations_post_handler,
    };
    httpd_register_uri_handler(s_server, &stations_post_uri);

    httpd_uri_t stations_del_uri = {
        .uri = "/api/stations",
        .method = HTTP_DELETE,
        .handler = api_stations_delete_handler,
    };
    httpd_register_uri_handler(s_server, &stations_del_uri);

    httpd_uri_t play_uri = {
        .uri = "/api/play",
        .method = HTTP_POST,
        .handler = api_play_handler,
    };
    httpd_register_uri_handler(s_server, &play_uri);

    httpd_uri_t stop_uri = {
        .uri = "/api/stop",
        .method = HTTP_POST,
        .handler = api_stop_handler,
    };
    httpd_register_uri_handler(s_server, &stop_uri);

    httpd_uri_t vol_uri = {
        .uri = "/api/volume",
        .method = HTTP_POST,
        .handler = api_volume_handler,
    };
    httpd_register_uri_handler(s_server, &vol_uri);

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
    };
    httpd_register_uri_handler(s_server, &status_uri);

    /* Rotas Captive Portal de Sistemas Operacionais */
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

    ESP_LOGI(TAG, "Servidor HTTP ativo com todas as rotas de Player, Estacoes e Portal Cativo configuradas.");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
