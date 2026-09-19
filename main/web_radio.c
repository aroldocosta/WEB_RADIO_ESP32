#include "web_radio.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "audio_kit.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "WEB_RADIO";

#define RING_BUF_SIZE       (256 * 1024)   /* 256 KB em PSRAM (~16 segundos de buffer a 128 kbps) */
#define PREBUFFER_BYTES     (32 * 1024)    /* 32 KB (~2 segundos) antes de iniciar reproducao */
#define STAGING_BUF_SIZE    (4096)
#define HTTP_CHUNK_SIZE     (2048)

static RingbufHandle_t s_ringbuf = NULL;
static SemaphoreHandle_t s_radio_mutex = NULL;
static TaskHandle_t s_http_task_hdl = NULL;
static TaskHandle_t s_decode_task_hdl = NULL;
static esp_http_client_handle_t s_http_client = NULL;

static bool s_tasks_created = false;
static volatile bool s_stream_requested = false;
static volatile bool s_http_reconnect = false;
static volatile bool s_decode_reset = false;
static volatile bool s_is_playing = false;
static volatile size_t s_buffered_bytes = 0;

static char s_current_url[384] = RADIO_DEFAULT_URL_BOSSA;
static char s_current_name[64] = "Bossa Nova Brazil";
static int s_active_hz = 44100;
static int s_active_bitrate = 128;
static uint32_t s_frames_decoded = 0;

static inline void buffer_add_bytes(size_t bytes)
{
    __atomic_fetch_add(&s_buffered_bytes, bytes, __ATOMIC_SEQ_CST);
}

static inline void buffer_sub_bytes(size_t bytes)
{
    size_t cur = __atomic_load_n(&s_buffered_bytes, __ATOMIC_SEQ_CST);
    while (cur > 0) {
        size_t next = (cur >= bytes) ? (cur - bytes) : 0;
        if (__atomic_compare_exchange_n(&s_buffered_bytes, &cur, next, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            break;
        }
    }
}

static inline size_t buffer_get_bytes(void)
{
    return __atomic_load_n(&s_buffered_bytes, __ATOMIC_SEQ_CST);
}

static inline void buffer_reset_bytes(void)
{
    __atomic_store_n(&s_buffered_bytes, 0, __ATOMIC_SEQ_CST);
}

/* Drena o RingBuffer de forma segura. DEVE ser chamada EXCLUSIVAMENTE pela tarefa consumidora (audio_decode_task) */
static void ringbuf_flush(void)
{
    if (!s_ringbuf) return;
    size_t rx_size = 0;
    void *item = NULL;
    while ((item = xRingbufferReceiveUpTo(s_ringbuf, &rx_size, 0, 8192)) != NULL) {
        vRingbufferReturnItem(s_ringbuf, item);
    }
    buffer_reset_bytes();
}

static void http_stream_task(void *pvParameters);
static void audio_decode_task(void *pvParameters);

esp_err_t web_radio_init(void)
{
    if (s_tasks_created && s_ringbuf != NULL) {
        return ESP_OK;
    }

    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            ESP_LOGE(TAG, "Falha ao criar mutex do Web Radio!");
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_ringbuf) {
        ESP_LOGI(TAG, "Inicializando buffer de Web Radio em PSRAM (%d KB)...", RING_BUF_SIZE / 1024);

        /* Aloca RingBuffer na memória externa PSRAM */
        s_ringbuf = xRingbufferCreateWithCaps(RING_BUF_SIZE, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_ringbuf) {
            ESP_LOGW(TAG, "Nao foi possivel alocar RingBuffer em PSRAM. Tentando memoria interna...");
            s_ringbuf = xRingbufferCreate(64 * 1024, RINGBUF_TYPE_BYTEBUF);
        }

        if (!s_ringbuf) {
            ESP_LOGE(TAG, "Falha critica: incapaz de alocar memoria para RingBuffer de audio!");
            return ESP_ERR_NO_MEM;
        }
    }

    buffer_reset_bytes();

    if (!s_tasks_created) {
        ESP_LOGI(TAG, "Criando tarefas permanentes de Streaming HTTP e Decodificacao de Audio...");

        /* Tarefa de decodificacao no Core 1 com 32 KB de stack */
        BaseType_t ret = xTaskCreatePinnedToCore(audio_decode_task,
                                                "radio_decode",
                                                32768,
                                                NULL,
                                                5,
                                                &s_decode_task_hdl,
                                                1);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Falha ao criar tarefa audio_decode_task!");
            return ESP_FAIL;
        }

        /* Tarefa de streaming HTTP no Core 0 com 10 KB de stack */
        ret = xTaskCreatePinnedToCore(http_stream_task,
                                     "radio_http",
                                     10240,
                                     NULL,
                                     4,
                                     &s_http_task_hdl,
                                     0);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Falha ao criar tarefa http_stream_task!");
            return ESP_FAIL;
        }

        s_tasks_created = true;
    }

    ESP_LOGI(TAG, "Subsistema Web Radio inicializado com sucesso.");
    return ESP_OK;
}

