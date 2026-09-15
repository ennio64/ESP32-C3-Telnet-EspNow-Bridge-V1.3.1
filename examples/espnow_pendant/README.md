# CNC_Pendant_DEMO – ESP-NOW CNC Web Pendant

Demo of a **wireless CNC Web Pendant based on ESP32**, using **ESP-NOW** for communication with a CNC Bridge and an integrated **Web UI** for machine control and real-time DRO display.

This project is intended as a **demo and development foundation** for a future, more complete CNC pendant, with the goal of experimenting with:

- low-latency ESP-NOW communication;
- G-code and realtime command transmission;
- automatic pairing with the Bridge;
- real-time DRO display;
- WebSocket-based browser updates;
- local Web UI;
- connection and Bridge timeout management.

> **Note:** This project is a DEMO. It is not intended to replace a professional CNC pendant or a machine safety system.

---

# 🚀 Features

The demo includes:

- **ESP-NOW** wireless communication with the CNC Bridge
- Automatic pairing with the Bridge
- Automatic Bridge loss detection and re-pairing
- G-code transmission via ESP-NOW
- GrblHAL realtime command transmission
- `sequence + ACK` mechanism for G-code lines
- Real-time DRO:
  - Machine State
  - X
  - Y
  - Z
- Integrated Web UI
- WebSocket-based real-time updates
- Dark/industrial-style interface
- Quick machine control commands
- Manual G-code input
- Automatic G-code test
- Debug console
- Serial test commands

---

# 🧩 Architecture

The demo architecture is:

```text
                ┌──────────────────────┐
                │      Web Browser     │
                │                      │
                │  CNC Web Pendant UI  │
                └──────────┬───────────┘
                           │
                    HTTP / WebSocket
                           │
                           ▼
                ┌──────────────────────┐
                │      ESP32 Pendant   │
                │                      │
                │  WebServer           │
                │  WebSocket           │
                │  ESP-NOW             │
                └──────────┬───────────┘
                           │
                        ESP-NOW
                           │
                           ▼
                ┌──────────────────────┐
                │      CNC Bridge      │
                │                      │
                │     ESP-NOW          │
                │        │             │
                │        ▼             │
                │       UART           │
                └──────────┬───────────┘
                           │
                           ▼
                ┌──────────────────────┐
                │       GrblHAL        │
                │     CNC Controller   │
                └──────────────────────┘
```

The Pendant **does not communicate directly with GrblHAL**.

The command path is:

```text
Browser
   ↓
HTTP POST
   ↓
ESP32 Pendant
   ↓
ESP-NOW
   ↓
CNC Bridge
   ↓
UART
   ↓
GrblHAL
```

The DRO path is the reverse:

```text
GrblHAL
   ↓
UART
   ↓
CNC Bridge
   ↓
ESP-NOW
   ↓
ESP32 Pendant
   ↓
WebSocket
   ↓
Browser
```

---

# 📡 ESP-NOW Communication

The Pendant uses ESP-NOW to communicate with the Bridge.

During startup, the Pendant:

1. configures the ESP32 in `WIFI_STA` mode;
2. disconnects from any previous Wi-Fi connection;
3. scans for configured Wi-Fi networks;
4. determines the Wi-Fi channel;
5. configures ESP-NOW on that channel;
6. registers the ESP-NOW receive callback;
7. adds the broadcast peer;
8. sends:

```text
PAIR
```

The Bridge responds with:

```text
PAIR_OK
```

When `PAIR_OK` is received, the Pendant stores the Bridge MAC address and adds the Bridge as an ESP-NOW unicast peer.

ESP-NOW peer encryption is disabled in this demo.

---

# 🔗 Pairing

Pairing is handled automatically.

The Pendant maintains:

```cpp
bool paired;
bool bridge_peer_added;
```

When pairing is completed:

```text
PAIR
   ↓
Bridge
   ↓
PAIR_OK
   ↓
Pendant
```

The Bridge MAC address received with `PAIR_OK` is stored and used for subsequent unicast communication.

The browser also receives a WebSocket message that updates the connection status.

### Bridge connected

```text
🟢 Bridge connected
```

### Bridge not connected

```text
🔴 Wait pairing...
```

---

# 🔄 Automatic Re-Pairing

The Pendant continuously monitors communication with the Bridge.

The current firmware uses:

```cpp
#define HEARTBEAT_TIMEOUT_MS 10000
```

If no data is received from the Bridge for 10 seconds, the pairing is considered lost.

The following function is then executed:

```cpp
resetPairing();
```

The Pendant returns to pairing mode and periodically sends:

```text
PAIR
```

New pairing attempts are made every:

```cpp
#define PAIR_RETRY_INTERVAL_MS 5000
```

