#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/uart.h>
#include <esp_log.h>
#include "serial_handler.h"
#include "config.h"

void espnow_send_uart_response(const uint8_t* data, int len);

static const char *TAG = "SERIAL_HANDLER";
static QueueHandle_t uart_to_tcp_queue = NULL;
static QueueHandle_t uart_espnow_queue = NULL;
static QueueHandle_t uart_tx_queue = NULL;

#define UART_TX_QUEUE_SIZE 50
#define UART_ESPNOW_QUEUE_SIZE 50

// Buffer pool per ridurre allocazioni
#define DATA_POOL_SIZE 10
typedef struct {
    uint8_t *data;
    int length;
    bool in_use;
} buffer_pool_entry_t;

static buffer_pool_entry_t buffer_pool[DATA_POOL_SIZE];
static SemaphoreHandle_t pool_mutex = NULL;

static void init_buffer_pool(void) {
    pool_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < DATA_POOL_SIZE; i++) {
        buffer_pool[i].data = malloc(UART_BUF_SIZE);
        buffer_pool[i].in_use = false;
        buffer_pool[i].length = 0;
    }
}

static uart_data_t* allocate_buffer(void) {
    uart_data_t *result = malloc(sizeof(uart_data_t));
    if (!result) return NULL;
    
    xSemaphoreTake(pool_mutex, portMAX_DELAY);
    for (int i = 0; i < DATA_POOL_SIZE; i++) {
        if (!buffer_pool[i].in_use && buffer_pool[i].data != NULL) {
            buffer_pool[i].in_use = true;
            result->data = buffer_pool[i].data;
            result->length = 0;
            xSemaphoreGive(pool_mutex);
            return result;
        }
    }
    xSemaphoreGive(pool_mutex);
    
    // Pool esaurito, allocazione diretta
    result->data = malloc(UART_BUF_SIZE);
    if (!result->data) {
        free(result);
        return NULL;
    }
    result->length = 0;
    return result;
}

static void free_buffer(uart_data_t *buffer) {
    if (!buffer) return;
    
    xSemaphoreTake(pool_mutex, portMAX_DELAY);
    for (int i = 0; i < DATA_POOL_SIZE; i++) {
        if (buffer_pool[i].data == buffer->data && buffer_pool[i].in_use) {
            buffer_pool[i].in_use = false;
            buffer_pool[i].length = 0;
            xSemaphoreGive(pool_mutex);
            free(buffer);
            return;
        }
    }
    xSemaphoreGive(pool_mutex);
    
    // Non nel pool, free diretto
    if (buffer->data) free(buffer->data);
    free(buffer);
}

void serial_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    // Non usiamo la UART event queue di ESP-IDF: la ricezione viene gestita
    // direttamente da serial_get_data()/serial_read_task(). La coda applicativa
    // uart_to_tcp_queue è separata e contiene uart_data_t*.
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2,
                                         UART_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, 
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    uart_to_tcp_queue = xQueueCreate(UART_TX_QUEUE_SIZE, sizeof(uart_data_t*));
    uart_espnow_queue = xQueueCreate(UART_ESPNOW_QUEUE_SIZE, sizeof(uart_data_t*));
    uart_tx_queue = xQueueCreate(UART_TX_QUEUE_SIZE, sizeof(uart_data_t*));

    if (!uart_to_tcp_queue || !uart_espnow_queue || !uart_tx_queue) {
        ESP_LOGE(TAG, "Impossibile creare le code UART");
        abort();
    }

    init_buffer_pool();
}

void serial_send_data(const uint8_t *data, int length) {
    if (!data || length <= 0 || length > UART_BUF_SIZE || !uart_tx_queue) {
        if (length > UART_BUF_SIZE) {
            ESP_LOGW(TAG, "Dati UART troppo lunghi: %d > %d", length, UART_BUF_SIZE);
        }
        return;
    }

    uart_data_t *tx_data = allocate_buffer();
    if (!tx_data) {
        ESP_LOGW(TAG, "Buffer TX UART esauriti");
        return;
    }

    memcpy(tx_data->data, data, length);
    tx_data->length = length;

    if (xQueueSend(uart_tx_queue, &tx_data, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Coda TX UART piena: dati scartati");
        serial_free_data(tx_data);
    }
}

void serial_send_string(const char *str) {
    if (str) {
        serial_send_data((const uint8_t*)str, strlen(str));
    }
}

QueueHandle_t serial_get_queue(void) {
    return uart_to_tcp_queue;
}

bool serial_get_data(uart_data_t **data, TickType_t wait_time) {
    uart_data_t *uart_data = allocate_buffer();
    if (!uart_data) return false;
    
    int len = uart_read_bytes(UART_NUM, uart_data->data, UART_BUF_SIZE, wait_time);
    if (len > 0) {
        uart_data->length = len;
        *data = uart_data;
        return true;
    }
    
    free_buffer(uart_data);
    return false;
}

void serial_free_data(uart_data_t *data) {
    free_buffer(data);
}

static void uart_tx_task(void *pvParameters) {
    uart_data_t *data = NULL;

    while (1) {
        if (xQueueReceive(uart_tx_queue, &data, portMAX_DELAY) == pdTRUE) {
            if (data && data->data && data->length > 0) {
                int written = uart_write_bytes(UART_NUM, (const char *)data->data, data->length);
                if (written < 0 || written != data->length) {
                    ESP_LOGW(TAG, "Scrittura UART incompleta: %d/%d", written, data->length);
                } else {
                    esp_err_t err = uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "Timeout/errore attesa TX UART: %s", esp_err_to_name(err));
                    }
                }
            }
            serial_free_data(data);
            data = NULL;
        }
    }
}

static void espnow_sender_task(void *pvParameters) {
    uart_data_t *data = NULL;
    while (1) {
        if (xQueueReceive(uart_espnow_queue, &data, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (data && data->data && data->length > 0) {
                espnow_send_uart_response(data->data, data->length);
            }
            serial_free_data(data);
        }
    }
    vTaskDelete(NULL);
}

static void serial_read_task(void *pvParameters) {
    while (1) {
        uart_data_t *data = NULL;
        if (serial_get_data(&data, pdMS_TO_TICKS(10))) {
            if (data && data->data && data->length > 0) {
                // COPIA per ESP-NOW
                uart_data_t *espnow_data = allocate_buffer();
                if (espnow_data) {
                    memcpy(espnow_data->data, data->data, data->length);
                    espnow_data->length = data->length;
                    if (xQueueSend(uart_espnow_queue, &espnow_data, pdMS_TO_TICKS(10)) != pdTRUE) {
                        ESP_LOGW(TAG, "Coda ESP-NOW piena: risposta UART scartata");
                        serial_free_data(espnow_data);
                    }
                }
                
                // COPIA per TCP
                uart_data_t *tcp_data = allocate_buffer();
                if (tcp_data) {
                    memcpy(tcp_data->data, data->data, data->length);
                    tcp_data->length = data->length;
                    if (xQueueSend(uart_to_tcp_queue, &tcp_data, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Coda TCP piena: risposta UART scartata");
                        serial_free_data(tcp_data);
                    }
                }
            }
            serial_free_data(data);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}

void serial_task_start(void) {
    xTaskCreate(serial_read_task, "uart_reader", SERIAL_TASK_SIZE,
                NULL, SERIAL_TASK_PRIORITY, NULL);
    xTaskCreate(uart_tx_task, "uart_tx", 4096,
                NULL, SERIAL_TASK_PRIORITY, NULL);
    xTaskCreate(espnow_sender_task, "espnow_sender", 4096,
                NULL, 4, NULL);
}