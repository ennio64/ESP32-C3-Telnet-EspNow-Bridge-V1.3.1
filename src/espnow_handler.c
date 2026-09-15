#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "espnow_handler.h"
#include "espnow_config.h"
#include "espnow_protocol.h"
#include "serial_handler.h"
#include "my_logs.h"
#include "nvs_storage.h"

static const char *TAG = "ESPNOW";

static int find_peer_index_locked(const uint8_t *mac);

#define ESPNOW_RX_QUEUE_SIZE       20
#define ESPNOW_LEGACY_RX_QUEUE_SIZE 20
#define ESPNOW_LEGACY_PACKET_MAX   ESPNOW_MAX_FRAME_SIZE

typedef enum {
    ESPNOW_PEER_PROTOCOL_UNKNOWN = 0,
    ESPNOW_PEER_PROTOCOL_LEGACY,
    ESPNOW_PEER_PROTOCOL_NEW
} espnow_peer_protocol_t;

typedef struct {
    uint8_t mac[6];
    bool active;
    int64_t last_activity_ms;
    int64_t last_ping_ms;
    uint8_t ping_retries;

    espnow_peer_protocol_t protocol;

    // Sequence state for the new protocol.
    uint32_t last_rx_session;
    uint16_t last_rx_seq;
    bool rx_seq_valid;
    uint8_t last_rx_result;

    // Sequence state for the legacy pendant protocol.
    uint16_t legacy_last_seq;
    bool legacy_seq_valid;
} espnow_peer_t;

typedef struct {
    uint8_t mac[6];
    uint8_t type;
    uint32_t session_id;
    uint16_t seq;
    uint16_t payload_len;
    uint8_t payload[ESPNOW_MAX_PAYLOAD];
} espnow_rx_packet_t;

typedef struct {
    uint8_t mac[6];
    uint16_t seq;
    uint16_t payload_len;
    uint8_t payload[ESPNOW_LEGACY_PACKET_MAX - 4];
} espnow_legacy_rx_packet_t;

static espnow_peer_t peers[MAX_ESPNOW_PEERS] = {0};
static SemaphoreHandle_t peers_mutex = NULL;
static QueueHandle_t espnow_rx_queue = NULL;
static QueueHandle_t espnow_legacy_rx_queue = NULL;
static bool espnow_initialized = false;
static int espnow_channel = 11;
static bool output_state[ESPNOW_GPIO_COUNT] = {0};

// MODIFICA 2: handle delle due task RX
static TaskHandle_t espnow_rx_task_handle = NULL;
static TaskHandle_t espnow_legacy_rx_task_handle = NULL;

static const char *const espnow_output_names[ESPNOW_GPIO_COUNT] = {
    "PROBE", "XLIMIT", "YLIMIT", "ZLIMIT", "CUSTOM1", "CUSTOM2", "CUSTOM3"
};

static void espnow_peer_monitor_task(void *pvParameters);
static void espnow_rx_task(void *pvParameters);
static void espnow_legacy_rx_task(void *pvParameters);

static bool is_zero_mac(const uint8_t *mac)
{
    if (!mac) return true;
    for (int i = 0; i < 6; i++)
        if (mac[i] != 0) return false;
    return true;
}

static int sensor_function_for_mac_locked(const uint8_t *mac)
{
    const bridge_config_t *cfg = nvs_storage_get_config();
    for (int f = 0; f < ESPNOW_GPIO_COUNT; f++) {
        if (!(cfg->espnow_sensor_binding_mask & (1u << f))) continue;
        if (memcmp(cfg->espnow_sensor_macs[f], mac, 6) == 0)
            return f;
    }
    return -1;
}

static bool sensor_is_bound_to_function(const uint8_t *mac, uint8_t function)
{
    if (!mac || function >= ESPNOW_GPIO_COUNT || !peers_mutex) return false;
    bool ok = false;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    if (idx >= 0 && peers[idx].protocol == ESPNOW_PEER_PROTOCOL_NEW) {
        const bridge_config_t *cfg = nvs_storage_get_config();
        ok = (cfg->espnow_sensor_binding_mask & (1u << function)) &&
             memcmp(cfg->espnow_sensor_macs[function], mac, 6) == 0;
    }
    xSemaphoreGive(peers_mutex);
    return ok;
}

static int get_wifi_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0)
        return primary;
    return 11;
}

static bool is_allowed_output_pin(int pin)
{
    return pin == 4 || pin == 5 || pin == 6 || pin == 7 || pin == 10;
}

static int find_peer_index_locked(const uint8_t *mac)
{
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active && memcmp(peers[i].mac, mac, 6) == 0)
            return i;
    }
    return -1;
}

static int find_peer_index(const uint8_t *mac)
{
    if (!peers_mutex) return -1;
    int idx;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    idx = find_peer_index_locked(mac);
    xSemaphoreGive(peers_mutex);
    return idx;
}

