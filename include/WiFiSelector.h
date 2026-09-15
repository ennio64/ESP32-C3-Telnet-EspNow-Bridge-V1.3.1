#ifndef WIFI_SELECTOR_H
#define WIFI_SELECTOR_H

#include <stdbool.h>
#include <stdint.h>

// Scansiona e seleziona la migliore rete WiFi conosciuta
bool selectBestWiFi(void);

// Ottiene SSID della rete selezionata (dopo selectBestWiFi)
const char* getSelectedSSID(void);

// Ottiene password della rete selezionata
const char* getSelectedPassword(void);

#endif