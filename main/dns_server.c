#include "dns_server.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DNS_SERVER";

static int s_dns_sock = -1;
static TaskHandle_t s_dns_task_handle = NULL;
static uint8_t s_resolved_ip[4] = {10, 10, 10, 1};

static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[256];
    uint8_t tx_buffer[256];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "Servidor DNS Captive Portal escutando na porta 53 (Redirecionando para %d.%d.%d.%d)...",
             s_resolved_ip[0], s_resolved_ip[1], s_resolved_ip[2], s_resolved_ip[3]);

    while (s_dns_sock >= 0) {
        int len = recvfrom(s_dns_sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &client_addr_len);

        if (len < 12) {
            continue; // Pacote menor que o cabeçalho DNS
        }

        // Copia a pergunta recebida
        memcpy(tx_buffer, rx_buffer, len);

        // Ajusta Flags de Resposta no cabeçalho DNS:
        // QR=1 (Response), Opcode=0, AA=1, RD=1, RA=1, RCODE=0 -> 0x8180
        tx_buffer[2] = 0x81;
        tx_buffer[3] = 0x80;

        // QDCOUNT permanece o mesmo recebido (tx_buffer[4..5])
        // ANCOUNT = 1 resposta (tx_buffer[6..7])
        tx_buffer[6] = 0x00;
        tx_buffer[7] = 0x01;
        // NSCOUNT = 0
        tx_buffer[8] = 0x00;
        tx_buffer[9] = 0x00;
        // ARCOUNT = 0
        tx_buffer[10] = 0x00;
        tx_buffer[11] = 0x00;

        // Anexa o Registro de Resposta (Answer A Record) ao final da consulta
        int idx = len;
        // Ponteiro de compressão para o nome na pergunta (offset 12 = 0x0C)
        tx_buffer[idx++] = 0xC0;
        tx_buffer[idx++] = 0x0C;

        // TYPE = A (0x0001)
        tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x01;

        // CLASS = IN (0x0001)
        tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x01;

        // TTL = 60 segundos (0x0000003C)
        tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x3C;

        // RDLENGTH = 4 bytes (tamanho do IPv4)
        tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x04;

        // RDATA = IP resolvido (10.10.10.1)
        tx_buffer[idx++] = s_resolved_ip[0];
        tx_buffer[idx++] = s_resolved_ip[1];
        tx_buffer[idx++] = s_resolved_ip[2];
        tx_buffer[idx++] = s_resolved_ip[3];

        sendto(s_dns_sock, tx_buffer, idx, 0, (struct sockaddr *)&client_addr, client_addr_len);
    }

    ESP_LOGI(TAG, "Servidor DNS encerrado.");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(const uint8_t resolved_ip_bytes[4])
{
    if (resolved_ip_bytes) {
        memcpy(s_resolved_ip, resolved_ip_bytes, 4);
    }

    if (s_dns_sock >= 0) {
        return ESP_OK; // Já está rodando
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket DNS: errno %d", errno);
        return ESP_FAIL;
    }

    if (bind(s_dns_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Falha ao associar socket DNS na porta 53: errno %d", errno);
        close(s_dns_sock);
        s_dns_sock = -1;
        return ESP_FAIL;
    }

    xTaskCreate(dns_server_task, "dns_server_task", 3072, NULL, 5, &s_dns_task_handle);
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_dns_sock >= 0) {
        int sock = s_dns_sock;
        s_dns_sock = -1;
        close(sock);
    }
    s_dns_task_handle = NULL;
}
