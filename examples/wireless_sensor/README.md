# ESP32-S3 Wireless Sensor

Wireless ESP-NOW sensor example for the **ESP32-C3 Telnet / ESP-NOW Bridge**.

This project implements a wireless sensor based on an ESP32-S3 and communicates with the Bridge through a dedicated binary **ESP-NOW Protocol v1**.

The sensor can operate in two control modes:

- **SERIAL** — default mode
- **PIN** — optional hardware input mode

In **SERIAL mode**, the sensor state is controlled from the serial terminal using:

```text
ON
OFF
```

The sensor automatically pairs with the Bridge, receives its assigned logical function, maintains the ESP-NOW connection with heartbeat messages, and reports its status to the Bridge.

---

# 1. Overview

The Wireless Sensor is designed to provide a wireless input/output endpoint for the ESP32-C3 Serial Bridge.

The Bridge can assign one of the following logical functions to a sensor:

| Function | ID |
|---|---:|
| PROBE | 0 |
| XLIMIT | 1 |
| YLIMIT | 2 |
| ZLIMIT | 3 |
| CUSTOM1 | 4 |
| CUSTOM2 | 5 |
| CUSTOM3 | 6 |
| UNASSIGNED | 255 (`0xFF`) |

The sensor does not directly use the physical GPIO number as the logical function identifier.

The Bridge assigns the logical function after pairing.

---

# 2. Hardware

The example is intended for an:

```text
ESP32-S3 SuperMini
```

Default GPIO configuration:

```text
Sensor GPIO : GPIO4
LED         : LED_BUILTIN
```

The sensor GPIO can be used in two different modes.

### SERIAL mode

The sensor state is controlled from the USB/serial terminal.

Commands:

```text
ON
OFF
```

This is the **default mode**.

### PIN mode

GPIO4 is configured as:

```text
INPUT_PULLUP
```

The input is active LOW.

A debounce time of:

```text
30 ms
```

is applied.

---

# 3. Software

The example is based on:

```text
Arduino-ESP32
```

Tested with:

```text
Arduino-ESP32 3.0.7
```

The target board is:

```text
ESP32-S3 SuperMini
```

No external ESP-NOW library is required.

The ESP-NOW functionality is provided by the ESP32 Arduino/ESP-IDF environment.

---

# 4. Main Configuration

The main configuration values are:

```cpp
#define SERIAL_BAUD             115200
#define SENSOR_GPIO_PIN         4
#define SENSOR_LED_PIN          LED_BUILTIN
```

Control modes:

```cpp
#define CONTROL_MODE_SERIAL     0
#define CONTROL_MODE_PIN        1
```

The default control mode is:

```text
SERIAL
```

Therefore, immediately after startup, the sensor state can be controlled from the serial terminal.

---

# 5. Serial Mode — Default

The sensor starts in:

```text
SERIAL
```

mode by default.

In this mode, the physical sensor GPIO is not used as the source of the logical state.

The state is controlled directly from the serial terminal.

## Turn sensor ON

Send:

```text
ON
```

The sensor changes its logical state to:

```text
ON
```

and sends the corresponding GPIO status to the Bridge.

## Turn sensor OFF

Send:

```text
OFF
```

The sensor changes its logical state to:

```text
OFF
```

and sends the corresponding GPIO status to the Bridge.

This mode is particularly useful for:

- testing the ESP-NOW connection
- testing the Bridge
- testing the assigned logical function
- testing GPIO outputs on the Bridge
- debugging the wireless protocol without an external sensor
- manually simulating PROBE/LIMIT/CUSTOM inputs

---

# 6. PIN Mode

The alternative operating mode is:

```text
PIN
```

In PIN mode the sensor reads:

```text
GPIO4
```

as a digital input.

The input configuration is:

```text
INPUT_PULLUP
```

The input is active LOW.

Therefore:

| GPIO4 | Logical state |
|---|---|
| HIGH | OFF |
| LOW | ON |

A debounce time of:

```text
30 ms
```

is used to avoid rapid state changes caused by mechanical contacts or electrical noise.

---

# 7. Switching Control Mode

