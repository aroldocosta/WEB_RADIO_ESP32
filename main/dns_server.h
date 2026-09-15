#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia o servidor DNS Captive Portal na porta UDP 53.
 *        Qualquer requisição de resolução de nome receberá como resposta o IP especificado.
 * @param resolved_ip_bytes Array de 4 bytes do endereço IP IPv4 (ex: {10, 10, 10, 1})
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t dns_server_start(const uint8_t resolved_ip_bytes[4]);

/**
 * @brief Para o servidor DNS
 */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // DNS_SERVER_H