static int add_peer_to_list(const uint8_t *mac, espnow_peer_protocol_t protocol)
{
    int result = -1;
    int64_t now = esp_timer_get_time() / 1000;

    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int existing = find_peer_index_locked(mac);
    if (existing >= 0) {
        peers[existing].last_activity_ms = now;
        peers[existing].ping_retries = 0;
        peers[existing].last_ping_ms = 0;
        if (protocol != ESPNOW_PEER_PROTOCOL_UNKNOWN)
            peers[existing].protocol = protocol;
        xSemaphoreGive(peers_mutex);
        return existing;
    }

    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (!peers[i].active) {
            memset(&peers[i], 0, sizeof(peers[i]));
            memcpy(peers[i].mac, mac, 6);
            peers[i].active = true;
            peers[i].last_activity_ms = now;
            peers[i].protocol = protocol;
            result = i;
            break;
        }
    }
    xSemaphoreGive(peers_mutex);

    if (result >= 0)
        ESP_LOGI(TAG, "Peer aggiunto: " MACSTR " (slot %d, protocollo=%s)",
                 MAC2STR(mac), result,
                 protocol == ESPNOW_PEER_PROTOCOL_LEGACY ? "legacy" :
                 protocol == ESPNOW_PEER_PROTOCOL_NEW ? "new" : "unknown");
    else
        ESP_LOGW(TAG, "Limite peer raggiunto (%d)", MAX_ESPNOW_PEERS);
    return result;
}

static bool ensure_radio_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac))
        return true;

    esp_now_peer_info_t info = {0};
    memcpy(info.peer_addr, mac, 6);
    info.channel = espnow_channel;
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;

    esp_err_t err = esp_now_add_peer(&info);
    if (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST)
        return true;

    ESP_LOGW(TAG, "Impossibile aggiungere peer " MACSTR ": %s",
             MAC2STR(mac), esp_err_to_name(err));
    return false;
}

static void clear_peer_locked(int idx)
{
    memset(&peers[idx], 0, sizeof(peers[idx]));
}

static void mark_peer_activity(const uint8_t *mac)
{
    if (!peers_mutex) return;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    if (idx >= 0) {
        peers[idx].last_activity_ms = esp_timer_get_time() / 1000;
        peers[idx].ping_retries = 0;
        peers[idx].last_ping_ms = 0;
    }
    xSemaphoreGive(peers_mutex);
}

static void espnow_outputs_init(void)
{
    const bridge_config_t *cfg = nvs_storage_get_config();
    memset(output_state, 0, sizeof(output_state));

    for (int i = 0; i < ESPNOW_GPIO_COUNT; i++) {
        int pin = cfg->espnow_output_pins[i];
        if (is_allowed_output_pin(pin) && pin != cfg->state_pin) {
            gpio_reset_pin((gpio_num_t)pin);
            gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
            // Logical OFF always means inactive electrical level.
            // Active LOW -> OFF = HIGH; Active HIGH -> OFF = LOW.
            const bool active_low =
                (cfg->espnow_gpio_active_low_mask & (1UL << pin)) != 0;
            gpio_set_level((gpio_num_t)pin, active_low ? 1 : 0);
            ESP_LOGI(TAG, "ESP-NOW output %s -> GPIO%d (OFF)",
                     espnow_output_names[i], pin);
        }
    }
}

static bool build_packet(uint8_t type, uint32_t session_id, uint16_t seq,
                         const uint8_t *payload, uint16_t payload_len,
                         uint8_t *packet, size_t packet_capacity, size_t *packet_len)
{
    if (!packet || !packet_len || payload_len > ESPNOW_MAX_PAYLOAD)
        return false;

    size_t total = ESPNOW_HEADER_SIZE + payload_len;
    if (total > packet_capacity || total > 250)
        return false;

    packet[0] = ESPNOW_PROTOCOL_VERSION;
    packet[1] = type;
    packet[2] = (uint8_t)(session_id & 0xFF);
    packet[3] = (uint8_t)((session_id >> 8) & 0xFF);
    packet[4] = (uint8_t)((session_id >> 16) & 0xFF);
    packet[5] = (uint8_t)((session_id >> 24) & 0xFF);
    packet[6] = (uint8_t)(seq & 0xFF);
    packet[7] = (uint8_t)(seq >> 8);
    packet[8] = (uint8_t)(payload_len & 0xFF);
    packet[9] = (uint8_t)(payload_len >> 8);

    if (payload_len && payload)
        memcpy(packet + ESPNOW_HEADER_SIZE, payload, payload_len);

    *packet_len = total;
    return true;
}

static bool new_type_is_valid(uint8_t type, uint16_t payload_len)
{
    switch (type) {
    case ESPNOW_MSG_PAIR:
        return payload_len >= 6 && payload_len <= 32;
    case ESPNOW_MSG_PAIR_ACK:
        return payload_len == ESPNOW_PAIR_ACK_PAYLOAD_LEN;
    case ESPNOW_MSG_HEARTBEAT:
        return payload_len == 0;
    case ESPNOW_MSG_HEARTBEAT_ACK:
        return payload_len == 0;
    case ESPNOW_MSG_GPIO_COMMAND:
        return payload_len == 2;
    case ESPNOW_MSG_GPIO_ACK:
        return payload_len == 3;
    case ESPNOW_MSG_GPIO_STATUS:
        return payload_len == 4;
    case ESPNOW_MSG_GPIO_STATUS_REQ:
        return payload_len == 0;
    case ESPNOW_MSG_SENSOR_ASSIGNMENT:
        return payload_len == 1;
    case ESPNOW_MSG_UART_COMMAND:
        return payload_len > 0;
    case ESPNOW_MSG_UART_ACK:
        return payload_len == 1;
    case ESPNOW_MSG_UART_RESPONSE:
        return payload_len > 0;
    case ESPNOW_MSG_ERROR:
        return payload_len > 0;
    default:
        return false;
    }
}