The following terminal commands are available:

```text
MODE SERIAL
```

Selects serial control.

After this command the sensor state is controlled using:

```text
ON
OFF
```

The alternative is:

```text
MODE PIN
```

which selects GPIO input control.

The current mode can be queried with:

```text
MODE STATUS
```

---

# 8. Serial Commands

The sensor provides the following terminal commands.

| Command | Function |
|---|---|
| `ON` | Set sensor state ON |
| `OFF` | Set sensor state OFF |
| `MODE SERIAL` | Select serial control |
| `MODE PIN` | Select GPIO input control |
| `MODE STATUS` | Show current control mode |
| `STATUS` | Show general sensor status |
| `GPIOSTATUS` | Request GPIO status information |
| `HEARTBEAT` | Test heartbeat communication |
| `MAC` | Show sensor MAC address |
| `CHANNEL` | Show current ESP-NOW channel |
| `HELP` | Show available commands |

Commands are intended to be entered through the serial monitor.

Serial speed:

```text
115200 baud
```

---

# 9. Wi-Fi Networks and ESP-NOW Channel

The sensor scans for configured Wi-Fi **network names** in order to determine the Wi-Fi channel used by the network.

The sensor does **not connect to these Wi-Fi networks**.

It does **not authenticate** with them.

It does **not use Wi-Fi passwords**.

The configured network names are used only to identify the Wi-Fi channel.

Example:

```cpp
const char *knownNetworks[] = {
    {"WiFi_Network_1"},
    {"WiFi_Network_2"},
    {"WiFi_Network_3"},
};
```

The network names above are examples only.

They must be replaced with the Wi-Fi network names available at the installation site.

No Wi-Fi passwords are required for this mechanism.

The procedure is:

```text
Scan Wi-Fi networks
        ↓
Find configured network name
        ↓
Read its Wi-Fi channel
        ↓
Configure ESP-NOW on that channel
        ↓
Start ESP-NOW communication
```

The sensor does not use the Wi-Fi network for normal data communication.

Normal sensor-to-Bridge communication takes place through:

```text
ESP-NOW
```

If none of the configured networks is detected, the sensor uses the fallback channel:

```text
11
```

The ESP-NOW secondary channel is configured as:

```text
WIFI_SECOND_CHAN_NONE
```

---

# 10. ESP-NOW Communication

The sensor uses ESP-NOW for communication with the Bridge.

Before pairing, the sensor uses the broadcast MAC:

```text
FF:FF:FF:FF:FF:FF
```

The sensor broadcasts a pairing request.

Once the Bridge responds, the sensor learns the Bridge MAC address from the received ESP-NOW packet.

After pairing, normal communication is performed using unicast ESP-NOW packets.

---

# 11. Pairing

The sensor automatically attempts to pair with the Bridge.

The pairing sequence is:

```text
Sensor
   |
   |  MSG_PAIR
   |  broadcast
   |
   v
Bridge
   |
   |  MSG_PAIR_ACK
   |
   v
Sensor
```

The sensor sends:

```text
MSG_PAIR
```

using the broadcast MAC address.

The pairing payload contains the sensor MAC address:

```text
6 bytes
```

The Bridge responds with:

```text
MSG_PAIR_ACK
```

The sensor verifies that the response is valid before accepting the pairing.

---

# 12. Pairing Retry

The pairing retry interval is:

```text
5000 ms
```

Therefore, if the sensor is not paired, it periodically sends another pairing request.

This allows the sensor to be powered on before the Bridge without requiring a specific startup sequence.

---

# 13. Pairing LED

After a successful pairing, the sensor LED blinks:

```text
3 times
```

with:

```text
LED ON  = 150 ms
LED OFF = 150 ms
```

This provides a simple visual indication that the sensor has successfully paired with the Bridge.

---

# 14. Assigned Function

After pairing, the Bridge can assign a logical function to the sensor.

The assignment is transmitted using:

```text
MSG_SENSOR_ASSIGNMENT
```

The payload contains one byte.

Valid assignments are:

```text
0 = PROBE
1 = XLIMIT
2 = YLIMIT
3 = ZLIMIT
4 = CUSTOM1
5 = CUSTOM2
6 = CUSTOM3
255 = UNASSIGNED
```

The sensor stores and displays the assigned function.

---

# 15. Protocol Version

The wireless protocol version is:

```text
1
```

The protocol version is included in the ESP-NOW frame header.

The sensor verifies the protocol version when processing packets.

---

# 16. ESP-NOW Frame Format

Every normal protocol frame contains a fixed 10-byte header.

```text
Offset   Size   Field
------   ----   ----------------
0        1      Protocol version
1        1      Message type
2        4      Session ID
6        2      Sequence number
8        2      Payload length
10       N      Payload
```

Maximum frame size:

```text
250 bytes
```

Maximum payload:

```text
240 bytes
```

---

# 17. Session ID

Each communication session has a session ID.

The sensor generates the session ID using random and timing information.

The generated value must not be zero.

The session ID is used to distinguish the current communication session from old or stale packets.

When pairing is lost and a new pairing procedure is started, the sensor resets the session information and establishes a new session.

This prevents packets from an old session from being incorrectly accepted.

---

# 18. Sequence Number

The protocol also uses a 16-bit sequence number.

The sequence number is:

```text
uint16_t
```

It is incremented after successful operations such as:

- heartbeat
- GPIO command
- GPIO status request

The sensor checks the sequence number in the corresponding response.

A response with an unexpected sequence number is not accepted as the response to the current operation.

---

# 19. Message Types

The protocol defines the following messages.

| Type | Hex | Direction | Description |
|---|---:|---|---|
| `MSG_PAIR` | `0x01` | Sensor → Bridge | Pairing request |
| `MSG_PAIR_ACK` | `0x02` | Bridge → Sensor | Pairing response |
| `MSG_HEARTBEAT` | `0x03` | Sensor → Bridge | Connection keep-alive |
| `MSG_HEARTBEAT_ACK` | `0x04` | Bridge → Sensor | Heartbeat response |
| `MSG_GPIO_COMMAND` | `0x10` | Sensor → Bridge | Sensor state command |
| `MSG_GPIO_ACK` | `0x11` | Bridge → Sensor | GPIO command response |
| `MSG_GPIO_STATUS` | `0x12` | Bridge → Sensor | GPIO status |
| `MSG_GPIO_STATUS_REQ` | `0x13` | Sensor → Bridge | Request GPIO status |
| `MSG_SENSOR_ASSIGNMENT` | `0x14` | Bridge → Sensor | Logical function assignment |

---

# 20. MSG_PAIR

Message:

```text
0x01
```

Direction:

```text
Sensor → Bridge
```

The pairing request is broadcast.

Payload:

```text
6 bytes
```

The payload contains the sensor MAC address.

---

# 21. MSG_PAIR_ACK

Message:

```text
0x02
```

Direction:

```text
Bridge → Sensor
```

Payload size:

```text
9 bytes
```

Payload format:

```text
Byte 0    Device type
Byte 1    Protocol version
Byte 2-7  Sensor MAC
Byte 8    Assigned function
```

The sensor verifies:

```text
Device type = Bridge
Protocol version = 1
Sensor MAC = local sensor MAC
```

The assigned function is:

```text
0..6
```

or:

```text
0xFF
```

for unassigned.

---

# 22. Device Types

The protocol defines:

```text
DEVICE_TYPE_BRIDGE = 1
DEVICE_TYPE_SENSOR = 2
```

The sensor expects:

```text
DEVICE_TYPE_BRIDGE
```

when processing a pairing acknowledgement.

---

# 23. GPIO Command

Message:

```text
MSG_GPIO_COMMAND
```

Type:

```text
0x10
```

Direction:

```text
Sensor → Bridge
```

Payload:

```text
2 bytes
```

Format:

```text
Byte 0 = function
Byte 1 = state
```

State values:

```text
0 = OFF
1 = ON
```

Example:

```text
Function = PROBE
State    = ON
```

The Bridge receives the logical command and handles the corresponding configured output.

---

# 24. GPIO ACK

Message:

```text
MSG_GPIO_ACK
```

Type:

```text
0x11
```

Direction:

```text
Bridge → Sensor
```

Payload:

```text
3 bytes
```

Format:

```text
Byte 0 = function
Byte 1 = state
Byte 2 = result
```

Result values:

```text
0 = ERROR
1 = OK
```

The sensor verifies the function and sequence information before accepting the response.

---

# 25. GPIO Command Timing

The sensor measures the application-level response time for a GPIO command.

The measurement starts when:

```text
MSG_GPIO_COMMAND
```

is transmitted.

It ends when:

```text
MSG_GPIO_ACK
```

is received.

The measurement uses:

```cpp
micros()
```

This provides a useful diagnostic indication of the ESP-NOW communication response time.

The measured value represents the application-level communication time and should not be interpreted as a pure physical radio propagation time.

---

# 26. GPIO Status Request

Message:

```text
MSG_GPIO_STATUS_REQ
```

Type:

```text
0x13
```

Direction:

```text
Sensor → Bridge
```

Payload:

```text
0 bytes
```

The sensor uses this message to request the current GPIO configuration and state from the Bridge.

---

# 27. GPIO Status

Message:

```text
MSG_GPIO_STATUS
```

Type:

```text
0x12
```

Direction:

```text
Bridge → Sensor
```

Payload:

```text
4 bytes
```

Format:

```text
Byte 0-1 = configured mask
Byte 2-3 = state mask
```

Both masks are little-endian.

---

# 28. Logical Function Masks

The sensor status masks use **logical function bits**.

They are not physical GPIO numbers.

The bit assignment is:

| Bit | Function |
|---:|---|
| 0 | PROBE |
| 1 | XLIMIT |
| 2 | YLIMIT |
| 3 | ZLIMIT |
| 4 | CUSTOM1 |
| 5 | CUSTOM2 |
| 6 | CUSTOM3 |

For example:

```text
bit 0 = PROBE
bit 1 = XLIMIT
```

If the state mask is:

```text
0x0001
```

then:

```text
PROBE = ON
```

If the state mask is:

```text
0x0004
```

then:

```text
YLIMIT = ON
```

---

# 29. Configured Mask

The configured mask indicates which logical functions are configured.

For example:

```text
configured mask = 0x0003
```

means:

```text
PROBE
XLIMIT
```

are configured.

The mask is based on the **logical function IDs**.

It must not be confused with the Bridge's physical GPIO polarity mask.

---

# 30. Bridge GPIO Polarity

The Bridge can independently configure each physical ESP-NOW output as:

```text
Active HIGH
```

or:

```text
Active LOW
```

This polarity configuration is a property of the Bridge output GPIO.

The sensor protocol itself always uses the logical states:

```text
ON
OFF
```

Therefore the sensor does not need to know whether the corresponding Bridge GPIO is Active HIGH or Active LOW.

The conversion from logical state to electrical level is performed by the Bridge.

For example:

```text
Sensor:
ON
```

always means:

```text
logical output = ON
```

The Bridge may then generate either:

```text
HIGH
```

or:

```text
LOW
```

depending on its GPIO configuration.

---

# 31. Heartbeat

The sensor periodically sends:

```text
MSG_HEARTBEAT
```

The heartbeat interval is:

```text
2000 ms
```

The Bridge responds with:

```text
MSG_HEARTBEAT_ACK
```

The heartbeat is used to verify that the ESP-NOW connection is still alive.

---

# 32. Heartbeat Timeout

The heartbeat ACK timeout is:

```text
1000 ms
```

If an expected heartbeat response is not received within the timeout, the sensor considers that heartbeat attempt failed.

---

# 33. Automatic Re-Pairing

The sensor allows:

```text
3
```

consecutive heartbeat failures before repairing.

The configured value is:

```text
HEARTBEAT_FAILURES_BEFORE_REPAIR = 3
```

After three consecutive failures:

```text
Current pairing is cleared
        ↓
Session information is reset
        ↓
Sensor returns to pairing
        ↓
New MSG_PAIR is transmitted
```

This allows the sensor to recover automatically after a Bridge restart, radio interruption, or other communication failure.

---