esp_err_t web_radio_play(const char *name, const char *stream_url)
{
    if (!s_tasks_created) {
        esp_err_t ret = web_radio_init();
        if (ret != ESP_OK) return ret;
    }

    if (s_radio_mutex) xSemaphoreTake(s_radio_mutex, portMAX_DELAY);

    if (name && strlen(name) > 0) {
        strncpy(s_current_name, name, sizeof(s_current_name) - 1);
        s_current_name[sizeof(s_current_name) - 1] = '\0';
    } else {
        strncpy(s_current_name, "Web Radio", sizeof(s_current_name) - 1);
    }

    if (stream_url && strlen(stream_url) > 0) {
        strncpy(s_current_url, stream_url, sizeof(s_current_url) - 1);
        s_current_url[sizeof(s_current_url) - 1] = '\0';
    }

    s_http_reconnect = true;
    s_decode_reset = true;
    s_stream_requested = true;
    s_frames_decoded = 0;

    if (s_radio_mutex) xSemaphoreGive(s_radio_mutex);

    ESP_LOGI(TAG, "Sintonizando nova estacao: '%s' (%s)", s_current_name, s_current_url);

    /* Se houver cliente HTTP ativo na conexao anterior, cancela requisicao para destravar leitura imediatamente */
    if (s_http_client) {
        esp_http_client_cancel_request(s_http_client);
    }

    return ESP_OK;
}

esp_err_t web_radio_start(const char *stream_url)
{
    return web_radio_play(s_current_name, stream_url);
}

esp_err_t web_radio_stop(void)
{
    ESP_LOGI(TAG, "Parando reproducao de Web Radio...");

    if (s_radio_mutex) xSemaphoreTake(s_radio_mutex, portMAX_DELAY);
    s_stream_requested = false;
    s_http_reconnect = true;
    s_decode_reset = true;
    s_is_playing = false;
    if (s_radio_mutex) xSemaphoreGive(s_radio_mutex);

    if (s_http_client) {
        esp_http_client_cancel_request(s_http_client);
    }

    return ESP_OK;
}

bool web_radio_is_playing(void)
{
    return s_is_playing;
}

esp_err_t web_radio_get_status(web_radio_status_t *out_status)
{
    if (!out_status) return ESP_ERR_INVALID_ARG;
    out_status->is_playing = s_is_playing;

    if (s_radio_mutex) xSemaphoreTake(s_radio_mutex, portMAX_DELAY);
    strncpy(out_status->current_name, s_current_name, sizeof(out_status->current_name) - 1);
    out_status->current_name[sizeof(out_status->current_name) - 1] = '\0';
    strncpy(out_status->current_url, s_current_url, sizeof(out_status->current_url) - 1);
    out_status->current_url[sizeof(out_status->current_url) - 1] = '\0';
    if (s_radio_mutex) xSemaphoreGive(s_radio_mutex);

    out_status->sample_rate_hz = s_active_hz;
    out_status->bitrate_kbps = s_active_bitrate;
    out_status->buffer_bytes = (uint32_t)buffer_get_bytes();
    out_status->frames_decoded = s_frames_decoded;
    return ESP_OK;
}