static bool parse_packet(const uint8_t *data, int data_len,
                         espnow_rx_packet_t *out, const uint8_t *mac)
{
    if (!data || !out || !mac || data_len < ESPNOW_HEADER_SIZE ||
        data[0] != ESPNOW_PROTOCOL_VERSION)
        return false;

    uint16_t payload_len = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
    if (payload_len > ESPNOW_MAX_PAYLOAD ||
        payload_len != (uint16_t)(data_len - ESPNOW_HEADER_SIZE))
        return false;

    if (!new_type_is_valid(data[1], payload_len))
        return false;

    memset(out, 0, sizeof(*out));
    memcpy(out->mac, mac, 6);
    out->type = data[1];
    out->session_id = (uint32_t)data[2] |
                      ((uint32_t)data[3] << 8) |
                      ((uint32_t)data[4] << 16) |
                      ((uint32_t)data[5] << 24);
    out->seq = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    out->payload_len = payload_len;
    if (payload_len)
        memcpy(out->payload, data + ESPNOW_HEADER_SIZE, payload_len);
    return true;
}

static void send_simple_message(const uint8_t *mac, uint8_t type,
                                uint32_t session_id, uint16_t seq,
                                const uint8_t *payload, uint16_t payload_len)
{
    uint8_t packet[250];
    size_t packet_len = 0;
    if (!build_packet(type, session_id, seq, payload, payload_len,
                      packet, sizeof(packet), &packet_len))
        return;

    esp_err_t err = esp_now_send(mac, packet, packet_len);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "ESP-NOW send type=0x%02X fallito: %s",
                 type, esp_err_to_name(err));
}

static void send_gpio_ack(const uint8_t *mac, uint32_t session_id, uint16_t seq,
                          uint8_t function, uint8_t state, uint8_t result)
{
    uint8_t payload[3] = {function, state, result};
    send_simple_message(mac, ESPNOW_MSG_GPIO_ACK, session_id, seq,
                        payload, sizeof(payload));
}

static void send_sensor_assignment(const uint8_t *mac, uint32_t session_id,
                                   uint16_t seq, uint8_t function)
{
    uint8_t payload[1] = { function };
    send_simple_message(mac, ESPNOW_MSG_SENSOR_ASSIGNMENT, session_id, seq,
                        payload, sizeof(payload));
}

static void send_gpio_status(const uint8_t *mac, uint32_t session_id, uint16_t seq)
{
    const bridge_config_t *cfg = nvs_storage_get_config();
    uint16_t configured_mask = 0;
    uint16_t state_mask = 0;

    for (int i = 0; i < ESPNOW_GPIO_COUNT; i++) {
        int pin = cfg->espnow_output_pins[i];
        if (is_allowed_output_pin(pin) && pin != cfg->state_pin) {
            configured_mask |= (uint16_t)(1u << i);
            if (output_state[i])
                state_mask |= (uint16_t)(1u << i);
        }
    }

    uint8_t payload[4] = {
        (uint8_t)(configured_mask & 0xFF),
        (uint8_t)(configured_mask >> 8),
        (uint8_t)(state_mask & 0xFF),
        (uint8_t)(state_mask >> 8)
    };
    send_simple_message(mac, ESPNOW_MSG_GPIO_STATUS, session_id, seq,
                        payload, sizeof(payload));
}

static bool seq_is_newer(uint16_t seq, uint16_t last_seq)
{
    return (int16_t)(seq - last_seq) > 0;
}

static bool peer_accepts_sequence(const espnow_rx_packet_t *packet,
                                  uint8_t *duplicate_result)
{
    bool duplicate = false;
    *duplicate_result = ESPNOW_RESULT_ERROR;

    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(packet->mac);
    if (idx >= 0 && peers[idx].rx_seq_valid &&
        peers[idx].last_rx_session == packet->session_id) {
        if (!seq_is_newer(packet->seq, peers[idx].last_rx_seq)) {
            duplicate = true;
            *duplicate_result = peers[idx].last_rx_result;
        }
    }
    xSemaphoreGive(peers_mutex);
    return duplicate;
}

static void remember_sequence(const espnow_rx_packet_t *packet, uint8_t result)
{
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(packet->mac);
    if (idx >= 0) {
        peers[idx].last_rx_session = packet->session_id;
        peers[idx].last_rx_seq = packet->seq;
        peers[idx].rx_seq_valid = true;
        peers[idx].last_rx_result = result;
    }
    xSemaphoreGive(peers_mutex);
}

static void reset_new_sequence(const uint8_t *mac, uint32_t session_id)
{
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    if (idx >= 0) {
        peers[idx].last_rx_session = session_id;
        peers[idx].last_rx_seq = 0;
        peers[idx].rx_seq_valid = false;
        peers[idx].last_rx_result = ESPNOW_RESULT_ERROR;
    }
    xSemaphoreGive(peers_mutex);
}

static bool legacy_sequence_is_duplicate(const uint8_t *mac, uint16_t seq)
{
    bool duplicate = false;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    if (idx >= 0 && peers[idx].legacy_seq_valid && peers[idx].legacy_last_seq == seq)
        duplicate = true;
    xSemaphoreGive(peers_mutex);
    return duplicate;
}

