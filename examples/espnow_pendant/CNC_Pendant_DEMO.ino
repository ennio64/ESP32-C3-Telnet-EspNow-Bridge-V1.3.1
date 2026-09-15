// ========================================
// CNC Web Pendant – ESP‑NOW based – DEMO
// ========================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <vector>

#define SERIAL_BAUD 115200

// --- COMANDI REALTIME GRBLHAL ---
#define CMD_RESET        0x18
#define CMD_CYCLE_START  0x81
#define CMD_FEED_HOLD    0x82

// --- TIMEOUT E RETRY ---
#define HEARTBEAT_TIMEOUT_MS    10000
#define PAIR_RETRY_INTERVAL_MS  5000

// --- RETI CONOSCIUTE (SSID + password centralizzate) ---
struct WiFiNetwork {
    const char* ssid;
    const char* password;
};

const WiFiNetwork knownNetworks[] = {
    {"SSID1",     "PASSWORD1"},      
    {"SSID2",   "PASSWORD2"},      
    {"SSID3",  "PASSWORD3"}      
};
const int knownCount = sizeof(knownNetworks) / sizeof(knownNetworks[0]);

// --- VARIABILI GLOBALI ---
uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t bridge_mac[6] = {0};
bool paired = false;
bool bridge_peer_added = false;
uint32_t last_print = 0;
uint32_t last_data_received = 0;
String buffer = "";
uint16_t sequence = 0;
unsigned long pairStartTime = 0;
int currentChannel = 11;
volatile uint16_t lastAckSeq = 0xFFFF;

// --- DRO ---
float posX = 0.0, posY = 0.0, posZ = 0.0;
String machineState = "Idle";

// --- WEB SERVER E WEBSOCKET ---
WebServer server(80);
WebSocketsServer webSocket(81);

// ========== SCANSIONE CANALE (restituisce l'indice della rete migliore) ==========
int scanForChannel() {
    Serial.println("\n🔍 Scansione reti WiFi per trovare il canale...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("❌ Nessuna rete trovata");
        WiFi.scanDelete();
        return -1;
    }
    Serial.printf("📡 Trovate %d reti WiFi:\n", n);
    int bestIndex = -1;
    int bestRSSI = -127;
    int foundChannel = -1;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int channel = WiFi.channel(i);
        int rssi = WiFi.RSSI(i);
        Serial.printf("   %s (Canale: %d, RSSI: %d dBm)\n", ssid.c_str(), channel, rssi);
        for (int j = 0; j < knownCount; j++) {
            if (ssid == knownNetworks[j].ssid) {
                if (rssi > bestRSSI) {
                    bestRSSI = rssi;
                    bestIndex = j;
                    foundChannel = channel;
                }
                break;
            }
        }
    }
    WiFi.scanDelete();
    if (bestIndex >= 0) {
        Serial.printf("\n✅ Rete trovata: %s\n", knownNetworks[bestIndex].ssid);
        Serial.printf("   Canale: %d\n", foundChannel);
        Serial.printf("   RSSI: %d dBm\n", bestRSSI);
        currentChannel = foundChannel;
        return bestIndex;
    }
    Serial.println("\n⚠️ Nessuna rete conosciuta trovata, uso canale default 11");
    return -1;
}

// ========== RESET PAIRING ==========
void resetPairing() {
    if (paired) Serial.println("\n🔄 Bridge perso! Reset pairing in corso...");
    paired = false;
    bridge_peer_added = false;
    memset(bridge_mac, 0, 6);
    lastAckSeq = 0xFFFF;
    sequence = 0;
    if (bridge_mac[0] != 0) esp_now_del_peer(bridge_mac);
    esp_now_send(broadcastMac, (uint8_t*)"PAIR", 4);
    pairStartTime = millis();
    // Notifica al browser
    StaticJsonDocument<64> statusDoc;
    statusDoc["type"] = "status";
    statusDoc["paired"] = false;
    String statusMsg;
    serializeJson(statusDoc, statusMsg);
    webSocket.broadcastTXT(statusMsg);
}

// ========== VALIDAZIONE CARATTERI ==========
bool isValidGcodeChar(char c) {
    return (c >= ' ' && c <= '~') || c == '\n' || c == '\r';
}

// ========== GENERATORE G-CODE TEST ==========
String generateTestGcode(int repetitions, float x1 = 10.0, float x2 = 0.0) {
    String out;
    out.reserve(repetitions * 20 + 10);
    for (int i = 0; i < repetitions; i++) {
        out += "G0 X"; out += String(x1, 3); out += "\n";
        out += "G0 X"; out += String(x2, 3); out += "\n";
    }
    out += "M30\n";
    return out;
}

