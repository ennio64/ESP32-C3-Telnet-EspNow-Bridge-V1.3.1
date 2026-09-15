#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "web_server.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "config.h"
#include "tcp_server.h"
#include "espnow_handler.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// =================== HTML PAGE (con form OTA) ===================
static const char* HTML_PAGE = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>ESP32 C3 Serial Bridge</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 1000px; margin: 0 auto; }
        .card { background: #16213e; border-radius: 10px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        h1 { color: #ffd700; margin: 0 0 10px 0; font-size: 28px; text-shadow: 2px 2px 4px rgba(0,0,0,0.5); }
        h2 { color: #ffd700; margin: 0 0 15px 0; font-size: 1.4em; border-left: 4px solid #ffd700; padding-left: 10px; }
        .status { padding: 12px; border-radius: 8px; margin: 10px 0; background: #0f3460; }
        .status-online { border-left: 4px solid #00ff00; }
        .status-offline { border-left: 4px solid #ff4444; }
        .info-row { margin: 12px 0; display: flex; flex-wrap: wrap; align-items: center; }
        .info-label { font-weight: bold; width: 150px; color: #ffd700; }
        .info-value { flex: 1; font-family: monospace; }
        input, button, select { padding: 8px 12px; margin: 5px; border-radius: 5px; border: none; }
        input, select { background: #0f3460; color: #eee; }
        input[type='text'] { width: 250px; }
        input[type='password'] { width: 180px; }
        input[type='checkbox'] { width: 18px; height: 18px; margin: 0 5px 0 0; vertical-align: middle; }
        button { background: #ffd700; color: #1a1a2e; cursor: pointer; transition: 0.3s; font-weight: bold; padding: 8px 16px; }
        button:hover { background: #ffaa00; transform: scale(1.02); }
        button.danger { background: #ff4444; color: white; }
        button.danger:hover { background: #cc0000; }
        button.success { background: #00cc66; color: white; }
        button.success:hover { background: #009944; }
        .network-item { background: #0f3460; padding: 12px; margin: 8px 0; border-radius: 5px; display: flex; justify-content: space-between; align-items: center; }
        .network-ssid { font-weight: bold; color: #ffd700; }
        .flex { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
        .flex-right { display: flex; justify-content: flex-end; gap: 10px; margin-top: 15px; }
        .checkbox-row { display: flex; align-items: center; margin: 12px 0; }
        hr { border-color: #0f3460; margin: 15px 0; }
        .warning-box { background: #ffaa001a; border-left: 4px solid #ffaa00; padding: 10px; margin-bottom: 15px; border-radius: 5px; }
        .note-box { background: #0f3460; padding: 10px; border-radius: 5px; margin-bottom: 15px; border-left: 3px solid #ffd700; }
        .label-show { display: flex; align-items: center; margin-left: 5px; cursor: pointer; }
        .label-show span { font-size: 12px; color: #ffd700; }
    </style>
</head>
<body>
<div class='container'>
    <div class='card'>
        <h1>🔧 ESP32 C3 Serial Bridge</h1>
        <div id='status'></div>
    </div>
    <div class='card'>
        <h2>📡 System</h2>
        <div id='sysinfo'></div>
    </div>
    <div class='card'>
        <h2>📶 Known WiFi Networks</h2>
        <div id='network-list'></div>
        <div class='note-box'>
            <small>📝 Add your networks below (max 10). Bridge auto-connects to best signal.</small>
        </div>
        <div class='flex' style='margin-top: 15px;'>
            <input type='text' id='new-ssid' placeholder='SSID'>
            <input type='password' id='new-pwd' placeholder='Password'>
            <label class='label-show'>
                <input type='checkbox' id='show-pwd' onclick='togglePassword()'>
                <span>Show</span>
            </label>
            <button onclick='addNetwork()'>➕ Add / Update</button>
        </div>
    </div>
    <div class='card'>
        <h2>⚙️ Network Settings (STA Mode)</h2>
        <div class='note-box'>
            <small>⚠️ <strong>Note:</strong> These settings apply only when the bridge connects to your home/work WiFi network.<br>
            The <strong>AP (Access Point)</strong> always uses <strong>192.168.4.1</strong> regardless of this setting.</small>
        </div>
        <div class='info-row'>
            <span class='info-label'>Static IP (STA):</span>
            <input type='text' id='static-ip' placeholder='192.168.1.123' style='width:130px'>
        </div>
        <div class='checkbox-row'>
            <input type='checkbox' id='use-static'>
            <span class='info-label' style='width:auto; margin-left:5px;'>Use static IP for STA mode</span>
        </div>
    </div>
    <div class='card'>
        <h2>⚙️ Access Point Settings (AP Mode)</h2>
        <div class='info-row'>
            <span class='info-label'>AP SSID:</span>
            <input type='text' id='ap-ssid' style='width:250px'>
        </div>
        <div class='info-row'>
            <span class='info-label'>AP Password:</span>
            <input type='text' id='ap-pwd' style='width:250px'>
        </div>
        <div class='info-row'>
            <span class='info-label'>AP Channel:</span>
            <input type='number' id='ap-channel' min='1' max='13' style='width:80px'>
        </div>
    </div>
    <div class='card'>
        <h2>🐛 Debug Settings</h2>
        <div class='info-row'>
            <span class='info-label'>Debug Mode:</span>
            <select id='debug-level'>
                <option value='0'>Disabled</option>
                <option value='1'>Basic Logs</option>
                <option value='2'>Verbose</option>
            </select>
            <button onclick='setDebugLevel()'>Apply</button>
        </div>
        <div class='info-row'>
            <span class='info-label'>Current Status:</span>
            <span class='info-value' id='debug-current'>Loading...</span>
        </div>
    </div>
    <div class='card'>
        <h2>📡 ESP-NOW Digital Outputs</h2>
        <div class='note-box'>
            <small>These seven outputs are controllable <strong>only via ESP-NOW</strong> using dedicated ON/OFF commands.
            All outputs are set to <strong>OFF</strong> at startup. Each GPIO can be assigned to only one function. GPIOs assigned to ESP-NOW outputs cannot be used by the State Pin, and vice versa.</small>
        </div>
        <div class='info-row'><span class='info-label'>PROBE:</span><select id='probe-pin'></select><select id='probe-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='info-row'><span class='info-label'>XLIMIT:</span><select id='xlimit-pin'></select><select id='xlimit-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='info-row'><span class='info-label'>YLIMIT:</span><select id='ylimit-pin'></select><select id='ylimit-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='info-row'><span class='info-label'>ZLIMIT:</span><select id='zlimit-pin'></select><select id='zlimit-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='info-row'><span class='info-label'>CUSTOM1:</span><select id='custom1-pin'></select><select id='custom1-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='info-row'><span class='info-label'>CUSTOM2:</span><select id='custom2-pin'></select><select id='custom2-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='info-row'><span class='info-label'>CUSTOM3:</span><select id='custom3-pin'></select><select id='custom3-polarity' title='GPIO polarity'><option value='0'>Active HIGH</option><option value='1'>Active LOW</option></select></div>
        <div class='flex-right' style='justify-content:flex-start; margin-top:10px;'>
            <button onclick='applyEspnowGpioSettings()'>💾 Apply GPIO</button>
        </div>
    </div>

    <div class='card'>
        <h2>📡 Wireless Sensors</h2>
        <div class='note-box'>
            <small>Assign each NEW-protocol wireless sensor to one logical function. The sensor is identified by its MAC address. Existing LEGACY pendants are not modified and are not affected by these assignments.</small>
        </div>
        <div class='flex-right' style='justify-content:flex-start; margin-bottom:10px;'>
            <button onclick='fetchSensors()'>🔄 Refresh Sensors</button>
        </div>
        <div id='sensor-list'><p>Loading sensors...</p></div>
    </div>

    <div class='card'>
        <h2>⚙️ GrblHAL Advanced Features</h2>
        <div class='warning-box'>
            <small>⚠️ <strong>Pin Safety for ESP32-C3 SuperMini:</strong><br>
            ✅ <strong>Safe pins:</strong> GPIO4, GPIO5, GPIO6, GPIO7, GPIO10<br>
            ❌ <strong>DO NOT USE:</strong> GPIO8 (LED RGB), GPIO9 (BOOT button), GPIO0-3 (boot affect)<br>
            🔒 <strong>Reserved:</strong> GPIO20 (UART RX), GPIO21 (UART TX)</small>
        </div>
        <div class='info-row'>
            <span class='info-label'>State Pin (output):</span>
            <select id='state-pin'>
                <option value='-1'>Disabled (default)</option>
                <option value='4'>GPIO4</option>
                <option value='5'>GPIO5</option>
                <option value='6'>GPIO6</option>
                <option value='7'>GPIO7</option>
                <option value='10'>GPIO10</option>
            </select>
        </div>
        <div class='info-row' id='pin-logic-row' style='display: none;'>
            <span class='info-label'>Pin State Logic:</span>
            <select id='state-pin-mode'>
                <option value='0'>LOW when client connected</option>
                <option value='1'>HIGH when client connected</option>
            </select>
        </div>
        <div class='info-row'>
            <span class='info-label'>Client Source:</span>
            <select id='client-mode'>
                <option value='0'>Any Client (Telnet or ESP-NOW)</option>
                <option value='1'>Telnet Only</option>
                <option value='2'>ESP-NOW Only</option>
            </select>
        </div>
        <div class='info-row'>
            <span class='info-label'>Reset on Telnet Disconnect:</span>
            <select id='reset-on-disconnect'>
                <option value='1'>Enabled (send Ctrl-X)</option>
                <option value='0'>Disabled</option>
            </select>
        </div>
    </div>
    <!-- OTA CARD -->
    <div class='card'>
        <h2>✨ Over‑The‑Air (OTA) Update</h2>
        <div class='warning-box'>
            <small>⚠️ <strong>CAUTION:</strong> Uploading an incorrect firmware may brick the device. Use only the .bin file generated by `idf.py build`.</small>
        </div>
        <div class='info-row'>
            <span class='info-label'>Select Firmware:</span>
            <input type='file' id='firmware-file' accept='.bin'>
        </div>
        <div class='flex-right' style='justify-content: flex-start;'>
            <button onclick='performOTAUpdate()'>🚀 Upload & Update</button>
        </div>
        <div id='ota-status' style='margin-top: 15px; color: #ffd700;'></div>
    </div>
    <div class='flex-right'>
        <button onclick='saveSettings()'>💾 Save All</button>
        <button class='danger' onclick='resetConfig()'>🔄 Reset Default</button>
        <button class='success' onclick='reboot()'>🔁 Reboot</button>
    </div>
</div>
<script>
function togglePassword() {
    var pwd = document.getElementById('new-pwd');
    if (pwd.type === 'password') { pwd.type = 'text'; }
    else { pwd.type = 'password'; }
}
const sensorPendingSelections = {};
let espnowOutputPins = null;

function rememberSensorSelection(func) {
    const select = document.getElementById('sensor-f-' + func);
    if (select && !select.disabled) sensorPendingSelections[func] = select.value;
}

function updateSensorAvailability() {
    const names = ['PROBE','XLIMIT','YLIMIT','ZLIMIT','CUSTOM1','CUSTOM2','CUSTOM3'];
    const pins = espnowOutputPins;

    for (let f = 0; f < 7; f++) {
        const select = document.getElementById('sensor-f-' + f);
        const button = document.getElementById('sensor-apply-' + f);
        if (!select) continue;

        // Una funzione wireless e' disponibile solo se il relativo GPIO
        // ESP-NOW e' configurato (GPIO4/5/6/7/10).
        const gpioEnabled = Array.isArray(pins) && pins[f] >= 0;
        select.disabled = !gpioEnabled;
        if (button) button.disabled = !gpioEnabled;

        Array.from(select.options).forEach(option => {
            const mac = option.value;
            if (!mac) {
                option.disabled = false;
                return;
            }

            // Il sensore gia' associato alla funzione corrente resta selezionabile.
            // Un sensore associato ad un'altra funzione non puo' essere riutilizzato.
            // I sensori non ancora associati restano disponibili per una funzione GPIO abilitata.
            const sensor = window.currentEspnowSensors
                ? window.currentEspnowSensors.find(x => x.mac === mac)
                : null;
            const assignedElsewhere = sensor && sensor.function >= 0 && sensor.function !== f;
            option.disabled = !gpioEnabled || !!assignedElsewhere;
        });

        if (!gpioEnabled) {
            // Non conserviamo una scelta provvisoria per una funzione disabilitata.
            delete sensorPendingSelections[f];
        }
    }
}

function fetchSensors() {
    fetch('/api/espnow/sensors').then(r=>r.json()).then(data=>{
        const names = ['PROBE','XLIMIT','YLIMIT','ZLIMIT','CUSTOM1','CUSTOM2','CUSTOM3'];
        const sensors = data.sensors || [];
        const bindings = data.bindings || [];
        window.currentEspnowSensors = sensors;
        let html = '';
        for (let f = 0; f < 7; f++) {
            const b = bindings.find(x => x.function === f);
            const selectedMac = Object.prototype.hasOwnProperty.call(sensorPendingSelections, f)
                ? sensorPendingSelections[f] : (b ? b.mac : '');
            const gpioEnabled = Array.isArray(espnowOutputPins) && espnowOutputPins[f] >= 0;
            const gpioText = gpioEnabled ? 'GPIO' + espnowOutputPins[f] : 'Disabled';

            html += '<div class="info-row">';
            html += '<span class="info-label">' + names[f] + ':</span>';
            html += '<select id="sensor-f-' + f + '" style="min-width:280px" onchange="rememberSensorSelection(' + f + ')">';
            html += '<option value="">-- Not assigned --</option>';
            sensors.forEach(sensor => {
                const assignedElsewhere = sensor.function >= 0 && sensor.function !== f;
                const disabled = !gpioEnabled || assignedElsewhere;
                html += '<option value="' + sensor.mac + '"' +
                    (selectedMac === sensor.mac ? ' selected' : '') +
                    (disabled ? ' disabled' : '') + '>';
                html += sensor.mac + (sensor.function >= 0 ? ' [' + names[sensor.function] + ']' : ' [unassigned]');
                html += '</option>';
            });
            html += '</select>';
            html += '<button id="sensor-apply-' + f + '" onclick="assignSensor(' + f + ')"' +
                (gpioEnabled ? '' : ' disabled') + '>Apply</button>';
            html += '<span style="margin-left:10px; opacity:0.65; font-size:0.85em">' + gpioText + '</span>';
            html += '</div>';
        }
        if (sensors.length === 0) {
            html = '<p style="color:#ffaa00">⚠️ No NEW wireless sensors currently paired. Start the sensor firmware and wait for PAIR.</p>' + html;
        }
        document.getElementById('sensor-list').innerHTML = html;
        updateSensorAvailability();
    }).catch(() => {
        document.getElementById('sensor-list').innerHTML = '<p style="color:#ff6666">Unable to read wireless sensors.</p>';
    });
}
function assignSensor(func) {
    const mac = document.getElementById('sensor-f-' + func).value;
    const endpoint = mac ? '/api/espnow/assign' : '/api/espnow/unassign';
    const body = {function: func};
    if (mac) body.mac = mac;
    fetch(endpoint, {
        method: 'POST', body: JSON.stringify(body),
        headers: {'Content-Type': 'application/json'}
    }).then(r => r.json()).then(data => {
        if (data.status !== 'ok') {
            alert(data.error || 'Sensor assignment failed');
        }
        // La selezione provvisoria termina quando Apply ha ricevuto la risposta.
        delete sensorPendingSelections[func];
        fetchSensors();
    }).catch(() => {
        delete sensorPendingSelections[func];
        fetchSensors();
    });
}

function fetchStatus() {
    fetch('/api/status').then(r=>r.json()).then(data=>{
        let cls = data.wifi_connected ? 'status-online' : 'status-offline';
        let html = '<div class="status ' + cls + '">';
        html += '<div class="info-row"><span class="info-label">📡 WiFi (STA):</span><span class="info-value">' + (data.wifi_connected ? data.wifi_ssid + ' (' + data.wifi_ip + ') channel ' + data.wifi_channel : 'Disconnected') + '</span></div>';
        html += '<div class="info-row"><span class="info-label">🔗 ESP-NOW:</span><span class="info-value">' + (data.espnow_paired ? '✅ Paired' : '⏳ Waiting') + '</span></div>';
        html += '<div class="info-row"><span class="info-label">🖥️ Telnet Clients:</span><span class="info-value">' + data.telnet_clients + '</span></div>';
        html += '<div class="info-row"><span class="info-label">📱 AP IP:</span><span class="info-value">192.168.4.1</span></div>';
        html += '</div>';
        document.getElementById('status').innerHTML = html;
    });
}
function fetchSysInfo() {
    fetch('/api/sysinfo').then(r=>r.json()).then(data=>{
        let html = '<div class="info-row"><span class="info-label">Version:</span><span class="info-value">1.3.1</span></div>';
        html += '<div class="info-row"><span class="info-label">Compiled:</span><span class="info-value">' + data.compile_date + ' ' + data.compile_time + '</span></div>';
        html += '<div class="info-row"><span class="info-label">Free Memory:</span><span class="info-value">' + (data.memory.free_heap / 1024).toFixed(1) + ' KB</span></div>';
        document.getElementById('sysinfo').innerHTML = html;
    });
}
function fetchNetworks() {
    fetch('/api/networks').then(r=>r.json()).then(data=>{
        let html = '';
        if (data.networks.length === 0) {
            html = '<p style="color: #ffaa00;">⚠️ No networks configured.</p>';
        } else {
            data.networks.forEach(n => {
                html += '<div class="network-item">';
                html += '<span class="network-ssid">📡 ' + n.ssid + '</span>';
                html += '<button onclick="removeNetwork(\'' + n.ssid + '\')">🗑️ Remove</button>';
                html += '</div>';
            });
        }
        document.getElementById('network-list').innerHTML = html;
    });
}
const espnowPinOptions = [
    ['-1', 'Disabled'], ['4', 'GPIO4'], ['5', 'GPIO5'],
    ['6', 'GPIO6'], ['7', 'GPIO7'], ['10', 'GPIO10']
];
const espnowPinFields = ['probe-pin','xlimit-pin','ylimit-pin','zlimit-pin','custom1-pin','custom2-pin','custom3-pin'];
const espnowPinNames = ['PROBE','XLIMIT','YLIMIT','ZLIMIT','CUSTOM1','CUSTOM2','CUSTOM3'];
const espnowPolarityFields = ['probe-polarity','xlimit-polarity','ylimit-polarity','zlimit-polarity','custom1-polarity','custom2-polarity','custom3-polarity'];
const sharedGpios = [4, 5, 6, 7, 10];

function initEspnowPinSelectors() {
    espnowPinFields.forEach(id => {
        const select = document.getElementById(id);
        select.innerHTML = '';
        espnowPinOptions.forEach(opt => {
            const option = document.createElement('option');
            option.value = opt[0];
            option.textContent = opt[1];
            select.appendChild(option);
        });
        select.addEventListener('change', updatePinAvailability);
    });

    espnowPolarityFields.forEach(id => {
        document.getElementById(id).addEventListener('change', updatePinAvailability);
    });

    document.getElementById('state-pin').addEventListener('change', function() {
        updateStatePinVisibility();
        updatePinAvailability();
    });
}

function setEspnowPinSelectors(pins) {
    espnowPinFields.forEach((id, index) => {
        document.getElementById(id).value = String((pins && pins[index] !== undefined) ? pins[index] : -1);
    });
    updatePinAvailability();
}

function getEspnowPins() {
    return espnowPinFields.map(id => parseInt(document.getElementById(id).value));
}

function getEspnowPolarityMask() {
    const pins = getEspnowPins();
    let mask = 0;
    pins.forEach((pin, index) => {
        const select = document.getElementById(espnowPolarityFields[index]);
        if (pin >= 0 && select && select.value === '1') mask |= (1 << pin);
    });
    return mask >>> 0;
}

function setEspnowPolaritySelectors(mask) {
    const pins = getEspnowPins();
    const m = Number(mask) >>> 0;
    espnowPolarityFields.forEach((id, index) => {
        const select = document.getElementById(id);
        const pin = pins[index];
        if (select) select.value = (pin >= 0 && (m & (1 << pin))) ? '1' : '0';
    });
}

function updatePinAvailability() {
    const pins = getEspnowPins();
    const statePin = parseInt(document.getElementById('state-pin').value);

    espnowPinFields.forEach((id, index) => {
        const select = document.getElementById(id);
        const ownValue = pins[index];

        Array.from(select.options).forEach(option => {
            const value = parseInt(option.value);
            let usedByOtherOutput = false;

            if (value >= 0) {
                for (let j = 0; j < pins.length; j++) {
                    if (j !== index && pins[j] === value) {
                        usedByOtherOutput = true;
                        break;
                    }
                }
            }

            const usedByStatePin = value >= 0 && value === statePin;
            option.disabled = value >= 0 && (usedByOtherOutput || usedByStatePin) && value !== ownValue;
        });
    });

    const stateSelect = document.getElementById('state-pin');
    const stateValue = parseInt(stateSelect.value);

    Array.from(stateSelect.options).forEach(option => {
        const value = parseInt(option.value);
        let usedByOutput = false;

        if (value >= 0) {
            usedByOutput = pins.some(pin => pin === value);
        }

        option.disabled = value >= 0 && usedByOutput && value !== stateValue;
    });

    // Le associazioni wireless dipendono direttamente dalla configurazione GPIO.
    espnowOutputPins = pins.slice();
    updateSensorAvailability();
}

function updateStatePinVisibility() {
    const row = document.getElementById('pin-logic-row');
    const statePin = parseInt(document.getElementById('state-pin').value);
    row.style.display = (statePin == -1) ? 'none' : 'flex';
}

function fetchSettings() {
    fetch('/api/settings').then(r=>r.json()).then(data=>{
        document.getElementById('ap-ssid').value = data.ap_ssid;
        document.getElementById('ap-pwd').value = data.ap_password;
        document.getElementById('ap-channel').value = data.ap_channel;
        document.getElementById('static-ip').value = data.static_ip;
        document.getElementById('use-static').checked = data.use_static_ip;
        document.getElementById('state-pin').value = data.state_pin;
        document.getElementById('state-pin-mode').value = data.state_pin_mode;
        document.getElementById('client-mode').value = data.client_mode;
        document.getElementById('reset-on-disconnect').value = data.reset_on_disconnect;
        setEspnowPinSelectors(data.espnow_output_pins);
        setEspnowPolaritySelectors(data.espnow_gpio_active_low_mask || 0);
        espnowOutputPins = getEspnowPins();
        updateStatePinVisibility();
        updatePinAvailability();
        fetchSensors();
    });
}
function fetchDebugStatus() {
    fetch('/api/debug').then(r=>r.json()).then(data=>{
        let status = ['🔇 Disabled', '🔊 Basic', '🔊🔊 Verbose'][data.level] || 'Unknown';
        document.getElementById('debug-current').innerHTML = status;
        document.getElementById('debug-level').value = data.level;
    });
}
function setDebugLevel() {
    let level = document.getElementById('debug-level').value;
    fetch('/api/debug', {
        method: 'POST',
        body: JSON.stringify({level: parseInt(level)}),
        headers: {'Content-Type': 'application/json'}
    }).then(() => { alert('Debug level changed. Reboot to take effect.'); });
}
function addNetwork() {
    let ssid = document.getElementById('new-ssid').value.trim();
    let pwd = document.getElementById('new-pwd').value;
    if (!ssid) { alert('Enter SSID'); return; }
    fetch('/api/networks/add', {
        method: 'POST',
        body: JSON.stringify({ssid: ssid, password: pwd}),
        headers: {'Content-Type': 'application/json'}
    }).then(() => { fetchNetworks(); document.getElementById('new-ssid').value = ''; document.getElementById('new-pwd').value = ''; });
}
function removeNetwork(ssid) {
    if (confirm('Remove network ' + ssid + '?')) {
        fetch('/api/networks/remove', {
            method: 'POST',
            body: JSON.stringify({ssid: ssid}),
            headers: {'Content-Type': 'application/json'}
        }).then(() => fetchNetworks());
    }
}
function applyEspnowGpioSettings() {
    const pins = getEspnowPins();
    const settings = {
        ap_ssid: document.getElementById('ap-ssid').value,
        ap_password: document.getElementById('ap-pwd').value,
        ap_channel: parseInt(document.getElementById('ap-channel').value) || 6,
        static_ip: document.getElementById('static-ip').value,
        use_static_ip: document.getElementById('use-static').checked,
        state_pin: parseInt(document.getElementById('state-pin').value),
        state_pin_mode: parseInt(document.getElementById('state-pin-mode').value),
        client_mode: parseInt(document.getElementById('client-mode').value),
        reset_on_disconnect: parseInt(document.getElementById('reset-on-disconnect').value),
        probe_pin: pins[0],
        xlimit_pin: pins[1],
        ylimit_pin: pins[2],
        zlimit_pin: pins[3],
        custom1_pin: pins[4],
        custom2_pin: pins[5],
        custom3_pin: pins[6],
        espnow_gpio_active_low_mask: getEspnowPolarityMask()
    };
    fetch('/api/settings', {
        method: 'POST',
        body: JSON.stringify(settings),
        headers: {'Content-Type': 'application/json'}
    }).then(async response => {
        const data = await response.json().catch(() => ({}));
        if (response.ok && data.status === 'saved') {
            alert('GPIO settings saved. Reboot to apply.');
            fetchSettings();
            fetchSensors();
        } else {
            alert(data.error || 'GPIO settings were not saved.');
            fetchSettings();
        }
    }).catch(() => {
        alert('Failed to save GPIO settings.');
        fetchSettings();
    });
}

function saveSettings() {
    let settings = {
        ap_ssid: document.getElementById('ap-ssid').value,
        ap_password: document.getElementById('ap-pwd').value,
        ap_channel: parseInt(document.getElementById('ap-channel').value) || 6,
        static_ip: document.getElementById('static-ip').value,
        use_static_ip: document.getElementById('use-static').checked,
        state_pin: parseInt(document.getElementById('state-pin').value),
        state_pin_mode: parseInt(document.getElementById('state-pin-mode').value),
        client_mode: parseInt(document.getElementById('client-mode').value),
        reset_on_disconnect: parseInt(document.getElementById('reset-on-disconnect').value),
        probe_pin: getEspnowPins()[0],
        xlimit_pin: getEspnowPins()[1],
        ylimit_pin: getEspnowPins()[2],
        zlimit_pin: getEspnowPins()[3],
        custom1_pin: getEspnowPins()[4],
        custom2_pin: getEspnowPins()[5],
        custom3_pin: getEspnowPins()[6],
        espnow_gpio_active_low_mask: getEspnowPolarityMask()
    };
    fetch('/api/settings', {
        method: 'POST',
        body: JSON.stringify(settings),
        headers: {'Content-Type': 'application/json'}
    }).then(async response => {
        const data = await response.json().catch(() => ({}));
        if (response.ok && data.status === 'saved') {
            alert('Settings saved. Reboot to apply.');
        } else {
            alert(data.error || 'Settings were not saved.');
            fetchSettings();
        }
    }).catch(() => {
        alert('Failed to save settings.');
    });
}
function resetConfig() {
    if (confirm('⚠️ Reset ALL settings? The bridge will reboot.')) {
        fetch('/api/reset', {method: 'POST'}).then(() => { setTimeout(() => { location.reload(); }, 2000); });
    }
}
function reboot() {
    if (confirm('Reboot the bridge?')) {
        fetch('/api/reboot', {method: 'POST'});
        alert('Rebooting...');
    }
}
function performOTAUpdate() {
    let fileInput = document.getElementById('firmware-file');
    let file = fileInput.files[0];
    if (!file) {
        alert('Select a firmware .bin file first');
        return;
    }
    let statusDiv = document.getElementById('ota-status');
    statusDiv.innerHTML = 'Uploading and updating firmware... please wait.';
    
    fetch('/update', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: file
    }).then(response => {
        if (response.ok) {
            statusDiv.innerHTML = 'Update successful! Rebooting...';
            setTimeout(() => { location.reload(); }, 5000);
        } else {
            return response.text().then(text => { throw new Error(text); });
        }
    }).catch(error => {
        statusDiv.innerHTML = 'Error: ' + error.message;
    });
}
document.getElementById('state-pin').addEventListener('change', function() {
    var row = document.getElementById('pin-logic-row');
    if (this.value == '-1') { row.style.display = 'none'; }
    else { row.style.display = 'flex'; }
});
initEspnowPinSelectors();
setInterval(() => { fetchStatus(); fetchSysInfo(); fetchDebugStatus(); fetchSensors(); }, 5000);
fetchStatus();
fetchNetworks();
fetchSettings();
fetchSensors();
fetchSysInfo();
fetchDebugStatus();
</script>
</body>
</html>
)raw";

// =================== Helper JSON ===================
static void send_json_response(httpd_req_t *req, const char *fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, strlen(buffer));
}

static char* extract_json_value(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *key_pos = strstr(json, search);
    if (!key_pos) return NULL;
    char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;
    char *value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') value_start++;
    char *value_end = NULL;
    bool is_quoted = (*value_start == '"');
    if (is_quoted) {
        value_start++;
        value_end = strchr(value_start, '"');
    } else {
        value_end = value_start;
        while (*value_end && *value_end != ',' && *value_end != '}' && *value_end != ' ') value_end++;
    }
    if (!value_end || value_end == value_start) return NULL;
    int len = value_end - value_start;
    char *value = malloc(len + 1);
    if (!value) return NULL;
    strncpy(value, value_start, len);
    value[len] = '\0';
    return value;
}

// =================== Handlers esistenti ===================
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    char connected_ssid[64] = "None";
    uint8_t primary = 0;
    wifi_second_chan_t second;
    int channel = 0;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) channel = primary;
    if (wifi_is_connected() && cfg->network_count > 0) {
        strncpy(connected_ssid, cfg->networks[0].ssid, sizeof(connected_ssid)-1);
        connected_ssid[sizeof(connected_ssid)-1] = '\0';
    }
    int telnet_clients = tcp_server_get_client_count();
    bool espnow_paired = espnow_is_paired();
    send_json_response(req,
        "{\"wifi_connected\":%s,\"wifi_ip\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_channel\":%d,\"telnet_clients\":%d,\"espnow_paired\":%s}",
        wifi_is_connected() ? "true" : "false",
        wifi_get_ip(),
        connected_ssid,
        channel,
        telnet_clients,
        espnow_paired ? "true" : "false");
    return ESP_OK;
}

static esp_err_t sysinfo_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    char connected_ssid[64] = "None";
    if (wifi_is_connected() && cfg->network_count > 0) {
        strncpy(connected_ssid, cfg->networks[0].ssid, sizeof(connected_ssid)-1);
        connected_ssid[sizeof(connected_ssid)-1] = '\0';
    }
    send_json_response(req,
        "{\"version\":\"1.3.1\",\"compile_date\":\"%s\",\"compile_time\":\"%s\","
        "\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
        "\"memory\":{\"free_heap\":%d,\"min_free_heap\":%d}}",
        __DATE__, __TIME__,
        wifi_is_connected() ? "true" : "false",
        connected_ssid,
        wifi_get_ip(),
        esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    return ESP_OK;
}

static esp_err_t networks_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    char networks_json[800] = "";
    for (int i = 0; i < cfg->network_count; i++) {
        if (i > 0) strcat(networks_json, ",");
        char net[100];
        snprintf(net, sizeof(net), "{\"ssid\":\"%s\"}", cfg->networks[i].ssid);
        strcat(networks_json, net);
    }
    send_json_response(req, "{\"networks\":[%s]}", networks_json);
    return ESP_OK;
}

static esp_err_t settings_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    send_json_response(req,
        "{\"ap_ssid\":\"%s\",\"ap_password\":\"%s\",\"ap_channel\":%d,"
        "\"static_ip\":\"%s\",\"use_static_ip\":%s,"
        "\"state_pin\":%d,\"state_pin_mode\":%d,\"client_mode\":%d,\"reset_on_disconnect\":%d,"
        "\"espnow_output_pins\":[%d,%d,%d,%d,%d,%d,%d],\"espnow_gpio_active_low_mask\":%lu}",
        cfg->ap_ssid, cfg->ap_password, cfg->ap_channel,
        cfg->static_ip, cfg->use_static_ip ? "true" : "false",
        cfg->state_pin, cfg->state_pin_mode, cfg->client_mode, cfg->reset_on_disconnect,
        cfg->espnow_output_pins[0], cfg->espnow_output_pins[1], cfg->espnow_output_pins[2],
        cfg->espnow_output_pins[3], cfg->espnow_output_pins[4], cfg->espnow_output_pins[5],
        cfg->espnow_output_pins[6],
        (unsigned long)cfg->espnow_gpio_active_low_mask);
    return ESP_OK;
}

static esp_err_t debug_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    send_json_response(req, "{\"level\":%d}", cfg->debug_level);
    return ESP_OK;
}

static esp_err_t debug_post_handler(httpd_req_t *req) {
    char buffer[256];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer)-1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    char *level_str = extract_json_value(buffer, "level");
    if (level_str) {
        int new_level = atoi(level_str);
        free(level_str);
        if (new_level >= 0 && new_level <= 2) {
            bridge_config_t *cfg = nvs_storage_get_config_mutable();
            cfg->debug_level = (uint8_t)new_level;
            nvs_storage_save_config();
            httpd_resp_send(req, "{\"status\":\"reboot_required\"}", 28);
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Level must be 0,1,2");
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid level");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t reboot_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reboot requested via web");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}", 22);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reset config requested via web");
    nvs_storage_reset_default();
    httpd_resp_send(req, "{\"status\":\"reset_done\"}", 21);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_post_handler(httpd_req_t *req) {
    char buffer[1024];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer)-1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    if (strstr(req->uri, "/add")) {
        char *ssid = extract_json_value(buffer, "ssid");
        char *pwd = extract_json_value(buffer, "password");
        if (ssid && pwd) {
            nvs_storage_add_network(ssid, pwd);
            free(ssid); free(pwd);
        }
        httpd_resp_send(req, "OK", 2);
    } else if (strstr(req->uri, "/remove")) {
        char *ssid = extract_json_value(buffer, "ssid");
        if (ssid) {
            nvs_storage_remove_network(ssid);
            free(ssid);
        }
        httpd_resp_send(req, "OK", 2);
    } else if (strstr(req->uri, "/settings")) {
        bridge_config_t *cfg = nvs_storage_get_config_mutable();
        bridge_config_t candidate = *cfg;

        char *ap_ssid = extract_json_value(buffer, "ap_ssid");
        char *ap_pwd = extract_json_value(buffer, "ap_password");
        char *ap_ch_str = extract_json_value(buffer, "ap_channel");
        char *static_ip = extract_json_value(buffer, "static_ip");
        char *use_static_str = extract_json_value(buffer, "use_static_ip");
        char *state_pin_str = extract_json_value(buffer, "state_pin");
        char *state_pin_mode_str = extract_json_value(buffer, "state_pin_mode");
        char *client_mode_str = extract_json_value(buffer, "client_mode");
        char *reset_on_disconnect_str = extract_json_value(buffer, "reset_on_disconnect");
        char *probe_pin_str = extract_json_value(buffer, "probe_pin");
        char *xlimit_pin_str = extract_json_value(buffer, "xlimit_pin");
        char *ylimit_pin_str = extract_json_value(buffer, "ylimit_pin");
        char *zlimit_pin_str = extract_json_value(buffer, "zlimit_pin");
        char *custom1_pin_str = extract_json_value(buffer, "custom1_pin");
        char *custom2_pin_str = extract_json_value(buffer, "custom2_pin");
        char *custom3_pin_str = extract_json_value(buffer, "custom3_pin");
        char *gpio_active_low_mask_str = extract_json_value(buffer, "espnow_gpio_active_low_mask");

        if (ap_ssid) {
            strncpy(candidate.ap_ssid, ap_ssid, MAX_AP_SSID_LEN - 1);
            candidate.ap_ssid[MAX_AP_SSID_LEN - 1] = '\0';
            free(ap_ssid);
        }
        if (ap_pwd) {
            strncpy(candidate.ap_password, ap_pwd, MAX_AP_PASSWORD_LEN - 1);
            candidate.ap_password[MAX_AP_PASSWORD_LEN - 1] = '\0';
            free(ap_pwd);
        }
        if (ap_ch_str) {
            candidate.ap_channel = atoi(ap_ch_str);
            free(ap_ch_str);
        }
        if (static_ip) {
            strncpy(candidate.static_ip, static_ip, MAX_IP_STR_LEN - 1);
            candidate.static_ip[MAX_IP_STR_LEN - 1] = '\0';
            free(static_ip);
        }
        if (use_static_str) {
            candidate.use_static_ip = (strcmp(use_static_str, "true") == 0);
            free(use_static_str);
        }
        if (state_pin_str) {
            candidate.state_pin = atoi(state_pin_str);
            free(state_pin_str);
        }
        if (state_pin_mode_str) {
            candidate.state_pin_mode = atoi(state_pin_mode_str);
            free(state_pin_mode_str);
        }
        if (client_mode_str) {
            candidate.client_mode = atoi(client_mode_str);
            free(client_mode_str);
        }
        if (reset_on_disconnect_str) {
            candidate.reset_on_disconnect = atoi(reset_on_disconnect_str);
            free(reset_on_disconnect_str);
        }
        if (gpio_active_low_mask_str) {
            unsigned long mask = strtoul(gpio_active_low_mask_str, NULL, 10);
            candidate.espnow_gpio_active_low_mask = ((uint32_t)mask) &
                ((1UL << 4) | (1UL << 5) | (1UL << 6) | (1UL << 7) | (1UL << 10));
            free(gpio_active_low_mask_str);
        }

        char *pin_values[7] = {
            probe_pin_str,
            xlimit_pin_str,
            ylimit_pin_str,
            zlimit_pin_str,
            custom1_pin_str,
            custom2_pin_str,
            custom3_pin_str
        };

        const char *pin_names[7] = {
            "PROBE",
            "XLIMIT",
            "YLIMIT",
            "ZLIMIT",
            "CUSTOM1",
            "CUSTOM2",
            "CUSTOM3"
        };

        bool pin_error = false;
        char error_message[160] = {0};

        for (int i = 0; i < 7; i++) {
            if (!pin_values[i]) {
                continue;
            }

            int pin = atoi(pin_values[i]);

            if (pin == -1 || pin == 4 || pin == 5 || pin == 6 || pin == 7 || pin == 10) {
                candidate.espnow_output_pins[i] = (int8_t)pin;
            } else {
                pin_error = true;
                snprintf(error_message, sizeof(error_message),
                         "%s: invalid GPIO%d", pin_names[i], pin);
            }

            free(pin_values[i]);
        }

        /* ----------------------------------------------------
         * STATE PIN VALIDATION
         * ---------------------------------------------------- */

        if (!pin_error) {
            int state_pin = candidate.state_pin;

            if (!(state_pin == -1 || state_pin == 4 || state_pin == 5 ||
                  state_pin == 6 || state_pin == 7 || state_pin == 10)) {
                pin_error = true;
                snprintf(error_message, sizeof(error_message),
                         "State Pin: invalid GPIO%d", state_pin);
            }
        }

        /* ----------------------------------------------------
         * CHECK ESP-NOW OUTPUT DUPLICATES
         * ---------------------------------------------------- */

        if (!pin_error) {
            for (int i = 0; i < 7 && !pin_error; i++) {
                int pin_i = candidate.espnow_output_pins[i];

                if (pin_i < 0) {
                    continue;
                }

                for (int j = i + 1; j < 7; j++) {
                    if (pin_i == candidate.espnow_output_pins[j]) {
                        pin_error = true;
                        snprintf(error_message, sizeof(error_message),
                                 "%s: GPIO%d is already assigned to %s",
                                 pin_names[j],
                                 pin_i,
                                 pin_names[i]);
                        break;
                    }
                }
            }
        }

        /* ----------------------------------------------------
         * CHECK STATE PIN VS ESP-NOW OUTPUTS
         * ---------------------------------------------------- */

        if (!pin_error && candidate.state_pin >= 0) {
            for (int i = 0; i < 7; i++) {
                if (candidate.espnow_output_pins[i] == candidate.state_pin) {
                    pin_error = true;
                    snprintf(error_message, sizeof(error_message),
                             "State Pin: GPIO%d is already assigned to %s",
                             candidate.state_pin,
                             pin_names[i]);
                    break;
                }
            }
        }

        if (pin_error) {
            httpd_resp_set_type(req, "application/json");
            char response[220];
            snprintf(response, sizeof(response),
                     "{\"status\":\"error\",\"error\":\"%s\"}",
                     error_message);
            httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        /* ----------------------------------------------------
         * VALID CONFIGURATION: COMMIT TO RAM + NVS
         * ---------------------------------------------------- */

        /* Se un'uscita ESP-NOW viene disabilitata, rimuoviamo anche
         * l'eventuale associazione SENSOR della stessa funzione.
         * La cancellazione viene fatta solo dopo tutte le validazioni. */
        bool sensor_unassign[7] = { false };
        for (int i = 0; i < 7; i++) {
            if (candidate.espnow_output_pins[i] < 0 &&
                (cfg->espnow_sensor_binding_mask & (1u << i))) {
                sensor_unassign[i] = true;
                candidate.espnow_sensor_binding_mask &= (uint8_t)~(1u << i);
                memset(candidate.espnow_sensor_macs[i], 0, 6);
            }
        }

        /* espnow_unassign_sensor() conserva anche la sincronizzazione radio
         * con il SENSOR online (UNASSIGNED) e aggiorna il binding persistente. */
        for (int i = 0; i < 7; i++) {
            if (sensor_unassign[i] && !espnow_unassign_sensor((uint8_t)i)) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req,
                                "{\"status\":\"error\",\"error\":\"Failed to remove sensor association\"}",
                                HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
        }

        *cfg = candidate;

        if (!nvs_storage_save_config()) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req,
                            "{\"status\":\"error\",\"error\":\"Failed to save configuration to NVS\"}",
                            HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
                        "{\"status\":\"saved\"}",
                        HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool parse_mac_string(const char *text, uint8_t mac[6])
{
    if (!text || !mac) return false;
    unsigned int b[6];
    if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
    return true;
}

static const char *sensor_function_name(int function)
{
    static const char *names[7] = {
        "PROBE", "XLIMIT", "YLIMIT", "ZLIMIT", "CUSTOM1", "CUSTOM2", "CUSTOM3"
    };
    return (function >= 0 && function < 7) ? names[function] : "UNASSIGNED";
}

static esp_err_t espnow_sensors_get_handler(httpd_req_t *req)
{
    espnow_sensor_info_t sensors[16];
    int count = espnow_get_sensor_list(sensors, 16);
    const char *names_dummy = "";
    (void)names_dummy;

    char json[2048];
    int used = snprintf(json, sizeof(json), "{\"sensors\":[");
    if (used < 0 || used >= (int)sizeof(json)) return ESP_FAIL;

    for (int i = 0; i < count; i++) {
        if (i > 0) used += snprintf(json + used, sizeof(json) - used, ",");
        used += snprintf(json + used, sizeof(json) - used,
                         "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"function\":%d}",
                         sensors[i].mac[0], sensors[i].mac[1], sensors[i].mac[2],
                         sensors[i].mac[3], sensors[i].mac[4], sensors[i].mac[5],
                         sensors[i].function);
        if (used >= (int)sizeof(json)) return ESP_FAIL;
    }

    used += snprintf(json + used, sizeof(json) - used, "],\"bindings\":[");
    bool first = true;
    for (int f = 0; f < 7; f++) {
        uint8_t mac[6];
        if (!espnow_get_sensor_binding((uint8_t)f, mac)) continue;
        if (!first) used += snprintf(json + used, sizeof(json) - used, ",");
        first = false;
        used += snprintf(json + used, sizeof(json) - used,
                         "{\"function\":%d,\"name\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
                         f, sensor_function_name(f), mac[0], mac[1], mac[2],
                         mac[3], mac[4], mac[5]);
        if (used >= (int)sizeof(json)) return ESP_FAIL;
    }
    used += snprintf(json + used, sizeof(json) - used, "]}");
    if (used < 0 || used >= (int)sizeof(json)) return ESP_FAIL;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, used);
    return ESP_OK;
}

static esp_err_t espnow_sensor_assignment_post_handler(httpd_req_t *req, bool unassign)
{
    char buffer[256];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';

    char *function_str = extract_json_value(buffer, "function");
    if (!function_str) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid function");
        return ESP_FAIL;
    }
    int function = atoi(function_str);
    free(function_str);
    if (function < 0 || function >= 7) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Function must be 0..6");
        return ESP_FAIL;
    }

    bool ok = false;
    if (unassign) {
        ok = espnow_unassign_sensor((uint8_t)function);
    } else {
        char *mac_str = extract_json_value(buffer, "mac");
        uint8_t mac[6];
        if (!mac_str || !parse_mac_string(mac_str, mac)) {
            if (mac_str) free(mac_str);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid sensor MAC");
            return ESP_FAIL;
        }
        free(mac_str);
        ok = espnow_assign_sensor((uint8_t)function, mac);
    }

    httpd_resp_set_type(req, "application/json");
    if (ok)
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    else
        httpd_resp_send(req, "{\"status\":\"error\",\"error\":\"Assignment rejected\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t espnow_sensor_assign_handler(httpd_req_t *req)
{
    return espnow_sensor_assignment_post_handler(req, false);
}

static esp_err_t espnow_sensor_unassign_handler(httpd_req_t *req)
{
    return espnow_sensor_assignment_post_handler(req, true);
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// =================== OTA HANDLER ===================
static esp_err_t ota_update_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "OTA update handler called");
    esp_err_t err;
    esp_ota_handle_t ota_handle;
    char buf[1024];
    int remaining = req->content_len;

    // Find next OTA partition (alternates ota_0 / ota_1)
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA update on partition %s (size %d)", update_partition->label, update_partition->size);
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    // Receive and write data
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, (remaining < sizeof(buf)) ? remaining : sizeof(buf));
        if (recv_len <= 0) {
            ESP_LOGE(TAG, "Data reception failed");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Data receive error");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        remaining -= recv_len;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "Update success. Rebooting...");
    ESP_LOGI(TAG, "OTA update completed, rebooting in 1 second");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

// =================== Avvia web server ===================
void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.max_uri_handlers = 20;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_uri_t favicon = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler };
        httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
        httpd_uri_t sysinfo = { .uri = "/api/sysinfo", .method = HTTP_GET, .handler = sysinfo_get_handler };
        httpd_uri_t networks = { .uri = "/api/networks", .method = HTTP_GET, .handler = networks_get_handler };
        httpd_uri_t settings = { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler };
        httpd_uri_t debug_get = { .uri = "/api/debug", .method = HTTP_GET, .handler = debug_get_handler };
        httpd_uri_t debug_post = { .uri = "/api/debug", .method = HTTP_POST, .handler = debug_post_handler };
        httpd_uri_t add = { .uri = "/api/networks/add", .method = HTTP_POST, .handler = api_post_handler };
        httpd_uri_t remove = { .uri = "/api/networks/remove", .method = HTTP_POST, .handler = api_post_handler };
        httpd_uri_t save = { .uri = "/api/settings", .method = HTTP_POST, .handler = api_post_handler };
        httpd_uri_t espnow_sensors = { .uri = "/api/espnow/sensors", .method = HTTP_GET, .handler = espnow_sensors_get_handler };
        httpd_uri_t espnow_assign = { .uri = "/api/espnow/assign", .method = HTTP_POST, .handler = espnow_sensor_assign_handler };
        httpd_uri_t espnow_unassign = { .uri = "/api/espnow/unassign", .method = HTTP_POST, .handler = espnow_sensor_unassign_handler };
        httpd_uri_t reset = { .uri = "/api/reset", .method = HTTP_POST, .handler = reset_post_handler };
        httpd_uri_t reboot = { .uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_post_handler };
        httpd_uri_t ota = { .uri = "/update", .method = HTTP_POST, .handler = ota_update_handler };

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &favicon);
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &sysinfo);
        httpd_register_uri_handler(server, &networks);
        httpd_register_uri_handler(server, &settings);
        httpd_register_uri_handler(server, &debug_get);
        httpd_register_uri_handler(server, &debug_post);
        httpd_register_uri_handler(server, &add);
        httpd_register_uri_handler(server, &remove);
        httpd_register_uri_handler(server, &save);
        httpd_register_uri_handler(server, &espnow_sensors);
        httpd_register_uri_handler(server, &espnow_assign);
        httpd_register_uri_handler(server, &espnow_unassign);
        httpd_register_uri_handler(server, &reset);
        httpd_register_uri_handler(server, &reboot);
        httpd_register_uri_handler(server, &ota);

        ESP_LOGI(TAG, "✅ Web server started (OTA handler registered)");
    } else {
        ESP_LOGE(TAG, "❌ Failed to start web server");
    }
}

void web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

bool web_server_is_running(void) {
    return (server != NULL);
}