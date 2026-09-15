#include <string.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "nvs_storage.h"
#include "MyWiFiData.h"
#include "config.h"

static const char *TAG = "NVS_STORAGE";
static bridge_config_t g_config;
static nvs_handle_t nvs_handle_storage = 0;

typedef struct {
    network_t networks[MAX_KNOWN_NETWORKS];
    uint8_t network_count;
    uint8_t ap_channel;
    char ap_ssid[MAX_AP_SSID_LEN];
    char ap_password[MAX_AP_PASSWORD_LEN];
    char static_ip[MAX_IP_STR_LEN];
    char static_gateway[MAX_IP_STR_LEN];
    char static_netmask[MAX_IP_STR_LEN];
    bool use_static_ip;
    uint8_t debug_level;
    int8_t state_pin;
    uint8_t state_pin_mode;
    uint8_t client_mode;
    uint8_t reset_on_disconnect;
    bool configured;
} legacy_bridge_config_t;

// Configurazione precedente alla gestione delle associazioni SENSOR.
// Contiene gia' gli output GPIO ESP-NOW ma non i MAC dei sensori.
typedef struct {
    network_t networks[MAX_KNOWN_NETWORKS];
    uint8_t network_count;
    uint8_t ap_channel;
    char ap_ssid[MAX_AP_SSID_LEN];
    char ap_password[MAX_AP_PASSWORD_LEN];
    char static_ip[MAX_IP_STR_LEN];
    char static_gateway[MAX_IP_STR_LEN];
    char static_netmask[MAX_IP_STR_LEN];
    bool use_static_ip;
    uint8_t debug_level;
    int8_t state_pin;
    uint8_t state_pin_mode;
    uint8_t client_mode;
    uint8_t reset_on_disconnect;
    int8_t espnow_output_pins[7];
    bool configured;
} legacy_bridge_config_v2_t;


// Configurazione immediatamente precedente alla gestione della polarità GPIO.
// È uguale a bridge_config_t ma senza espnow_gpio_active_low_mask.
typedef struct {
    network_t networks[MAX_KNOWN_NETWORKS];
    uint8_t network_count;
    uint8_t ap_channel;
    char ap_ssid[MAX_AP_SSID_LEN];
    char ap_password[MAX_AP_PASSWORD_LEN];
    char static_ip[MAX_IP_STR_LEN];
    char static_gateway[MAX_IP_STR_LEN];
    char static_netmask[MAX_IP_STR_LEN];
    bool use_static_ip;
    uint8_t debug_level;
    int8_t state_pin;
    uint8_t state_pin_mode;
    uint8_t client_mode;
    uint8_t reset_on_disconnect;
    int8_t espnow_output_pins[7];
    uint8_t espnow_sensor_binding_mask;
    uint8_t espnow_sensor_macs[7][6];
    bool configured;
} legacy_bridge_config_v3_t;

#define ESPNOW_GPIO_ACTIVE_LOW_VALID_MASK     ((1UL << 4) | (1UL << 5) | (1UL << 6) | (1UL << 7) | (1UL << 10))

