#ifndef ESPNOW_CONFIG_H
#define ESPNOW_CONFIG_H

/*
 * ESP-NOW bridge runtime/radio configuration.
 *
 * Wire-protocol definitions are intentionally kept in espnow_protocol.h.
 * The legacy pendant ASCII commands remain here because they belong to the
 * compatibility layer and must not be changed.
 */

#include "espnow_protocol.h"

/* Radio / bridge timing configuration */
#define ESPNOW_CHANNEL          1
#define ESPNOW_WAIT_TIME_MS     200
#define ESPNOW_MAX_RETRY        3

/* Legacy pendant protocol - DO NOT CHANGE */
#define CMD_PAIR                "PAIR"
#define CMD_PAIR_OK             "PAIR_OK"
#define CMD_PING                "PING"
#define CMD_PONG                "PONG"

/* Legacy-compatible heartbeat used by the bridge */
#define ESPNOW_PING_IDLE_MS         (5 * 1000)
#define ESPNOW_PING_INTERVAL_MS     (2500)
#define ESPNOW_PING_MAX_RETRIES     3

#endif