# 34. ACK Timeout

The general ACK timeout is:

```text
1000 ms
```

This applies to operations where the sensor waits for a corresponding response.

---

# 35. ESP-NOW Peer

After successful pairing, the Bridge becomes a unicast ESP-NOW peer of the sensor.

The sensor obtains the Bridge MAC address from the source address of the pairing response.

The peer is configured for:

```text
WIFI_IF_STA
```

and no ESP-NOW encryption is used by this example.

---

# 36. Communication Flow

A typical startup sequence is:

```text
ESP32-S3 starts
       |
       v
Scan configured Wi-Fi network names
       |
       v
Determine ESP-NOW channel
       |
       v
Configure ESP-NOW
       |
       v
Broadcast MSG_PAIR
       |
       v
Bridge receives pairing request
       |
       v
Bridge sends MSG_PAIR_ACK
       |
       v
Sensor validates response
       |
       v
Sensor stores Bridge MAC
       |
       v
Pairing successful
       |
       v
LED blinks 3 times
       |
       v
Normal ESP-NOW communication
```

After pairing, the sensor periodically performs heartbeat communication:

```text
Sensor                  Bridge
  |                       |
  |--- HEARTBEAT -------->|
  |                       |
  |<-- HEARTBEAT_ACK -----|
  |                       |
```

---

# 37. Sensor State Flow

In the default SERIAL mode:

```text
Serial terminal
      |
      | ON / OFF
      v
Sensor logical state
      |
      v
MSG_GPIO_COMMAND
      |
      v
Bridge
      |
      v
Configured logical function
```

For example:

```text
Terminal:
ON

      ↓

Sensor:
state = ON

      ↓

ESP-NOW:
MSG_GPIO_COMMAND

      ↓

Bridge:
PROBE = ON

      ↓

Bridge GPIO:
electrical level depends on Active HIGH / Active LOW configuration
```

---

# 38. SERIAL Mode Example

After opening the serial terminal at:

```text
115200 baud
```

the sensor can be tested with:

```text
STATUS
```

Then:

```text
ON
```

The sensor changes its logical state to ON.

To turn it off:

```text
OFF
```

This makes SERIAL mode useful as a simple manual test generator.

No external switch is required.

---

# 39. PIN Mode Example

To use GPIO4 as a physical sensor input:

```text
MODE PIN
```

The sensor then reads:

```text
GPIO4
```

with:

```text
INPUT_PULLUP
```

The input is active LOW.

Typical connection:

```text
GPIO4 ---- switch ---- GND
```

With the switch open:

```text
GPIO4 = HIGH
state  = OFF
```

With the switch closed:

```text
GPIO4 = LOW
state  = ON
```

The 30 ms debounce prevents short mechanical transitions from being interpreted as multiple state changes.

---

# 40. Status and Diagnostics

The following commands are useful during testing:

```text
STATUS
```

Displays general sensor information.

```text
GPIOSTATUS
```

Requests GPIO status information from the Bridge.

```text
MAC
```

Displays the sensor MAC address.

```text
CHANNEL
```

Displays the current ESP-NOW channel.

```text
HEARTBEAT
```

Allows heartbeat communication to be tested manually.

```text
MODE STATUS
```

Displays the current control mode.

```text
HELP
```

Displays the available terminal commands.

---

# 41. Protocol Constants

The main protocol constants are:

```cpp
#define PROTOCOL_VERSION        1

#define HEADER_SIZE             10
#define MAX_FRAME_SIZE          250
#define MAX_PAYLOAD             (MAX_FRAME_SIZE - HEADER_SIZE)
```

Message identifiers:

```cpp
#define MSG_PAIR                0x01
#define MSG_PAIR_ACK            0x02
#define MSG_HEARTBEAT           0x03
#define MSG_HEARTBEAT_ACK       0x04
#define MSG_GPIO_COMMAND        0x10
#define MSG_GPIO_ACK            0x11
#define MSG_GPIO_STATUS         0x12
#define MSG_GPIO_STATUS_REQ     0x13
#define MSG_SENSOR_ASSIGNMENT   0x14
```

---

# 42. Timing Constants