// ========== PARSING PER DRO ==========
void parseAndSendDRO(const String& status) {
    int start = status.indexOf('<');
    int end = status.indexOf('>');
    if (start == -1 || end == -1 || end <= start) return;
    String content = status.substring(start + 1, end);
    int firstBar = content.indexOf('|');
    if (firstBar == -1) return;
    machineState = content.substring(0, firstBar);
    int mposIdx = content.indexOf("MPos:");
    if (mposIdx != -1) {
        int endMpos = content.indexOf('|', mposIdx);
        if (endMpos == -1) endMpos = content.length();
        String mposStr = content.substring(mposIdx + 5, endMpos);
        float x, y, z, a;
        if (sscanf(mposStr.c_str(), "%f,%f,%f,%f", &x, &y, &z, &a) >= 3) {
            posX = x; posY = y; posZ = z;
        }
    }
    StaticJsonDocument<200> doc;
    doc["type"] = "dro";
    doc["state"] = machineState;
    doc["x"] = posX;
    doc["y"] = posY;
    doc["z"] = posZ;
    String output;
    serializeJson(doc, output);
    webSocket.broadcastTXT(output);
}

// ========== CALLBACK RICEZIONE ESP-NOW ==========
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (!info || len <= 0) return;
    last_data_received = millis();

    // ACK
    if (len == 3 && data[2] == 0x01) {
        lastAckSeq = data[0] | (data[1] << 8);
        return;
    }

    // PING (opzionale)
    if (len == 4 && memcmp(data, "PING", 4) == 0) {
        esp_now_send(bridge_mac, (uint8_t*)"PONG", 4);
        return;
    }

    // PAIR_OK
    if (len == 7 && memcmp(data, "PAIR_OK", 7) == 0) {
        memcpy(bridge_mac, info->src_addr, 6);
        esp_now_peer_info_t bridgePeer = {};
        memcpy(bridgePeer.peer_addr, bridge_mac, 6);
        bridgePeer.channel = currentChannel;
        bridgePeer.encrypt = false;
        esp_err_t res = esp_now_add_peer(&bridgePeer);
        if (res == ESP_OK || res == ESP_ERR_ESPNOW_EXIST) {
            bridge_peer_added = true;
            paired = true;
            Serial.println("\n✅ BRIDGE TROVATO!");
            Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          bridge_mac[0], bridge_mac[1], bridge_mac[2],
                          bridge_mac[3], bridge_mac[4], bridge_mac[5]);
            Serial.printf("Canale: %d\n", currentChannel);
            // Notifica al browser
            StaticJsonDocument<64> statusDoc;
            statusDoc["type"] = "status";
            statusDoc["paired"] = true;
            String statusMsg;
            serializeJson(statusDoc, statusMsg);
            webSocket.broadcastTXT(statusMsg);
        }
        return;
    }

    // ----- ACCUMULO DATI PER STAMPA SERIALE (originale) -----
    for (int i = 0; i < len; i++) {
        char c = (char)data[i];
        if (isValidGcodeChar(c)) buffer += c;
    }
    if (buffer.length() > 0 && (millis() - last_print > 100 || buffer.indexOf('\n') >= 0)) {
        Serial.print(buffer);
        buffer = "";
        last_print = millis();
    }

    // ----- PARSING PER DRO -----
    String response = "";
    for (int i = 0; i < len; i++) {
        char c = (char)data[i];
        if (isValidGcodeChar(c)) response += c;
        else if (c == '\n') response += '\n';
    }
    int startIdx = response.indexOf('<');
    int endIdx = response.indexOf('>');
    if (startIdx != -1 && endIdx != -1 && endIdx > startIdx) {
        String status = response.substring(startIdx, endIdx + 1);
        parseAndSendDRO(status);
    }
}