static void remember_legacy_sequence(const uint8_t *mac, uint16_t seq)
{
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    if (idx >= 0) {
        peers[idx].legacy_last_seq = seq;
        peers[idx].legacy_seq_valid = true;
    }
    xSemaphoreGive(peers_mutex);
}

static void reset_legacy_sequence(const uint8_t *mac)
{
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    if (idx >= 0) {
        peers[idx].legacy_last_seq = 0;
        peers[idx].legacy_seq_valid = false;
    }
    xSemaphoreGive(peers_mutex);
}

static bool process_gpio_command(const espnow_rx_packet_t *packet,
                                 uint8_t *function, uint8_t *state,
                                 uint8_t *result)
{
    if (packet->payload_len != 2)
        return false;

    *function = packet->payload[0];
    *state = packet->payload[1];

    if (*function >= ESPNOW_GPIO_COUNT || *state > 1) {
        *result = ESPNOW_RESULT_ERROR;
        return true;
    }

    const bridge_config_t *cfg = nvs_storage_get_config();

    // I comandi GPIO del nuovo protocollo sono accettati solo dal SENSOR
    // esplicitamente associato alla funzione. I pendant legacy restano
    // completamente fuori da questa logica.
    if (!sensor_is_bound_to_function(packet->mac, *function)) {
        ESP_LOGW(TAG, "GPIO %s rifiutato: sensore " MACSTR " non associato",
                 espnow_output_names[*function], MAC2STR(packet->mac));
        *result = ESPNOW_RESULT_ERROR;
        return true;
    }

    int pin = cfg->espnow_output_pins[*function];
    if (!is_allowed_output_pin(pin) || pin == cfg->state_pin) {
        ESP_LOGW(TAG, "GPIO function %u non configurata", *function);
        *result = ESPNOW_RESULT_ERROR;
        return true;
    }

    // 1=ON and 0=OFF are logical states. The GPIO polarity is
    // applied only at the final electrical output level.
    const bool active_low =
        (cfg->espnow_gpio_active_low_mask & (1UL << pin)) != 0;
    const int physical_level =
        active_low ? (*state ? 0 : 1) : (*state ? 1 : 0);

    esp_err_t err = gpio_set_level((gpio_num_t)pin, physical_level);
    if (err != ESP_OK) {
        *result = ESPNOW_RESULT_ERROR;
        return true;
    }

    output_state[*function] = (*state != 0);
    ESP_LOGI(TAG, "ESP-NOW GPIO %s -> GPIO%d %s",
             espnow_output_names[*function], pin, *state ? "ON" : "OFF");
    *result = ESPNOW_RESULT_OK;
    return true;
}

static void espnow_rx_task(void *pvParameters)
{
    espnow_rx_packet_t packet;

    while (1) {
        if (xQueueReceive(espnow_rx_queue, &packet, portMAX_DELAY) != pdTRUE)
            continue;

        if (find_peer_index(packet.mac) < 0)
            continue;

        mark_peer_activity(packet.mac);

        switch (packet.type) {
        case ESPNOW_MSG_PAIR:
            // PAIR is handled in the callback because it must be accepted from
            // an unknown peer. The task only receives post-pair messages.
            break;

        case ESPNOW_MSG_GPIO_COMMAND: {
            uint8_t duplicate_result = ESPNOW_RESULT_ERROR;
            if (peer_accepts_sequence(&packet, &duplicate_result)) {
                uint8_t function = packet.payload_len > 0 ? packet.payload[0] : 0;
                uint8_t state = packet.payload_len > 1 ? packet.payload[1] : 0;
                send_gpio_ack(packet.mac, packet.session_id, packet.seq,
                              function, state, duplicate_result);
                break;
            }

            uint8_t function = 0;
            uint8_t state = 0;
            uint8_t result = ESPNOW_RESULT_ERROR;
            if (!process_gpio_command(&packet, &function, &state, &result))
                result = ESPNOW_RESULT_ERROR;

            remember_sequence(&packet, result);
            send_gpio_ack(packet.mac, packet.session_id, packet.seq,
                          function, state, result);
            break;
        }

        case ESPNOW_MSG_GPIO_STATUS_REQ:
            send_gpio_status(packet.mac, packet.session_id, packet.seq);
            break;

        case ESPNOW_MSG_SENSOR_ASSIGNMENT:
            // Direzione prevista: bridge -> sensor. Non accettare questo
            // messaggio come comando proveniente dal peer.
            break;

        case ESPNOW_MSG_UART_COMMAND: {
            uint8_t duplicate_result = ESPNOW_RESULT_ERROR;
            if (peer_accepts_sequence(&packet, &duplicate_result)) {
                send_simple_message(packet.mac, ESPNOW_MSG_UART_ACK,
                                     packet.session_id, packet.seq,
                                     &duplicate_result, 1);
                break;
            }

            uint8_t result = ESPNOW_RESULT_ERROR;
            if (packet.payload_len > 0) {
                serial_send_data(packet.payload, packet.payload_len);
                if (packet.payload[packet.payload_len - 1] != '\n')
                    serial_send_data((const uint8_t*)"\n", 1);
                result = ESPNOW_RESULT_OK;
            }

            remember_sequence(&packet, result);
            send_simple_message(packet.mac, ESPNOW_MSG_UART_ACK,
                                packet.session_id, packet.seq, &result, 1);
            break;
        }

        case ESPNOW_MSG_HEARTBEAT:
            send_simple_message(packet.mac, ESPNOW_MSG_HEARTBEAT_ACK,
                                packet.session_id, packet.seq, NULL, 0);
            break;

        default:
            ESP_LOGW(TAG, "Messaggio ESP-NOW non gestito: type=0x%02X",
                     packet.type);
            break;
        }
    }
}