/**
 * @brief Tarefa HTTP: baixa o stream Icecast/Shoutcast e alimenta o RingBuffer
 */
static void http_stream_task(void *pvParameters)
{
    uint8_t *http_buf = (uint8_t *)malloc(HTTP_CHUNK_SIZE);
    if (!http_buf) {
        ESP_LOGE(TAG, "Sem memoria para buffer de recepcao HTTP!");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (!s_stream_requested) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        char url[384];
        if (s_radio_mutex) xSemaphoreTake(s_radio_mutex, portMAX_DELAY);
        strncpy(url, s_current_url, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
        s_http_reconnect = false;
        if (s_radio_mutex) xSemaphoreGive(s_radio_mutex);

        if (strlen(url) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG, "Iniciando conexao ao stream: %s ...", url);

        esp_http_client_config_t config = {
            .url = url,
            .timeout_ms = 8000,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .keep_alive_enable = false,
            .disable_auto_redirect = false,
            .max_redirection_count = 4,
            .user_agent = "ESP32-Audio-Kit/1.0",
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "Erro ao instanciar cliente HTTP.");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        s_http_client = client;

        /* Solicita fluxo continuo de MP3 sem metadados intercalados */
        esp_http_client_set_header(client, "Icy-MetaData", "0");

        int redirect_count = 0;

        while (s_stream_requested && !s_http_reconnect) {
            esp_err_t err = esp_http_client_open(client, 0);
            if (err != ESP_OK) {
                if (s_stream_requested && !s_http_reconnect) {
                    ESP_LOGE(TAG, "Falha ao abrir conexao HTTP: %s", esp_err_to_name(err));
                }
                break;
            }

            int64_t content_len = esp_http_client_fetch_headers(client);
            int status_code = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "Conexao HTTP estabelecida! Status: %d (Len: %lld)", status_code, (long long)content_len);

            /* Trata redirecionamentos 301, 302, 307, 308 */
            if (status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308) {
                if (redirect_count < 4 && esp_http_client_set_redirection(client) == ESP_OK) {
                    redirect_count++;
                    ESP_LOGI(TAG, "Seguindo redirecionamento HTTP %d (%d/4)...", status_code, redirect_count);
                    esp_http_client_close(client);
                    continue;
                } else {
                    ESP_LOGE(TAG, "Limite de redirecionamento ou falha com status %d", status_code);
                    break;
                }
            }

            if (status_code >= 200 && status_code < 300) {
                uint32_t total_bytes_streamed = 0;
                uint32_t last_log_bytes = 0;

                while (s_stream_requested && !s_http_reconnect) {
                    int read_len = esp_http_client_read(client, (char *)http_buf, HTTP_CHUNK_SIZE);
                    if (read_len > 0) {
                        /* Envia bloco para o RingBuffer em PSRAM respeitando o limite maximo continuo */
                        size_t sent = 0;
                        while (s_stream_requested && !s_http_reconnect && sent < (size_t)read_len) {
                            size_t remaining = (size_t)read_len - sent;
                            size_t cur_free = xRingbufferGetCurFreeSize(s_ringbuf);
                            if (cur_free == 0) {
                                vTaskDelay(pdMS_TO_TICKS(15));
                                continue;
                            }
                            size_t to_send = (remaining > cur_free) ? cur_free : remaining;
                            BaseType_t res = xRingbufferSend(s_ringbuf, http_buf + sent, to_send, pdMS_TO_TICKS(100));
                            if (res == pdTRUE) {
                                buffer_add_bytes(to_send);
                                sent += to_send;
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(15));
                            }
                        }

                        total_bytes_streamed += read_len;
                        if (total_bytes_streamed - last_log_bytes >= 64 * 1024) {
                            ESP_LOGI(TAG, "📥 Stream HTTP: %u KB recebidos | Buffer PSRAM: %u KB",
                                     (unsigned int)(total_bytes_streamed / 1024),
                                     (unsigned int)(buffer_get_bytes() / 1024));
                            last_log_bytes = total_bytes_streamed;
                        }
                    } else if (read_len == 0) {
                        ESP_LOGW(TAG, "Fim do stream ou desconexao remota (read_len = 0)");
                        break;
                    } else {
                        if (s_http_reconnect || !s_stream_requested) {
                            ESP_LOGI(TAG, "Stream cancelado para comutacao de estacao.");
                        } else {
                            ESP_LOGW(TAG, "Erro de leitura HTTP (read_len = %d)", read_len);
                        }
                        break;
                    }
                }
            } else {
                ESP_LOGE(TAG, "Servidor retornou status invalido (%d)", status_code);
            }
            break;
        }

        s_http_client = NULL;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (s_stream_requested && !s_http_reconnect) {
            ESP_LOGI(TAG, "Reconectando em 2 segundos...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    free(http_buf);
    s_http_task_hdl = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Tarefa de Decodificação e Reprodução de Áudio (Core 1)
 */
static void audio_decode_task(void *pvParameters)
{
    mp3dec_t *mp3d = (mp3dec_t *)calloc(1, sizeof(mp3dec_t));
    uint8_t *staging_buf = (uint8_t *)malloc(STAGING_BUF_SIZE);
    mp3d_sample_t *pcm = (mp3d_sample_t *)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));
    int16_t *stereo_pcm = (int16_t *)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t));

    if (!mp3d || !staging_buf || !pcm || !stereo_pcm) {
        ESP_LOGE(TAG, "Sem memoria para buffers de decodificacao de audio!");
        if (mp3d) free(mp3d);
        if (staging_buf) free(staging_buf);
        if (pcm) free(pcm);
        if (stereo_pcm) free(stereo_pcm);
        vTaskDelete(NULL);
        return;
    }

    mp3dec_init(mp3d);

    size_t staging_len = 0;
    uint32_t frames_decoded = 0;
    int s_candidate_hz = 44100;
    int s_candidate_hz_count = 0;
    bool needs_prebuffer = true;

    while (1) {
        if (!s_stream_requested) {
            s_is_playing = false;
            staging_len = 0;
            needs_prebuffer = true;
            ringbuf_flush();
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        /* Se houver comando de troca de estacao ou reset */
        if (s_decode_reset) {
            s_is_playing = false;
            staging_len = 0;
            ringbuf_flush();
            mp3dec_init(mp3d);
            needs_prebuffer = true;
            frames_decoded = 0;
            s_decode_reset = false;
        }

        /* 1. Fase de Pré-buffering (apenas ao iniciar nova estacao ou apos underrun) */
        if (needs_prebuffer) {
            s_is_playing = false;
            staging_len = 0;
            mp3dec_init(mp3d);
            ESP_LOGI(TAG, "Aguardando pre-buffering (%d KB)...", PREBUFFER_BYTES / 1024);

            while (s_stream_requested && !s_decode_reset && buffer_get_bytes() < PREBUFFER_BYTES) {
                vTaskDelay(pdMS_TO_TICKS(40));
            }

            if (!s_stream_requested || s_decode_reset) continue;

            ESP_LOGI(TAG, "Pre-buffering concluido (%u bytes em PSRAM)! Iniciando reproducao...",
                     (unsigned int)buffer_get_bytes());
            needs_prebuffer = false;
        }

        /* 2. Mantém o staging buffer abastecido */
        if (staging_len < 2048) {
            size_t needed = STAGING_BUF_SIZE - staging_len;
            size_t rx_size = 0;
            void *data = xRingbufferReceiveUpTo(s_ringbuf, &rx_size, pdMS_TO_TICKS(20), needed);
            if (data && rx_size > 0) {
                memcpy(staging_buf + staging_len, data, rx_size);
                staging_len += rx_size;
                vRingbufferReturnItem(s_ringbuf, data);
                buffer_sub_bytes(rx_size);
            } else if (staging_len < 450 && buffer_get_bytes() == 0) {
                /* Underrun real: RingBuffer esgotado e menos de 1 quadro no staging */
                s_is_playing = false;
                int16_t silence[256 * 2] = {0};
                size_t written = 0;
                audio_kit_i2s_write(silence, sizeof(silence), &written, 50);

                ESP_LOGW(TAG, "Buffer underrun detectado! Aguardando rebuffering (16 KB)...");
                while (s_stream_requested && !s_decode_reset && buffer_get_bytes() < (16 * 1024)) {
                    vTaskDelay(pdMS_TO_TICKS(40));
                }
                if (!s_stream_requested || s_decode_reset) continue;

                ESP_LOGI(TAG, "Rebuffering concluido (%u bytes em PSRAM). Retomando reproducao.",
                         (unsigned int)buffer_get_bytes());
                continue;
            }
        }

        /* 3. Decodifica um quadro MP3 */
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(mp3d, staging_buf, (int)staging_len, pcm, &info);

        if (info.frame_bytes > 0) {
            size_t consumed = (size_t)info.frame_bytes;
            if (consumed > staging_len) consumed = staging_len;
            memmove(staging_buf, staging_buf + consumed, staging_len - consumed);
            staging_len -= consumed;

            if (samples > 0) {
                /* Filtro de estabilidade para reconfigurar o clock I2S quando necessario */
                if (info.hz > 0 && info.hz != s_active_hz) {
                    if (info.hz == s_candidate_hz) {
                        s_candidate_hz_count++;
                        if (s_candidate_hz_count >= 10 || s_active_hz == 0) {
                            audio_kit_set_sample_rate(info.hz);
                            s_active_hz = info.hz;
                            s_candidate_hz_count = 0;
                        }
                    } else {
                        s_candidate_hz = info.hz;
                        s_candidate_hz_count = 1;
                    }
                } else {
                    s_candidate_hz_count = 0;
                }

                size_t written = 0;
                if (info.channels == 2) {
                    audio_kit_i2s_write(pcm, samples * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
                } else if (info.channels == 1) {
                    /* Converte mono para estéreo intercalado */
                    for (int i = 0; i < samples; i++) {
                        stereo_pcm[i * 2]     = pcm[i];
                        stereo_pcm[i * 2 + 1] = pcm[i];
                    }
                    audio_kit_i2s_write(stereo_pcm, samples * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
                }

                frames_decoded++;
                s_frames_decoded = frames_decoded;
                s_active_bitrate = info.bitrate_kbps;
                s_is_playing = true;

                if (frames_decoded % 200 == 1) {
                    ESP_LOGI(TAG, "📻 Tocando: %s | %d Hz | %d kbps | %s | Buffer: %u KB | Frames: %u",
                             s_current_name,
                             info.hz, info.bitrate_kbps,
                             (info.channels == 2 ? "Estéreo" : "Mono"),
                             (unsigned int)(buffer_get_bytes() / 1024),
                             (unsigned int)frames_decoded);
                }
            }
        } else {
            /* Avança buffer se sincronização de quadro ainda não foi encontrada */
            if (info.frame_offset > 0 && info.frame_offset < (int)staging_len) {
                memmove(staging_buf, staging_buf + info.frame_offset, staging_len - info.frame_offset);
                staging_len -= info.frame_offset;
            } else if (staging_len >= 2048) {
                memmove(staging_buf, staging_buf + 1, staging_len - 1);
                staging_len--;
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }

    if (mp3d) free(mp3d);
    free(staging_buf);
    free(pcm);
    free(stereo_pcm);
    s_decode_task_hdl = NULL;
    vTaskDelete(NULL);
}
