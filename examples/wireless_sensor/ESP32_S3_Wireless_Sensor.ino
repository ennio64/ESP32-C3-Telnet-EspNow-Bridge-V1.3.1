/*
 * ESP32-S3 SuperMini - Wireless Sensor Test
 *
 * Nuovo protocollo ESP-NOW v1 compatibile con:
 *   ESP32-C3 Serial Bridge
 *
 * Caratteristiche:
 * - NON richiede il MAC del bridge
 * - esegue una scansione Wi-Fi per determinare il canale
 * - invia PAIR in broadcast
 * - apprende il MAC del bridge dal PAIR_ACK ricevuto
 * - dopo il pairing usa comunicazione unicast
 * - GPIO4 locale per test ON/OFF
 * - dopo PAIR richiede automaticamente GPIO_STATUS al bridge
 * - comando seriale GPIOSTATUS per richiedere nuovamente lo stato
 * - la funzione GPIO viene assegnata dal bridge
 * - misura il TEMPO ACK BRIDGE: invio GPIO_COMMAND -> ricezione GPIO_ACK
 *
 * Arduino-ESP32 3.0.7
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// -----------------------------------------------------------------------------
// CONFIGURAZIONE
// -----------------------------------------------------------------------------

#define SERIAL_BAUD             115200
#define SENSOR_GPIO_PIN         4
#define SENSOR_LED_PIN          LED_BUILTIN

#define CONTROL_MODE_SERIAL     0
#define CONTROL_MODE_PIN        1

#define PIN_DEBOUNCE_MS         30
#define PAIR_LED_BLINKS         3
#define PAIR_LED_ON_MS          150
#define PAIR_LED_OFF_MS         150

// Nuovo protocollo bridge
#define PROTOCOL_VERSION        1
#define HEADER_SIZE             10
#define MAX_FRAME_SIZE          250
#define MAX_PAYLOAD             (MAX_FRAME_SIZE - HEADER_SIZE)

#define MSG_PAIR                0x01
#define MSG_PAIR_ACK            0x02
#define MSG_HEARTBEAT           0x03
#define MSG_HEARTBEAT_ACK       0x04
#define MSG_GPIO_COMMAND        0x10
#define MSG_GPIO_ACK            0x11
#define MSG_GPIO_STATUS         0x12
#define MSG_GPIO_STATUS_REQ     0x13
#define MSG_SENSOR_ASSIGNMENT   0x14

#define DEVICE_TYPE_BRIDGE      1
#define DEVICE_TYPE_SENSOR      2

#define RESULT_ERROR            0
#define RESULT_OK               1

#define GPIO_OFF                0
#define GPIO_ON                 1

// Comportamento del test
#define PAIR_RETRY_MS           5000
#define HEARTBEAT_INTERVAL_MS   2000
#define ACK_TIMEOUT_MS          1000
#define HEARTBEAT_FAILURES_BEFORE_REPAIR  3

// Reti usate dal sistema esistente.
// Il canale del primo AP trovato viene usato per ESP-NOW.
const char *knownNetworks[] = {
  "WiFi_Network_1",
  "WiFi_Network_2",
  "WiFi_Network_3"
};

const int knownNetworkCount =
    sizeof(knownNetworks) / sizeof(knownNetworks[0]);

// Fallback usato anche dal vecchio peer.
#define DEFAULT_ESPNOW_CHANNEL  11

// Il bridge assegna la funzione al sensore. 0xFF = non assegnato.
#define SENSOR_FUNCTION_UNASSIGNED  0xFF

// -----------------------------------------------------------------------------
// STATO
// -----------------------------------------------------------------------------

uint8_t broadcastMac[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

uint8_t bridgeMac[6] = {0};
uint8_t sensorMac[6] = {0};

bool paired = false;
bool bridgePeerAdded = false;

uint8_t espnowChannel = DEFAULT_ESPNOW_CHANNEL;

uint32_t sessionId = 0;
uint16_t sequence = 0;

bool sensorState = false;
uint8_t sensorFunction = SENSOR_FUNCTION_UNASSIGNED;
uint8_t controlMode = CONTROL_MODE_SERIAL;
bool pinControlState = false;
bool pinControlRawState = false;
unsigned long pinControlLastChange = 0;

volatile bool heartbeatAckReceived = false;
volatile bool gpioAckReceived = false;
volatile uint8_t lastGpioAckResult = RESULT_ERROR;

volatile bool gpioStatusReceived = false;
volatile uint16_t gpioConfiguredMask = 0;
volatile uint16_t gpioStateMask = 0;
bool requestGpioStatusAfterPair = false;

volatile bool pairAckPending = false;
volatile uint8_t pairAssignedFunction = SENSOR_FUNCTION_UNASSIGNED;

unsigned long lastPairAttempt = 0;
unsigned long lastHeartbeat = 0;
uint8_t heartbeatFailures = 0;

// -----------------------------------------------------------------------------
// UTILITY LITTLE-ENDIAN
// -----------------------------------------------------------------------------

void putU16(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t)(value & 0xFF);
  p[1] = (uint8_t)((value >> 8) & 0xFF);
}

void putU32(uint8_t *p, uint32_t value)
{
  p[0] = (uint8_t)(value & 0xFF);
  p[1] = (uint8_t)((value >> 8) & 0xFF);
  p[2] = (uint8_t)((value >> 16) & 0xFF);
  p[3] = (uint8_t)((value >> 24) & 0xFF);
}

uint16_t getU16(const uint8_t *p)
{
  return (uint16_t)p[0] |
         ((uint16_t)p[1] << 8);
}

uint32_t getU32(const uint8_t *p)
{
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// -----------------------------------------------------------------------------
// MAC
// -----------------------------------------------------------------------------

void printMac(const uint8_t *mac)
{
  for (int i = 0; i < 6; i++) {
    if (i)
      Serial.print(':');

    if (mac[i] < 0x10)
      Serial.print('0');

    Serial.print(mac[i], HEX);
  }
}

bool isZeroMac(const uint8_t *mac)
{
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0)
      return false;
  }

  return true;
}

// -----------------------------------------------------------------------------
// CANALE WI-FI
// -----------------------------------------------------------------------------

int scanForEspNowChannel()
{
  Serial.println();
  Serial.println("--------------------------------------------");
  Serial.println("Scansione reti Wi-Fi per determinare il canale");
  Serial.println("--------------------------------------------");

  int networkCount = WiFi.scanNetworks(false, true);

  if (networkCount < 0) {
    Serial.print("Scansione Wi-Fi fallita: ");
    Serial.println(networkCount);
    WiFi.scanDelete();
    return DEFAULT_ESPNOW_CHANNEL;
  }

  Serial.print("Reti trovate: ");
  Serial.println(networkCount);

  int selectedChannel = DEFAULT_ESPNOW_CHANNEL;

  for (int k = 0; k < knownNetworkCount; k++) {
    for (int i = 0; i < networkCount; i++) {
      String ssid = WiFi.SSID(i);

      if (ssid == knownNetworks[k]) {
        int channel = WiFi.channel(i);

        Serial.print("Rete conosciuta trovata: ");
        Serial.print(ssid);
        Serial.print("  CH=");
        Serial.println(channel);

        if (channel > 0 && channel <= 13) {
          selectedChannel = channel;
          WiFi.scanDelete();

          Serial.print("Canale ESP-NOW selezionato: ");
          Serial.println(selectedChannel);

          return selectedChannel;
        }
      }
    }
  }

  Serial.print("Nessuna rete conosciuta trovata.");
  Serial.print(" Uso canale di fallback: ");
  Serial.println(selectedChannel);

  WiFi.scanDelete();

  return selectedChannel;
}

bool setEspNowChannel(uint8_t channel)
{
  if (channel < 1 || channel > 13)
    return false;

  esp_err_t err =
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  if (err != ESP_OK) {
    Serial.print("Errore esp_wifi_set_channel: ");
    Serial.println((int)err);
    return false;
  }

  espnowChannel = channel;
  return true;
}

// -----------------------------------------------------------------------------
// PEER BRIDGE
// -----------------------------------------------------------------------------

bool addBridgePeer()
{
  if (bridgePeerAdded)
    return true;

  if (isZeroMac(bridgeMac))
    return false;

  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, bridgeMac, 6);
  peer.channel = espnowChannel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  esp_err_t err = esp_now_add_peer(&peer);

  if (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST) {
    bridgePeerAdded = true;

    return true;
  }

  Serial.print("ERRORE esp_now_add_peer bridge: ");
  Serial.println((int)err);

  return false;
}

// -----------------------------------------------------------------------------
// PROTOCOLLO NUOVO
// -----------------------------------------------------------------------------

bool sendNewPacket(uint8_t type,
                   uint32_t sid,
                   uint16_t seq,
                   const uint8_t *payload,
                   uint16_t payloadLen,
                   const uint8_t *destination)
{
  if (!destination)
    return false;

  if (payloadLen > MAX_PAYLOAD)
    return false;

  uint8_t packet[MAX_FRAME_SIZE];

  packet[0] = PROTOCOL_VERSION;
  packet[1] = type;

  putU32(&packet[2], sid);
  putU16(&packet[6], seq);
  putU16(&packet[8], payloadLen);

  if (payloadLen > 0 && payload)
    memcpy(&packet[HEADER_SIZE], payload, payloadLen);

  uint16_t totalLen = HEADER_SIZE + payloadLen;

  esp_err_t err =
      esp_now_send(destination, packet, totalLen);

  if (err != ESP_OK) {
    Serial.print("esp_now_send ERROR=");
    Serial.println((int)err);
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------------
// PAIR
// -----------------------------------------------------------------------------

void sendPair()
{
  uint8_t payload[6];

  uint8_t localMac[6];
  WiFi.macAddress(localMac);

  memcpy(payload, localMac, 6);

  sendNewPacket(
      MSG_PAIR,
      sessionId,
      0,
      payload,
      sizeof(payload),
      broadcastMac);

  lastPairAttempt = millis();
}

// -----------------------------------------------------------------------------
// HEARTBEAT
// -----------------------------------------------------------------------------

bool sendHeartbeatAndWait()
{
  if (!paired || !bridgePeerAdded)
    return false;

  heartbeatAckReceived = false;

  uint16_t seq = sequence;

  if (!sendNewPacket(
          MSG_HEARTBEAT,
          sessionId,
          seq,
          nullptr,
          0,
          bridgeMac)) {
    heartbeatFailures++;
    return false;
  }

  unsigned long start = millis();

  while (!heartbeatAckReceived) {
    if (millis() - start >= ACK_TIMEOUT_MS)
      break;

    delay(1);
  }

  if (!heartbeatAckReceived) {
    heartbeatFailures++;

    Serial.print("HEARTBEAT ACK TIMEOUT (");
    Serial.print(heartbeatFailures);
    Serial.print("/");
    Serial.print(HEARTBEAT_FAILURES_BEFORE_REPAIR);
    Serial.println(") - nessun ACK applicativo entro il timeout.");

    if (heartbeatFailures >= HEARTBEAT_FAILURES_BEFORE_REPAIR) {
      Serial.println();
      Serial.println("Bridge non raggiungibile.");
      Serial.println("Avvio automaticamente un nuovo PAIR...");

      paired = false;
      bridgePeerAdded = false;
      memset(bridgeMac, 0, sizeof(bridgeMac));

      sequence = 0;
      requestGpioStatusAfterPair = false;
      gpioStatusReceived = false;
      gpioConfiguredMask = 0;
      gpioStateMask = 0;
      sensorFunction = SENSOR_FUNCTION_UNASSIGNED;

      createNewSession();

      heartbeatFailures = 0;
      lastPairAttempt = 0;

      sendPair();
    }

    return false;
  }

  heartbeatFailures = 0;

  sequence++;

  return true;
}

// -----------------------------------------------------------------------------
// GPIO STATUS REQUEST
// -----------------------------------------------------------------------------

void printGpioStatusMasks(uint16_t configuredMask, uint16_t stateMask)
{
  static const char *functionNames[] = {
    "PROBE", "XLIMIT", "YLIMIT", "ZLIMIT", "CUSTOM1", "CUSTOM2", "CUSTOM3"
  };

  Serial.println();
  Serial.println("============== BRIDGE GPIO STATUS ==============");
  Serial.print("Configured mask: 0x");
  if (configuredMask < 0x1000) Serial.print('0');
  if (configuredMask < 0x0100) Serial.print('0');
  if (configuredMask < 0x0010) Serial.print('0');
  Serial.println(configuredMask, HEX);

  Serial.print("State mask:      0x");
  if (stateMask < 0x1000) Serial.print('0');
  if (stateMask < 0x0100) Serial.print('0');
  if (stateMask < 0x0010) Serial.print('0');
  Serial.println(stateMask, HEX);

  for (uint8_t function = 0; function < 7; function++) {
    bool configured = (configuredMask & (1U << function)) != 0;
    bool state = (stateMask & (1U << function)) != 0;
    Serial.print("  ");
    Serial.print(functionNames[function]);
    Serial.print(" : ");
    if (!configured) Serial.println("DISABLED");
    else Serial.println(state ? "ON" : "OFF");
  }

  Serial.println("=================================================");
  Serial.println();
}

bool sendGPIOStatusRequestAndWait()
{
  if (!paired || !bridgePeerAdded) {
    Serial.println("Bridge non associato: impossibile richiedere GPIO STATUS.");
    return false;
  }

  gpioStatusReceived = false;
  uint16_t seq = sequence;

  if (!sendNewPacket(MSG_GPIO_STATUS_REQ, sessionId, seq, nullptr, 0, bridgeMac)) {
    Serial.println("GPIO_STATUS_REQ: invio fallito.");
    return false;
  }

  unsigned long start = millis();
  while (!gpioStatusReceived) {
    if (millis() - start >= ACK_TIMEOUT_MS) break;
    delay(1);
  }

  sequence++;

  if (!gpioStatusReceived) {
    Serial.println("GPIO_STATUS TIMEOUT");
    return false;
  }

  printGpioStatusMasks(gpioConfiguredMask, gpioStateMask);
  return true;
}

// -----------------------------------------------------------------------------
// GPIO COMMAND (misura solo il tempo ACK del bridge)
// -----------------------------------------------------------------------------

bool sendGPIOCommand(bool state)
{
  if (!paired || !bridgePeerAdded) {
    Serial.println("Bridge non associato.");
    return false;
  }

  if (sensorFunction == SENSOR_FUNCTION_UNASSIGNED || sensorFunction > 6) {
    Serial.println("Sensore non assegnato: GPIO_COMMAND bloccato.");
    return false;
  }

  uint8_t payload[2];

  payload[0] = sensorFunction;
  payload[1] = state ? GPIO_ON : GPIO_OFF;

  gpioAckReceived = false;
  lastGpioAckResult = RESULT_ERROR;

  uint16_t seq = sequence;

  static const char *functionNames[] = {
    "PROBE", "XLIMIT", "YLIMIT", "ZLIMIT",
    "CUSTOM1", "CUSTOM2", "CUSTOM3"
  };

  Serial.print("WIRELESS_COMMAND -> ");
  Serial.print(functionNames[sensorFunction]);
  Serial.print(" = ");
  Serial.println(state ? "ON" : "OFF");

  // >>> INIZIO MISURA TEMPO ACK BRIDGE
  unsigned long tStartUs = micros();

  if (!sendNewPacket(
          MSG_GPIO_COMMAND,
          sessionId,
          seq,
          payload,
          sizeof(payload),
          bridgeMac)) {
    return false;
  }

  unsigned long start = millis();

  while (!gpioAckReceived) {
    if (millis() - start >= ACK_TIMEOUT_MS)
      break;

    delay(1);
  }

  unsigned long ackUs = micros() - tStartUs;

  if (!gpioAckReceived) {
    Serial.println("GPIO ACK TIMEOUT");
    return false;
  }

  if (lastGpioAckResult != RESULT_OK) {
    Serial.println("GPIO ACK: ERROR");
    return false;
  }

  // >>> LOG TEMPO ACK BRIDGE
  Serial.print("Tempo ACK bridge: ");
  Serial.print(ackUs);
  Serial.print(" us  (");
  Serial.print(ackUs / 1000.0f, 3);
  Serial.println(" ms)");

  sequence++;

  return true;
}

// -----------------------------------------------------------------------------
// GPIO LOCALE
// -----------------------------------------------------------------------------

void applyControlMode(uint8_t mode)
{
  if (mode == CONTROL_MODE_PIN) {
    controlMode = CONTROL_MODE_PIN;

    pinMode(SENSOR_GPIO_PIN, INPUT_PULLUP);

    pinControlRawState = (digitalRead(SENSOR_GPIO_PIN) == LOW);
    pinControlState = pinControlRawState;
    pinControlLastChange = millis();

    sensorState = pinControlState;

    Serial.println("Control mode: PIN (GPIO4 input, active LOW)");
    return;
  }

  controlMode = CONTROL_MODE_SERIAL;

  pinMode(SENSOR_GPIO_PIN, INPUT);

  Serial.println("Control mode: SERIAL (GPIO4 ignored)");
}

void setSensorState(bool state)
{
  sensorState = state;

  Serial.print("Sensor state = ");
  Serial.println(sensorState ? "ON" : "OFF");

  Serial.print("Control mode: ");
  Serial.println(controlMode == CONTROL_MODE_PIN ? "PIN" : "SERIAL");
}

// -----------------------------------------------------------------------------
// RX ESP-NOW
// -----------------------------------------------------------------------------

void onDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *data,
                int len)
{
  if (!info || !data)
    return;

  if (len < HEADER_SIZE)
    return;

  uint8_t version = data[0];
  uint8_t type = data[1];

  uint32_t sid = getU32(&data[2]);
  uint16_t seq = getU16(&data[6]);
  uint16_t payloadLen = getU16(&data[8]);

  if (version != PROTOCOL_VERSION)
    return;

  if (payloadLen != (uint16_t)(len - HEADER_SIZE))
    return;

  const uint8_t *payload = &data[HEADER_SIZE];

  // ---------------------------------------------------------------------------
  // PAIR_ACK
  // ---------------------------------------------------------------------------

  if (type == MSG_PAIR_ACK) {

    if (payloadLen != 9)
      return;

    if (payload[0] != DEVICE_TYPE_BRIDGE)
      return;

    if (payload[1] != PROTOCOL_VERSION)
      return;

    if (memcmp(&payload[2], sensorMac, 6) != 0)
      return;

    uint8_t assignedFunction = payload[8];
    if (assignedFunction != SENSOR_FUNCTION_UNASSIGNED && assignedFunction > 6)
      return;

    memcpy(bridgeMac, info->src_addr, 6);
    pairAssignedFunction = assignedFunction;
    pairAckPending = true;
    return;
  }

  if (!paired)
    return;

  if (memcmp(info->src_addr, bridgeMac, 6) != 0)
    return;

  // ---------------------------------------------------------------------------
  // SENSOR_ASSIGNMENT
  // ---------------------------------------------------------------------------

  if (type == MSG_SENSOR_ASSIGNMENT) {

    if (payloadLen != 1)
      return;

    if (sid != sessionId)
      return;

    uint8_t assignedFunction = payload[0];
    if (assignedFunction != SENSOR_FUNCTION_UNASSIGNED && assignedFunction > 6)
      return;

    sensorFunction = assignedFunction;

    return;
  }

  // ---------------------------------------------------------------------------
  // HEARTBEAT_ACK
  // ---------------------------------------------------------------------------

  if (type == MSG_HEARTBEAT_ACK) {

    if (payloadLen != 0)
      return;

    if (sid != sessionId)
      return;

    if (seq != sequence)
      return;

    heartbeatAckReceived = true;

    return;
  }

  // ---------------------------------------------------------------------------
  // GPIO_ACK
  // ---------------------------------------------------------------------------

  if (type == MSG_GPIO_ACK) {

    if (payloadLen != 3)
      return;

    if (sid != sessionId)
      return;

    if (seq != sequence)
      return;

    uint8_t function = payload[0];
    uint8_t result = payload[2];

    if (sensorFunction == SENSOR_FUNCTION_UNASSIGNED ||
        function != sensorFunction)
      return;

    gpioAckReceived = true;
    lastGpioAckResult = result;

    return;
  }

  // ---------------------------------------------------------------------------
  // GPIO_STATUS
  // ---------------------------------------------------------------------------

  if (type == MSG_GPIO_STATUS) {

    if (payloadLen != 4)
      return;

    if (sid != sessionId)
      return;

    if (seq != sequence)
      return;

    gpioConfiguredMask = getU16(&payload[0]);
    gpioStateMask = getU16(&payload[2]);
    gpioStatusReceived = true;

    return;
  }
}

// -----------------------------------------------------------------------------
// TX CALLBACK
// -----------------------------------------------------------------------------

void onDataSent(const uint8_t *mac_addr,
                esp_now_send_status_t status)
{
  (void)mac_addr;
  (void)status;
}

// -----------------------------------------------------------------------------
// STATUS / HELP
// -----------------------------------------------------------------------------

void printStatus()
{
  Serial.println();
  Serial.println("============== STATUS ==============");

  Serial.print("Sensor MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("GPIO");
  Serial.print(SENSOR_GPIO_PIN);
  Serial.print(": ");
  Serial.println(sensorState ? "ON" : "OFF");

  Serial.print("Paired: ");
  Serial.println(paired ? "YES" : "NO");

  Serial.print("Assigned function: ");
  if (sensorFunction == SENSOR_FUNCTION_UNASSIGNED)
    Serial.println("UNASSIGNED");
  else {
    static const char *functionNames[] = {
      "PROBE", "XLIMIT", "YLIMIT", "ZLIMIT",
      "CUSTOM1", "CUSTOM2", "CUSTOM3"
    };
    Serial.println(functionNames[sensorFunction]);
  }

  Serial.print("Bridge peer: ");
  Serial.println(bridgePeerAdded ? "YES" : "NO");

  Serial.print("ESP-NOW channel: ");
  Serial.println(espnowChannel);

  if (paired) {
    Serial.print("Bridge MAC: ");
    printMac(bridgeMac);
    Serial.println();
  }

  Serial.print("Session ID: 0x");
  Serial.println(sessionId, HEX);

  Serial.print("Sequence: ");
  Serial.println(sequence);

  Serial.println("====================================");
  Serial.println();
}

void printHelp()
{
  Serial.println();
  Serial.println("Serial Monitor Commands:");
  Serial.println("  PAIR       perform a new pairing");
  Serial.println("  ON         activate the assigned function");
  Serial.println("  OFF        deactivate the assigned function");
  Serial.println("  MODE SERIAL use serial ON/OFF control");
  Serial.println("  MODE PIN   use GPIO4 contact input control");
  Serial.println("  MODE STATUS show current control mode");
  Serial.println("  STATUS     show local status");
  Serial.println("  GPIOSTATUS request GPIO status from the bridge");
  Serial.println("  HEARTBEAT  send heartbeat");
  Serial.println("  MAC        show sensor MAC address");
  Serial.println("  CHANNEL    show the selected channel");
  Serial.println("  HELP       show this menu");
  Serial.println();
}

// -----------------------------------------------------------------------------
// NUOVO SESSION ID
// -----------------------------------------------------------------------------

void createNewSession()
{
  sessionId =
      ((uint32_t)esp_random() << 1) ^
      (uint32_t)micros();

  if (sessionId == 0)
    sessionId = 1;
}

// -----------------------------------------------------------------------------
// RESET PAIRING
// -----------------------------------------------------------------------------

void resetPairing()
{
  paired = false;
  bridgePeerAdded = false;

  memset(bridgeMac, 0, sizeof(bridgeMac));

  sequence = 0;
  requestGpioStatusAfterPair = false;
  pairAckPending = false;
  gpioStatusReceived = false;
  gpioConfiguredMask = 0;
  gpioStateMask = 0;
  sensorFunction = SENSOR_FUNCTION_UNASSIGNED;

  createNewSession();

  Serial.println("Pairing resettato.");
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------

void blinkPairingLed()
{
  for (int i = 0; i < PAIR_LED_BLINKS; i++) {
    digitalWrite(SENSOR_LED_PIN, HIGH);
    delay(PAIR_LED_ON_MS);
    digitalWrite(SENSOR_LED_PIN, LOW);
    delay(PAIR_LED_OFF_MS);
  }
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(SENSOR_GPIO_PIN, INPUT);

  pinMode(SENSOR_LED_PIN, OUTPUT);
  digitalWrite(SENSOR_LED_PIN, LOW);

  sensorState = false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(200);

  WiFi.macAddress(sensorMac);

  createNewSession();

  Serial.println();
  Serial.println("================================================");
  Serial.println(" ESP32-S3 SuperMini - WIRELESS SENSOR TEST");
  Serial.println("================================================");

  Serial.print("Sensor MAC: ");
  Serial.println(WiFi.macAddress());

  espnowChannel = scanForEspNowChannel();

  Serial.print("Canale ESP-NOW selezionato: ");
  Serial.println(espnowChannel);

  if (!setEspNowChannel(espnowChannel)) {
    Serial.println("ERRORE: impossibile impostare il canale ESP-NOW");
    return;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERRORE: esp_now_init()");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t broadcastPeer = {};

  memcpy(
      broadcastPeer.peer_addr,
      broadcastMac,
      6);

  broadcastPeer.channel = espnowChannel;
  broadcastPeer.ifidx = WIFI_IF_STA;
  broadcastPeer.encrypt = false;

  esp_err_t peerErr =
      esp_now_add_peer(&broadcastPeer);

  if (peerErr != ESP_OK &&
      peerErr != ESP_ERR_ESPNOW_EXIST) {

    Serial.print("ERRORE peer broadcast: ");
    Serial.println((int)peerErr);

  } else {
    Serial.println("Peer broadcast ESP-NOW: OK");
  }

  Serial.println();
  Serial.println("Sensore pronto.");
  printHelp();

  sendPair();
}

void processPinControl()
{
  if (controlMode != CONTROL_MODE_PIN)
    return;

  bool rawState = (digitalRead(SENSOR_GPIO_PIN) == LOW);

  if (rawState != pinControlRawState) {
    pinControlRawState = rawState;
    pinControlLastChange = millis();
  }

  if (rawState != pinControlState &&
      millis() - pinControlLastChange >= PIN_DEBOUNCE_MS) {

    pinControlState = rawState;
    sensorState = pinControlState;

    Serial.print("GPIO4 contact = ");
    Serial.println(sensorState ? "ON" : "OFF");

    if (paired) {
      if (!sendGPIOCommand(sensorState))
        Serial.println("Wireless command NOT confirmed.");
    }
  }
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------

void loop()
{
  if (pairAckPending) {
    uint8_t assignedFunction = pairAssignedFunction;
    pairAckPending = false;

    if (addBridgePeer()) {
      sensorFunction = assignedFunction;
      paired = true;
      heartbeatFailures = 0;
      sequence = 0;
      lastHeartbeat = millis();
      requestGpioStatusAfterPair = true;

      static const char *functionNames[] = {
        "PROBE", "XLIMIT", "YLIMIT", "ZLIMIT",
        "CUSTOM1", "CUSTOM2", "CUSTOM3"
      };

      Serial.print("PAIR OK - ");

      if (assignedFunction == SENSOR_FUNCTION_UNASSIGNED)
        Serial.println("UNASSIGNED");
      else
        Serial.println(functionNames[assignedFunction]);

      blinkPairingLed();

    } else {
      Serial.println("ERRORE: impossibile aggiungere peer bridge");
    }
  }

  if (!paired &&
      millis() - lastPairAttempt >= PAIR_RETRY_MS) {

    sendPair();
  }

  if (paired && requestGpioStatusAfterPair) {
    requestGpioStatusAfterPair = false;
    sendGPIOStatusRequestAndWait();
  }

  if (paired &&
      millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {

    lastHeartbeat = millis();

    if (!sendHeartbeatAndWait()) {
      Serial.println("Heartbeat non confermato dal bridge.");
    }
  }

  processPinControl();

  if (Serial.available()) {

    String command =
        Serial.readStringUntil('\n');

    command.trim();
    command.toUpperCase();

    if (command == "PAIR") {

      resetPairing();
      sendPair();
    }

    else if (command == "ON") {

      if (controlMode == CONTROL_MODE_PIN) {
        Serial.println("ON ignored: control mode is PIN.");

      } else {

        if (!paired) {
          Serial.println(
              "Bridge non associato: eseguire PAIR.");

        } else {

          setSensorState(true);

          if (!sendGPIOCommand(true)) {
            Serial.println(
                "Comando ON wireless NON confermato.");
          }
        }
      }
    }

    else if (command == "OFF") {

      if (controlMode == CONTROL_MODE_PIN) {
        Serial.println("OFF ignored: control mode is PIN.");

      } else {

        if (!paired) {
          Serial.println(
              "Bridge non associato: eseguire PAIR.");

        } else {

          setSensorState(false);

          if (!sendGPIOCommand(false)) {
            Serial.println(
                "Comando OFF wireless NON confermato.");
          }
        }
      }
    }

    else if (command == "MODE SERIAL") {
      applyControlMode(CONTROL_MODE_SERIAL);
    }

    else if (command == "MODE PIN") {
      applyControlMode(CONTROL_MODE_PIN);
    }

    else if (command == "MODE STATUS") {

      Serial.print("Control mode: ");
      Serial.println(
          controlMode == CONTROL_MODE_PIN ? "PIN" : "SERIAL");
    }

    else if (command == "HEARTBEAT") {

      if (!sendHeartbeatAndWait())
        Serial.println("Heartbeat fallito.");
    }

    else if (command == "STATUS") {

      printStatus();
    }

    else if (command == "GPIOSTATUS") {

      if (!paired) {
        Serial.println("Bridge non associato: eseguire PAIR.");

      } else {
        sendGPIOStatusRequestAndWait();
      }
    }

    else if (command == "MAC") {

      Serial.print("Sensor MAC: ");
      Serial.println(WiFi.macAddress());
    }

    else if (command == "CHANNEL") {

      Serial.print(
          "Canale ESP-NOW: ");
      Serial.println(espnowChannel);
    }

    else if (command == "HELP") {

      printHelp();
    }

    else if (command.length() > 0) {

      Serial.print(
          "Comando sconosciuto: ");
      Serial.println(command);

      Serial.println(
          "Digitare HELP.");
    }
  }

  delay(5);
}