Therefore, the current pairing retry interval is 5 seconds.

---

# 📶 Wi-Fi and ESP-NOW Channel

Wi-Fi is used to:

- determine the radio channel;
- provide the IP connectivity required by the Web UI.

Known Wi-Fi networks are defined in the firmware:

```cpp
struct WiFiNetwork {
    const char* ssid;
    const char* password;
};

const WiFiNetwork knownNetworks[] = {
    {"SSID1", "PASSWORD1"},
    {"SSID2", "PASSWORD2"},
    {"SSID3", "PASSWORD3"}
};
```

The firmware scans the available Wi-Fi networks.

If one or more configured networks are found, the Pendant selects the matching network with the strongest RSSI and uses its channel for ESP-NOW.

Example:

```text
SSID1    → Channel 6
SSID2    → Channel 11
SSID3    → Channel 1
```

If `SSID2` is found with the strongest signal, the Pendant uses:

```text
ESP-NOW Channel = 11
```

If no known network is found, the firmware uses the default ESP-NOW channel:

```text
Channel = 11
```

> The Pendant and the Bridge must use the same ESP-NOW channel.

---

# 🌐 Wi-Fi Connection

After determining the channel, the Pendant connects to the selected Wi-Fi network.

The current firmware uses:

```cpp
WiFi.begin(sta_ssid, sta_password);
```

The Wi-Fi connection provides IP connectivity for the Web UI.

The assigned IP address is printed to the Serial Monitor during startup.

The Web UI can then be accessed through:

```text
http://<PENDANT_IP>
```

Example:

```text
http://192.168.1.50
```

> The current demo requires a configured Wi-Fi network for normal Web UI operation. The Wi-Fi scan is also used to determine the ESP-NOW channel.

---

# 📦 ESP-NOW G-code Protocol

G-code lines are not sent as plain text.

Each packet contains a small header:

```text
Byte 0     Sequence LOW
Byte 1     Sequence HIGH
Byte 2     Length LOW
Byte 3     Length HIGH
Byte 4...  G-code
```

Packet structure:

```text
┌──────────────┬──────────────┬────────────────────┐
│ Sequence 2 B │ Length 2 B   │ G-code             │
└──────────────┴──────────────┴────────────────────┘
```

The maximum G-code payload is:

```text
246 bytes
```

Therefore, the maximum ESP-NOW packet size used by this protocol is:

```text
4 + 246 = 250 bytes
```

---

# 🔢 Sequence Number

Each command uses a progressive sequence number:

```cpp
uint16_t sequence;
```

Example:

```text
Command 1 → sequence 0
Command 2 → sequence 1
Command 3 → sequence 2
Command 4 → sequence 3
...
```

The Bridge returns an ACK containing the received sequence number.

The Pendant waits for the corresponding ACK before considering the G-code line successfully transmitted.

The sequence number is also used by realtime command packets.

---

# ✅ ACK and Timeout

For each normal G-code line, the Pendant waits for an ACK.

The timeout is:

```text
3000 ms
```

The Bridge ACK format is:

```text
[seq_low][seq_high][0x01]
```

When the expected ACK is received:

```text
✓
```

the sequence number is incremented and the next line can be sent.

If the ACK is not received:

```text
⚠️ Timeout ACK
```

the function returns an error.

This creates a sequential transmission flow:

```text
LINE 1
  ↓
ACK
  ↓
LINE 2
  ↓
ACK
  ↓
LINE 3
  ↓
ACK
...
```

The current demo does not implement an independent retransmission queue. If an ACK times out, that line is reported as failed.

---

# ⚡ Realtime Commands

GrblHAL realtime commands are handled separately from normal G-code lines.

The commands are defined as:

```cpp
#define CMD_RESET        0x18
#define CMD_CYCLE_START 0x81
#define CMD_FEED_HOLD    0x82
```

The Pendant provides:

```cpp
sendReset();
sendCycleStart();
sendFeedHold();
```

Realtime commands are transmitted without adding a newline.

The packet structure is:

```text
Byte 0     Sequence LOW
Byte 1     Sequence HIGH
Byte 2     Length LOW
Byte 3     Length HIGH
Byte 4     Command
```

The payload length is `1`.

Examples:

```text
RESET       → 0x18
CYCLE START → 0x81
FEED HOLD   → 0x82
```

Realtime commands are sent without waiting for the normal G-code ACK mechanism.

---

# 🎛️ Available Commands

The Web UI provides several groups of commands.

## Machine Control

```text
RESET
HOLD
RESUME
HOME
UNLOCK
```

| Button | Command |
|---|---|
| Reset | Ctrl-X `0x18` |
| Hold | `!` |
| Resume | `~` |
| Home | `$H` |
| Unlock | `$X` |