// Inizializza la configurazione di default
static void init_default_config(bridge_config_t *config) {
    memset(config, 0, sizeof(bridge_config_t));
    
    // Carica le reti da MyWiFiData.h
    for (int i = 0; i < (int)DEFAULT_NETWORKS_COUNT && i < MAX_KNOWN_NETWORKS; i++) {
        strncpy(config->networks[i].ssid, default_networks[i].ssid, MAX_SSID_LEN - 1);
        config->networks[i].ssid[MAX_SSID_LEN - 1] = '\0';
        strncpy(config->networks[i].password, default_networks[i].password, MAX_PASSWORD_LEN - 1);
        config->networks[i].password[MAX_PASSWORD_LEN - 1] = '\0';
        config->network_count++;
    }
    
    // Imposta i valori di default
    config->ap_channel = 6;
    strcpy(config->ap_ssid, "ESP32-C3-Serial-Bridge");
    strcpy(config->ap_password, "12345678");
    strcpy(config->static_ip, "192.168.1.123");
    strcpy(config->static_gateway, "192.168.1.1");
    strcpy(config->static_netmask, "255.255.255.0");
    config->use_static_ip = true;
    config->debug_level = ENABLE_DEBUG_LOGS;
    
    // ========== GrblHAL Advanced defaults ==========
    config->state_pin = -1;              // Disabilitato
    config->state_pin_mode = 1;          // HIGH quando connesso
    config->client_mode = 0;             // Telnet Only (0=Any, 1=Telnet Only, 2=ESP-NOW Only)
    config->reset_on_disconnect = 1;     // Abilitato (invia reset)
    for (int i = 0; i < 7; i++) {
        config->espnow_output_pins[i] = -1;
        memset(config->espnow_sensor_macs[i], 0, 6);
    }
    config->espnow_sensor_binding_mask = 0;
    config->espnow_gpio_active_low_mask = 0;
    // ==============================================
    
    config->configured = true;
    
    ESP_LOGI(TAG, "Default config initialized with %d networks, debug_level=%d", 
             config->network_count, config->debug_level);
}

void nvs_storage_init(void) {
    bridge_config_t default_config;
    init_default_config(&default_config);
    
    esp_err_t err = nvs_open("bridge_cfg", NVS_READWRITE, &nvs_handle_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore apertura NVS: %d", err);
        return;
    }
    
    // Carica configurazione o usa default
    if (!nvs_storage_load_config(&g_config)) {
        ESP_LOGI(TAG, "Nessuna config salvata, uso default con %d reti", default_config.network_count);
        memcpy(&g_config, &default_config, sizeof(bridge_config_t));
        nvs_storage_save_config();
    } else {
        // Verifica che l'IP statico sia valido, altrimenti correggi
        if (strlen(g_config.static_ip) == 0 || strcmp(g_config.static_ip, "0.0.0.0") == 0) {
            ESP_LOGW(TAG, "IP statico non valido, correggo a 192.168.1.123");
            strcpy(g_config.static_ip, "192.168.1.123");
            strcpy(g_config.static_gateway, "192.168.1.1");
            strcpy(g_config.static_netmask, "255.255.255.0");
            g_config.use_static_ip = true;
            nvs_storage_save_config();
        }
        
        // Se debug_level non è valido, correggi
        if (g_config.debug_level > 2) {
            ESP_LOGW(TAG, "debug_level non valido (%d), resetto a 0", g_config.debug_level);
            g_config.debug_level = 0;
            nvs_storage_save_config();
        }
        
        // ========== Verifica campi GrblHAL (migrazione vecchie config) ==========
        if (g_config.state_pin < -1 || g_config.state_pin > 10) {
            ESP_LOGW(TAG, "state_pin non valido (%d), resetto a -1", g_config.state_pin);
            g_config.state_pin = -1;
            nvs_storage_save_config();
        }
        if (g_config.state_pin_mode > 1) {
            ESP_LOGW(TAG, "state_pin_mode non valido (%d), resetto a 0", g_config.state_pin_mode);
            g_config.state_pin_mode = 0;
            nvs_storage_save_config();
        }
        if (g_config.client_mode > 2) {
            ESP_LOGW(TAG, "client_mode non valido (%d), resetto a 0", g_config.client_mode);
            g_config.client_mode = 0;
            nvs_storage_save_config();
        }
        if (g_config.reset_on_disconnect > 1) {
            ESP_LOGW(TAG, "reset_on_disconnect non valido (%d), resetto a 1", g_config.reset_on_disconnect);
            g_config.reset_on_disconnect = 1;
            nvs_storage_save_config();
        }

        // Valida gli output ESP-NOW. GPIO4,5,6,7,10 sono quelli esposti dalla UI.
        for (int i = 0; i < 7; i++) {
            int pin = g_config.espnow_output_pins[i];
            bool valid = (pin == -1 || pin == 4 || pin == 5 || pin == 6 || pin == 7 || pin == 10);
            if (!valid) {
                ESP_LOGW(TAG, "ESP-NOW output %d: GPIO%d non valido, imposto Disabled", i, pin);
                g_config.espnow_output_pins[i] = -1;
            } else if (pin >= 0 && pin == g_config.state_pin) {
                ESP_LOGW(TAG, "ESP-NOW output %d usa il GPIO%d dello State Pin: imposto Disabled", i, pin);
                g_config.espnow_output_pins[i] = -1;
            }
        }
        // Non permettere di assegnare lo stesso GPIO a due funzioni.
        for (int i = 0; i < 7; i++) {
            if (g_config.espnow_output_pins[i] < 0) continue;
            for (int j = i + 1; j < 7; j++) {
                if (g_config.espnow_output_pins[i] == g_config.espnow_output_pins[j]) {
                    ESP_LOGW(TAG, "GPIO%d assegnato sia a output %d che %d: disabilito il secondo",
                             g_config.espnow_output_pins[j], i, j);
                    g_config.espnow_output_pins[j] = -1;
                    }
            }
        }
        // Valida la mask di polarità: vengono accettati solo i GPIO esposti dalla UI.
        uint32_t sanitized_polarity =
            g_config.espnow_gpio_active_low_mask & ESPNOW_GPIO_ACTIVE_LOW_VALID_MASK;
        if (sanitized_polarity != g_config.espnow_gpio_active_low_mask) {
            ESP_LOGW(TAG, "ESP-NOW Active LOW mask non valida: 0x%08lX -> 0x%08lX",
                     (unsigned long)g_config.espnow_gpio_active_low_mask,
                     (unsigned long)sanitized_polarity);
            g_config.espnow_gpio_active_low_mask = sanitized_polarity;
            nvs_storage_save_config();
        }

        if (g_config.espnow_sensor_binding_mask & (uint8_t)~0x7Fu) {
            g_config.espnow_sensor_binding_mask &= 0x7Fu;
            for (int i = 0; i < 7; i++) {
                if (!(g_config.espnow_sensor_binding_mask & (1u << i)))
                    memset(g_config.espnow_sensor_macs[i], 0, 6);
            }
        }
        // ========================================================================
        
        ESP_LOGI(TAG, "Configurazione caricata: %d reti, IP=%s, debug_level=%d", 
                 g_config.network_count, g_config.static_ip, g_config.debug_level);
    }
}

