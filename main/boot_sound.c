#include "boot_sound.h"
#include "boot_sound_data.h"
#include "audio_kit.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BOOT_SOUND";

static TaskHandle_t s_boot_sound_task_hdl = NULL;
static volatile bool s_boot_sound_running = false;
static volatile bool s_boot_sound_playing = false;

static const int16_t s_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t s_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static inline int16_t decode_nibble(uint8_t nibble, int32_t *predicted, int32_t *index)
{
    int32_t step = s_step_table[*index];
    int32_t diff = step >> 3;
    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;

    if (nibble & 8) *predicted -= diff;
    else            *predicted += diff;

    if (*predicted > 32767) *predicted = 32767;
    else if (*predicted < -32768) *predicted = -32768;

    *index += s_index_table[nibble & 0x0F];
    if (*index < 0) *index = 0;
    else if (*index > 88) *index = 88;

    return (int16_t)*predicted;
}

static void boot_sound_task(void *pvParameters)
{
    s_boot_sound_playing = true;
    ESP_LOGI(TAG, "🕊️ Iniciando abertura sacra: Ave Maria de Bach/Gounod (Domínio Público)...");

    int32_t predicted = 0;
    int32_t index = 0;

    /* Buffer para I2S no padrão 44.100 Hz Estéreo (2 canais intercalados) */
    #define CHUNK_SAMPLES 256
    int16_t stereo_buf[CHUNK_SAMPLES * 2];
    size_t chunk_pos = 0;

    for (size_t b = 0; b < BOOT_SOUND_DATA_LEN && s_boot_sound_running; b++) {
        uint8_t byte = s_boot_sound_data[b];

        /* Amostra 1: nibble inferior.
           Enviada 2 vezes para reproduzir 22.050 Hz perfeitamente no clock I2S nativo de 44.100 Hz,
           sem interromper o clock mestre (MCLK) do Codec ES8388. */
        int16_t s1 = decode_nibble(byte & 0x0F, &predicted, &index);
        stereo_buf[chunk_pos * 2]     = s1;
        stereo_buf[chunk_pos * 2 + 1] = s1;
        chunk_pos++;
        stereo_buf[chunk_pos * 2]     = s1;
        stereo_buf[chunk_pos * 2 + 1] = s1;
        chunk_pos++;

        /* Amostra 2: nibble superior. Enviada 2 vezes. */
        int16_t s2 = decode_nibble((byte >> 4) & 0x0F, &predicted, &index);
        stereo_buf[chunk_pos * 2]     = s2;
        stereo_buf[chunk_pos * 2 + 1] = s2;
        chunk_pos++;
        stereo_buf[chunk_pos * 2]     = s2;
        stereo_buf[chunk_pos * 2 + 1] = s2;
        chunk_pos++;

        if (chunk_pos >= CHUNK_SAMPLES) {
            size_t written = 0;
            audio_kit_i2s_write(stereo_buf, sizeof(stereo_buf), &written, pdMS_TO_TICKS(100));
            chunk_pos = 0;
        }
    }

    if (chunk_pos > 0 && s_boot_sound_running) {
        size_t written = 0;
        audio_kit_i2s_write(stereo_buf, chunk_pos * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    }

    /* Envia uma fração de segundo de silêncio para limpar os buffers DMA I2S */
    int16_t silence[128 * 2] = {0};
    size_t written = 0;
    audio_kit_i2s_write(silence, sizeof(silence), &written, pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "🕊️ Abertura sacra concluída.");
    s_boot_sound_playing = false;
    s_boot_sound_task_hdl = NULL;
    vTaskDelete(NULL);
}

esp_err_t boot_sound_start(void)
{
    if (s_boot_sound_task_hdl != NULL) {
        return ESP_OK;
    }
    s_boot_sound_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(boot_sound_task, "boot_sound", 4096, NULL, 5, &s_boot_sound_task_hdl, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

void boot_sound_stop(void)
{
    s_boot_sound_running = false;
    while (s_boot_sound_task_hdl != NULL) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool boot_sound_is_playing(void)
{
    return s_boot_sound_playing;
}
