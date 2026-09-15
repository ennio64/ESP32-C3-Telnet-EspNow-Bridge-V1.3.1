#ifndef CONFIG_H
#define CONFIG_H

// Log sistema
#define ENABLE_DEBUG_LOGS             1   // sempre 1 (sono gestiti da NVS)
#define LOGS_DEBUG_BYPASS             0   // 1 = forza DEBUG_LEVEL 2 all'avvio, 0 = disabilita

// Configurazione seriale
#define UART_NUM             UART_NUM_0
#define UART_TX_PIN          21      // TX pin (GPIO21) -> collegare a RX del controller
#define UART_RX_PIN          20      // RX pin (GPIO20) -> collegare a TX del controller
#define UART_BAUD_RATE       115200  // Baud rate tipico
#define UART_BUF_SIZE        1024
#define UART_QUEUE_SIZE      20

// Configurazione TCP/Telnet
#define TELNET_PORT          23
#define MAX_TCP_CLIENTS      4
#define TCP_SERVER_PRIORITY  5
#define TCP_RECV_TIMEOUT_MS  100     // Timeout recv() in millisecondi (usato per SO_RCVTIMEO)

// Task stack size per i sottotask del server TCP
#define TCP_SERVER_TASK_SIZE 8192        // era 4096
#define TCP_BROADCAST_TASK_SIZE 4096     // era 2048
#define TCP_CLIENT_TASK_SIZE 8192        // era 4096

// Timeout per select() nel server (secondi) - permette terminazione pulita
#define TCP_SELECT_TIMEOUT_SEC    1

// Task priorities
#define SERIAL_TASK_PRIORITY  4
#define SERIAL_TASK_SIZE      4096
#define WIFI_TASK_PRIORITY    2

// Queue sizes
#define UART_TO_TCP_QUEUE_SIZE   100
#define TCP_TO_UART_QUEUE_SIZE   100

// Timeouts
#define CLIENT_INACTIVITY_TIMEOUT_MS  (5 * 60 * 1000)  // 5 minuti

// Configurazione WiFi (default per AP mode)
#define WIFI_AP_SSID          "ESP32-C3-Serial-Bridge"
#define WIFI_AP_PASSWORD      "12345678"
#define WIFI_AP_MAX_CONNECT   4
#define WIFI_AP_CHANNEL       6

#endif // CONFIG_H