static void espnow_legacy_rx_task(void *pvParameters)
{
    espnow_legacy_rx_packet_t packet;

    while (1) {
        if (xQueueReceive(espnow_legacy_rx_queue, &packet, portMAX_DELAY) != pdTRUE)
            continue;

        if (find_peer_index(packet.mac) < 0)
            continue;

        mark_peer_activity(packet.mac);

        // The original pendant protocol is intentionally forwarded unchanged.
        // The callback has already sent the legacy 3-byte ACK, preserving the
        // original pendant timing/behavior.
        serial_send_data(packet.payload, packet.payload_len);
        if (packet.payload_len > 0 &&
            packet.payload[packet.payload_len - 1] != '\n') {
            serial_send_data((const uint8_t*)"\n", 1);
        }
    }
}

static bool parse_legacy_command(const uint8_t *data, int data_len,
                                 uint16_t *seq, uint16_t *payload_len)
{
    if (!data || data_len < 5 || !seq || !payload_len)
        return false;

    *seq = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    *payload_len = (uint16_t)data[2] | ((uint16_t)data[3] << 8);

    if (*payload_len == 0 || *payload_len > ESPNOW_MAX_PAYLOAD)
        return false;
    if ((int)*payload_len > data_len - 4)
        return false;

    return true;
}

static bool enqueue_legacy_command(const uint8_t *mac, uint16_t seq,
                                   const uint8_t *payload, uint16_t payload_len)
{
    if (!espnow_legacy_rx_queue || !mac || !payload || payload_len == 0 ||
        payload_len > sizeof(((espnow_legacy_rx_packet_t *)0)->payload))
        return false;

    espnow_legacy_rx_packet_t packet = {0};
    memcpy(packet.mac, mac, 6);
    packet.seq = seq;
    packet.payload_len = payload_len;
    memcpy(packet.payload, payload, payload_len);

    if (xQueueSend(espnow_legacy_rx_queue, &packet, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Legacy RX queue piena, scarto seq=%u", seq);
        return false;
    }
    return true;
}

static void on_espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                              const uint8_t *data, int data_len)
{
    if (!recv_info || !data || data_len <= 0)
        return;

    const uint8_t *mac = recv_info->src_addr;

    // -------------------------------------------------------------------------
    // LEGACY PENDANT PROTOCOL - DO NOT CHANGE
    // -------------------------------------------------------------------------
    if (data_len == 4 && memcmp(data, CMD_PAIR, 4) == 0) {
        ESP_LOGI(TAG, "Legacy PAIR da " MACSTR, MAC2STR(mac));

        if (ensure_radio_peer(mac) && add_peer_to_list(mac,
                ESPNOW_PEER_PROTOCOL_LEGACY) >= 0) {
            reset_legacy_sequence(mac);
            reset_new_sequence(mac, 0);
            esp_now_send(mac, (const uint8_t *)CMD_PAIR_OK,
                         strlen(CMD_PAIR_OK));
        }
        return;
    }

    if (data_len == 4 && memcmp(data, CMD_PONG, 4) == 0) {
        int idx = find_peer_index(mac);
        if (idx >= 0) {
            xSemaphoreTake(peers_mutex, portMAX_DELAY);
            idx = find_peer_index_locked(mac);
            if (idx >= 0) {
                peers[idx].protocol = ESPNOW_PEER_PROTOCOL_LEGACY;
                peers[idx].last_activity_ms = esp_timer_get_time() / 1000;
                peers[idx].ping_retries = 0;
                peers[idx].last_ping_ms = 0;
            }
            xSemaphoreGive(peers_mutex);
        }
        return;
    }

    uint16_t legacy_seq = 0;
    uint16_t legacy_len = 0;
    bool legacy_valid = parse_legacy_command(data, data_len,
                                             &legacy_seq, &legacy_len);

    // Strict new-protocol parsing. A valid new packet is recognized before
    // legacy handling, except for the explicit legacy PAIR/PONG strings above.
    espnow_rx_packet_t new_packet;
    bool new_valid = parse_packet(data, data_len, &new_packet, mac);

    if (new_valid) {
        if (new_packet.type == ESPNOW_MSG_PAIR) {
            // Il PAIR del nuovo sensore contiene il proprio MAC nei primi 6 byte.
            // Questo permette al bridge di distinguere un SENSOR dal protocollo
            // legacy senza modificare in alcun modo i pendant esistenti.
            if (new_packet.payload_len < 6 ||
                memcmp(new_packet.payload, mac, 6) != 0) {
                ESP_LOGW(TAG, "Nuovo PAIR rifiutato: MAC payload non corrisponde al mittente");
                return;
            }
            if (!ensure_radio_peer(mac))
                return;
            if (add_peer_to_list(mac, ESPNOW_PEER_PROTOCOL_NEW) < 0)
                return;

            reset_new_sequence(mac, new_packet.session_id);
            // PAIR_ACK per il nuovo protocollo: oltre al tipo/versione,
            // conferma esplicitamente quale sensore il bridge ha riconosciuto
            // e quale funzione gli risulta associata.
            // Payload: [bridge_type][protocol_version][sensor_mac x6][function]
            // function 0..6 = PROBE..CUSTOM3, 0xFF = non assegnato.
            const bridge_config_t *cfg = nvs_storage_get_config();
            uint8_t assigned_function = 0xFF;
            for (int f = 0; f < ESPNOW_GPIO_COUNT; f++) {
                if ((cfg->espnow_sensor_binding_mask & (1u << f)) &&
                    memcmp(cfg->espnow_sensor_macs[f], mac, 6) == 0) {
                    assigned_function = (uint8_t)f;
                    break;
                }
            }

            uint8_t ack_payload[9] = {
                ESPNOW_DEVICE_TYPE_BRIDGE,
                ESPNOW_PROTOCOL_VERSION,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                assigned_function
            };
            send_simple_message(mac, ESPNOW_MSG_PAIR_ACK,
                                new_packet.session_id, 0,
                                ack_payload, sizeof(ack_payload));

            // Invia al nuovo SENSOR l'associazione attuale.
            // 0xFF significa non assegnato.
            send_sensor_assignment(mac, new_packet.session_id, 0,
                                   assigned_function);

            ESP_LOGI(TAG, "PAIR v%d accettato da " MACSTR
                     " session=0x%08lX",
                     ESPNOW_PROTOCOL_VERSION, MAC2STR(mac),
                     (unsigned long)new_packet.session_id);
            return;
        }

        if (find_peer_index(mac) < 0)
            return;

        // A legacy pendant is never fed the new protocol by the bridge, but a
        // peer can explicitly identify itself through a new PAIR to migrate.
        xSemaphoreTake(peers_mutex, portMAX_DELAY);
        int idx = find_peer_index_locked(mac);
        if (idx >= 0)
            peers[idx].protocol = ESPNOW_PEER_PROTOCOL_NEW;
        xSemaphoreGive(peers_mutex);

        if (!espnow_rx_queue)
            return;

        if (xQueueSend(espnow_rx_queue, &new_packet, 0) != pdTRUE)
            ESP_LOGW(TAG, "RX queue piena, scarto type=0x%02X seq=%u",
                     new_packet.type, new_packet.seq);
        return;
    }

    // Legacy ACKs are generated by the pendant and ignored by the bridge.
    if (data_len == 3)
        return;

    // Legacy command packet: [SEQ little endian][LEN little endian][payload].
    if (legacy_valid && find_peer_index(mac) >= 0) {
        mark_peer_activity(mac);

        xSemaphoreTake(peers_mutex, portMAX_DELAY);
        int idx = find_peer_index_locked(mac);
        if (idx >= 0)
            peers[idx].protocol = ESPNOW_PEER_PROTOCOL_LEGACY;
        xSemaphoreGive(peers_mutex);

        ESP_LOGI(TAG, "G-code legacy ricevuto da " MACSTR
                 ": seq=%u, len=%u", MAC2STR(mac), legacy_seq, legacy_len);

        // Preserve the original ACK format and timing: ACK is sent immediately,
        // before the command is forwarded to UART.
        uint8_t ack[3] = {
            data[0], data[1], 0x01
        };
        esp_now_send(mac, ack, sizeof(ack));

        // Avoid executing the same legacy packet twice if the pendant retries
        // after an ACK was already accepted.
        if (legacy_sequence_is_duplicate(mac, legacy_seq))
            return;

        remember_legacy_sequence(mac, legacy_seq);
        enqueue_legacy_command(mac, legacy_seq, &data[4], legacy_len);
        return;
    }

    ESP_LOGD(TAG, "Pacchetto ESP-NOW non riconosciuto da " MACSTR ", len=%d",
             MAC2STR(mac), data_len);
}

