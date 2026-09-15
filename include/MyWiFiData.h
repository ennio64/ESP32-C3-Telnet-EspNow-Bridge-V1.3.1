#ifndef MY_WIFI_DATA_H
#define MY_WIFI_DATA_H

// ============================================================
// DEFAULT WIFI NETWORKS - Modify this file to add your networks
// ============================================================
// The bridge will use these as default when NVS is empty.
// You can still add/remove networks via the web interface.
// Max 10 networks.

typedef struct {
    const char* ssid;
    const char* password;
} default_network_t;

// List your default networks here
static const default_network_t default_networks[] = {
    // Add your networks below:
    // {"YOUR_SSID_1", "YOUR_PASSWORD_1"},
    // {"YOUR_SSID_2", "YOUR_PASSWORD_2"},
    // {"YOUR_SSID_3", "YOUR_PASSWORD_3"},
    
    // Example (commented by default):
    // {"MyHomeWiFi", "my_password"},
    // {"MyOfficeWiFi", "office_password"},
};

#define DEFAULT_NETWORKS_COUNT (sizeof(default_networks) / sizeof(default_networks[0]))

#endif