bool nvs_storage_save_config(void) {
    if (nvs_handle_storage == 0) return false;
    
    esp_err_t err = nvs_set_blob(nvs_handle_storage, "config", &g_config, sizeof(bridge_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore salvataggio: %d", err);
        return false;
    }
    
    err = nvs_commit(nvs_handle_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore commit: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Configurazione salvata");
    return true;
}

bool nvs_storage_load_config(bridge_config_t *config) {
    if (nvs_handle_storage == 0) return false;
    
    size_t size = 0;
    esp_err_t err = nvs_get_blob(nvs_handle_storage, "config", NULL, &size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Nessuna configurazione salvata in NVS (err=%d)", err);
        return false;
    }

    if (size == sizeof(bridge_config_t)) {
        size_t full_size = sizeof(bridge_config_t);
        err = nvs_get_blob(nvs_handle_storage, "config", config, &full_size);
    } else if (size == sizeof(legacy_bridge_config_v3_t)) {
        legacy_bridge_config_v3_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        err = nvs_get_blob(nvs_handle_storage, "config", &legacy, &legacy_size);
        if (err == ESP_OK) {
            memset(config, 0, sizeof(*config));
            memcpy(config->networks, legacy.networks, sizeof(legacy.networks));
            config->network_count = legacy.network_count;
            config->ap_channel = legacy.ap_channel;
            memcpy(config->ap_ssid, legacy.ap_ssid, sizeof(config->ap_ssid));
            memcpy(config->ap_password, legacy.ap_password, sizeof(config->ap_password));
            memcpy(config->static_ip, legacy.static_ip, sizeof(config->static_ip));
            memcpy(config->static_gateway, legacy.static_gateway, sizeof(config->static_gateway));
            memcpy(config->static_netmask, legacy.static_netmask, sizeof(config->static_netmask));
            config->use_static_ip = legacy.use_static_ip;
            config->debug_level = legacy.debug_level;
            config->state_pin = legacy.state_pin;
            config->state_pin_mode = legacy.state_pin_mode;
            config->client_mode = legacy.client_mode;
            config->reset_on_disconnect = legacy.reset_on_disconnect;
            memcpy(config->espnow_output_pins, legacy.espnow_output_pins,
                   sizeof(config->espnow_output_pins));
            config->espnow_sensor_binding_mask = legacy.espnow_sensor_binding_mask;
            memcpy(config->espnow_sensor_macs, legacy.espnow_sensor_macs,
                   sizeof(config->espnow_sensor_macs));
            config->espnow_gpio_active_low_mask = 0;
            config->configured = legacy.configured;
            ESP_LOGI(TAG, "Configurazione NVS precedente alla polarità GPIO rilevata: Active LOW mask impostata a 0");
        }
    } else if (size == sizeof(legacy_bridge_config_v2_t)) {
        legacy_bridge_config_v2_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        err = nvs_get_blob(nvs_handle_storage, "config", &legacy, &legacy_size);
        if (err == ESP_OK) {
            memset(config, 0, sizeof(*config));
            memcpy(config->networks, legacy.networks, sizeof(legacy.networks));
            config->network_count = legacy.network_count;
            config->ap_channel = legacy.ap_channel;
            memcpy(config->ap_ssid, legacy.ap_ssid, sizeof(config->ap_ssid));
            memcpy(config->ap_password, legacy.ap_password, sizeof(config->ap_password));
            memcpy(config->static_ip, legacy.static_ip, sizeof(config->static_ip));
            memcpy(config->static_gateway, legacy.static_gateway, sizeof(config->static_gateway));
            memcpy(config->static_netmask, legacy.static_netmask, sizeof(config->static_netmask));
            config->use_static_ip = legacy.use_static_ip;
            config->debug_level = legacy.debug_level;
            config->state_pin = legacy.state_pin;
            config->state_pin_mode = legacy.state_pin_mode;
            config->client_mode = legacy.client_mode;
            config->reset_on_disconnect = legacy.reset_on_disconnect;
            memcpy(config->espnow_output_pins, legacy.espnow_output_pins, sizeof(config->espnow_output_pins));
            config->espnow_sensor_binding_mask = 0;
            memset(config->espnow_sensor_macs, 0, sizeof(config->espnow_sensor_macs));
            config->espnow_gpio_active_low_mask = 0;
            config->configured = legacy.configured;
            ESP_LOGI(TAG, "Configurazione NVS precedente rilevata: associazioni SENSOR inizializzate vuote");
        }
    } else if (size == sizeof(legacy_bridge_config_t)) {
        legacy_bridge_config_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        err = nvs_get_blob(nvs_handle_storage, "config", &legacy, &legacy_size);
        if (err == ESP_OK) {
            memset(config, 0, sizeof(*config));
            memcpy(config, &legacy, sizeof(legacy));
            for (int i = 0; i < 7; i++) {
                config->espnow_output_pins[i] = -1;
                memset(config->espnow_sensor_macs[i], 0, 6);
            }
            config->espnow_sensor_binding_mask = 0;
            config->espnow_gpio_active_low_mask = 0;
            ESP_LOGI(TAG, "Configurazione NVS legacy rilevata: nuovi output ESP-NOW e associazioni sensori impostati su Disabled");
        }
    } else {
        ESP_LOGW(TAG, "Dimensione configurazione NVS non riconosciuta: %u byte", (unsigned)size);
        return false;
    }
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "📖 CONFIGURAZIONE CARICATA DA NVS:");
        if (config->network_count > MAX_KNOWN_NETWORKS) {
            ESP_LOGW(TAG, "network_count non valido (%d), limito a %d", config->network_count, MAX_KNOWN_NETWORKS);
            config->network_count = MAX_KNOWN_NETWORKS;
        }
        ESP_LOGI(TAG, "   Reti configurate: %d", config->network_count);
        for (int i = 0; i < config->network_count; i++) {
            ESP_LOGI(TAG, "   [%d] SSID: '%s'", i, config->networks[i].ssid);
        }
        ESP_LOGI(TAG, "   AP SSID: '%s'", config->ap_ssid);
        ESP_LOGI(TAG, "   AP Channel: %d", config->ap_channel);
        ESP_LOGI(TAG, "   Static IP: %s (use: %s)", config->static_ip, 
                 config->use_static_ip ? "YES" : "NO");
        ESP_LOGI(TAG, "   Debug Level: %d", config->debug_level);
        ESP_LOGI(TAG, "   --- GrblHAL Advanced ---");
        ESP_LOGI(TAG, "   State Pin: %d", config->state_pin);
        ESP_LOGI(TAG, "   State Pin Mode: %d", config->state_pin_mode);
        ESP_LOGI(TAG, "   Client Mode: %d", config->client_mode);
        ESP_LOGI(TAG, "   Reset on Disconnect: %d", config->reset_on_disconnect);
        ESP_LOGI(TAG, "   ESP-NOW GPIO Active LOW mask: 0x%08lX", (unsigned long)config->espnow_gpio_active_low_mask);
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGI(TAG, "Nessuna configurazione salvata in NVS (err=%d)", err);
    }
    
    return (err == ESP_OK);
}