Pairing retry:

```cpp
#define PAIR_RETRY_MS           5000
```

Heartbeat interval:

```cpp
#define HEARTBEAT_INTERVAL_MS   2000
```

ACK timeout:

```cpp
#define ACK_TIMEOUT_MS          1000
```

Heartbeat failures before re-pairing:

```cpp
#define HEARTBEAT_FAILURES_BEFORE_REPAIR  3
```

PIN debounce:

```cpp
#define PIN_DEBOUNCE_MS         30
```

Pairing LED:

```cpp
#define PAIR_LED_BLINKS         3
#define PAIR_LED_ON_MS          150
#define PAIR_LED_OFF_MS         150
```

---

# 43. Important Difference Between Sensor and Bridge Masks

There are two different concepts in the complete system.

## Sensor logical function masks

Used by:

```text
MSG_GPIO_STATUS
```

These bits represent logical functions:

```text
bit 0 = PROBE
bit 1 = XLIMIT
bit 2 = YLIMIT
bit 3 = ZLIMIT
bit 4 = CUSTOM1
bit 5 = CUSTOM2
bit 6 = CUSTOM3
```

## Bridge Active LOW mask

The Bridge uses a separate mask for physical GPIO polarity.

The Bridge mask is based on the **physical GPIO number**.

For example:

```text
GPIO4
GPIO5
GPIO6
GPIO7
GPIO10
```

Therefore the two masks must not be confused.

The sensor only needs to know:

```text
ON
OFF
```

The Bridge is responsible for translating the logical state into the configured electrical level.

---

# 44. Default Sensor Behavior

After power-up, the intended default operating mode is:

```text
SERIAL
```

Therefore the sensor can immediately be tested from the serial terminal.

The basic test sequence is:

```text
STATUS
ON
OFF
ON
OFF
```

This allows the ESP-NOW connection and the Bridge GPIO handling to be tested without connecting an external sensor.

---

# 45. Typical Test Procedure

A recommended first test is:

### Step 1 — Power the Bridge

Verify that the ESP32-C3 Bridge is running.

### Step 2 — Power the ESP32-S3 sensor

Open the serial monitor:

```text
115200 baud
```

### Step 3 — Check the mode

Send:

```text
MODE STATUS
```

Verify that the sensor is in:

```text
SERIAL
```

### Step 4 — Wait for pairing

The sensor should automatically attempt to pair.

After successful pairing, the LED blinks three times.

### Step 5 — Check status

Send:

```text
STATUS
```

### Step 6 — Turn the sensor ON

Send:

```text
ON
```

### Step 7 — Turn the sensor OFF

Send:

```text
OFF
```

### Step 8 — Verify the Bridge output

The corresponding logical function on the Bridge should follow the sensor state.

If the Bridge output is configured as Active LOW, the physical electrical level will be inverted accordingly.

---

# 46. Troubleshooting

## Sensor does not pair

Check:

- Bridge is powered
- sensor is powered
- ESP-NOW channel is correct
- configured Wi-Fi network name is correct
- the configured network is visible during scanning
- both devices are operating on the same ESP-NOW channel

Remember that the sensor does not connect to the Wi-Fi network.

The Wi-Fi network name is used only to determine the channel.

---

## Sensor cannot determine the Wi-Fi channel

Check the configured network names.

If none of the configured networks is detected, the sensor uses:

```text
Channel 11
```

The Bridge must therefore use the same ESP-NOW channel for communication.

---

## Sensor pairs but communication is lost

Check:

- ESP-NOW channel
- physical distance
- radio interference
- Bridge availability
- heartbeat messages

The sensor automatically attempts to recover after:

```text
3 consecutive heartbeat failures
```

It clears the pairing and starts the pairing procedure again.

---

## ON/OFF commands do not change the Bridge output

First verify:

```text
MODE STATUS
```

The sensor must be in:

```text
SERIAL
```

mode.

Then test:

```text
ON
```

and:

```text
OFF
```

Also check the sensor assignment received from the Bridge.

The assigned function must correspond to the desired Bridge logical output.

---

## GPIO mode does not react correctly

