#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <driver/gpio.h>

#include "serial_handler.h"
#include "tcp_server.h"
#include "wifi_manager.h"
#include "espnow_handler.h"
#include "espnow_config.h"
#include "nvs_storage.h"
#include "web_server.h"
#include "grblHAL_advanced.h"
#include "config.h"
#include "my_logs.h"

//static const char *TAG = "ESP32-C3-SERIAL-BRIDGE V1.2";

#define BUILTIN_LED_PIN 8 // GPIO8, LOW = acceso

// ============================================================
// TASK LED
// ============================================================

static void led_blink_task(void *pvParameters)
{
    // Configura il pin LED come output e inizialmente acceso
    /*
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << BUILTIN_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);
    */

    gpio_set_level(BUILTIN_LED_PIN, 0); // acceso all'avvio

    // Stati della macchina a stati
    enum
    {
        LED_ON_SOLID,  // acceso fisso (nessun client)
        LED_ON_SHORT,  // acceso breve (100ms)
        LED_OFF_SHORT, // spento breve (900ms)
        LED_ON_LONG,   // acceso lungo (500ms)
        LED_OFF_LONG,  // spento lungo (500ms)
    } state = LED_ON_SOLID;

    int tick_counter = 0; // contatore di tick da 100ms

    bool last_espnow = false;
    bool last_telnet = false;


    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // tick ogni 100ms

        bool espnow_connected = espnow_is_paired();
        bool telnet_connected = (tcp_server_get_client_count() > 0);

        // Determinazione del pattern richiesto
        int pattern = 0; // 0 = nessuno, 1 = solo ESP-NOW, 2 = solo Telnet, 3 = entrambi

        if (espnow_connected && telnet_connected)
            pattern = 3;
        else if (espnow_connected)
            pattern = 1;
        else if (telnet_connected)
            pattern = 2;
        else
            pattern = 0;

        // Se lo stato dei client è cambiato, resetta la macchina a stati
        if (espnow_connected != last_espnow ||
            telnet_connected != last_telnet)
        {
            last_espnow = espnow_connected;
            last_telnet = telnet_connected;

            tick_counter = 0;

            if (pattern == 0)
            {
                state = LED_ON_SOLID;
                gpio_set_level(BUILTIN_LED_PIN, 0); // acceso fisso
            }
            else
            {
                // Inizia il ciclo appropriato
                if (pattern == 1)
                    state = LED_ON_SHORT;
                else if (pattern == 2)
                    state = LED_ON_LONG;
                else if (pattern == 3)
                {
                    // alterna: inizia con breve
                            state = LED_ON_SHORT;
                }
            }

            continue;
        }

        // Gestione della macchina a stati
        if (pattern == 0)
        {
            // Già nello stato ON_SOLID, non fare nulla
            continue;
        }

        switch (state)
        {
        case LED_ON_SOLID:
            // Non dovrebbe succedere perché pattern != 0
            break;

        case LED_ON_SHORT:
            gpio_set_level(BUILTIN_LED_PIN, 0); // acceso
            tick_counter++;

            if (tick_counter >= 1)
            {
                // 100 ms
                state = LED_OFF_SHORT;
                tick_counter = 0;
            }
            break;

        case LED_OFF_SHORT:
            gpio_set_level(BUILTIN_LED_PIN, 1); // spento
            tick_counter++;

            if (tick_counter >= 9)
            {
                // 900 ms

                // Fine ciclo breve: ricomincia o alterna
                if (pattern == 3)
                {
                    // Alterna: prossimo ciclo lungo
                    state = LED_ON_LONG;
                }
                else
                {
                    state = LED_ON_SHORT;
                }

                tick_counter = 0;
            }
            break;

        case LED_ON_LONG:
            gpio_set_level(BUILTIN_LED_PIN, 0); // acceso
            tick_counter++;

            if (tick_counter >= 5)
            {
                // 500 ms
                state = LED_OFF_LONG;
                tick_counter = 0;
            }
            break;

        case LED_OFF_LONG:
            gpio_set_level(BUILTIN_LED_PIN, 1); // spento
            tick_counter++;

            if (tick_counter >= 5)
            {
                // 500 ms

                // Fine ciclo lungo: ricomincia o alterna
                if (pattern == 3)
                {
                    // Alterna: prossimo ciclo breve
                    state = LED_ON_SHORT;
                }
                else
                {
                    state = LED_ON_LONG;
                }

                tick_counter = 0;
            }
            break;
        }
    }

    vTaskDelete(NULL);
}