---

# 🔧 Modes

The following commands are available:

```text
G90
G91
G10
```

### G90

Absolute coordinate mode:

```text
G90
```

### G91

Incremental coordinate mode:

```text
G91
```

### G10

The button sends:

```text
G10 P0 L20 P0
```

---

# ➡️ Quick Moves

The Web UI includes several quick movement commands:

```text
G0 X0
G0 X10

G0 Y0
G0 Y10

G0 Z0
G0 Z10
```

These commands are provided for demonstration purposes only.

> Always verify coordinate mode, offsets, machine limits and machine safety conditions before using automatic movements.

---

# 📝 Manual G-code

The Web UI provides a text field for entering a custom command.

Example:

```text
G1 X50 F100
```

The command is sent to the Pendant through the HTTP `/command` endpoint and is then transmitted to the Bridge through ESP-NOW.

Normal G-code is converted to uppercase and a newline is added when required.

---

# 📦 G-code Program Transmission

The demo can also split a G-code program into individual lines.

The function:

```cpp
sendGCodeEspNowStream()
```

performs:

1. line parsing;
2. sequential transmission;
3. ACK waiting for each line;
4. completed-line processing.

Example:

```text
G0 X10
G0 X0
G0 X10
G0 X0
...
M30
```

Each line is transmitted individually.

The next line is sent only after the previous line has been acknowledged.

---

# 🧪 RUN TEST

The Web UI contains:

```text
RUN TEST (401 lines)
```

The test program is generated by:

```cpp
generateTestGcode(200, 10.0, 0.0);
```

This generates 200 movement pairs:

```text
G0 X10.000
G0 X0.000
```

for a total of:

```text
400 movement lines
```

and then adds:

```text
M30
```

Therefore, the complete generated program contains:

```text
401 lines
```

The test is useful for checking:

- ESP-NOW communication;
- ACK handling;
- sequence numbering;
- Bridge handling of a long command sequence;
- DRO updates during execution.

---

# 📺 DRO – Digital Read Out

The Pendant analyzes machine status messages received from the Bridge.

The expected format contains a structure similar to:

```text
<Idle|MPos:0.000,0.000,0.000|...>
```

The Pendant extracts:

```text
Machine State
X
Y
Z
```

and updates:

```cpp
float posX;
float posY;
float posZ;

String machineState;
```

The current implementation extracts the `MPos:` values for X, Y and Z.

---

# 🔄 DRO Updates via WebSocket

The DRO is not continuously updated through HTTP polling.

When the Pendant receives a new machine status:

1. the ESP-NOW packet is analyzed;
2. the machine state is extracted;
3. X/Y/Z are extracted;
4. a JSON message is created;
5. the JSON is sent to connected WebSocket clients.

Example:

```json
{
  "type": "dro",
  "state": "Idle",
  "x": 0.000,
  "y": 0.000,
  "z": 0.000
}
```

The browser receives the message and immediately updates the DRO.

---

# 🔌 Web Server

The Pendant includes an HTTP Web Server on port:

```text
80
```

Available endpoints include:

```text
/
```

Main Web UI.

```text
/command
```

HTTP POST command endpoint.

```text
/test
```

Starts the automatic G-code test.

```text
/ping
```

Used by the Web UI to check communication with the Pendant.

---

# 🔗 WebSocket Server

The WebSocket Server uses port:

```text
81
```

The browser automatically connects to:

```text
ws://<PENDANT_IP>:81/
```

WebSocket communication is used mainly for:

- pairing status;
- real-time DRO updates;
- Pendant status information.

---

# 🖥️ Web UI

The Web page is completely embedded in the firmware:

```cpp
const char index_html[] PROGMEM
```

No external HTML files are required.

The interface contains:

```text
┌─────────────────────────────┐
│       CNC WEB PENDANT       │
├─────────────────────────────┤
│      Connection Status      │
├─────────────────────────────┤
│            DRO              │
│                             │
│ Status    X    Y    Z       │
├──────────┬──────────┬───────┤
│ Machine  │ Modes    │ Quick │
│ Control  │ Utility  │ Moves │
├──────────┴──────────┴───────┤
│       G-code / Test         │
├─────────────────────────────┤
│          Console            │
└─────────────────────────────┘
```

The interface is responsive and can also be used from mobile devices.

The Web UI communicates with the firmware using HTTP and WebSocket.

---

# 🖥️ Serial Console

The Pendant also provides a test interface through the Serial Monitor.

Baud rate:

```text
115200
```

Available commands include:

```text
Test
reset
start
hold
```

Any other input string is interpreted as G-code.

Example:

```text
G0 X10
```

or:

```text
G1 X50 F100
```

---

# 🧪 Serial Debug

During operation, the Pendant prints diagnostic information such as:

```text
BRIDGE FOUND!
MAC: XX:XX:XX:XX:XX:XX
Channel: 11
```

During G-code transmission:

```text
📤 Sending [seq=10]: G0 X10
 ✓
```

In case of an ACK timeout:

```text
⚠️ Timeout ACK
```

During pairing:

```text
❌ Bridge not found, retrying...
```

---

# 🛠️ Requirements

Required hardware/software:

- ESP32 board with Wi-Fi and ESP-NOW support
- Compatible ESP-NOW CNC Bridge
- CNC controller compatible with the Bridge protocol
- GrblHAL or equivalent CNC controller
- Wi-Fi network for Web UI access
- Web browser

The ESP32 Pendant must operate on the same ESP-NOW radio channel as the Bridge.

---

# ▶️ Installation

The project can be compiled using an Arduino-compatible ESP32 development environment.

Upload:

```text
CNC_Pendant_DEMO.ino
```

to the ESP32 board.

Before uploading, configure the Wi-Fi networks in:

```cpp
const WiFiNetwork knownNetworks[] = {
    {"SSID1", "PASSWORD1"},
    {"SSID2", "PASSWORD2"},
    {"SSID3", "PASSWORD3"}
};
```

After uploading:

1. power on the Pendant;
2. power on the CNC Bridge;
3. wait for the Wi-Fi scan;
4. wait for the Wi-Fi connection;
5. wait for `PAIR_OK`;
6. check the IP address in the Serial Monitor;
7. open the IP address in a browser.

Example:

```text
http://192.168.1.50
```

---

# 🔐 Security

This demo does not implement a complete CNC security system.

In particular:

- commands can be sent directly from the Web UI;
- there is no Web authentication;
- there is no HTTPS/TLS;
- ESP-NOW encryption is not enabled in this demonstration;
- Home, Unlock and movement commands are directly accessible from the UI.

ESP-NOW peers are configured with:

```cpp
encrypt = false;
```

For real CNC machine operation, safety functions must be designed separately, including:

- emergency stop;
- hardware/software interlocks;
- error handling;
- user authorization;
- communication-loss handling;
- software and hardware limits;
- safe behavior after reboot.

---

# 🧱 Firmware Logical Structure

The firmware can be logically divided into the following blocks:

```text
SETUP
 │
 ├── Serial
 ├── Wi-Fi
 ├── Channel Scan
 ├── ESP-NOW
 ├── Pairing
 ├── Web Server
 └── WebSocket
       │
       ▼
LOOP
 │
 ├── HTTP requests
 ├── WebSocket
 ├── Pairing supervision
 ├── Serial commands
 └── Bridge timeout
```

Main functions include:

```cpp
scanForChannel()
resetPairing()
onDataRecv()
sendGcodeLine()
sendGCodeEspNowStream()
sendRealtime()
parseAndSendDRO()
generateTestGcode()
```

---

# 🧱 Extensibility

The project is intentionally simple so that additional hardware and functions can be added later.

Possible future developments include:

- Jogging encoder;
- MPG;
- physical buttons;
- feed override;
- spindle override;
- mode selection;
- touchscreen;
- additional axis controls;
- Feed / RPM display;
- extended machine status;
- G-code loading from SD card;
- G-code file management;
- macros;
- physical keyboard;
- battery operation;
- sleep mode;
- advanced ESP-NOW connection-loss handling;
- ESP-NOW encryption;
- Web authentication;
- multi-pendant support.

---

# ⚠️ Current Demo Limitations

This version should be considered a **functional prototype**.

It is not a complete CNC control system.

In particular, it must not be considered:

- a safety system;
- a standalone CNC controller;
- a replacement for an emergency stop;
- an industrial control system;
- a final professional CNC pendant.

Its primary purpose is to provide a working foundation for experimenting with:

```text
ESP32
  ↕
ESP-NOW
  ↕
CNC Bridge
  ↕
GrblHAL
```

together with the corresponding Web interface.

---

# 🎯 Project Goal

The project provides a clean starting point for developing a **wireless CNC Web Pendant**.

The architecture is intentionally divided into three main layers:

```text
┌─────────────────────────────┐
│          Web UI             │
│       User Interface        │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│       Pendant Logic         │
│  Commands / DRO / Pairing   │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│          ESP-NOW            │
│      Wireless Transport     │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│         CNC Bridge          │
└─────────────────────────────┘
```

This structure makes it possible to add new hardware and functionality later without having to completely redesign the basic communication layer.

---

# 📄 License

Released under the **MIT License**.

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