// ========== INVIO G-CODE ==========
bool sendGcodeLine(const String& cmd) {
    if (!paired || !bridge_peer_added) {
        Serial.println("⏳ Attendi pairing...");
        return false;
    }
    String command = cmd;
    // I comandi realtime sono un singolo carattere (0x18, '!', '~') – non aggiungere newline
    bool isRealtime = (command.length() == 1 && (command[0] == '!' || command[0] == '~' || command[0] == 0x18));
    if (!isRealtime) {
        if (!command.endsWith("\n")) command += "\n";
        command.toUpperCase();
    }
    uint16_t len = command.length();
    if (len > 246) len = 246;
    uint8_t packet[250];
    packet[0] = sequence & 0xFF;
    packet[1] = (sequence >> 8) & 0xFF;
    packet[2] = len & 0xFF;
    packet[3] = (len >> 8) & 0xFF;
    memcpy(&packet[4], command.c_str(), len);
    Serial.printf("📤 Invio [seq=%d]: %s", sequence, command.c_str());
    esp_err_t err = esp_now_send(bridge_mac, packet, len + 4);
    if (err != ESP_OK) {
        Serial.printf(" ❌ ERR=%d\n", err);
        return false;
    }
    unsigned long t0 = millis();
    while (lastAckSeq != sequence) {
        if (millis() - t0 > 3000) {
            Serial.println(" ⚠️ Timeout ACK");
            return false;
        }
        delay(1);
    }
    Serial.println(" ✓");
    sequence++;
    return true;
}

bool sendGCodeEspNowStream(const String &gcode) {
    Serial.println("📦 Parsing G-code...");
    std::vector<String> lines;
    lines.reserve(128);
    String cur = "";
    for (int i = 0; i < gcode.length(); i++) {
        char c = gcode[i];
        if (c == '\n' || c == '\r') {
            if (cur.length() > 0) lines.push_back(cur + "\n");
            cur = "";
        } else {
            cur += c;
        }
    }
    if (cur.length() > 0) lines.push_back(cur + "\n");
    Serial.printf("📊 Righe da inviare: %d\n", lines.size());
    int ok = 0;
    for (int i = 0; i < lines.size(); i++) {
        Serial.printf("➡️ [%d/%d] %s", i+1, lines.size(), lines[i].c_str());
        if (sendGcodeLine(lines[i])) ok++;
        else Serial.println("❌ Errore invio riga");
    }
    Serial.printf("🎉 COMPLETATO: %d/%d righe OK\n", ok, lines.size());
    return ok == lines.size();
}

void sendRealtime(uint8_t cmd) {
    if (!paired || !bridge_peer_added) {
        Serial.println("⏳ Attendi pairing...");
        return;
    }
    uint8_t packet[5] = { (uint8_t)(sequence & 0xFF), (uint8_t)((sequence >> 8) & 0xFF), 1, 0, cmd };
    Serial.printf("⚡ Realtime [seq=%d] cmd=0x%02X\n", sequence, cmd);
    esp_err_t err = esp_now_send(bridge_mac, packet, 5);
    if (err == ESP_OK) sequence++;
    else Serial.printf(" ❌ ERR=%d\n", err);
}
void sendReset()      { sendRealtime(CMD_RESET); }
void sendCycleStart() { sendRealtime(CMD_CYCLE_START); }
void sendFeedHold()   { sendRealtime(CMD_FEED_HOLD); }