// ============================================================
// OTTIENE IL CANALE WIFI CORRENTE DELLA STA
// ============================================================

static int get_sta_channel(void)
{
    wifi_second_chan_t second;
    uint8_t primary;

    if (esp_wifi_get_channel(&primary, &second) == ESP_OK)
    {
        return primary;
    }

    return 0;
}


// ============================================================
// STAMPA INFO SISTEMA
// ============================================================

static void print_system_info(const bridge_config_t *cfg)
{
    char connected_ssid[64] = "None";

    if (wifi_is_connected() && cfg->network_count > 0)
    {
        strncpy(
            connected_ssid,
            cfg->networks[0].ssid,
            sizeof(connected_ssid) - 1
        );

        connected_ssid[sizeof(connected_ssid) - 1] = '\0';
    }

    int sta_channel = get_sta_channel();

    printf("\n");
    printf("========================================\n");
    printf("         SYSTEM INFORMATION             \n");
    printf("========================================\n");

    printf(
        "WiFi STA:      %s (%s)\n",
        connected_ssid,
        wifi_is_connected() ? "CONNECTED" : "DISCONNECTED"
    );

    printf("STA Channel:   %d\n", sta_channel);
    printf("IP Address:    %s\n", wifi_get_ip());

    printf("AP SSID:       %s\n", cfg->ap_ssid);
    printf("AP IP:         192.168.4.1\n");
    printf("AP Channel:    %d\n", cfg->ap_channel);

    printf("Telnet Port:   %d\n", TELNET_PORT);
    printf("Web Interface: http://%s\n", cfg->static_ip);

    printf("Baud Rate:     %d\n", UART_BAUD_RATE);
    printf("Debug Level:   %d\n", cfg->debug_level);

    printf("ESP-NOW:       Send 'PAIR' to pair pendant\n");

    printf("========================================\n");


    // --------------------------------------------------------
    // GRBLHAL ADVANCED
    // --------------------------------------------------------

    printf("\n");
    printf("========================================\n");
    printf("         GRBLHAL ADVANCED               \n");
    printf("========================================\n");

    if (cfg->state_pin == -1)
    {
        printf("State Pin:        Not configured\n");
    }
    else
    {
        printf("State Pin:        GPIO%d\n", cfg->state_pin);
    }

    printf(
        "State Pin Mode:   %s\n",
        cfg->state_pin_mode == 0
            ? "LOW when connected"
            : "HIGH when connected"
    );

    printf(
        "Client Source:    %s\n",
        cfg->client_mode == 0
            ? "Any Client"
            : (cfg->client_mode == 1
                   ? "Telnet Only"
                   : "ESP-NOW Only")
    );

    printf(
        "Reset on Disconnect: %s\n",
        cfg->reset_on_disconnect ? "ENABLED" : "DISABLED"
    );

    printf("========================================\n");
}


// ============================================================
// TASK MONITOR CLIENT
// ============================================================