static void espnow_broadcast_to_peers(const uint8_t *data, int len)
{
    if (!data || len <= 0 || !peers_mutex)
        return;

    uint8_t macs[MAX_ESPNOW_PEERS][6];
    int count = 0;

    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active) {
            memcpy(macs[count], peers[i].mac, 6);
            count++;
        }
    }
    xSemaphoreGive(peers_mutex);

    for (int i = 0; i < count; i++) {
        esp_err_t err = esp_now_send(macs[i], data, len);
        if (err != ESP_OK)
            ESP_LOGW(TAG, "Broadcast verso peer %d fallito: %s",
                     i, esp_err_to_name(err));
    }
}

int espnow_get_peer_count(void)
{
    int count = 0;
    if (!peers_mutex) return 0;

    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++)
        if (peers[i].active) count++;
    xSemaphoreGive(peers_mutex);
    return count;
}

bool espnow_is_paired(void)
{
    return espnow_get_peer_count() > 0;
}

void espnow_clear_all_peers(void)
{
    uint8_t macs[MAX_ESPNOW_PEERS][6];
    int count = 0;

    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active) {
            memcpy(macs[count], peers[i].mac, 6);
            count++;
            clear_peer_locked(i);
        }
    }
    xSemaphoreGive(peers_mutex);

    for (int i = 0; i < count; i++) {
        if (esp_now_is_peer_exist(macs[i]))
            esp_now_del_peer(macs[i]);
    }

    ESP_LOGI(TAG, "Tutti i peer rimossi");
}

