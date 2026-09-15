#ifndef ESPNOW_PROTOCOL_H
#define ESPNOW_PROTOCOL_H

#include <stdint.h>

/*
 * ==========================================================================
 * ESP-NOW APPLICATION PROTOCOL
 * ==========================================================================
 *
 * This file is the single source of truth for the NEW binary ESP-NOW
 * application protocol used by the bridge and future devices.
 *
 * IMPORTANT:
 *   The existing LEGACY pendant protocol is NOT defined here and MUST NOT
 *   be changed. Legacy packets remain handled by espnow_handler.c exactly
 *   as required by the existing pendants.
 *
 * ESP-NOW v1.0 application frame maximum: 250 bytes.
 *
 * --------------------------------------------------------------------------
 * COMMON FRAME FORMAT
 * --------------------------------------------------------------------------
 *
 * Offset  Size  Field
 * ------  ----  ------------------------------------------------------------
 *   0      1    VERSION
 *   1      1    TYPE
 *   2      4    SESSION_ID, uint32, little-endian
 *   6      2    SEQ, uint16, little-endian
 *   8      2    PAYLOAD_LEN, uint16, little-endian
 *  10      N    PAYLOAD
 *
 * HEADER_SIZE = 10 bytes.
 *
 * There is NO CRC, ETX, terminator, or application checksum in protocol v1.
 * Frame validity is determined by VERSION, PAYLOAD_LEN and total frame size.
 *
 * --------------------------------------------------------------------------
 * MESSAGE TYPES
 * --------------------------------------------------------------------------
 */

#define ESPNOW_PROTOCOL_VERSION        1
#define ESPNOW_HEADER_SIZE             10
#define ESPNOW_MAX_FRAME_SIZE          250
#define ESPNOW_MAX_PAYLOAD             (ESPNOW_MAX_FRAME_SIZE - ESPNOW_HEADER_SIZE)

/*
 * Fixed payload size for PAIR_ACK.
 *
 * Payload:
 *   Byte 0    DEVICE_TYPE
 *   Byte 1    PROTOCOL_VERSION
 *   Byte 2-7 SENSOR MAC address
 *   Byte 8    ASSIGNED FUNCTION
 *
 * Total payload size = 9 bytes.
 */
#define ESPNOW_PAIR_ACK_PAYLOAD_LEN    9

#define ESPNOW_MSG_PAIR                0x01
#define ESPNOW_MSG_PAIR_ACK            0x02
#define ESPNOW_MSG_HEARTBEAT           0x03
#define ESPNOW_MSG_HEARTBEAT_ACK       0x04

#define ESPNOW_MSG_GPIO_COMMAND        0x10
#define ESPNOW_MSG_GPIO_ACK            0x11
#define ESPNOW_MSG_GPIO_STATUS         0x12
#define ESPNOW_MSG_GPIO_STATUS_REQ     0x13
#define ESPNOW_MSG_SENSOR_ASSIGNMENT   0x14

#define ESPNOW_MSG_UART_COMMAND        0x20
#define ESPNOW_MSG_UART_ACK            0x21
#define ESPNOW_MSG_UART_RESPONSE       0x22

#define ESPNOW_MSG_ERROR               0x7F

/* --------------------------------------------------------------------------
 * DEVICE TYPES
 * -------------------------------------------------------------------------- */

#define ESPNOW_DEVICE_TYPE_BRIDGE      1
#define ESPNOW_DEVICE_TYPE_SENSOR      2
#define ESPNOW_DEVICE_TYPE_PENDANT     3

/* --------------------------------------------------------------------------
 * GENERIC RESULT VALUES
 * -------------------------------------------------------------------------- */

#define ESPNOW_RESULT_ERROR            0
#define ESPNOW_RESULT_OK               1

/* --------------------------------------------------------------------------
 * GPIO FUNCTION IDENTIFIERS
 *
 * These numeric values are part of the wire protocol and MUST NOT be
 * renumbered for cosmetic reasons.
 * -------------------------------------------------------------------------- */

#define ESPNOW_GPIO_PROBE              0
#define ESPNOW_GPIO_XLIMIT             1
#define ESPNOW_GPIO_YLIMIT             2
#define ESPNOW_GPIO_ZLIMIT             3
#define ESPNOW_GPIO_CUSTOM1            4
#define ESPNOW_GPIO_CUSTOM2            5
#define ESPNOW_GPIO_CUSTOM3            6
#define ESPNOW_GPIO_COUNT              7

#define ESPNOW_GPIO_OFF                0
#define ESPNOW_GPIO_ON                 1