static void client_monitor_task(void *pvParameters)
{
    while (1)
    {
        bool telnet_connected =
            (tcp_server_get_client_count() > 0);

        bool espnow_connected =
            espnow_is_paired();

        grblHAL_advanced_update_state(
            telnet_connected,
            espnow_connected
        );

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vTaskDelete(NULL);
}


// ============================================================
// APP MAIN
// ============================================================

void app_main(void)
{
    // --------------------------------------------------------
    // LED BUILT-IN
    // --------------------------------------------------------

    // Spegne subito il LED built-in
    // GPIO8, LOW = acceso, HIGH = spento

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << BUILTIN_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&led_conf);

    gpio_set_level(
        BUILTIN_LED_PIN,
        1
    ); // spento


    // --------------------------------------------------------
    // ATTESA SERIAL MONITOR
    // --------------------------------------------------------

    vTaskDelay(pdMS_TO_TICKS(3000));


    // --------------------------------------------------------
    // MAC
    // --------------------------------------------------------

    uint8_t mac[6];


    // --------------------------------------------------------
    // BOOT MESSAGE
    // --------------------------------------------------------

    printf("\n\n");

    printf("========================================\n");
    printf("ESP32-C3 SERIAL BRIDGE V1.3 - BOOT\n");
    printf("========================================\n");


    // ========================================================
    // INIZIALIZZAZIONE NVS
    // ========================================================
    //
    // IMPORTANTE:
    //
    // NON leggiamo più direttamente il blob "config" qui.
    //
    // Prima veniva creato:
    //
    //     bridge_config_t boot_cfg;
    //
    // sullo stack del task main.
    //
    // bridge_config_t è una struttura grande perché contiene
    // anche tutte le reti configurate.
    //
    // Questo provocava Stack protection fault dentro
    // nvs_get_blob().
    //
    // Adesso l'inizializzazione NVS viene fatta qui e la
    // configurazione viene caricata esclusivamente da
    // nvs_storage_init().
    // ========================================================

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);


    // ========================================================
    // DISABILITA LOG DI DEFAULT
    // ========================================================

    esp_log_level_set(
        "*",
        ESP_LOG_NONE
    );


    // ========================================================
    // CARICA CONFIGURAZIONE
    // ========================================================
    //
    // Questa è l'unica funzione responsabile del caricamento
    // della configurazione dal namespace bridge_cfg.
    // ========================================================

    nvs_storage_init();

    const bridge_config_t *cfg =
        nvs_storage_get_config();


    // ========================================================
    // STAMPA CONFIGURAZIONE NVS
    // ========================================================
    //
    // IMPORTANTE:
    //
    // Non viene creata nessuna copia locale di
    // bridge_config_t.
    //
    // Usiamo direttamente cfg, che punta alla configurazione
    // gestita da nvs_storage.c.
    // ========================================================

    printf("\n");
    printf("=== NVS CONFIGURATION ===\n");

    printf(
        "Networks: %d\n",
        cfg->network_count
    );

    for (int i = 0;
         i < cfg->network_count && i < MAX_KNOWN_NETWORKS;
         i++)
    {
        printf(
            "  [%d] SSID: %s\n",
            i,
            cfg->networks[i].ssid
        );

        printf(
            "  [%d] PWD:  %s\n",
            i,
            cfg->networks[i].password
        );
    }

    printf(
        "AP SSID:     %s\n",
        cfg->ap_ssid
    );

    printf(
        "AP Password: %s\n",
        cfg->ap_password
    );

    printf(
        "AP Channel:  %d\n",
        cfg->ap_channel
    );

    printf(
        "Static IP:   %s\n",
        cfg->static_ip
    );

    printf(
        "Use Static:  %s\n",
        cfg->use_static_ip ? "YES" : "NO"
    );

    printf(
        "Debug Level: %d\n",
        cfg->debug_level
    );

    printf(
        "GrblHAL State Pin: %d\n",
        cfg->state_pin
    );

    printf(
        "GrblHAL Client Mode: %d\n",
        cfg->client_mode
    );

    printf(
        "GrblHAL Reset on Disconnect: %d\n",
        cfg->reset_on_disconnect
    );

    printf(
        "ESP-NOW Output Pins:\n"
    );

    for (int i = 0; i < ESPNOW_GPIO_COUNT; i++)
    {
        printf(
            "  [%d] GPIO%d\n",
            i,
            cfg->espnow_output_pins[i]
        );
    }

    printf("=========================\n");


    // ========================================================
    // DEBUG BYPASS
    // ========================================================

#if LOGS_DEBUG_BYPASS

    /*
     * static = NON viene allocata sullo stack.
     *
     * Questo è importante perché bridge_config_t è una
     * struttura relativamente grande.
     */

    static bridge_config_t local_cfg;

    memcpy(
        &local_cfg,
        cfg,
        sizeof(bridge_config_t)
    );

    local_cfg.debug_level = 2;

    cfg = &local_cfg;

    printf(
        "\n🔧 DEBUG LEVEL FORCED TO 2 (bypass NVS)\n"
    );