// ========== PAGINA WEB (INTERFACCIA A GRIGLIA OTTIMIZZATA) ==========
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="it">
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>
  <title>CNC Web Pendant – Griglia</title>
  <style>
    * { box-sizing: border-box; }
    body {
      background: #121212;
      font-family: 'Segoe UI', 'Monaco', monospace;
      margin: 0;
      padding: 20px;
      color: #eeeeee;
    }
    .container {
      max-width: 1300px;
      margin: 0 auto;
    }
    .card {
      background: #1e1e2a;
      border-radius: 28px;
      padding: 20px 24px;
      margin-bottom: 24px;
      border: 1px solid #3a3a4a;
      box-shadow: 0 8px 20px rgba(0,0,0,0.5);
    }
    h1 {
      font-size: 1.6rem;
      margin: 0 0 6px 0;
      text-align: center;
      color: #ffaa44;
    }
    .sub {
      text-align: center;
      font-size: 0.8rem;
      color: #cccccc;
      margin-bottom: 12px;
    }
    /* Barra stato con LED e testo ben visibile */
    .status-bar {
      text-align: center;
      font-weight: bold;
      background: #0a0a10;
      padding: 10px;
      border-radius: 40px;
      margin-top: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 12px;
    }
    .led {
      width: 14px;
      height: 14px;
      border-radius: 50%;
      display: inline-block;
    }
    .led.red { background-color: #ff3333; box-shadow: 0 0 6px #ff3333; }
    .led.green { background-color: #33ff33; box-shadow: 0 0 6px #33ff33; }
    #pairStatus {
      color: #ffffff;
      background: #000000aa;
      padding: 4px 12px;
      border-radius: 30px;
      font-weight: bold;
    }
    /* Titolo DRO (ORA BEN VISIBILE) */
    .dro-title {
      margin: 0 0 12px 0;
      color: #ffffff !important;
      font-weight: bold;
      font-size: 1.3rem;
      text-shadow: 0 0 2px black;
    }
    /* DRO values */
    .dro {
      background: #0a0a10;
      padding: 20px;
      border-radius: 24px;
      display: flex;
      justify-content: space-around;
      flex-wrap: wrap;
      gap: 20px;
      text-align: center;
    }
    .dro div {
      background: #2a2a36;
      padding: 12px 24px;
      border-radius: 60px;
      font-size: 1.2rem;
      font-weight: bold;
    }
    .dro span {
      font-weight: bold;
      color: #ffcc55;
      font-family: monospace;
      font-size: 1.5rem;
      background: #00000066;
      padding: 4px 12px;
      border-radius: 30px;
      margin-left: 8px;
    }
    /* Griglia 3 colonne */
    .grid-3cols {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 24px;
      margin-bottom: 30px;
    }
    .group {
      background: #0b0e14;
      border-radius: 24px;
      padding: 12px 16px;
      border: 1px solid #252b3a;
    }
    .group h3 {
      margin: 0 0 12px 0;
      font-size: 1.2rem;
      border-left: 4px solid #ffaa44;
      padding-left: 12px;
      color: #ffaa44;
    }
    .button-grid {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }
    button {
      background: #2c3e50;
      border: none;
      color: white;
      padding: 8px 14px;
      border-radius: 40px;
      cursor: pointer;
      font-weight: bold;
      font-size: 0.85rem;
      transition: 0.1s;
      font-family: monospace;
    }
    button:active { transform: scale(0.97); }
    .danger { background: #c0392b; }
    .warning { background: #e67e22; }
    .success { background: #27ae60; }
    .primary { background: #2980b9; }
    .btn-test {
      background: #f39c12;
      color: #1e1e2e;
      font-weight: bold;
    }
    /* Sezione Run Test + Input */
    .test-section {
      background: #0b0e14;
      border-radius: 24px;
      padding: 16px;
      margin-bottom: 24px;
      border: 1px solid #252b3a;
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 16px;
      justify-content: space-between;
    }
    .input-group {
      flex: 3;
      display: flex;
      gap: 12px;
      align-items: center;
    }
    .input-group input {
      flex: 1;
      background: #0a0c12;
      border: 2px solid #3a3f4f;
      padding: 14px 16px;
      border-radius: 60px;
      color: white;
      font-size: 1rem;
      font-family: monospace;
    }
    .input-group input:focus {
      border-color: #ffaa44;
      outline: none;
    }
    .test-section button {
      padding: 12px 24px;
      font-size: 1rem;
    }
    /* Console */
    .console {
      background: #0a0a12;
      border-radius: 20px;
      padding: 12px;
      margin-top: 8px;
    }
    .console h3 {
      margin: 0 0 8px 0;
      font-size: 1rem;
      color: #ccc;
    }
    #log {
      background: #050608;
      height: 180px;
      overflow-y: auto;
      font-family: monospace;
      padding: 12px;
      font-size: 0.8rem;
      border-radius: 16px;
      white-space: pre-wrap;
      color: #0f0;
    }
    @media (max-width: 800px) {
      .grid-3cols { grid-template-columns: 1fr; gap: 16px; }
      .test-section { flex-direction: column; align-items: stretch; }
      .input-group { flex-direction: column; }
    }
  </style>
</head>
<body>
<div class="container">
  <div class="card">
    <h1> CNC Web Pendant – DEMO</h1>
    <div class="sub">ESP‑NOW based | realtime DRO</div>
    <div class="status-bar">
      <span id="pairLed" class="led red"></span>
      <span>Connection status: <span id="pairStatus">⏳ Wait pairing...</span></span>
    </div>
  </div>

  <div class="card">
    <h2 class="dro-title">DRO (Position)</h2>
    <div class="dro">
      <div>Status: <span id="state">---</span></div>
      <div>X: <span id="x">0.000</span> mm</div>
      <div>Y: <span id="y">0.000</span> mm</div>
      <div>Z: <span id="z">0.000</span> mm</div>
    </div>
  </div>

  <div class="grid-3cols">
    <!-- Machine Control -->
    <div class="group">
      <h3>Machine Control</h3>
      <div class="button-grid">
        <button class="danger" onclick='sendReset()'>Reset</button>
        <button class="warning" onclick='sendRealtime("!")'>Hold</button>
        <button class="success" onclick='sendRealtime("~")'>Resume</button>
        <button onclick='sendCmd("$H")'>Home</button>
        <button onclick='sendCmd("$X")'>Unlock</button>
      </div>
    </div>

    <!-- Modes & Utility -->
    <div class="group">
      <h3>Modes & Utility</h3>
      <div class="button-grid">
        <button class="primary" onclick='sendCmd("G90")'>G90 (Abs)</button>
        <button class="primary" onclick='sendCmd("G91")'>G91 (Inc)</button>
        <button onclick='sendCmd("G10 P0 L20 P0")'>G10</button>
      </div>
    </div>

    <!-- Quick Moves (X,Y,Z) -->
    <div class="group">
      <h3>Quick Moves</h3>
      <div class="button-grid">
        <button onclick='sendCmd("G0 X0")'>X0</button>
        <button onclick='sendCmd("G0 X10")'>X10</button>
        <button onclick='sendCmd("G0 Y0")'>Y0</button>
        <button onclick='sendCmd("G0 Y10")'>Y10</button>
        <button onclick='sendCmd("G0 Z0")'>Z0</button>
        <button onclick='sendCmd("G0 Z10")'>Z10</button>
      </div>
    </div>
  </div>

  <div class="test-section">
    <button class="btn-test" onclick='runTest()'>RUN TEST (401 lines)</button>
    <div class="input-group">
      <input type="text" id="customCmd" placeholder="G-code command (eg: G1 X50 F100)" autocomplete="off">
      <button class="success" onclick='sendCustom()'>SEND</button>
    </div>
  </div>

  <div class="console">
    <h3>Console</h3>
    <div id="log"></div>
  </div>
</div>

<script>
  var ws = new WebSocket('ws://' + window.location.hostname + ':81/');
  ws.onopen = function(){ console.log("WebSocket connesso"); };
  ws.onmessage = function(e){
    console.log("Ricevuto:", e.data);
    try{
      var d = JSON.parse(e.data);
      if(d.type === 'dro'){
        document.getElementById('state').innerText = d.state;
        document.getElementById('x').innerText = d.x.toFixed(3);
        document.getElementById('y').innerText = d.y.toFixed(3);
        document.getElementById('z').innerText = d.z.toFixed(3);
      } else if(d.type === 'status'){
        var paired = d.paired;
        var statusSpan = document.getElementById('pairStatus');
        var ledSpan = document.getElementById('pairLed');
        if(paired){
          statusSpan.innerHTML = '✅ Bridge connected';
          ledSpan.className = 'led green';
        } else {
          statusSpan.innerHTML = '⏳ Wait pairing...';
          ledSpan.className = 'led red';
        }
      } else {
        log(d);
      }
    } catch(err){
      log(e.data);
    }
  };

  function log(msg){
    let div = document.getElementById('log');
    div.innerHTML += '> ' + msg + '<br>';
    div.scrollTop = div.scrollHeight;
  }

  function sendCmd(cmd){
    if (!cmd.endsWith("\n")) cmd += "\n";
    fetch('/command', { method: 'POST', body: cmd }).catch(console.error);
    log('➡️ ' + cmd.trim());
  }

  function sendRealtime(cmd){
    fetch('/command', { method: 'POST', body: cmd }).catch(console.error);
    log('➡️ ' + cmd);
  }

  function sendReset(){
    fetch('/command', { method: 'POST', body: new Uint8Array([0x18]) }).catch(console.error);
    log('➡️ RESET (Ctrl-X)');
  }

  function sendCustom(){
    let cmd = document.getElementById('customCmd').value.trim();
    if (cmd) sendCmd(cmd);
    document.getElementById('customCmd').value = '';
    document.getElementById('customCmd').focus();
  }

  function runTest(){
    fetch('/test', { method: 'POST' })
      .then(r => r.text())
      .then(msg => alert(msg))
      .catch(err => alert("Errore test"));
  }

  setInterval(() => fetch('/ping'), 30000);
</script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", index_html); }
void handleCommand() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
  String cmd = server.arg("plain");
  // Per i comandi realtime inviati come Uint8Array, il body potrebbe contenere byte non stampabili.
  // Dobbiamo gestire correttamente 0x18 (Reset) che arriva come singolo byte.
  if (cmd.length() == 1 && cmd[0] == 0x18) {
    sendReset();
    server.send(200, "text/plain", "OK");
    return;
  }
  cmd.replace("\r", "");
  bool isRealtime = (cmd.length() == 1 && (cmd[0] == '!' || cmd[0] == '~'));
  if (!isRealtime) {
    if (!cmd.endsWith("\n")) cmd += "\n";
    cmd.toUpperCase();
  }
  Serial.printf("🔧 Comando ricevuto: '%s' (len=%d)\n", cmd.c_str(), cmd.length());
  if (sendGcodeLine(cmd)) server.send(200, "text/plain", "OK");
  else server.send(500, "text/plain", "ESP‑NOW error");
}
void handlePing() { server.send(200, "text/plain", "pong"); }
void handleTest() {
  Serial.println("🚀 Test avviato dal web");
  String gcode = generateTestGcode(200, 10.0, 0.0);
  if (sendGCodeEspNowStream(gcode))
    server.send(200, "text/plain", "Test OK (400 lines sent)");
  else
    server.send(500, "text/plain", "Test error");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch(type) {
        case WStype_CONNECTED:
            Serial.printf("[WebSocket] Client %u connesso\n", num);
            {
                StaticJsonDocument<64> statusDoc;
                statusDoc["type"] = "status";
                statusDoc["paired"] = paired;
                String statusMsg;
                serializeJson(statusDoc, statusMsg);
                webSocket.sendTXT(num, statusMsg);
            }
            break;
        case WStype_DISCONNECTED:
            Serial.printf("[WebSocket] Client %u disconnesso\n", num);
            break;
        default:
            break;
    }
}

// ========== SETUP ==========
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    Serial.println("\n====================================");
    Serial.println("CNC Web Pendant - ESP‑NOW based");
    Serial.println("====================================\n");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int netIndex = scanForChannel();  // ora restituisce l'indice della rete migliore
    if (netIndex >= 0) {
        Serial.printf("📡 Imposto canale ESP-NOW: %d\n", currentChannel);
    } else {
        Serial.println("📡 Uso canale default: 11");
        currentChannel = 11;
        netIndex = 0;  // fallback (usa il primo network della lista)
    }
    
    // Connessione alla rete WiFi usando SSID e password dal network trovato
    const char* sta_ssid = knownNetworks[netIndex].ssid;
    const char* sta_password = knownNetworks[netIndex].password;
    WiFi.begin(sta_ssid, sta_password);
    Serial.printf("Connessione a %s...\n", sta_ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi connesso");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Forza il canale e disabilita power save
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);
    delay(100);

    // ---- ESP-NOW (originale) ----
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed!");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, broadcastMac, 6);
    broadcastPeer.channel = currentChannel;
    broadcastPeer.encrypt = false;
    esp_now_add_peer(&broadcastPeer);

    Serial.println("ESP-NOW OK");
    Serial.println("Invio PAIR al bridge...\n");
    esp_now_send(broadcastMac, (uint8_t*)"PAIR", 4);
    pairStartTime = millis();
    last_data_received = millis();

    // ---- Web server e WebSocket ----
    server.on("/", handleRoot);
    server.on("/command", HTTP_POST, handleCommand);
    server.on("/ping", HTTP_GET, handlePing);
    server.on("/test", HTTP_POST, handleTest);
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.print("🌐 Web server avviato, pagina disponibile su http://");
    Serial.println(WiFi.localIP());
}

// ========== LOOP ==========
void loop() {
    server.handleClient();
    webSocket.loop();

    if (paired && (millis() - last_data_received) > HEARTBEAT_TIMEOUT_MS) {
        Serial.println("\n⚠️ Nessun dato dal bridge per 3 secondi!");
        resetPairing();
        delay(500);
    }
    if (!paired && (millis() - pairStartTime) > PAIR_RETRY_INTERVAL_MS) {
        Serial.println("❌ Bridge non trovato, riprovo...");
        esp_now_send(broadcastMac, (uint8_t*)"PAIR", 4);
        pairStartTime = millis();
    }

    // Input seriale per debug
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.equalsIgnoreCase("Test")) {
            String gcode = generateTestGcode(200, 10.0, 0.0);
            sendGCodeEspNowStream(gcode);
            Serial.print("> ");
        } else if (input.equalsIgnoreCase("reset")) sendReset();
        else if (input.equalsIgnoreCase("start")) sendCycleStart();
        else if (input.equalsIgnoreCase("hold")) sendFeedHold();
        else if (input.length() > 0) {
            if (!input.endsWith("\n")) input += "\n";
            sendGcodeLine(input);
        }
        Serial.print("> ");
    }
    delay(10);
}