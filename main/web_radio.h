#ifndef WEB_RADIO_H
#define WEB_RADIO_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* URLs de rádios públicas MP3 estáveis */
#define RADIO_DEFAULT_URL_BOSSA   "http://54.38.43.201:8009/stream-128kmp3-BossaNovaBrazil"
#define RADIO_DEFAULT_URL_SOMAFM  "http://ice1.somafm.com/groovesalad-128-mp3"

/**
 * @brief Inicializa o subsistema de Web Radio (aloca RingBuffer em PSRAM)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t web_radio_init(void);

/**
 * @brief Inicia o streaming e a decodificação da rádio da URL fornecida
 * @param stream_url URL do stream Icecast/Shoutcast MP3 (se NULL, usa a URL padrão da Bossa Nova)
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t web_radio_start(const char *stream_url);

/**
 * @brief Interrompe o streaming e a decodificação da rádio
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t web_radio_stop(void);

/**
 * @brief Retorna se a rádio está em execução / tocando
 */
bool web_radio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_RADIO_H