static void espnow_peer_monitor_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time() / 1000;

        uint8_t ping_macs[MAX_ESPNOW_PEERS][6];
        espnow_peer_protocol_t ping_protocol[MAX_ESPNOW_PEERS];
        int ping_count = 0;

        uint8_t remove_macs[MAX_ESPNOW_PEERS][6];
        int remove_count = 0;

        xSemaphoreTake(peers_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
            if (!peers[i].active) continue;

            int64_t idle = now - peers[i].last_activity_ms;
            if (peers[i].ping_retries == 0 && idle > ESPNOW_PING_IDLE_MS) {
                memcpy(ping_macs[ping_count], peers[i].mac, 6);
                ping_protocol[ping_count] = peers[i].protocol;
                ping_count++;
                peers[i].ping_retries = 1;
                peers[i].last_ping_ms = now;
                continue;
            }

            if (peers[i].ping_retries > 0 &&
                now - peers[i].last_ping_ms >= ESPNOW_PING_INTERVAL_MS) {
                if (peers[i].ping_retries < ESPNOW_PING_MAX_RETRIES) {
                    memcpy(ping_macs[ping_count], peers[i].mac, 6);
                    ping_protocol[ping_count] = peers[i].protocol;
                    ping_count++;
                    peers[i].ping_retries++;
                    peers[i].last_ping_ms = now;
                } else {
                    memcpy(remove_macs[remove_count++], peers[i].mac, 6);
                    clear_peer_locked(i);
                }
            }
        }
        xSemaphoreGive(peers_mutex);

        for (int i = 0; i < ping_count; i++) {
            if (ping_protocol[i] == ESPNOW_PEER_PROTOCOL_LEGACY) {
                // Existing pendants require the original ASCII PING/PONG protocol.
                esp_now_send(ping_macs[i], (const uint8_t *)CMD_PING, 4);
            } else if (ping_protocol[i] == ESPNOW_PEER_PROTOCOL_NEW) {
                send_simple_message(ping_macs[i], ESPNOW_MSG_HEARTBEAT,
                                    0, 0, NULL, 0);
            } else {
                // Unknown peers are probed using the legacy heartbeat, which is
                // harmless for existing pendants and avoids sending new frames
                // before a protocol has been established.
                esp_now_send(ping_macs[i], (const uint8_t *)CMD_PING, 4);
            }
        }

        for (int i = 0; i < remove_count; i++) {
            if (esp_now_is_peer_exist(remove_macs[i]))
                esp_now_del_peer(remove_macs[i]);
            ESP_LOGW(TAG, "Peer " MACSTR " rimosso per timeout",
                     MAC2STR(remove_macs[i]));
        }
    }
}

int espnow_get_sensor_list(espnow_sensor_info_t *out, int max_items)
{
    if (!out || max_items <= 0 || !peers_mutex) return 0;
    int count = 0;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS && count < max_items; i++) {
        if (!peers[i].active || peers[i].protocol != ESPNOW_PEER_PROTOCOL_NEW)
            continue;
        memcpy(out[count].mac, peers[i].mac, 6);
        out[count].function = (int8_t)sensor_function_for_mac_locked(peers[i].mac);
        count++;
    }
    xSemaphoreGive(peers_mutex);
    return count;
}

bool espnow_get_sensor_binding(uint8_t function, uint8_t mac[6])
{
    if (!mac || function >= ESPNOW_GPIO_COUNT) return false;
    const bridge_config_t *cfg = nvs_storage_get_config();
    if (!(cfg->espnow_sensor_binding_mask & (1u << function)) ||
        is_zero_mac(cfg->espnow_sensor_macs[function])) {
        memset(mac, 0, 6);
        return false;
    }
    memcpy(mac, cfg->espnow_sensor_macs[function], 6);
    return true;
}

bool espnow_assign_sensor(uint8_t function, const uint8_t mac[6])
{
    if (function >= ESPNOW_GPIO_COUNT || !mac || is_zero_mac(mac)) return false;
    if (!peers_mutex) return false;

    uint32_t session_id = 0;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    int idx = find_peer_index_locked(mac);
    bool is_sensor = (idx >= 0 && peers[idx].protocol == ESPNOW_PEER_PROTOCOL_NEW);
    if (is_sensor)
        session_id = peers[idx].last_rx_session;
    xSemaphoreGive(peers_mutex);

    if (!is_sensor) {
        ESP_LOGW(TAG, "Impossibile associare SENSOR " MACSTR ": non presente come peer NEW", MAC2STR(mac));
        return false;
    }

    bridge_config_t *cfg = nvs_storage_get_config_mutable();
    // Un SENSOR puo' avere una sola funzione.
    for (int f = 0; f < ESPNOW_GPIO_COUNT; f++) {
        if (f != function && (cfg->espnow_sensor_binding_mask & (1u << f)) &&
            memcmp(cfg->espnow_sensor_macs[f], mac, 6) == 0) {
            ESP_LOGW(TAG, "SENSOR " MACSTR " gia' associato a %s",
                     MAC2STR(mac), espnow_output_names[f]);
            return false;
        }
    }

    memcpy(cfg->espnow_sensor_macs[function], mac, 6);
    cfg->espnow_sensor_binding_mask |= (uint8_t)(1u << function);
    if (!nvs_storage_save_config()) {
        ESP_LOGE(TAG, "Errore salvataggio associazione SENSOR -> %s", espnow_output_names[function]);
        return false;
    }

    // Notifica immediata al SENSOR online. La radio API viene chiamata senza
    // mantenere il mutex dei peer.
    send_sensor_assignment(mac, session_id, 0, function);

    ESP_LOGI(TAG, "SENSOR " MACSTR " associato a %s", MAC2STR(mac), espnow_output_names[function]);
    return true;
}

