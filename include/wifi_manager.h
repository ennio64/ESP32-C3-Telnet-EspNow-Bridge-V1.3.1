#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

// Funzioni WiFi base
void wifi_init(void);
void wifi_start_ap(void);
void wifi_start_sta(const char *ssid, const char *password);
bool wifi_is_connected(void);
const char* wifi_get_ip(void);

// Nuove funzioni con configurazione da NVS
void wifi_start_ap_with_config(void);
void wifi_auto_connect_from_nvs(void);
void set_static_ip_from_config(void);

// Vecchia funzione (mantenuta per compatibilità)
void wifi_auto_connect(void);

#endif