/* --------------------------------------------------------------------------
 * PAYLOAD DEFINITIONS
 * --------------------------------------------------------------------------
 *
 * GPIO_COMMAND (0x10)
 *   Direction: device -> bridge
 *   Payload length: exactly 2 bytes
 *
 *   Byte 0: FUNCTION (0..6)
 *   Byte 1: STATE    (0=OFF, 1=ON)
 *
 * GPIO_ACK (0x11)
 *   Direction: bridge -> device
 *   Payload length: exactly 3 bytes
 *
 *   Byte 0: FUNCTION (echo)
 *   Byte 1: STATE    (echo)
 *   Byte 2: RESULT   (0=ERROR, 1=OK)
 *
 *   SESSION_ID and SEQ are echoed from the corresponding command.
 *
 * GPIO_STATUS_REQ (0x13)
 *   Direction: device -> bridge
 *   Payload length: exactly 0 bytes
 *
 * GPIO_STATUS (0x12)
 *   Direction: bridge -> device
 *   Payload length: exactly 4 bytes
 *
 *   Byte 0: CONFIGURED_MASK[0]
 *   Byte 1: CONFIGURED_MASK[1]
 *   Byte 2: STATE_MASK[0]
 *   Byte 3: STATE_MASK[1]
 *
 *   Both masks are uint16 little-endian.
 *   Bits 0..6 correspond to GPIO function IDs 0..6.
 *
 * SENSOR_ASSIGNMENT (0x14)
 *   Direction: bridge -> sensor
 *   Payload length: exactly 1 byte
 *
 *   Byte 0: FUNCTION
 *     0..6 = assigned GPIO function
 *     0xFF = no function assigned
 *
 *   The bridge is authoritative for the sensor-to-function association.
 *   The sensor must not issue GPIO_COMMAND while FUNCTION is 0xFF.
 *
 * UART_COMMAND (0x20)
 *   Direction: device -> bridge
 *   Payload length: 1..ESPNOW_MAX_PAYLOAD bytes
 *   Payload is forwarded to the central UART TX queue.
 *
 * UART_ACK (0x21)
 *   Direction: bridge -> device
 *   Payload: one RESULT byte (0=ERROR, 1=OK).
 *
 * UART_RESPONSE (0x22)
 *   Reserved for the new protocol's UART response path.
 *   The current bridge continues to broadcast existing raw UART responses
 *   unchanged for compatibility with legacy pendants.
 *
 * PAIR (0x01)
 *   Direction: device -> bridge
 *   The current implementation uses SESSION_ID from the common header.
 *   PAIR acceptance establishes the peer as a NEW-protocol device.
 *
 * PAIR_ACK (0x02)
 *   Direction: bridge -> device
 *   Payload length: exactly 9 bytes.
 *
 *   Byte 0: DEVICE_TYPE
 *           ESPNOW_DEVICE_TYPE_BRIDGE
 *
 *   Byte 1: PROTOCOL_VERSION
 *           ESPNOW_PROTOCOL_VERSION
 *
 *   Bytes 2..7: MAC address of the paired SENSOR/device.
 *
 *   Byte 8: ASSIGNED_FUNCTION
 *           0..6 = PROBE..CUSTOM3
 *           0xFF = no function assigned
 *
 *   SESSION_ID is echoed from the corresponding PAIR message.
 *   SEQ is set to 0 by the current bridge implementation.
 *
 * HEARTBEAT (0x03)
 *   Direction: device -> bridge
 *   Current payload length: 0.
 *
 * HEARTBEAT_ACK (0x04)
 *   Direction: bridge -> device
 *   Current payload length: 0.
 *
 * ERROR (0x7F)
 *   Reserved for protocol errors.
 */

/* --------------------------------------------------------------------------
 * PACKET BUILD/PARSE HELPERS
 * --------------------------------------------------------------------------
 *
 * Multi-byte integer encoding used by protocol v1:
 *
 *   uint16: low byte first, high byte second
 *   uint32: least significant byte first
 *
 * The helpers below are intentionally macros rather than packed C structs;
 * this avoids compiler-dependent padding/alignment on the wire.
 * -------------------------------------------------------------------------- */

#define ESPNOW_U16_LE_LO(v) ((uint8_t)((uint16_t)(v) & 0xFFu))
#define ESPNOW_U16_LE_HI(v) ((uint8_t)(((uint16_t)(v) >> 8) & 0xFFu))

#define ESPNOW_U32_LE_B0(v) ((uint8_t)((uint32_t)(v) & 0xFFu))
#define ESPNOW_U32_LE_B1(v) ((uint8_t)(((uint32_t)(v) >> 8) & 0xFFu))
#define ESPNOW_U32_LE_B2(v) ((uint8_t)(((uint32_t)(v) >> 16) & 0xFFu))
#define ESPNOW_U32_LE_B3(v) ((uint8_t)(((uint32_t)(v) >> 24) & 0xFFu))

/* --------------------------------------------------------------------------
 * PROTOCOL INVARIANTS
 * --------------------------------------------------------------------------
 *
 * 1. VERSION must equal ESPNOW_PROTOCOL_VERSION.
 * 2. PAYLOAD_LEN must be <= ESPNOW_MAX_PAYLOAD.
 * 3. Total frame length must equal ESPNOW_HEADER_SIZE + PAYLOAD_LEN.
 * 4. SESSION_ID is the session identity for sequence/deduplication.
 * 5. SEQ is uint16 little-endian and is interpreted per session/peer.
 * 6. GPIO function IDs 0..6 are fixed on the wire.
 * 7. Existing legacy pendant frames remain outside this specification.
 * 8. SENSOR_ASSIGNMENT is valid only for NEW-protocol sensors.
 * 9. PAIR_ACK payload length is exactly 9 bytes.
 *
 * ==========================================================================
 */

#endif /* ESPNOW_PROTOCOL_H */