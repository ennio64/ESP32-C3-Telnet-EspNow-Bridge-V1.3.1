#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void tcp_server_start(void);
void tcp_broadcast_data(const uint8_t *data, int length);
void tcp_server_set_queue(QueueHandle_t queue);
int tcp_server_get_client_count(void);  // NUOVA

#endif