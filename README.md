# ESP32-C3 Serial Bridge — Telnet + ESP-NOW + Wireless Sensor

Universal WiFi serial bridge for UART-based controllers, with Telnet, ESP-NOW devices, Wireless Sensor support, Web UI configuration, OTA firmware update, and optional GrblHAL Advanced integration.

The bridge is designed to connect an ESP32-C3 to any device that communicates through a UART interface.

Typical applications include:

- CNC controllers
- Grbl / GrblHAL
- 3D printers
- embedded controllers
- industrial serial devices
- custom UART-based systems

The UART bridge is protocol-agnostic: serial data can be exchanged between the connected controller and network clients without requiring a specific controller protocol.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Hardware Connections](#hardware-connections)
- [GPIO Resources](#gpio-resources)
- [System Architecture](#system-architecture)
- [ESP-IDF Build Environment](#esp-idf-build-environment)
- [ESP-IDF Build Configuration](#esp-idf-build-configuration)
- [Building from Source](#building-from-source)
- [Precompiled Firmware](#precompiled-firmware)
- [WiFi Configuration](#wifi-configuration)
- [Default WiFi Networks](#default-wifi-networks)
- [Access Point Fallback](#access-point-fallback)
- [Web Interface](#web-interface)
- [OTA Firmware Update](#ota-firmware-update)
- [Telnet Server](#telnet-server)
- [Web UI and Telnet Resource Usage](#web-ui-and-telnet-resource-usage)
- [ESP-NOW](#esp-now)
- [ESP-NOW Peer Limit](#esp-now-peer-limit)
- [Wireless Sensor](#wireless-sensor)
- [Wireless Sensor MAC Detection](#wireless-sensor-mac-detection)
- [Wireless Sensor Pairing](#wireless-sensor-pairing)
- [Wireless Sensor Assignment](#wireless-sensor-assignment)
- [Wireless Sensor Protocol](#wireless-sensor-protocol)
- [Frame Format](#frame-format)
- [Device Types](#device-types)
- [Message Types](#message-types)
- [ESP-NOW GPIO Outputs](#esp-now-gpio-outputs)
- [GPIO Active HIGH / Active LOW](#gpio-active-high--active-low)
- [GPIO Polarity Storage](#gpio-polarity-storage)
- [GPIO Startup State](#gpio-startup-state)
- [GrblHAL Advanced](#grblhal-advanced)
- [GrblHAL State Pin](#grblhal-state-pin)
- [GrblHAL Active Client Filter](#grblhal-active-client-filter)
- [Reset on Telnet Disconnect](#reset-on-telnet-disconnect)
- [NVS Configuration Storage](#nvs-configuration-storage)
- [Web API](#web-api)
- [ESP-NOW Web API](#esp-now-web-api)
- [Debug Logging](#debug-logging)
- [Project Structure](#project-structure)
- [Troubleshooting](#troubleshooting)
- [Default Configuration Summary](#default-configuration-summary)
- [Important Notes](#important-notes)
- [License](#license)
- [Project Status](#project-status)

---

# Features

The ESP32-C3 Serial Bridge provides:

- UART serial bridge
- WiFi Station mode
- WiFi Access Point fallback
- automatic WiFi network selection
- configurable WiFi networks
- static IP configuration
- Telnet server on TCP port 23
- Web configuration interface
- OTA firmware update through the Web UI
- ESP-NOW communication
- ESP-NOW peer management
- legacy ESP-NOW pendant compatibility
- new binary ESP-NOW application protocol
- Wireless Sensor support
- automatic Wireless Sensor MAC detection
- Wireless Sensor logical function assignment
- configurable ESP-NOW GPIO outputs
- independent Active HIGH / Active LOW configuration for ESP-NOW GPIO outputs
- persistent configuration using NVS
- runtime debug logging
- optional GrblHAL Advanced integration
- configurable GrblHAL State Pin
- active-client filtering
- optional controller reset when a Telnet client disconnects

The primary identity of the project is a **general-purpose ESP32-C3 UART serial bridge**.

GrblHAL Advanced functionality is optional and does not define the project as a Grbl-only bridge.

---

# Hardware

The reference hardware is an ESP32-C3 SuperMini.

Other ESP32-C3 boards may also be suitable provided that the required GPIOs and UART interface are available.

---

# Hardware Connections

The default UART connection is:

| ESP32-C3 | Function |
|---|---|
| GPIO21 | UART TX |
| GPIO20 | UART RX |
| GND | Ground |

Typical connection to a UART controller:

```text
ESP32-C3                    Controller
---------                   ----------
GPIO21  TX  ------------->  RX
GPIO20  RX  <-------------  TX
GND      -----------------  GND
```

The UART voltage levels must be compatible with the connected controller.

The ESP32-C3 UART must **not** be connected directly to a true RS-232 interface. A suitable level converter is required for RS-232 electrical levels.

Default UART configuration:

```text
UART:       UART0
Baud rate:  115200
TX:         GPIO21
RX:         GPIO20
```

---

# GPIO Resources

The firmware uses several ESP32-C3 GPIOs for dedicated functions.

| GPIO | Function |
|---:|---|
| GPIO20 | UART RX |
| GPIO21 | UART TX |
| GPIO8 | RGB LED |
| GPIO9 | BOOT button |
| GPIO4 | ESP-NOW output / State Pin |
| GPIO5 | ESP-NOW output / State Pin |
| GPIO6 | ESP-NOW output / State Pin |
| GPIO7 | ESP-NOW output / State Pin |
| GPIO10 | ESP-NOW output / State Pin |

GPIO4, GPIO5, GPIO6, GPIO7 and GPIO10 are the available physical GPIOs for:

- ESP-NOW logical outputs
- GrblHAL State Pin

The State Pin configuration and ESP-NOW GPIO polarity configuration are independent.

GPIO20 and GPIO21 are reserved for the UART connection.

GPIO8 and GPIO9 are used by the board hardware and should not be reassigned without considering the target ESP32-C3 board.

---

# System Architecture

The bridge provides several communication paths around the ESP32-C3.

```text
                          ┌─────────────────────┐
                          │      Web Browser    │
                          │     HTTP / Web UI   │
                          └──────────┬──────────┘
                                     │
                                     │ HTTP
                                     ▼
┌───────────────┐           ┌─────────────────────┐
│ Telnet Client │◄─────────►│                     │
└───────────────┘    TCP    │    ESP32-C3 Bridge  │
                            │                     │
┌───────────────┐           │                     │
│ ESP-NOW       │◄─────────►│                     │
│ Pendant       │  ESP-NOW  │                     │
└───────────────┘           │                     │
                            │                     │
┌───────────────┐           │                     │
│ Wireless      │◄─────────►│                     │
│ Sensor        │  ESP-NOW  │       UART          │
└───────────────┘           └─────────┬───────────┘
                                      │
                                      ▼
                              ┌───────────────┐
                              │ CNC / GrblHAL │
                              │ or UART device│
                              └───────────────┘
```

The Web UI is used for configuration and monitoring.

The UART bridge remains the main serial communication path.

---

# ESP-IDF Build Environment

This project is built using **ESP-IDF**.

The project is intended to be built and flashed using the ESP-IDF toolchain and its associated utilities.

This repository is **ESP-IDF only**.

PlatformIO is not required and is not part of the project build system.

Verify the ESP-IDF installation:

```bash
idf.py --version
```

Select the ESP32-C3 target:

```bash
idf.py set-target esp32c3
```

---

# ESP-IDF Build Configuration

The project uses a custom partition table with OTA support.

The ESP32-C3 flash configuration must be set to:

```text
Flash Size:
4 MB
```

The partition table must be configured as:

```text
Custom partition table CSV
```

The custom partition table file is:

```text
partitions.csv
```

The project partition table is:

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x6000,
otadata,    data, ota,     0xF000,   0x2000,
phy_init,   data, phy,    0x11000,  0x1000,
ota_0,      app,  ota_0,  0x20000,  0x1D0000,
ota_1,      app,  ota_1,  0x1F0000, 0x1D0000,
```

The resulting partition layout is:

| Name | Type | SubType | Offset | Size |
|---|---|---|---:|---:|
| nvs | data | nvs | `0x9000` | `0x6000` |
| otadata | data | ota | `0xF000` | `0x2000` |
| phy_init | data | phy | `0x11000` | `0x1000` |
| ota_0 | app | ota_0 | `0x20000` | `0x1D0000` |
| ota_1 | app | ota_1 | `0x1F0000` | `0x1D0000` |

This provides two application partitions for OTA firmware updates.

The application firmware is written to the active OTA partition.

The generated ESP-IDF partition table binary is:

```text
partition-table.bin
```

The file is generated inside:

```text
build/partition_table/partition-table.bin
```

The current application image is approximately 975 KB and therefore fits comfortably inside each OTA application partition.

---

# Building from Source

Open a terminal in the project root.

Build the project:

```bash
idf.py build
```

Flash the firmware:

```bash
idf.py flash
```

Open the serial monitor:

```bash
idf.py monitor
```

Flash and open the serial monitor:

```bash
idf.py flash monitor
```

For a clean rebuild:

```bash
idf.py fullclean
```

ESP-IDF configuration can be accessed with:

```bash
idf.py menuconfig
```

Before building, verify:

```text
Flash Size = 4 MB
Partition Table = Custom partition table CSV
Custom partition table CSV = partitions.csv
```

The project source includes the WiFi configuration file:

```text
include/MyWiFiData.h
```

The `MyWiFiData.h` file contains the optional default WiFi network list compiled into the firmware.

---

# Precompiled Firmware

Precompiled firmware images are provided in the `firmware/` directory for users who do not want to build the project from source.

The firmware package contains:

```text
firmware/
├── bootloader.bin
├── partition-table.bin
├── ESP32-C3-Serial-Bridge.bin
├── flash.bat
├── flash_no_erase.bat
└── flash.sh
```

## Firmware Images

| File | Flash Address | Description |
|---|---:|---|
| `bootloader.bin` | `0x0000` | ESP32-C3 bootloader |
| `partition-table.bin` | `0x8000` | ESP-IDF partition table |
| `ESP32-C3-Serial-Bridge.bin` | `0x20000` | Main application firmware |

The three binary files are intended to be used together as one firmware release.

---

## Automatic Flashing Scripts

The firmware package provides flashing scripts for Windows and Linux/macOS.

```text
flash.bat
flash_no_erase.bat
flash.sh
```

### Windows — Complete Flash

Run:

```text
flash.bat
```

The Windows complete-flash script:

1. Checks that `esptool.py` is available.
2. Uses `COM3` as the default serial port.
3. Allows the user to enter another COM port.
4. Checks the firmware files.
5. Erases the complete ESP32-C3 flash.
6. Flashes `bootloader.bin` at `0x0000`.
7. Flashes `partition-table.bin` at `0x8000`.
8. Flashes `ESP32-C3-Serial-Bridge.bin` at `0x20000`.

Because the complete flash is erased, all NVS configuration is also erased.

After flashing, the bridge starts with its default configuration.

---

## Windows — Flash Without Erasing NVS

Run:

```text
flash_no_erase.bat
```

This script does **not** execute:

```text
erase_flash
```

It writes:

```text
bootloader.bin
partition-table.bin
ESP32-C3-Serial-Bridge.bin
```

at:

```text
0x0000
0x8000
0x20000
```

The NVS area is not erased.

This is the preferred procedure when updating the complete firmware image while preserving the configuration stored in NVS.

---

## Linux / macOS

Run:

```bash
./flash.sh
```

The script:

1. Checks that `esptool.py` is available.
2. Uses `/dev/ttyUSB0` as the default serial port.
3. Allows the user to enter another serial port.
4. Checks the firmware files.
5. Erases the complete ESP32-C3 flash.
6. Flashes `bootloader.bin` at `0x0000`.
7. Flashes `partition-table.bin` at `0x8000`.
8. Flashes `ESP32-C3-Serial-Bridge.bin` at `0x20000`.

The Linux/macOS script performs a complete flash erase and therefore also erases NVS.

---

## Installing esptool

The supplied scripts require `esptool.py`.

If it is not installed:

```bash
pip install esptool
```

The scripts check for `esptool.py` before starting the flashing process.

---

## Manual Flashing

The same flashing procedure can be performed manually.

First erase the complete flash:

```bash
esptool.py --chip esp32c3 --port <PORT> erase_flash
```

Flash the bootloader:

```bash
esptool.py --chip esp32c3 --port <PORT> write_flash 0x0 bootloader.bin
```

Flash the partition table:

```bash
esptool.py --chip esp32c3 --port <PORT> write_flash 0x8000 partition-table.bin
```

Flash the application:

```bash
esptool.py --chip esp32c3 --port <PORT> write_flash 0x20000 ESP32-C3-Serial-Bridge.bin
```

Replace `<PORT>` with the serial port used by the ESP32-C3.

Examples:

```text
Windows:
COM3

Linux:
/dev/ttyUSB0
```

---

## Manual Flashing Without Erasing NVS

To update the bootloader, partition table and application without erasing NVS:

```bash
esptool.py --chip esp32c3 --port <PORT> write_flash 0x0 bootloader.bin
```

```bash
esptool.py --chip esp32c3 --port <PORT> write_flash 0x8000 partition-table.bin
```

```bash
esptool.py --chip esp32c3 --port <PORT> write_flash 0x20000 ESP32-C3-Serial-Bridge.bin
```

No `erase_flash` command is required.

The NVS partition remains at:

```text
0x9000
```

and is therefore preserved.

---

## Important: Flash Erase

The complete flashing scripts execute:

```text
erase_flash
```

before writing the firmware.

This erases the complete ESP32-C3 flash, including the configuration stored in NVS.

Use:

```text
flash.bat
```

or:

```text
flash.sh
```

when a completely clean installation is required.

Use:

```text
flash_no_erase.bat
```

when the existing NVS configuration should be preserved.

---

## First Boot After Flashing

After flashing, the ESP32-C3 starts the Serial Bridge firmware.

If no configured WiFi Station network is available, the bridge provides its default Access Point:

```text
SSID:      ESP32-C3-Serial-Bridge
Password:  12345678
Channel:   6
```

Connect to this WiFi network and open:

```text
http://192.168.4.1
```

The Web UI can then be used to configure the bridge.

---

# WiFi Configuration

The bridge supports:

1. WiFi Station mode
2. WiFi Access Point fallback

WiFi networks can be configured through the Web UI.

When building the firmware from source, a list of default WiFi networks can also be compiled into the firmware through:

```text
include/MyWiFiData.h
```

The bridge can use these networks during automatic WiFi selection.

The Web UI can be used to manage additional network configuration at runtime.

> **Important:** `MyWiFiData.h` applies only when the firmware is built from source. It cannot be modified inside an already compiled precompiled firmware image.

---

# Default WiFi Networks

## Firmware built from source

When building the firmware from source, `include/MyWiFiData.h` contains the default network list.

The structure is:

```c
static const default_network_t default_networks[] = {
    {"Your_SSID", "Your_Password"},
    {"Another_SSID", "Another_Password"},
};
```

Replace the example entries with the networks that should be available to the firmware.

This configuration is **compiled into the firmware**.

If the list is changed, the firmware must be rebuilt:

```bash
idf.py build
```

The resulting firmware must then be flashed to the ESP32-C3.

---

## Precompiled Firmware

If you are using the precompiled firmware from the `firmware/` directory, you **cannot modify `include/MyWiFiData.h`**.

That file exists only in the source project and its contents are already compiled into the `.bin` firmware.

For a precompiled firmware installation, use the **Web UI** to configure the WiFi networks available to the bridge.

Connect to the bridge and open:

```text
http://192.168.4.1
```

when the Access Point fallback is active.

> **Important:** Editing `include/MyWiFiData.h` has no effect on an already compiled `ESP32-C3-Serial-Bridge.bin`.

To change the compiled default WiFi network list, the firmware must be rebuilt from source.

---

# Access Point Fallback

When a suitable WiFi Station connection is not available, the bridge can provide an Access Point.

Default configuration:

```text
SSID:      ESP32-C3-Serial-Bridge
Password:  12345678
Channel:   6
```

The default AP address is:

```text
192.168.4.1
```

Web interface:

```text
http://192.168.4.1
```

Telnet:

```text
192.168.4.1:23
```

---

# Web Interface

The bridge includes an HTTP Web UI for configuration and monitoring.

The Web UI provides access to:

- system status
- WiFi configuration
- network configuration
- static IP configuration
- Access Point configuration
- debug configuration
- ESP-NOW configuration
- ESP-NOW GPIO assignment
- ESP-NOW GPIO polarity
- Wireless Sensor configuration
- Wireless Sensor assignment
- GrblHAL Advanced configuration
- OTA firmware update
- reboot
- reset configuration

The Web UI periodically refreshes status information.

---

# OTA Firmware Update

The firmware provides an HTTP OTA update endpoint used by the Web UI.

The endpoint is:

```text
POST /update
```

The normal OTA workflow is:

```text
1. Open the Web UI
2. Select the firmware update function
3. Select the firmware image
4. Upload the firmware
5. Wait for the update to complete
6. The Bridge restarts using the new firmware
```

OTA uses the two application partitions:

```text
ota_0
ota_1
```

The currently inactive application partition is used for the new firmware image.

OTA does not erase the complete flash and therefore does not intentionally erase the NVS configuration.

---

# Telnet Server

The Telnet server listens on:

```text
TCP port 23
```

The maximum configured number of simultaneous TCP/Telnet clients is:

```c
#define MAX_TCP_CLIENTS 4
```

A typical connection is:

```bash
telnet <bridge-ip> 23
```

For the default Access Point:

```bash
telnet 192.168.4.1 23
```

Telnet data is bridged to and from the UART interface.

---

# Web UI and Telnet Resource Usage

The Web UI and Telnet server share ESP32-C3 resources.

The Web UI periodically sends HTTP requests to update status information.

When the Web UI is open in a browser, these requests can consume resources and TCP connections.

On the current firmware, this can affect the ability to establish additional simultaneous Telnet connections.

For example, a second Telnet client may fail to connect while the Web UI is actively open.

Closing the browser completely can release the resources and allow the additional Telnet connection to connect.

This is a known resource limitation of the current implementation.

The Telnet implementation is intentionally **not changed** as part of this documentation.

The configured maximum number of TCP/Telnet clients remains:

```text
4
```

---

# ESP-NOW

ESP-NOW is used for wireless communication with compatible ESP-NOW devices.

The bridge supports ESP-NOW communication with:

- ESP-NOW pendants
- Wireless Sensors
- supported ESP-NOW application devices

ESP-NOW operation depends on the WiFi radio channel.

Devices participating in the same ESP-NOW communication must operate on the appropriate channel.

The project maintains compatibility with the existing legacy pendant protocol while also defining a separate binary application protocol in:

```text
include/espnow_protocol.h
```

`espnow_protocol.h` is the **single source of truth for the new binary application protocol**.

The existing legacy pendant protocol is intentionally kept outside this definition and must remain compatible with existing devices.

---

# ESP-NOW Peer Limit

The current firmware supports up to:

```text
16 ESP-NOW peers total
```

This limit is **global**.

The 16 peer entries are shared by the existing legacy ESP-NOW communication and the new ESP-NOW application protocol.

They are not 16 peers for each protocol.

The limit is defined by:

```c
#define MAX_ESPNOW_PEERS 16
```

This limit is independent of the Telnet client limit.

| Resource | Maximum |
|---|---:|
| TCP/Telnet clients | 4 |
| ESP-NOW peers total | 16 |

---

# Wireless Sensor

The project includes support for a Wireless Sensor based on an ESP32-S3.

The Wireless Sensor communicates with the ESP32-C3 Bridge using the new binary ESP-NOW application protocol.

The Wireless Sensor example targets an ESP32-S3 board and uses Arduino-ESP32.

The example sensor supports two local control modes.

## SERIAL Mode

SERIAL mode is the default mode.

The sensor state can be controlled through the serial terminal:

```text
ON
OFF
```

## PIN Mode

PIN mode uses the sensor input GPIO.

The example uses:

```text
GPIO4
INPUT_PULLUP
```

The input is active LOW and uses approximately 30 ms debounce.

The Wireless Sensor example also provides diagnostic commands including:

```text
MODE SERIAL
MODE PIN
MODE STATUS
STATUS
GPIOSTATUS
HEARTBEAT
MAC
CHANNEL
HELP
```

---

# Wireless Sensor LED

The Wireless Sensor example can blink its local LED after successful pairing.

This LED belongs to the Wireless Sensor itself.

It is **not a function of the ESP32-C3 Bridge** and is not used by the Bridge for Wireless Sensor status indication.

---

# Wireless Sensor WiFi Channel Selection

The Wireless Sensor does not connect to the configured WiFi networks.

The network names used by the sensor are only used to identify the WiFi channel.

The sensor scans for the configured network names and determines the corresponding channel.

It then configures the ESP-NOW radio to use that channel.

If no configured network is found, the example uses its defined fallback channel.

WiFi credentials are therefore not used by the Wireless Sensor for normal WiFi authentication.

---

# Wireless Sensor MAC Detection

The Wireless Sensor MAC address is **not manually entered into the Bridge Web UI**.

The Bridge automatically detects the sensor MAC address from the sensor's:

```text
MSG_PAIR
```

message.

The sensor's `MAC` serial command can be used to display its MAC address for identification or diagnostics.

It is not necessary to copy the MAC address into the Web UI.

---

# Wireless Sensor Pairing

The Wireless Sensor periodically sends:

```text
MSG_PAIR
```

The Bridge responds with:

```text
MSG_PAIR_ACK
```

The sensor learns the Bridge MAC address from the received ESP-NOW packet information.

The Bridge is then added as a unicast ESP-NOW peer.

The pairing process also communicates the currently assigned logical function.

The pairing sequence is:

```text
Wireless Sensor
      │
      │ MSG_PAIR
      ▼
ESP32-C3 Bridge
      │
      │ MSG_PAIR_ACK
      ▼
Wireless Sensor
      │
      │ establish Bridge peer
      ▼
Paired
```

---

# Wireless Sensor Assignment

The Bridge Web UI allows the user to configure a detected Wireless Sensor.

The user selects the logical function associated with the sensor.

Available logical functions are:

| Value | Function |
|---:|---|
| 0 | PROBE |
| 1 | XLIMIT |
| 2 | YLIMIT |
| 3 | ZLIMIT |
| 4 | CUSTOM1 |
| 5 | CUSTOM2 |
| 6 | CUSTOM3 |
| 0xFF | Unassigned |

After the assignment is configured, the Bridge sends:

```text
MSG_SENSOR_ASSIGNMENT
```

to the Wireless Sensor.

The sensor therefore automatically knows which logical function it has been assigned to.

No manual function configuration is required on the sensor.

The Bridge is authoritative for the sensor-to-function association.

---

# Wireless Sensor Protocol

The new binary ESP-NOW application protocol is defined in:

```text
include/espnow_protocol.h
```

The existing legacy pendant protocol is separate and remains outside this specification.

## Protocol Parameters

```text
Protocol version: 1
Maximum frame:    250 bytes
Header size:      10 bytes
Maximum payload:  240 bytes
```

There is no CRC, ETX, terminator, or application checksum in protocol v1.

Frame validity is determined by the protocol version, payload length and total frame size.

---

# Frame Format

The fixed header is 10 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Protocol version |
| 1 | 1 | Message type |
| 2 | 4 | Session ID, little-endian |
| 6 | 2 | Sequence number, little-endian |
| 8 | 2 | Payload length, little-endian |
| 10 | Variable | Payload |

Multi-byte integers use little-endian encoding.

The total frame length is:

```text
10 + payload length
```

The maximum complete frame is:

```text
250 bytes
```

---

# Device Types

The protocol defines:

```text
DEVICE_TYPE_BRIDGE  = 1
DEVICE_TYPE_SENSOR  = 2
DEVICE_TYPE_PENDANT = 3
```

Generic result codes:

```text
RESULT_ERROR = 0
RESULT_OK    = 1
```

GPIO states:

```text
GPIO_OFF = 0
GPIO_ON  = 1
```

---

# Message Types

| Message | Value | Direction |
|---|---:|---|
| `MSG_PAIR` | `0x01` | Device → Bridge |
| `MSG_PAIR_ACK` | `0x02` | Bridge → Device |
| `MSG_HEARTBEAT` | `0x03` | Device → Bridge |
| `MSG_HEARTBEAT_ACK` | `0x04` | Bridge → Device |
| `MSG_GPIO_COMMAND` | `0x10` | Device → Bridge |
| `MSG_GPIO_ACK` | `0x11` | Bridge → Device |
| `MSG_GPIO_STATUS` | `0x12` | Bridge → Device |
| `MSG_GPIO_STATUS_REQ` | `0x13` | Device → Bridge |
| `MSG_SENSOR_ASSIGNMENT` | `0x14` | Bridge → Sensor |
| `MSG_UART_COMMAND` | `0x20` | Device → Bridge |
| `MSG_UART_ACK` | `0x21` | Bridge → Device |
| `MSG_UART_RESPONSE` | `0x22` | Reserved / protocol response |
| `MSG_ERROR` | `0x7F` | Protocol error |

---

# MSG_PAIR

`MSG_PAIR` is sent by a new-protocol device to announce itself to the Bridge.

For the current Wireless Sensor implementation, the payload contains the Wireless Sensor MAC address:

```text
6 bytes
```

The Bridge uses this information to automatically identify the sensor.

The generic protocol validation permits a `MSG_PAIR` payload between 6 and 32 bytes.

The current Wireless Sensor implementation specifically uses 6 bytes.

---

# MSG_PAIR_ACK

For the Wireless Sensor, the `MSG_PAIR_ACK` payload is exactly 9 bytes.

| Byte | Field |
|---:|---|
| 0 | Device type |
| 1 | Protocol version |
| 2–7 | Sensor MAC |
| 8 | Assigned function |

The Bridge device type is:

```text
1
```

The protocol version is:

```text
1
```

An unassigned sensor uses:

```text
0xFF
```

for the assigned function.

The protocol defines:

```c
#define ESPNOW_PAIR_ACK_PAYLOAD_LEN 9
```

---

# Heartbeat

The Wireless Sensor sends a heartbeat every:

```text
2000 ms
```

The heartbeat acknowledgement timeout is:

```text
1000 ms
```

After three consecutive heartbeat failures, the sensor clears the current pairing and starts the pairing process again.

The sensor uses a session ID and sequence numbers to identify the current communication session.

---

# MSG_GPIO_COMMAND

`MSG_GPIO_COMMAND` contains a two-byte payload:

| Byte | Field |
|---:|---|
| 0 | Logical function |
| 1 | State |

State:

```text
0 = OFF
1 = ON
```

The logical function is one of:

```text
PROBE
XLIMIT
YLIMIT
ZLIMIT
CUSTOM1
CUSTOM2
CUSTOM3
```

The Bridge converts the logical state into the physical GPIO level according to the GPIO polarity configuration.

A new GPIO command is accepted only when the sending device is associated with the corresponding logical function.

---

# MSG_GPIO_ACK

`MSG_GPIO_ACK` contains:

| Byte | Field |
|---:|---|
| 0 | Logical function |
| 1 | State |
| 2 | Result |

Result:

```text
0 = ERROR
1 = OK
```

The session ID and sequence number are associated with the corresponding command.

---

# MSG_GPIO_STATUS_REQ

A device can request the current logical GPIO status using:

```text
MSG_GPIO_STATUS_REQ
```

The request has no payload.

---

# MSG_GPIO_STATUS

The Bridge responds using:

```text
MSG_GPIO_STATUS
```

The payload contains:

| Bytes | Field |
|---|---|
| 0–1 | Configured logical-function mask |
| 2–3 | Current logical state mask |

Both values are little-endian.

Bits 0..6 correspond to the logical function IDs 0..6.

These masks represent logical functions on the ESP-NOW protocol side.

They are different from the Bridge's physical GPIO polarity mask.

---

# MSG_SENSOR_ASSIGNMENT

`MSG_SENSOR_ASSIGNMENT` contains one byte:

```text
assigned function
```

Valid values are:

```text
0..6   = assigned GPIO function
0xFF   = no function assigned
```

The Bridge sends this message after the Wireless Sensor has been configured through the Web UI.

This allows the sensor to automatically know its assigned logical function.

The Bridge is authoritative for the association.

---

# MSG_UART_COMMAND

`MSG_UART_COMMAND` is part of the new ESP-NOW application protocol.

Direction:

```text
Device → Bridge
```

Payload:

```text
1..240 bytes
```

A zero-length payload is not valid.

The payload is forwarded to the central UART transmit queue.

This allows a supported ESP-NOW application device to send serial data to the controller through the Bridge.

---

# MSG_UART_ACK

`MSG_UART_ACK` is sent by the Bridge in response to a UART command.

Direction:

```text
Bridge → Device
```

The payload contains one result byte:

```text
0 = ERROR
1 = OK
```

---

# MSG_UART_RESPONSE

`MSG_UART_RESPONSE` is reserved for the new protocol's dedicated UART response path.

The current Bridge continues to handle existing raw UART responses according to the established legacy pendant communication.

This separation allows the new binary protocol to coexist with the existing legacy pendant communication.

---

# MSG_ERROR

`MSG_ERROR` is reserved for protocol-level errors.

Value:

```text
0x7F
```

The current protocol defines the error payload as non-empty, but does not otherwise define application-specific error codes in this README.

---

# Session and Sequence Handling

The new protocol uses:

- a session ID
- a 16-bit sequence number

The session ID is generated locally by the device.

The sequence number is encoded as a 16-bit little-endian value.

Acknowledgements and status responses are associated with the corresponding communication sequence.

Protocol invariants:

1. `VERSION` must equal protocol version `1`.
2. `PAYLOAD_LEN` must be no greater than 240 bytes.
3. Total frame length must equal `10 + PAYLOAD_LEN`.
4. `SESSION_ID` identifies the current communication session.
5. `SEQ` is interpreted per session and peer.
6. GPIO function IDs `0..6` are fixed on the wire.
7. `MSG_SENSOR_ASSIGNMENT` applies to new-protocol Wireless Sensors.
8. Existing legacy pendant frames remain outside this specification.

---

# Legacy Pendant Protocol

The existing ESP-NOW Pendant protocol is maintained separately from the new binary application protocol.

The legacy protocol must remain compatible with existing Pendant devices.

The legacy communication is intentionally not redefined by:

```text
include/espnow_protocol.h
```

The new Wireless Sensor/application protocol must not be used as a replacement for the existing legacy Pendant protocol.

This separation allows both communication systems to coexist in the same ESP-NOW handler.

---

# ESP-NOW GPIO Outputs

The Bridge supports seven logical ESP-NOW output functions:

- PROBE
- XLIMIT
- YLIMIT
- ZLIMIT
- CUSTOM1
- CUSTOM2
- CUSTOM3

Each logical function can be assigned to:

- Disabled
- GPIO4
- GPIO5
- GPIO6
- GPIO7
- GPIO10

The logical ESP-NOW protocol does not change when GPIO polarity is configured.

---

# GPIO Active HIGH / Active LOW

Each assigned ESP-NOW physical GPIO can independently be configured as:

```text
Active HIGH
```

or:

```text
Active LOW
```

The setting defines the physical electrical level corresponding to the logical ON state.

## Active HIGH

```text
Logical OFF → GPIO LOW
Logical ON  → GPIO HIGH
```

## Active LOW

```text
Logical OFF → GPIO HIGH
Logical ON  → GPIO LOW
```

The logical ESP-NOW protocol remains unchanged.

For example:

```text
MSG_GPIO_COMMAND
function = PROBE
state    = ON
```

always means:

```text
PROBE = ON
```

The polarity setting only determines the electrical level applied to the assigned physical GPIO.

---

# GPIO Polarity Storage

The Bridge stores the GPIO polarity configuration using:

```c
uint32_t espnow_gpio_active_low_mask;
```

The bit position corresponds to the physical GPIO number.

| Bit | GPIO |
|---:|---|
| 4 | GPIO4 |
| 5 | GPIO5 |
| 6 | GPIO6 |
| 7 | GPIO7 |
| 10 | GPIO10 |

A set bit means:

```text
Active LOW
```

A cleared bit means:

```text
Active HIGH
```

The valid GPIOs are:

```text
GPIO4
GPIO5
GPIO6
GPIO7
GPIO10
```

Example:

```text
GPIO4  = Active LOW
GPIO6  = Active LOW
GPIO10 = Active LOW
```

produces:

```text
0x450
```

The default mask is:

```text
0
```

which preserves the previous Active HIGH behavior.

---

# GPIO Startup State

Logical outputs are initialized to OFF.

The physical GPIO is initialized to the corresponding inactive electrical level.

Therefore:

### Active HIGH

```text
OFF → LOW
```

### Active LOW

```text
OFF → HIGH
```

This prevents an Active LOW output from being asserted during normal startup.

---

# GrblHAL Advanced

GrblHAL Advanced is an additional integration layer for controllers running GrblHAL.

It does not change the primary identity of the project as a universal UART serial bridge.

The Web UI provides a:

```text
GrblHAL Advanced Features
```

section.

The available features include:

- configurable State Pin
- Active Client Filter
- Reset on Telnet Disconnect

---

# GrblHAL State Pin

The State Pin is an output used to indicate the connection state according to the selected client source.

Available GPIOs:

```text
Disabled
GPIO4
GPIO5
GPIO6
GPIO7
GPIO10
```

The State Pin logic can be configured as:

```text
LOW when client connected
```

or:

```text
HIGH when client connected
```

The State Pin is independent from the ESP-NOW GPIO Active HIGH / Active LOW configuration.

---

# GrblHAL Active Client Filter

The Active Client Filter determines which client types are considered active.

Available modes:

## Any Client

Both:

```text
Telnet
ESP-NOW
```

are considered active.

## Telnet Only

Only Telnet clients are considered active.

## ESP-NOW Only

Only ESP-NOW clients are considered active.

This allows the connection logic to be adapted to the intended controller configuration.

---

# Reset on Telnet Disconnect

The GrblHAL Advanced configuration includes:

```text
Reset on Telnet Disconnect
```

When enabled, the Bridge sends:

```text
0x18
```

to the UART controller when the Telnet client disconnects.

This is the Ctrl-X character used for Grbl/GrblHAL reset handling.

The Web UI provides:

```text
Enabled  — send Ctrl-X
Disabled
```

This feature can be used to return the controller to a known state when a Telnet client disconnects.

---

# NVS Configuration Storage

The bridge stores persistent configuration using ESP-IDF NVS.

Stored configuration includes items such as:

- WiFi configuration
- network settings
- Access Point settings
- static IP configuration
- GrblHAL Advanced settings
- ESP-NOW configuration
- Wireless Sensor assignments
- ESP-NOW GPIO assignments
- ESP-NOW GPIO polarity

## GPIO Polarity Migration

The GPIO polarity feature includes backward compatibility for older configurations.

Older configurations that do not contain the polarity mask are migrated with:

```text
espnow_gpio_active_low_mask = 0
```

This preserves the previous Active HIGH behavior.

The stored mask is sanitized against the valid ESP-NOW GPIO bits.

Duplicate ESP-NOW physical GPIO assignments are also validated when configuration is loaded; an invalid duplicate assignment is disabled rather than allowing two logical functions to control the same physical output.

---

# Web API

The Web UI communicates with the firmware through HTTP API endpoints.

## Status

```text
GET /api/status
```

Returns WiFi and ESP-NOW status information.

```text
GET /api/sysinfo
```

Returns system information such as memory and firmware information.

---

# WiFi Networks API

```text
GET /api/networks
```

Returns configured WiFi networks.

```text
POST /api/networks/add
```

Adds a WiFi network.

```text
POST /api/networks/remove
```

Removes a WiFi network.

---

# Settings API

```text
GET /api/settings
```

Reads the current settings.

The settings include GrblHAL Advanced parameters and ESP-NOW GPIO configuration.

The ESP-NOW GPIO polarity mask is exposed as:

```text
espnow_gpio_active_low_mask
```

Update settings:

```text
POST /api/settings
```

The firmware extracts and sanitizes the GPIO polarity mask before storing it.

---

# Debug API

```text
GET /api/debug
```

Reads the current debug configuration.

```text
POST /api/debug
```

Updates the debug configuration.

---

# System Control API

Reset:

```text
POST /api/reset
```

Reboot:

```text
POST /api/reboot
```

---

# ESP-NOW Web API

The Web UI provides dedicated endpoints for Wireless Sensor management.

## Detected Sensors

```text
GET /api/espnow/sensors
```

Returns the Wireless Sensor information detected by the Bridge.

The sensor MAC address is obtained automatically from the ESP-NOW pairing process.

It does not need to be manually entered.

## Assign Sensor

```text
POST /api/espnow/assign
```

Assigns a detected Wireless Sensor to a logical ESP-NOW function.

The valid logical functions are:

```text
0 = PROBE
1 = XLIMIT
2 = YLIMIT
3 = ZLIMIT
4 = CUSTOM1
5 = CUSTOM2
6 = CUSTOM3
0xFF = Unassigned
```

After assignment, the Bridge sends:

```text
MSG_SENSOR_ASSIGNMENT
```

to the Wireless Sensor.

## Unassign Sensor

```text
POST /api/espnow/unassign
```

Removes the logical function assignment from a Wireless Sensor.

The sensor can therefore be placed into the unassigned state:

```text
0xFF
```

---

# OTA API

Firmware upload is performed through:

```text
POST /update
```

The Web UI uses this endpoint for OTA firmware updates.

The OTA endpoint is separate from the complete-flash procedure described in the firmware flashing section.

---

# Debug Logging

The firmware uses ESP-IDF logging facilities.

Debug information can be useful for diagnosing:

- WiFi connection problems
- ESP-NOW initialization
- ESP-NOW pairing
- Wireless Sensor communication
- TCP/Telnet connections
- NVS configuration
- GPIO assignments
- GrblHAL connection state

When troubleshooting ESP-NOW devices, the serial monitor is particularly useful because pairing and communication events are reported there.

---

# Project Structure

The project is organized as an ESP-IDF application.

The main structure is:

```text
ESP32-C3-Telnet-EspNow-Bridge-V1.3.1/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
├── partitions.csv
│
├── include/
│   ├── config.h
│   ├── MyWiFiData.h
│   ├── my_logs.h
│   ├── espnow_config.h
│   ├── espnow_protocol.h
│   ├── espnow_handler.h
│   ├── tcp_server.h
│   ├── serial_handler.h
│   ├── WiFiSelector.h
│   ├── nvs_storage.h
│   ├── web_server.h
│   ├── wifi_manager.h
│   └── grblHAL_advanced.h
│
├── src/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── tcp_server.c
│   ├── espnow_handler.c
│   ├── serial_handler.c
│   ├── wifi_manager.c
│   ├── WiFiSelector.c
│   ├── nvs_storage.c
│   ├── web_server.c
│   └── grblHAL_advanced.c
│
├── firmware/
│   ├── bootloader.bin
│   ├── partition-table.bin
│   ├── ESP32-C3-Serial-Bridge.bin
│   ├── flash.bat
│   ├── flash_no_erase.bat
│   └── flash.sh
│
├── examples/
│   └── ...
│
└── photos/
    └── ...
```

The `partitions.csv` file is located in the project root and defines the OTA partition layout.

The `examples/` directory contains the example projects associated with the supported ESP-NOW devices.

The `photos/` directory contains project hardware/reference images.

There is no `data/` directory required by the current ESP-IDF project.

---

# Troubleshooting

## The Bridge does not connect to WiFi

Check:

1. The WiFi network is available.
2. The SSID and password are correct.
3. If building from source, the network is present in `MyWiFiData.h`.
4. If using precompiled firmware, the network is configured through the Web UI.
5. The ESP32-C3 is operating on the expected WiFi channel.
6. The serial log for WiFi errors.

If Station mode cannot connect, use the Access Point fallback.

Connect to:

```text
ESP32-C3-Serial-Bridge
```

Password:

```text
12345678
```

Then open:

```text
http://192.168.4.1
```

---

## The Web UI cannot be opened

Check that the computer or mobile device is connected to the same network as the ESP32-C3.

In AP mode:

```text
http://192.168.4.1
```

In Station mode, use the IP address assigned/configured for the bridge.

---

## A second Telnet client cannot connect

The firmware supports up to four configured TCP/Telnet clients.

However, the Web UI also consumes HTTP/TCP resources.

If the Web UI is open, additional Telnet connections may fail.

Close the Web UI/browser completely and retry the Telnet connection.

This is a resource limitation of the current implementation and does not mean that the configured Telnet limit has been changed.

---

## ESP-NOW pairing does not work

Check:

1. Both devices are using the correct WiFi channel.
2. The ESP-NOW device is powered correctly.
3. The serial monitor is open.
4. The Wireless Sensor is sending `MSG_PAIR`.
5. The protocol versions are compatible.
6. The ESP-NOW peer limit has not been exhausted.

The Wireless Sensor MAC address does not have to be manually entered into the Web UI.

The Bridge obtains the MAC address automatically from `MSG_PAIR`.

---

## Wireless Sensor is detected but not assigned

Open the Web UI and configure the detected Wireless Sensor.

Select the required logical function.

The Bridge sends:

```text
MSG_SENSOR_ASSIGNMENT
```

to the sensor.

The sensor then knows its assigned logical function.

---

## ESP-NOW GPIO output has inverted behavior

Check the polarity configuration for the assigned physical GPIO.

For:

```text
Active HIGH
```

logical ON produces:

```text
HIGH
```

For:

```text
Active LOW
```

logical ON produces:

```text
LOW
```

The logical ESP-NOW protocol itself does not change.

---

## GrblHAL State Pin behaves incorrectly

Check:

1. The selected GPIO.
2. State Pin logic.
3. Client Source.
4. Whether the expected Telnet or ESP-NOW client is connected.
5. Whether the selected GPIO is being used by another function.

Available State Pin GPIOs:

```text
GPIO4
GPIO5
GPIO6
GPIO7
GPIO10
```

---

## GrblHAL does not reset after Telnet disconnect

Check:

```text
GrblHAL Advanced
    Reset on Telnet Disconnect
```

The option must be enabled.

When enabled, the Bridge sends:

```text
0x18
```

to the controller after a Telnet disconnect.

---

## Configuration was lost after flashing

If the firmware was flashed using:

```text
flash.bat
```

or:

```text
flash.sh
```

the complete flash was erased first.

This also erases NVS.

Reconfigure the bridge using the Web UI.

To preserve NVS during a manual firmware update, use:

```text
flash_no_erase.bat
```

or flash the images manually without using:

```text
erase_flash
```

OTA updates do not use the complete-flash procedure.

---

## OTA update reports "No OTA partition available"

Check that the firmware was built with the custom OTA partition table.

The partition table must contain:

```text
otadata
ota_0
ota_1
```

The expected application partitions are:

```text
ota_0    0x20000
ota_1    0x1F0000
```

The ESP32-C3 flash configuration must be:

```text
4 MB
```

The generated partition table should be:

```text
build/partition_table/partition-table.bin
```

If the firmware needs to be rebuilt after changing partition settings, perform:

```bash
idf.py fullclean
idf.py build
```

and verify the generated partition table before flashing.

---

# Example Wireless Sensor Workflow

A typical Wireless Sensor installation works as follows:

```text
1. Power on the Wireless Sensor
             │
             ▼
2. Sensor determines the ESP-NOW WiFi channel
             │
             ▼
3. Sensor sends MSG_PAIR
             │
             ▼
4. Bridge automatically detects the sensor MAC
             │
             ▼
5. Bridge sends MSG_PAIR_ACK
             │
             ▼
6. Sensor establishes the Bridge peer
             │
             ▼
7. Sensor becomes available to the Web UI
             │
             ▼
8. User selects the logical function
             │
             ▼
9. Bridge sends MSG_SENSOR_ASSIGNMENT
             │
             ▼
10. Sensor knows its assigned function
             │
             ▼
11. Sensor sends GPIO commands
             │
             ▼
12. Bridge converts the logical state
    into the configured physical GPIO level
```

The MAC address does not have to be entered manually.

---

# Example GPIO Configuration

Example logical assignment:

```text
PROBE   → GPIO4
XLIMIT  → GPIO5
YLIMIT  → GPIO6
ZLIMIT  → GPIO7
CUSTOM1 → GPIO10
```

Example polarity:

```text
GPIO4  → Active LOW
GPIO5  → Active HIGH
GPIO6  → Active LOW
GPIO7  → Active HIGH
GPIO10 → Active LOW
```

The logical protocol remains:

```text
PROBE = ON
XLIMIT = ON
YLIMIT = ON
```

The physical GPIO levels become:

```text
GPIO4 = LOW
GPIO5 = HIGH
GPIO6 = LOW
```

This allows the Bridge to adapt the same logical ESP-NOW protocol to external hardware with different electrical polarity requirements.

---

# Default Configuration Summary

| Function | Default |
|---|---|
| UART | UART0 |
| UART TX | GPIO21 |
| UART RX | GPIO20 |
| UART baud rate | 115200 |
| Telnet port | 23 |
| Maximum TCP clients | 4 |
| Maximum ESP-NOW peers total | 16 |
| Flash size | 4 MB |
| Partition table | Custom `partitions.csv` |
| OTA partitions | `ota_0` + `ota_1` |
| AP SSID | ESP32-C3-Serial-Bridge |
| AP password | 12345678 |
| AP channel | 6 |
| AP IP | 192.168.4.1 |
| ESP-NOW GPIO polarity | Active HIGH |
| ESP-NOW GPIO polarity mask | `0` |

---

# Important Notes

## UART

The bridge is intended to operate as a transparent UART communication bridge.

The connected controller must use compatible UART voltage levels.

The ESP32-C3 UART must not be connected directly to a true RS-232 electrical interface.

---

## WiFi

The bridge can operate as a WiFi Station or provide an Access Point fallback.

When building from source, default WiFi networks can be compiled into the firmware through:

```text
include/MyWiFiData.h
```

When using precompiled firmware, WiFi networks must be configured through the Web UI.

---

## ESP-NOW

ESP-NOW devices must use the appropriate radio channel.

The existing legacy pendant protocol is preserved separately from the new binary application protocol.

The new binary application protocol is defined by:

```text
include/espnow_protocol.h
```

The maximum ESP-NOW peer count is:

```text
16 total peers
```

shared by the legacy and new protocols.

---

## Wireless Sensor

The Wireless Sensor MAC address is automatically detected from:

```text
MSG_PAIR
```

It does not need to be entered manually in the Web UI.

The Web UI is used to assign the detected sensor to its logical function.

The Bridge then sends:

```text
MSG_SENSOR_ASSIGNMENT
```

to the sensor.

---

## GPIO Polarity

Active HIGH / Active LOW affects the physical electrical level only.

The logical ESP-NOW protocol remains unchanged.

---

## GrblHAL

GrblHAL Advanced is an optional integration layer.

The bridge remains a general-purpose UART serial bridge.

---

## Web UI / Telnet

The current implementation shares TCP resources between the Web UI and Telnet.

Leaving the Web UI open can reduce the resources available for additional Telnet connections.

Closing the browser can release those resources.

The Telnet implementation is intentionally not changed as part of this README.

---

## Flashing

The complete firmware flashing scripts:

```text
flash.bat
flash.sh
```

erase the complete ESP32-C3 flash before installing the firmware.

Existing NVS configuration is therefore removed.

The no-erase Windows script:

```text
flash_no_erase.bat
```

does not erase the flash and preserves the NVS configuration.

OTA firmware updates are separate and do not use the complete-flash procedure.

---

## Partition Table

The project uses the custom partition table:

```text
partitions.csv
```

The application partitions are:

```text
ota_0
ota_1
```

The generated binary is:

```text
partition-table.bin
```

The application firmware is flashed at:

```text
0x20000
```

---

# License

This project is released under the MIT License.

The repository should contain the license using the standard filename:

```text
LICENSE
```

MIT License:

```text
MIT License

Copyright (c) 2026 Ennio Sesana

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

# Project Status

The ESP32-C3 Serial Bridge is an ESP-IDF-based UART-to-WiFi bridge providing:

- UART communication
- WiFi connectivity
- WiFi Station mode
- WiFi Access Point fallback
- Telnet access
- Web-based configuration
- OTA firmware update
- ESP-NOW communication
- legacy ESP-NOW pendant compatibility
- new binary ESP-NOW application protocol
- Wireless Sensor support
- automatic Wireless Sensor detection
- logical Wireless Sensor assignment
- configurable ESP-NOW GPIO outputs
- Active HIGH / Active LOW GPIO polarity
- persistent NVS configuration
- optional GrblHAL Advanced integration

The project is designed to remain useful as a general-purpose serial bridge while providing additional ESP-NOW and GrblHAL functionality for supported applications.

The legacy Pendant communication and the new ESP-NOW application protocol are intentionally maintained as separate protocol layers.