Select:

```text
MODE PIN
```

Verify that GPIO4 is wired correctly.

GPIO4 uses:

```text
INPUT_PULLUP
```

and is active LOW.

Typical connection:

```text
GPIO4 ---- switch ---- GND
```

---

# 47. Security and Encryption

This example does not use ESP-NOW encryption.

The communication is based on:

```text
ESP-NOW
```

with pairing and protocol validation.

The pairing mechanism validates:

- device type
- protocol version
- sensor MAC address
- session ID
- sequence number

This project is intended as a controlled local wireless sensor implementation.

---

# 48. Compatibility

The sensor is designed to communicate with the ESP32-C3 Bridge implementing:

```text
Wireless Sensor Protocol v1
```

The following protocol elements must remain compatible between the two devices:

- protocol version
- frame header
- message identifiers
- payload formats
- logical function IDs
- session ID handling
- sequence handling
- heartbeat mechanism
- ACK mechanism
- GPIO status masks
- sensor assignment

Changes to these elements require corresponding changes on both the sensor and Bridge side.

---

# 49. Protocol Summary

The complete communication architecture can be summarized as:

```text
                         ESP-NOW
                           |
                           |
              +------------+------------+
              |                         |
              |                         |
       ESP32-S3 Sensor            ESP32-C3 Bridge
              |                         |
              |                         |
        SERIAL / PIN              Logical outputs
              |                         |
              |                         |
          ON / OFF                PROBE / LIMIT /
                                  CUSTOM outputs
```

Pairing:

```text
Sensor  -- MSG_PAIR --------->  Bridge
Sensor  <-- MSG_PAIR_ACK ----- Bridge
```

Heartbeat:

```text
Sensor  -- HEARTBEAT --------> Bridge
Sensor  <-- HEARTBEAT_ACK ---- Bridge
```

GPIO command:

```text
Sensor  -- GPIO_COMMAND -----> Bridge
Sensor  <-- GPIO_ACK --------- Bridge
```

GPIO status:

```text
Sensor  -- STATUS_REQ -------> Bridge
Sensor  <-- GPIO_STATUS ------ Bridge
```

Assignment:

```text
Sensor  <-- SENSOR_ASSIGNMENT - Bridge
```

---

# 50. Files

The example can be organized as:

```text
wireless_sensor_example/
│
├── ESP32_S3_Wireless_Sensor.ino
│
└── README.md
```

The Arduino sketch contains the complete sensor implementation.

This README documents the protocol and the expected behavior of the example.

---

# 51. Default Configuration Summary

| Parameter | Default |
|---|---|
| Board | ESP32-S3 SuperMini |
| Serial baud | 115200 |
| Sensor GPIO | GPIO4 |
| GPIO mode | SERIAL |
| PIN input mode | INPUT_PULLUP |
| PIN active level | LOW |
| PIN debounce | 30 ms |
| ESP-NOW protocol | v1 |
| Frame header | 10 bytes |
| Maximum frame | 250 bytes |
| Maximum payload | 240 bytes |
| Pair retry | 5 s |
| Heartbeat | 2 s |
| ACK timeout | 1 s |
| Heartbeat failures before repair | 3 |
| Fallback ESP-NOW channel | 11 |
| Pairing LED blinks | 3 |
| Pairing LED ON | 150 ms |
| Pairing LED OFF | 150 ms |

---

# 52. Quick Reference

## Default mode

```text
SERIAL
```

## Turn ON

```text
ON
```

## Turn OFF

```text
OFF
```

## Select serial mode

```text
MODE SERIAL
```

## Select GPIO mode

```text
MODE PIN
```

## Show mode

```text
MODE STATUS
```

## Show status

```text
STATUS
```

## Request GPIO status

```text
GPIOSTATUS
```

## Show MAC

```text
MAC
```

## Show ESP-NOW channel

```text
CHANNEL
```

## Test heartbeat

```text
HEARTBEAT
```

## Show help

```text
HELP
```

---

# 53. License

This example is part of the ESP32-C3 Telnet / ESP-NOW Bridge project.

Use, modification and redistribution are subject to the license included with the project.