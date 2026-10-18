#ifndef RADIO_STORAGE_H
#define RADIO_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_NAME_MAX_LEN      64
#define RADIO_URL_MAX_LEN       256
#define MAX_RADIO_STATIONS      20

typedef struct {
    int id;
    char name[RADIO_NAME_MAX_LEN];
    char url[RADIO_URL_MAX_LEN];
} radio_station_t;

/**
 * @brief Inicializa o subsistema de armazenamento de rádios na NVS.
 *        Se for a primeira execução, grava a lista de estações padrão.
 */
esp_err_t radio_storage_init(void);

/**
 * @brief Retorna o total de estações cadastradas.
 */
int radio_storage_get_count(void);

/**
 * @brief Obtém uma estação pelo seu ID.
 */
esp_err_t radio_storage_get_by_id(int id, radio_station_t *out_station);

/**
 * @brief Obtém uma estação pelo índice (0 .. count-1).
 */
esp_err_t radio_storage_get_by_index(int index, radio_station_t *out_station);

/**
 * @brief Adiciona uma nova estação de rádio e persiste na NVS.
 * @return ID da rádio adicionada ou -1 em caso de erro.
 */
int radio_storage_add(const char *name, const char *url);

/**
 * @brief Exclui uma estação de rádio pelo ID e persiste na NVS.
 */
esp_err_t radio_storage_delete(int id);

/**
 * @brief Serializa todas as estações cadastradas em um array cJSON.
 */
cJSON* radio_storage_get_all_json(void);

/**
 * @brief Substitui atomicamente a lista de rádios salvas na NVS pelo array fornecido.
 */
esp_err_t radio_storage_replace_all(const cJSON *json_array);

/**
 * @brief Adiciona incrementalmente novas estações à lista existente (sem remover as já gravadas).
 * @return Quantidade de novas rádios adicionadas.
 */
int radio_storage_add_batch(const cJSON *json_array);

#ifdef __cplusplus
}
#endif

#endif // RADIO_STORAGE_H
