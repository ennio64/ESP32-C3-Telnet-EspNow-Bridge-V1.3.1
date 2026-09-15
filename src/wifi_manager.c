#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "wifi_manager.h"
#include "WiFiSelector.h"
#include "nvs_storage.h"
#include "config.h"
#include "my_logs.h"

static const char *TAG = "WIFI_MANAGER";
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int STA_START_BIT = BIT1;
const int DISCONNECTED_BIT = BIT2;      // indica che siamo in stato disconnesso

static int reconnect_attempts = 0;
static int current_backoff_ms = 1000;   // backoff iniziale
static TaskHandle_t reconnect_task_handle = NULL;

// Prototipi
static void wifi_reconnect_task(void *pvParameters);
static void start_reconnect_task(void);

// Event handler migliorato
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "✅ STA start event, interface ready");
        xEventGroupSetBits(wifi_event_group, STA_START_BIT);
        xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGW(TAG, "WiFi disconnesso da %s, reason=%d", disconnected->ssid, disconnected->reason);
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
        
        // Evita riconnessioni immediate troppo frequenti (backoff)
        start_reconnect_task();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✅ IP ottenuto: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
        // Reset contatori di riconnessione
        reconnect_attempts = 0;
        current_backoff_ms = 1000;
        if (reconnect_task_handle) {
            vTaskDelete(reconnect_task_handle);
            reconnect_task_handle = NULL;
        }
    }
}

// Task di riconnessione con backoff esponenziale (senza bloccare l'event loop)
static void wifi_reconnect_task(void *pvParameters) {
    while (1) {
        // Attendi il backoff prima di ritentare
        vTaskDelay(pdMS_TO_TICKS(current_backoff_ms));
        
        // Verifica se siamo già connessi
        if (xEventGroupGetBits(wifi_event_group) & CONNECTED_BIT) {
            ESP_LOGI(TAG, "Già connesso, interrompo task riconnessione");
            break;
        }
        
        ESP_LOGI(TAG, "Tentativo di riconnessione #%d (backoff %d ms)", 
                 reconnect_attempts + 1, current_backoff_ms);
        esp_err_t err = esp_wifi_connect();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Richiesta di connessione inviata");
            // Attendiamo per vedere se la connessione riesce (max 10 secondi)
            EventBits_t bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | DISCONNECTED_BIT,
                                                   pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
            if (bits & CONNECTED_BIT) {
                ESP_LOGI(TAG, "Riconnessione riuscita");
                break;
            } else {
                ESP_LOGW(TAG, "Riconnessione fallita (timeout)");
            }
        } else {
            ESP_LOGE(TAG, "esp_wifi_connect() fallito: %s", esp_err_to_name(err));
        }
        
        // Backoff esponenziale con limite massimo (60 secondi)
        reconnect_attempts++;
        current_backoff_ms = (current_backoff_ms * 2);
        if (current_backoff_ms > 60000) current_backoff_ms = 60000;
    }
    
    reconnect_task_handle = NULL;
    vTaskDelete(NULL);
}

static void start_reconnect_task(void) {
    if (reconnect_task_handle == NULL) {
        xTaskCreate(wifi_reconnect_task, "wifi_reconnect", 3072, NULL, 3, &reconnect_task_handle);
    }
}

void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi inizializzato (AP+STA)");
}

bool wifi_wait_for_sta_start(TickType_t timeout_ticks) {
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, STA_START_BIT,
                                           pdFALSE, pdTRUE, timeout_ticks);
    if (bits & STA_START_BIT) {
        ESP_LOGI(TAG, "STA ready");
        return true;
    }
    ESP_LOGW(TAG, "Timeout attesa STA_START_BIT");
    return false;
}

void wifi_start_ap_with_config(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    wifi_config_t ap_config = {0};
    ap_config.ap.ssid_len = strlen(cfg->ap_ssid);
    ap_config.ap.max_connection = WIFI_AP_MAX_CONNECT;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.channel = cfg->ap_channel;
    
    strncpy((char*)ap_config.ap.ssid, cfg->ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, cfg->ap_password, sizeof(ap_config.ap.password) - 1);
    
    if (strlen(cfg->ap_password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "AP avviato: SSID=%s, canale=%d", cfg->ap_ssid, cfg->ap_channel);
}

void wifi_start_sta(const char *ssid, const char *password) {
    wifi_config_t sta_config = {0};
    
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    ESP_LOGI(TAG, "Connessione a SSID: %s in corso...", ssid);
}

bool wifi_is_connected(void) {
    return (xEventGroupGetBits(wifi_event_group) & CONNECTED_BIT) != 0;
}

const char* wifi_get_ip(void) {
    static char ip_str[16] = "0.0.0.0";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && wifi_is_connected()) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            return ip_str;
        }
    }
    return ip_str;
}

void set_static_ip_from_config(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    if (!cfg->use_static_ip) {
        ESP_LOGI(TAG, "DHCP mode");
        return;
    }
    
    if (strlen(cfg->static_ip) == 0 || strcmp(cfg->static_ip, "0.0.0.0") == 0) {
        ESP_LOGW(TAG, "IP statico non valido, uso DHCP");
        return;
    }
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "netif STA non disponibile");
        return;
    }
    
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton(cfg->static_ip);
    ip_info.netmask.addr = esp_ip4addr_aton(cfg->static_netmask);
    ip_info.gw.addr = esp_ip4addr_aton(cfg->static_gateway);
    
    if (ip_info.ip.addr == 0 || ip_info.ip.addr == 0xFFFFFFFF) {
        ESP_LOGW(TAG, "IP statico invalido: %s", cfg->static_ip);
        return;
    }
    
    esp_netif_dhcpc_stop(netif);
    if (esp_netif_set_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "IP statico impostato: %s", cfg->static_ip);
    } else {
        ESP_LOGE(TAG, "Impostazione IP statico fallita");
    }
}

void wifi_auto_connect_from_nvs(void) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    if (cfg->network_count == 0) {
        ESP_LOGW(TAG, "Nessuna rete configurata");
        return;
    }
    
    ESP_LOGI(TAG, "Avvio selezione automatica WiFi...");
    
    // Attendi che l'interfaccia STA sia pronta (max 10 secondi)
    if (!wifi_wait_for_sta_start(pdMS_TO_TICKS(10000))) {
        ESP_LOGE(TAG, "Interfaccia STA non pronta, impossibile connettersi");
        return;
    }
    
    // NON disconnettersi se siamo già connessi (evita loop)
    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "Già connesso a una rete, salto selezione");
        return;
    }
    
    // Se ci sono tentativi di riconnessione in corso, lascia fare
    if (reconnect_task_handle) {
        ESP_LOGI(TAG, "Task di riconnessione già attivo");
        return;
    }
    
    // Scegli la migliore rete via scansione
    if (selectBestWiFi()) {
        const char* ssid = getSelectedSSID();
        const char* password = getSelectedPassword();
        if (ssid && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Tento connessione a: %s", ssid);
            wifi_start_sta(ssid, password);
            // Attendiamo per vedere se la connessione riesce entro 15 secondi
            EventBits_t bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | DISCONNECTED_BIT,
                                                   pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
            if (bits & CONNECTED_BIT) {
                ESP_LOGI(TAG, "Connessione riuscita a %s", ssid);
            } else {
                ESP_LOGW(TAG, "Connessione fallita a %s, attivo riconnessione automatica", ssid);
                start_reconnect_task();
            }
        }
    } else {
        ESP_LOGW(TAG, "Nessuna rete conosciuta disponibile, attivo scansione periodica");
        start_reconnect_task(); // Il task di riconnessione tenterà periodicamente
    }
}