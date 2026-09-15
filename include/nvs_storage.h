#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_KNOWN_NETWORKS 10
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_AP_SSID_LEN 32
#define MAX_AP_PASSWORD_LEN 64
#define MAX_IP_STR_LEN 16

typedef struct {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASSWORD_LEN];
} network_t;

typedef struct {
    network_t networks[MAX_KNOWN_NETWORKS];
    uint8_t network_count;
    
    // Configurazione AP
    uint8_t ap_channel;
    char ap_ssid[MAX_AP_SSID_LEN];
    char ap_password[MAX_AP_PASSWORD_LEN];
    
    // Configurazione IP statico
    char static_ip[MAX_IP_STR_LEN];
    char static_gateway[MAX_IP_STR_LEN];
    char static_netmask[MAX_IP_STR_LEN];
    bool use_static_ip;
    
    // Debug level (0=off, 1=basic, 2=verbose)
    uint8_t debug_level;
    
    // ========== GrblHAL Advanced ==========
    int8_t state_pin;           // GPIO per pin di stato (-1 = disabilitato)
    uint8_t state_pin_mode;     // 0 = LOW quando connesso, 1 = HIGH quando connesso
    uint8_t client_mode;        // 0 = Any Client, 1 = Telnet Only, 2 = ESP-NOW Only
    uint8_t reset_on_disconnect; // 1 = invia reset (Ctrl-X), 0 = non inviare

    // ========== ESP-NOW logical outputs ==========
    // -1 = Disabled, otherwise GPIO number.
    int8_t espnow_output_pins[7];

    // GPIO polarity for ESP-NOW logical outputs.
    // Bit N = 1 -> GPIO N is Active LOW.
    // Bit N = 0 -> GPIO N is Active HIGH.
    uint32_t espnow_gpio_active_low_mask;

    // Associazione dei sensori wireless alle funzioni logiche.
    // Bit 0..6 = funzione associata; MAC valido in espnow_sensor_macs[].
    uint8_t espnow_sensor_binding_mask;
    uint8_t espnow_sensor_macs[7][6];
    // ==============================================
    
    // Flag per prima configurazione
    bool configured;
} bridge_config_t;

// Inizializza NVS e carica configurazione
void nvs_storage_init(void);

// Salva configurazione corrente
bool nvs_storage_save_config(void);

// Carica configurazione salvata
bool nvs_storage_load_config(bridge_config_t *config);

// Aggiunge una rete alla lista
bool nvs_storage_add_network(const char *ssid, const char *password);

// Rimuove una rete dalla lista
bool nvs_storage_remove_network(const char *ssid);

// Ottiene la configurazione corrente (sola lettura)
const bridge_config_t* nvs_storage_get_config(void);

// Ottiene la configurazione modificabile
bridge_config_t* nvs_storage_get_config_mutable(void);

// Resetta alla configurazione di default
void nvs_storage_reset_default(void);

#endif