#endif


    // ========================================================
    // APPLICA LIVELLO DEBUG
    // ========================================================

    if (cfg->debug_level >= 1)
    {
        esp_log_level_set(
            "ESP32-C3-SERIAL-BRIDGE",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "TCP_SERVER",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "SERIAL_HANDLER",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "ESPNOW",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "WEB_SERVER",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "WIFI_MANAGER",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "WIFI_SELECTOR",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "wifi",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "phy",
            ESP_LOG_INFO
        );

        esp_log_level_set(
            "esp_netif_handlers",
            ESP_LOG_INFO
        );


        // ----------------------------------------------------
        // VERBOSE DEBUG
        // ----------------------------------------------------

        if (cfg->debug_level >= 2)
        {
            esp_log_level_set(
                "TCP_SERVER",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "SERIAL_HANDLER",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "ESPNOW",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "WIFI_MANAGER",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "WIFI_SELECTOR",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "NVS_STORAGE",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "WEB_SERVER",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "GRBLHAL_ADV",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "wifi",
                ESP_LOG_VERBOSE
            );

            esp_log_level_set(
                "esp_netif_handlers",
                ESP_LOG_VERBOSE
            );

            printf(
                "\n🔊🔊 VERBOSE DEBUG ENABLED (level %d)\n",
                cfg->debug_level
            );
        }
        else
        {
            printf(
                "\n🔊 Basic debug enabled (level %d)\n",
                cfg->debug_level
            );
        }
    }
    else
    {
        printf(
            "\n🔇 Debug mode disabled\n"
        );
    }


    // ========================================================
    // INIZIALIZZAZIONE WIFI
    // ========================================================

    wifi_init();

    set_static_ip_from_config();

    wifi_start_ap_with_config();

    wifi_auto_connect_from_nvs();


    // ========================================================
    // ATTESA CONNESSIONE WIFI
    // ========================================================

    // Attendi che il WiFi STA sia connesso
    // max 30 secondi

    int wait_sec = 0;

    printf(
        "\n⏳ In attesa della connessione WiFi...\n"
    );

    while (!wifi_is_connected() &&
           wait_sec < 30)
    {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );

        wait_sec++;

        printf(
            "  attesa %d sec\r",
            wait_sec
        );

        fflush(stdout);
    }


    if (wifi_is_connected())
    {
        printf(
            "\n✅ WiFi connesso dopo %d secondi\n",
            wait_sec
        );
    }
    else
    {
        printf(
            "\n⚠️ WiFi non connesso dopo 30 secondi. "
            "Funzionerà solo in modalità AP.\n"
        );
    }


    // ========================================================
    // STAMPA INFO SISTEMA
    // ========================================================

    print_system_info(cfg);


    // ========================================================
    // INIZIALIZZA ESP-NOW
    // ========================================================

    espnow_init_handler();


    // ========================================================
    // INIZIALIZZA UART
    // ========================================================

    serial_init();

    serial_task_start();


    // ========================================================
    // INIZIALIZZA SERVER TELNET
    // ========================================================

    tcp_server_set_queue(
        serial_get_queue()
    );

    tcp_server_start();


    // ========================================================
    // WEB SERVER
    // ========================================================

    web_server_start();


    // ========================================================
    // GRBLHAL ADVANCED
    // ========================================================

    grblHAL_advanced_init();


    // ========================================================
    // TASK MONITOR STATO CLIENT
    // ========================================================

    xTaskCreate(
        client_monitor_task,
        "client_monitor",
        2048,
        NULL,
        3,
        NULL
    );


    // ========================================================
    // TASK LED
    // ========================================================

    xTaskCreate(
        led_blink_task,
        "led_blink",
        2048,
        NULL,
        2,
        NULL
    );


    // ========================================================
    // MESSAGGIO DI BENVENUTO
    // ========================================================

    esp_read_mac(
        mac,
        ESP_MAC_WIFI_STA
    );

    char welcome_msg[256];

    snprintf(
        welcome_msg,
        sizeof(welcome_msg),

        "\r\n"
        "✅ System Ready - MAC: "
        "%02X:%02X:%02X:%02X:%02X:%02X"
        "\r\n",

        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );

    serial_send_string(
        welcome_msg
    );
}