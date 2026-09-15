#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_ESPNOW_PEERS 16

void espnow_init_handler(void);
void espnow_send_uart_response(const uint8_t* data, int len);
bool espnow_is_paired(void);           
int espnow_get_peer_count(void);      
void espnow_clear_all_peers(void);

typedef struct {
    uint8_t mac[6];
    int8_t function;   // -1 = non assegnato, 0..6 = funzione
} espnow_sensor_info_t;

int espnow_get_sensor_list(espnow_sensor_info_t *out, int max_items);
bool espnow_get_sensor_binding(uint8_t function, uint8_t mac[6]);
bool espnow_assign_sensor(uint8_t function, const uint8_t mac[6]);
bool espnow_unassign_sensor(uint8_t function);

#endif