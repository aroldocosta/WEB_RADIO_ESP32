#ifndef BOOT_SOUND_H
#define BOOT_SOUND_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia a reprodução assíncrona da vinheta sacra de abertura de Nossa Senhora (Ave Maria)
 */
esp_err_t boot_sound_start(void);

/**
 * @brief Sinaliza para a reprodução da vinheta encerrar suavemente e liberar o barramento I2S
 */
void boot_sound_stop(void);

/**
 * @brief Retorna true enquanto a vinheta de inicialização estiver em execução
 */
bool boot_sound_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // BOOT_SOUND_H
