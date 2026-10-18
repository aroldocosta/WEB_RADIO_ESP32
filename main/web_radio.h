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

typedef struct {
    bool is_playing;
    char current_name[64];
    char current_url[256];
    char codec[8];
    int sample_rate_hz;
    int bitrate_kbps;
    uint32_t buffer_bytes;
    uint32_t frames_decoded;
} web_radio_status_t;

/**
 * @brief Inicia a reprodução definindo também o nome da estação
 */
esp_err_t web_radio_play(const char *name, const char *stream_url);

/**
 * @brief Obtém o status em tempo real da reprodução e do buffer
 */
esp_err_t web_radio_get_status(web_radio_status_t *out_status);

/**
 * @brief Verifica se a rádio está tocando no momento
 */
bool web_radio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_RADIO_H
