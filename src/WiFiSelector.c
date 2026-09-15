#include <string.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "WiFiSelector.h"
#include "nvs_storage.h"
#include "my_logs.h"

static const char *TAG = "WIFI_SELECTOR";
static char selected_ssid[32] = {0};
static char selected_password[64] = {0};

bool selectBestWiFi(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    if (cfg->network_count == 0) {
        ESP_LOGW(TAG, "Nessuna rete configurata in NVS");
        return false;
    }
    
    ESP_LOGI(TAG, "=== Scansione reti WiFi ===");
    
    // NON cambiare modalità - è già in AP+STA da wifi_init()
    // La scansione funziona comunque in modalità AP+STA
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Configurazione scansione
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };
    
    // Avvia scansione (sincrona)
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    // Ottieni numero di reti trovate
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    if (ap_count == 0) {
        ESP_LOGW(TAG, "Nessuna rete trovata");
        return false;
    }
    
    // Alloca buffer per i risultati
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        ESP_LOGE(TAG, "Errore allocazione memoria");
        return false;
    }
    
    // Ottieni i record delle reti
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
    ESP_LOGI(TAG, "Trovate %d reti", ap_count);
    
    // Cerca la migliore rete conosciuta
    int best_rssi = -999;
    int best_index = -1;
    
    for (int i = 0; i < ap_count; i++) {
        for (int k = 0; k < cfg->network_count; k++) {
            if (strcmp((char*)ap_records[i].ssid, cfg->networks[k].ssid) == 0) {
                ESP_LOGI(TAG, "Rete conosciuta: %s (RSSI: %d dBm)", 
                         cfg->networks[k].ssid, ap_records[i].rssi);
                
                if (ap_records[i].rssi > best_rssi) {
                    best_rssi = ap_records[i].rssi;
                    best_index = k;
                }
                break;
            }
        }
    }
    
    free(ap_records);
    
    if (best_index < 0) {
        ESP_LOGW(TAG, "Nessuna rete conosciuta disponibile");
        return false;
    }
    
    // Salva SSID e password selezionati
    strncpy(selected_ssid, cfg->networks[best_index].ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';
    
    strncpy(selected_password, cfg->networks[best_index].password, sizeof(selected_password) - 1);
    selected_password[sizeof(selected_password) - 1] = '\0';
    
    ESP_LOGI(TAG, "✅ Selezionata rete: %s (RSSI: %d dBm)", selected_ssid, best_rssi);
    return true;
}

const char* getSelectedSSID(void) {
    return selected_ssid;
}

const char* getSelectedPassword(void) {
    return selected_password;
}