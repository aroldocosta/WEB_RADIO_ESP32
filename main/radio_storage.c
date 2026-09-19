#include "radio_storage.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "RADIO_STORAGE";

#define NVS_NAMESPACE_RADIOS    "radio_store"
#define NVS_KEY_STATIONS_JSON   "stations_v2"

static radio_station_t s_stations[MAX_RADIO_STATIONS];
static int s_station_count = 0;
static int s_next_id = 1;

static const radio_station_t DEFAULT_STATIONS[] = {
    {1, "Rádio Aparecida FM 104.3 [AAC]", "https://aparecida.jmvstream.com/stream"},
};

static esp_err_t save_to_nvs(void)
{
    cJSON *root = radio_storage_get_all_json();
    if (!root) return ESP_FAIL;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_RADIOS, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_STATIONS_JSON, json_str);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
            ESP_LOGI(TAG, "Lista de %d radios salva com sucesso na NVS.", s_station_count);
        } else {
            ESP_LOGE(TAG, "Erro ao gravar JSON na NVS: %s", esp_err_to_name(err));
        }
        nvs_close(handle);
    } else {
        ESP_LOGE(TAG, "Erro ao abrir NVS para gravacao: %s", esp_err_to_name(err));
    }

    free(json_str);
    return err;
}

static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_RADIOS, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t req_size = 0;
    err = nvs_get_str(handle, NVS_KEY_STATIONS_JSON, NULL, &req_size);
    if (err != ESP_OK || req_size == 0) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    char *json_str = malloc(req_size);
    if (!json_str) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(handle, NVS_KEY_STATIONS_JSON, json_str, &req_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        free(json_str);
        return err;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return ESP_FAIL;
    }

    s_station_count = 0;
    s_next_id = 1;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (s_station_count >= MAX_RADIO_STATIONS) break;

        cJSON *id_obj = cJSON_GetObjectItem(item, "id");
        cJSON *name_obj = cJSON_GetObjectItem(item, "name");
        cJSON *url_obj = cJSON_GetObjectItem(item, "url");

        if (cJSON_IsString(name_obj) && cJSON_IsString(url_obj)) {
            int id = (cJSON_IsNumber(id_obj)) ? id_obj->valueint : s_next_id;
            s_stations[s_station_count].id = id;
            strncpy(s_stations[s_station_count].name, name_obj->valuestring, RADIO_NAME_MAX_LEN - 1);
            s_stations[s_station_count].name[RADIO_NAME_MAX_LEN - 1] = '\0';
            strncpy(s_stations[s_station_count].url, url_obj->valuestring, RADIO_URL_MAX_LEN - 1);
            s_stations[s_station_count].url[RADIO_URL_MAX_LEN - 1] = '\0';

            if (id >= s_next_id) s_next_id = id + 1;
            s_station_count++;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Carregadas %d radios da NVS.", s_station_count);
    return ESP_OK;
}

esp_err_t radio_storage_init(void)
{
    esp_err_t err = load_from_nvs();
    if (err != ESP_OK || s_station_count == 0) {
        ESP_LOGI(TAG, "Nenhuma radio encontrada na NVS. Inicializando presets padrao...");
        s_station_count = sizeof(DEFAULT_STATIONS) / sizeof(DEFAULT_STATIONS[0]);
        for (int i = 0; i < s_station_count; i++) {
            s_stations[i] = DEFAULT_STATIONS[i];
            if (s_stations[i].id >= s_next_id) {
                s_next_id = s_stations[i].id + 1;
            }
        }
        save_to_nvs();
    }
    return ESP_OK;
}

int radio_storage_get_count(void)
{
    return s_station_count;
}

esp_err_t radio_storage_get_by_id(int id, radio_station_t *out_station)
{
    if (!out_station) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < s_station_count; i++) {
        if (s_stations[i].id == id) {
            *out_station = s_stations[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t radio_storage_get_by_index(int index, radio_station_t *out_station)
{
    if (!out_station || index < 0 || index >= s_station_count) return ESP_ERR_INVALID_ARG;
    *out_station = s_stations[index];
    return ESP_OK;
}

int radio_storage_add(const char *name, const char *url)
{
    if (!name || !url || strlen(name) == 0 || strlen(url) == 0) return -1;
    if (s_station_count >= MAX_RADIO_STATIONS) {
        ESP_LOGW(TAG, "Limite maximo de %d radios atingido!", MAX_RADIO_STATIONS);
        return -1;
    }

    int new_id = s_next_id++;
    s_stations[s_station_count].id = new_id;
    strncpy(s_stations[s_station_count].name, name, RADIO_NAME_MAX_LEN - 1);
    s_stations[s_station_count].name[RADIO_NAME_MAX_LEN - 1] = '\0';
    strncpy(s_stations[s_station_count].url, url, RADIO_URL_MAX_LEN - 1);
    s_stations[s_station_count].url[RADIO_URL_MAX_LEN - 1] = '\0';

    s_station_count++;
    save_to_nvs();
    ESP_LOGI(TAG, "Radio '%s' adicionada com ID %d.", name, new_id);
    return new_id;
}

esp_err_t radio_storage_delete(int id)
{
    int index = -1;
    for (int i = 0; i < s_station_count; i++) {
        if (s_stations[i].id == id) {
            index = i;
            break;
        }
    }

    if (index == -1) return ESP_ERR_NOT_FOUND;

    ESP_LOGI(TAG, "Excluindo radio '%s' (ID %d)...", s_stations[index].name, id);
    for (int i = index; i < s_station_count - 1; i++) {
        s_stations[i] = s_stations[i + 1];
    }
    s_station_count--;
    save_to_nvs();
    return ESP_OK;
}

cJSON* radio_storage_get_all_json(void)
{
    cJSON *root = cJSON_CreateArray();
    if (!root) return NULL;

    for (int i = 0; i < s_station_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", s_stations[i].id);
        cJSON_AddStringToObject(item, "name", s_stations[i].name);
        cJSON_AddStringToObject(item, "url", s_stations[i].url);
        cJSON_AddItemToArray(root, item);
    }
    return root;
}

esp_err_t radio_storage_replace_all(const cJSON *json_array)
{
    if (!json_array || !cJSON_IsArray(json_array)) return ESP_ERR_INVALID_ARG;

    s_station_count = 0;
    s_next_id = 1;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, json_array) {
        if (s_station_count >= MAX_RADIO_STATIONS) break;

        cJSON *name_obj = cJSON_GetObjectItem(item, "name");
        cJSON *url_obj = cJSON_GetObjectItem(item, "url");

        if (cJSON_IsString(name_obj) && cJSON_IsString(url_obj) &&
            strlen(name_obj->valuestring) > 0 && strlen(url_obj->valuestring) > 0) {
            
            s_stations[s_station_count].id = s_next_id++;
            strncpy(s_stations[s_station_count].name, name_obj->valuestring, RADIO_NAME_MAX_LEN - 1);
            s_stations[s_station_count].name[RADIO_NAME_MAX_LEN - 1] = '\0';
            strncpy(s_stations[s_station_count].url, url_obj->valuestring, RADIO_URL_MAX_LEN - 1);
            s_stations[s_station_count].url[RADIO_URL_MAX_LEN - 1] = '\0';
            s_station_count++;
        }
    }

    ESP_LOGI(TAG, "Substituicao em lote concluida: %d radios gravadas na NVS.", s_station_count);
    return save_to_nvs();
}

