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
#include "audio_kit.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "WEB_RADIO";

#define RING_BUF_SIZE       (256 * 1024)   /* 256 KB em PSRAM (~16 segundos de buffer a 128 kbps) */
#define PREBUFFER_BYTES     (48 * 1024)    /* 48 KB (~3 segundos) antes de iniciar reproducao */
#define STAGING_BUF_SIZE    (4096)
#define HTTP_CHUNK_SIZE     (2048)

static RingbufHandle_t s_ringbuf = NULL;
static TaskHandle_t s_http_task_hdl = NULL;
static TaskHandle_t s_decode_task_hdl = NULL;
static bool s_running = false;
static bool s_is_playing = false;
static volatile size_t s_buffered_bytes = 0;
static char s_current_url[384] = {0};

static void http_stream_task(void *pvParameters);
static void audio_decode_task(void *pvParameters);

esp_err_t web_radio_init(void)
{
    if (s_ringbuf != NULL) {
        return ESP_OK;
    }

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

    s_buffered_bytes = 0;
    ESP_LOGI(TAG, "RingBuffer de Web Radio inicializado com sucesso.");
    return ESP_OK;
}

esp_err_t web_radio_start(const char *stream_url)
{
    if (s_running) {
        web_radio_stop();
    }

    if (!s_ringbuf) {
        esp_err_t ret = web_radio_init();
        if (ret != ESP_OK) return ret;
    }

    if (stream_url && strlen(stream_url) > 0) {
        strncpy(s_current_url, stream_url, sizeof(s_current_url) - 1);
    } else {
        strncpy(s_current_url, RADIO_DEFAULT_URL_BOSSA, sizeof(s_current_url) - 1);
    }

    s_running = true;
    s_is_playing = false;
    s_buffered_bytes = 0;

    ESP_LOGI(TAG, "Iniciando Web Radio na URL: %s", s_current_url);

    /* Cria tarefa de decodificacao no Core 1 com 32 KB de stack */
    BaseType_t ret = xTaskCreatePinnedToCore(audio_decode_task,
                                            "radio_decode",
                                            32768,
                                            NULL,
                                            5,
                                            &s_decode_task_hdl,
                                            1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar tarefa audio_decode_task!");
        s_running = false;
        return ESP_FAIL;
    }

    /* Cria tarefa de streaming HTTP no Core 0 */
    ret = xTaskCreatePinnedToCore(http_stream_task,
                                 "radio_http",
                                 8192,
                                 NULL,
                                 4,
                                 &s_http_task_hdl,
                                 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar tarefa http_stream_task!");
        s_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t web_radio_stop(void)
{
    if (!s_running) return ESP_OK;

    ESP_LOGI(TAG, "Parando Web Radio...");
    s_running = false;
    s_is_playing = false;

    /* Aguarda término das tarefas */
    vTaskDelay(pdMS_TO_TICKS(200));

    if (s_http_task_hdl) {
        vTaskDelete(s_http_task_hdl);
        s_http_task_hdl = NULL;
    }
    if (s_decode_task_hdl) {
        vTaskDelete(s_decode_task_hdl);
        s_decode_task_hdl = NULL;
    }

    s_buffered_bytes = 0;
    return ESP_OK;
}

bool web_radio_is_playing(void)
{
    return s_is_playing;
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

    while (s_running) {
        ESP_LOGI(TAG, "Conectando ao stream: %s ...", s_current_url);

        esp_http_client_config_t config = {
            .url = s_current_url,
            .timeout_ms = 10000,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .keep_alive_enable = true,
            .user_agent = "ESP32-Audio-Kit/1.0",
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "Erro ao instanciar cliente HTTP.");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* Solicita fluxo continuo de MP3 sem metadados intercalados */
        esp_http_client_set_header(client, "Icy-MetaData", "0");

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao abrir conexao HTTP: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        int64_t content_len = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Conexao HTTP estabelecida! Status Code: %d (Content-Length: %lld)", status_code, (long long)content_len);

        if (status_code >= 200 && status_code < 300) {
            uint32_t total_bytes_streamed = 0;
            uint32_t last_log_bytes = 0;

            while (s_running) {
                int read_len = esp_http_client_read(client, (char *)http_buf, HTTP_CHUNK_SIZE);
                if (read_len > 0) {
                    /* Envia bloco para o RingBuffer em PSRAM respeitando o limite maximo continuo */
                    size_t sent = 0;
                    while (s_running && sent < (size_t)read_len) {
                        size_t remaining = (size_t)read_len - sent;
                        size_t cur_free = xRingbufferGetCurFreeSize(s_ringbuf);
                        if (cur_free == 0) {
                            vTaskDelay(pdMS_TO_TICKS(15));
                            continue;
                        }
                        size_t to_send = (remaining > cur_free) ? cur_free : remaining;
                        BaseType_t res = xRingbufferSend(s_ringbuf, http_buf + sent, to_send, pdMS_TO_TICKS(200));
                        if (res == pdTRUE) {
                            s_buffered_bytes += to_send;
                            sent += to_send;
                        } else {
                            vTaskDelay(pdMS_TO_TICKS(15));
                        }
                    }

                    total_bytes_streamed += read_len;
                    if (total_bytes_streamed - last_log_bytes >= 64 * 1024) {
                        ESP_LOGI(TAG, "📥 Stream HTTP: %u KB recebidos | Buffer PSRAM: %u KB",
                                 (unsigned int)(total_bytes_streamed / 1024),
                                 (unsigned int)(s_buffered_bytes / 1024));
                        last_log_bytes = total_bytes_streamed;
                    }
                } else if (read_len == 0) {
                    ESP_LOGW(TAG, "Fim do stream ou desconexao remota (read_len = 0)");
                    break;
                } else {
                    ESP_LOGW(TAG, "Erro de leitura HTTP (read_len = %d)", read_len);
                    break;
                }
            }
        } else {
            ESP_LOGE(TAG, "Servidor retornou status invalido (%d)", status_code);
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (s_running) {
            ESP_LOGI(TAG, "Reconectando em 3 segundos...");
            vTaskDelay(pdMS_TO_TICKS(3000));
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
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    uint8_t *staging_buf = (uint8_t *)malloc(STAGING_BUF_SIZE);
    mp3d_sample_t *pcm = (mp3d_sample_t *)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));
    int16_t *stereo_pcm = (int16_t *)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t));

    if (!staging_buf || !pcm || !stereo_pcm) {
        ESP_LOGE(TAG, "Sem memoria para buffers de decodificacao de audio!");
        if (staging_buf) free(staging_buf);
        if (pcm) free(pcm);
        if (stereo_pcm) free(stereo_pcm);
        vTaskDelete(NULL);
        return;
    }

    size_t staging_len = 0;
    uint32_t frames_decoded = 0;

    /* 1. Aguarda pré-buffering */
    ESP_LOGI(TAG, "Aguardando pre-buffering (%d KB)...", PREBUFFER_BYTES / 1024);
    while (s_running) {
        if (s_buffered_bytes >= PREBUFFER_BYTES) {
            ESP_LOGI(TAG, "Pre-buffering concluido (%u bytes em PSRAM)! Iniciando reproducao...", (unsigned int)s_buffered_bytes);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    while (s_running) {
        /* 2. Mantém o staging buffer abastecido */
        if (staging_len < 2048) {
            size_t needed = STAGING_BUF_SIZE - staging_len;
            size_t rx_size = 0;
            void *data = xRingbufferReceiveUpTo(s_ringbuf, &rx_size, pdMS_TO_TICKS(50), needed);
            if (data && rx_size > 0) {
                memcpy(staging_buf + staging_len, data, rx_size);
                staging_len += rx_size;
                vRingbufferReturnItem(s_ringbuf, data);
                if (s_buffered_bytes >= rx_size) {
                    s_buffered_bytes -= rx_size;
                } else {
                    s_buffered_bytes = 0;
                }
            } else if (staging_len == 0) {
                /* Buffer totalmente esgotado (underrun): silencia I2S e aguarda rebuffering */
                s_is_playing = false;
                int16_t silence[256 * 2] = {0};
                size_t written = 0;
                audio_kit_i2s_write(silence, sizeof(silence), &written, 50);

                ESP_LOGW(TAG, "Buffer underrun! Aguardando rebuffering (32 KB)...");
                while (s_running && s_buffered_bytes < (32 * 1024)) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                ESP_LOGI(TAG, "Rebuffering concluido. Retomando reproducao.");
                continue;
            }
        }

        /* 3. Decodifica um quadro MP3 */
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&mp3d, staging_buf, (int)staging_len, pcm, &info);

        if (info.frame_bytes > 0) {
            size_t consumed = (size_t)info.frame_bytes;
            if (consumed > staging_len) consumed = staging_len;
            memmove(staging_buf, staging_buf + consumed, staging_len - consumed);
            staging_len -= consumed;

            if (samples > 0) {
                /* Atualiza taxa de amostragem no codec e I2S se detectada */
                if (info.hz > 0) {
                    audio_kit_set_sample_rate(info.hz);
                }

                size_t written = 0;
                if (info.channels == 2) {
                    audio_kit_i2s_write(pcm, samples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
                } else if (info.channels == 1) {
                    /* Converte mono para estéreo intercalado */
                    for (int i = 0; i < samples; i++) {
                        stereo_pcm[i * 2]     = pcm[i];
                        stereo_pcm[i * 2 + 1] = pcm[i];
                    }
                    audio_kit_i2s_write(stereo_pcm, samples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
                }

                frames_decoded++;
                s_is_playing = true;

                if (frames_decoded % 150 == 1) {
                    ESP_LOGI(TAG, "📻 Tocando rádio: %d Hz | %d kbps | %s | Buffer: %u KB | Frames: %u",
                             info.hz, info.bitrate_kbps,
                             (info.channels == 2 ? "Estéreo" : "Mono"),
                             (unsigned int)(s_buffered_bytes / 1024),
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

    free(staging_buf);
    free(pcm);
    free(stereo_pcm);
    s_decode_task_hdl = NULL;
    vTaskDelete(NULL);
}