bool nvs_storage_add_network(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "📝 AGGIUNTA NUOVA RETE: SSID='%s'", ssid);
    
    if (g_config.network_count >= MAX_KNOWN_NETWORKS) {
        ESP_LOGW(TAG, "Limite reti raggiunto (%d)", MAX_KNOWN_NETWORKS);
        return false;
    }
    
    // Controlla se esiste già
    for (int i = 0; i < g_config.network_count; i++) {
        if (strcmp(g_config.networks[i].ssid, ssid) == 0) {
            // Aggiorna password
            strncpy(g_config.networks[i].password, password, MAX_PASSWORD_LEN - 1);
            g_config.networks[i].password[MAX_PASSWORD_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Password aggiornata per rete: %s", ssid);
            return nvs_storage_save_config();
        }
    }
    
    // Aggiungi nuova
    strncpy(g_config.networks[g_config.network_count].ssid, ssid, MAX_SSID_LEN - 1);
    g_config.networks[g_config.network_count].ssid[MAX_SSID_LEN - 1] = '\0';
    strncpy(g_config.networks[g_config.network_count].password, password, MAX_PASSWORD_LEN - 1);
    g_config.networks[g_config.network_count].password[MAX_PASSWORD_LEN - 1] = '\0';
    
    g_config.network_count++;
    g_config.configured = true;
    
    ESP_LOGI(TAG, "✅ Rete salvata in NVS: %s", ssid);
    return nvs_storage_save_config();
}

bool nvs_storage_remove_network(const char *ssid) {
    for (int i = 0; i < g_config.network_count; i++) {
        if (strcmp(g_config.networks[i].ssid, ssid) == 0) {
            for (int j = i; j < g_config.network_count - 1; j++) {
                memcpy(&g_config.networks[j], &g_config.networks[j + 1], sizeof(network_t));
            }
            g_config.network_count--;
            ESP_LOGI(TAG, "Rete rimossa: %s", ssid);
            return nvs_storage_save_config();
        }
    }
    
    ESP_LOGW(TAG, "Rete non trovata: %s", ssid);
    return false;
}

const bridge_config_t* nvs_storage_get_config(void) {
    return &g_config;
}

bridge_config_t* nvs_storage_get_config_mutable(void) {
    return &g_config;
}

void nvs_storage_reset_default(void) {
    ESP_LOGI(TAG, "Reset configurazione a default");
    bridge_config_t default_config;
    init_default_config(&default_config);
    memcpy(&g_config, &default_config, sizeof(bridge_config_t));
    nvs_storage_save_config();
}