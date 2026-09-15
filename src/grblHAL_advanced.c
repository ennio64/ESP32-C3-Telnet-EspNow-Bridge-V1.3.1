#include <stdio.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include "grblHAL_advanced.h"
#include "nvs_storage.h"
#include "serial_handler.h"
#include "my_logs.h"

static const char *TAG = "GRBLHAL_ADV";
static int8_t state_pin = -1;
static uint8_t state_pin_mode = 0;
static uint8_t client_mode = 0;
static uint8_t reset_on_disconnect = 1;

static bool current_telnet = false;
static bool current_espnow = false;

static int last_pin_level = -1;   // memorizza l'ultimo livello del pin

// Prototipo della funzione per evitare implicit declaration
static bool is_any_client_connected(void);

// Funzione che aggiorna il pin di stato e logga solo se il livello cambia
static void update_state_pin(void) {
    if (state_pin < 0) return;
    
    bool connected = is_any_client_connected();
    uint8_t pin_level = (state_pin_mode == 0) ? (connected ? 0 : 1) : (connected ? 1 : 0);
    
    if (pin_level != last_pin_level) {
        last_pin_level = pin_level;
        
        const char* mode_str = "";
        switch(client_mode) {
            case 0: mode_str = "ANY"; break;
            case 1: mode_str = "Telnet Only"; break;
            case 2: mode_str = "ESP-NOW Only"; break;
        }
        
        const char* level_str = (pin_level == 0) ? "LOW" : "HIGH";
        const char* conn_str = connected ? "client connected" : "client disconnected";
        
        ESP_LOGI(TAG, "📊 State pin GPIO%d -> %s (%s, client_mode=%s)",
                 state_pin, level_str, conn_str, mode_str);
    }
    
    gpio_set_level(state_pin, pin_level);
    ESP_LOGD(TAG, "State pin set: pin_level=%d", pin_level);
}

void grblHAL_advanced_init(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    state_pin = cfg->state_pin;
    state_pin_mode = cfg->state_pin_mode;
    client_mode = cfg->client_mode;
    reset_on_disconnect = cfg->reset_on_disconnect;
    
    ESP_LOGI(TAG, "Init: state_pin=%d, mode=%d, client_mode=%d, reset=%d",
             state_pin, state_pin_mode, client_mode, reset_on_disconnect);
    
    if (state_pin >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << state_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure GPIO%d", state_pin);
            state_pin = -1;
        } else {
            update_state_pin();  // ora la funzione è già dichiarata
            ESP_LOGI(TAG, "State pin GPIO%d enabled, mode=%s",
                     state_pin, state_pin_mode == 0 ? "LOW when connected" : "HIGH when connected");
        }
    } else {
        ESP_LOGI(TAG, "State pin disabled");
    }
    
    ESP_LOGI(TAG, "Client mode: %s", 
             client_mode == 0 ? "ANY" : (client_mode == 1 ? "TELNET_ONLY" : "ESPNOW_ONLY"));
    ESP_LOGI(TAG, "Reset on disconnect: %s", reset_on_disconnect ? "ENABLED" : "DISABLED");
}

static bool is_any_client_connected(void) {
    switch(client_mode) {
        case 0: return (current_telnet || current_espnow);
        case 1: return current_telnet;
        case 2: return current_espnow;
        default: return false;
    }
}

void grblHAL_advanced_update_state(bool telnet_connected, bool espnow_connected) {
    bool changed = false;
    
    if (current_telnet != telnet_connected) {
        current_telnet = telnet_connected;
        changed = true;
        ESP_LOGI(TAG, "Telnet: %s", telnet_connected ? "CONNECTED" : "DISCONNECTED");
    }
    
    if (current_espnow != espnow_connected) {
        current_espnow = espnow_connected;
        changed = true;
        ESP_LOGI(TAG, "ESP-NOW: %s", espnow_connected ? "PAIRED" : "UNPAIRED");
    }
    
    if (changed) {
        update_state_pin();
    }
}

void grblHAL_advanced_set_reset_on_disconnect(bool enable) {
    reset_on_disconnect = enable ? 1 : 0;
    bridge_config_t *cfg = nvs_storage_get_config_mutable();
    cfg->reset_on_disconnect = reset_on_disconnect;
    nvs_storage_save_config();
    ESP_LOGI(TAG, "Reset on disconnect: %s", enable ? "ENABLED" : "DISABLED");
}

void grblHAL_advanced_check_and_send_reset(void) {
    if (reset_on_disconnect) {
        ESP_LOGI(TAG, "Client disconnected, sending reset (Ctrl+X)");
        uint8_t reset_cmd = 0x18;
        serial_send_data(&reset_cmd, 1);
    } else {
        ESP_LOGD(TAG, "Client disconnected, reset disabled");
    }
}