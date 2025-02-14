#ifndef THINGSPEAK_UTILS_H
#define THINGSPEAK_UTILS_H

#include "pico/cyw43_arch.h"  // Para Wi-Fi (se estiver usando CYW43)
#include "lwip/pbuf.h"        // Cabeçalho do lwIP para buffers de pacotes
#include "lwip/tcp.h"         // Cabeçalho do lwIP para TCP
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"    // Cabeçalho do lwIP para resolução de DNS
#include <stdio.h>
#include <string.h>
#include "config.h"

// Função para obter a API Key do ThingSpeak
const char* get_thingspeak_api_key() {
    return THINGSPEAK_API_KEY;
}

// Função para obter a URL do ThingSpeak
const char* get_thingspeak_url() { 
    return THINGSPEAK_URL;  // Deve retornar apenas o domínio, sem 'http://'
}

// Função para obter o SSID do Wi-Fi
const char* get_wifi_ssid() {
    return WIFI_SSID;
}

// Função para obter a senha do Wi-Fi
const char* get_wifi_password() {
    return WIFI_PASSWORD;
}

// Função para enviar dados ao ThingSpeak
void enviar_dados_thingspeak(int num_pessoas) {
    // Inicializa o Wi-Fi (se estiver usando CYW43)
    if (cyw43_arch_init()) {
        printf("Erro ao inicializar Wi-Fi\n");
        return;
    }
    cyw43_arch_enable_sta_mode();

    // Conecta ao Wi-Fi
    if (cyw43_arch_wifi_connect_timeout_ms(get_wifi_ssid(), get_wifi_password(), CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Erro ao conectar ao Wi-Fi\n");
        return;
    }

    // Cria um socket TCP
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar socket TCP\n");
        return;
    }

    // Converte o nome do host (URL) para um endereço IP
    ip_addr_t server_ip;
    if (dns_gethostbyname(get_thingspeak_url(), &server_ip, NULL, NULL) != ERR_OK) {
        printf("Erro ao resolver o nome do host\n");
        return;
    }

    // Conecta ao servidor ThingSpeak (porta 80)
    if (tcp_connect(pcb, &server_ip, 80, NULL) != ERR_OK) {
        printf("Erro ao conectar ao servidor\n");
        tcp_close(pcb);
        return;
    }

    // Prepara a requisição HTTP GET
    char request[256];
    snprintf(request, sizeof(request), "GET /update?api_key=%s&field1=%d HTTP/1.1\r\nHost: %s\r\n\r\n", 
             get_thingspeak_api_key(), num_pessoas, get_thingspeak_url()); // Use o domínio sem 'http://'
    printf("Request: %s", request);
    // Envia a requisição HTTP
    err_t err = tcp_write(pcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Erro ao enviar requisição: %d\n", err);
        tcp_close(pcb);
        return;
    }

    // Fecha a conexão
    tcp_close(pcb);
    cyw43_arch_deinit();  // Desliga o Wi-Fi
}

#endif // THINGSPEAK_UTILS_H