bool espnow_unassign_sensor(uint8_t function)
{
    if (function >= ESPNOW_GPIO_COUNT) return false;

    uint8_t mac[6] = {0};
    bool had_binding = false;
    uint32_t session_id = 0;

    const bridge_config_t *before = nvs_storage_get_config();
    if (before->espnow_sensor_binding_mask & (1u << function)) {
        memcpy(mac, before->espnow_sensor_macs[function], 6);
        had_binding = !is_zero_mac(mac);
    }

    if (had_binding && peers_mutex) {
        xSemaphoreTake(peers_mutex, portMAX_DELAY);
        int idx = find_peer_index_locked(mac);
        if (idx >= 0 && peers[idx].protocol == ESPNOW_PEER_PROTOCOL_NEW)
            session_id = peers[idx].last_rx_session;
        xSemaphoreGive(peers_mutex);
    }

    bridge_config_t *cfg = nvs_storage_get_config_mutable();
    memset(cfg->espnow_sensor_macs[function], 0, 6);
    cfg->espnow_sensor_binding_mask &= (uint8_t)~(1u << function);
    if (!nvs_storage_save_config())
        return false;

    if (had_binding)
        send_sensor_assignment(mac, session_id, 0, 0xFF);

    return true;
}

void espnow_init_handler(void)
{
    if (espnow_initialized) return;

    peers_mutex = xSemaphoreCreateMutex();
    if (!peers_mutex) {
        ESP_LOGE(TAG, "Impossibile creare mutex peer");
        return;
    }

    espnow_channel = get_wifi_channel();
    ESP_LOGI(TAG, "Canale ESP-NOW: %d", espnow_channel);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);

    // MODIFICA 1: gestione errore esp_now_init() con cleanup del mutex
    esp_err_t espnow_init_err = esp_now_init();
    if (espnow_init_err != ESP_OK) {
        ESP_LOGE(TAG, "Errore ESP-NOW: %s",
                 esp_err_to_name(espnow_init_err));
        vSemaphoreDelete(peers_mutex);
        peers_mutex = NULL;
        return;
    }

    espnow_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_SIZE,
                                    sizeof(espnow_rx_packet_t));
    espnow_legacy_rx_queue = xQueueCreate(ESPNOW_LEGACY_RX_QUEUE_SIZE,
                                          sizeof(espnow_legacy_rx_packet_t));
    if (!espnow_rx_queue || !espnow_legacy_rx_queue) {
        ESP_LOGE(TAG, "Impossibile creare le RX queue ESP-NOW");
        if (espnow_rx_queue) {
            vQueueDelete(espnow_rx_queue);
            espnow_rx_queue = NULL;
        }
        if (espnow_legacy_rx_queue) {
            vQueueDelete(espnow_legacy_rx_queue);
            espnow_legacy_rx_queue = NULL;
        }
        esp_now_deinit();
        return;
    }

    esp_now_register_recv_cb(on_espnow_recv_cb);
    espnow_outputs_init();

    // MODIFICA 3: creazione delle due task RX con handle dedicati
    BaseType_t rx_task_result =
        xTaskCreate(espnow_rx_task,
                    "espnow_rx",
                    4096,
                    NULL,
                    5,
                    &espnow_rx_task_handle);

    BaseType_t legacy_rx_task_result =
        xTaskCreate(espnow_legacy_rx_task,
                    "espnow_legacy_rx",
                    3072,
                    NULL,
                    5,
                    &espnow_legacy_rx_task_handle);

    if (rx_task_result != pdPASS ||
        legacy_rx_task_result != pdPASS) {
        ESP_LOGE(TAG, "Impossibile creare le RX task ESP-NOW");
    }

    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t broadcast_peer = {0};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = espnow_channel;
    broadcast_peer.ifidx = WIFI_IF_STA;
    broadcast_peer.encrypt = false;
    esp_err_t add_broadcast = esp_now_add_peer(&broadcast_peer);
    if (add_broadcast != ESP_OK && add_broadcast != ESP_ERR_ESPNOW_EXIST)
        ESP_LOGW(TAG, "Broadcast peer non aggiunto: %s",
                 esp_err_to_name(add_broadcast));

    const bridge_config_t *cfg = nvs_storage_get_config();
    if (cfg->client_mode != 1)
        xTaskCreate(espnow_peer_monitor_task, "espnow_monitor", 3072,
                    NULL, 3, NULL);

    espnow_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW attivo sul canale %d, nuovo protocollo v%d, max peer=%d",
             espnow_channel, ESPNOW_PROTOCOL_VERSION, MAX_ESPNOW_PEERS);
    ESP_LOGI(TAG, "Compatibilita' protocollo legacy pendant attiva");
}

void espnow_send_uart_response(const uint8_t *data, int len)
{
    if (!data || len <= 0 || espnow_get_peer_count() == 0)
        return;

    int send_len = len;
    if (send_len > ESPNOW_MAX_PAYLOAD)
        send_len = ESPNOW_MAX_PAYLOAD;

    // IMPORTANT: UART responses remain raw ESP-NOW frames. Existing pendants
    // depend on this behavior, so do not wrap them in the new protocol.
    espnow_broadcast_to_peers(data, send_len);
}