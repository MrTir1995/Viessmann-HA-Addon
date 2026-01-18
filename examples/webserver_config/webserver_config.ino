/*
 * Viessmann Multi-Protocol Library - Web Server Configuration Example
 * 
 * This example demonstrates an optional web-based configuration interface
 * for the Viessmann decoder library. It allows users to:
 * - Configure protocol type (VBUS, KW-Bus, P300, KM-Bus)
 * - Set serial communication parameters (baud rate, parity)
 * - Monitor real-time heating system data
 * - Save/load configuration from flash memory
 * 
 * Hardware Requirements:
 * - ESP32 or ESP8266 board with WiFi
 * - Serial connection to Viessmann heating system (see HARDWARE_SETUP.md)
 * 
 * Usage:
 * 1. Update WiFi credentials below
 * 2. Upload sketch to ESP32/ESP8266
 * 3. Connect to WiFi network
 * 4. Open browser to http://<device-ip>
 * 5. Configure protocol and serial settings
 * 6. Monitor heating system data
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <Preferences.h>
  #define WEBSERVER_CLASS WebServer
  #define USE_PREFERENCES
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <EEPROM.h>
  #define WEBSERVER_CLASS ESP8266WebServer
  #define USE_EEPROM
#else
  #error "This example requires ESP32 or ESP8266"
#endif

#include "vbusdecoder.h"

// ============================================================================
// WiFi Configuration
// ============================================================================
const char* ssid = "YourWiFiSSID";          // Change to your WiFi SSID
const char* password = "YourWiFiPassword";  // Change to your WiFi password

// ============================================================================
// Configuration Structure
// ============================================================================
struct Config {
  uint8_t protocol;      // 0=VBUS, 1=KW, 2=P300, 3=KM
  uint32_t baudRate;     // Serial baud rate
  uint8_t serialConfig;  // 0=8N1, 1=8E2
  uint8_t rxPin;         // RX pin
  uint8_t txPin;         // TX pin
  uint32_t magic;        // Magic number for validation
};

const uint32_t CONFIG_MAGIC = 0xDEADBEEF;

Config config = {
  PROTOCOL_VBUS,  // Default protocol
  9600,           // Default baud rate for VBUS
  0,              // 8N1
  16,             // Default RX pin (GPIO16 on ESP32/ESP8266)
  17,             // Default TX pin (GPIO17 on ESP32)
  CONFIG_MAGIC
};

// ============================================================================
// Hardware Serial and Decoder
// ============================================================================
#if defined(ESP32)
  HardwareSerial vbusSerial(2);  // Use Serial2 on ESP32
#elif defined(ESP8266)
  // ESP8266 uses Serial for communication (swap pins)
  #define vbusSerial Serial
#endif

VBUSDecoder vbus(&vbusSerial);

// ============================================================================
// Web Server
// ============================================================================
WEBSERVER_CLASS server(80);

// ============================================================================
// Preferences/EEPROM
// ============================================================================
#ifdef USE_PREFERENCES
Preferences preferences;
#endif

// ============================================================================
// Function Prototypes
// ============================================================================
void loadConfig();
void saveConfig();
void applyConfig();
void handleRoot();
void handleConfig();
void handleSaveConfig();
void handleStatus();
void handleData();
void handleRestart();
String getHTML();
String getConfigHTML();
String getStatusHTML();

// ============================================================================
// Setup
// ============================================================================
void setup() {
  // Initialize debug serial
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nViessmann Multi-Protocol Library - Web Server Configuration");
  
  // Load configuration from flash
  loadConfig();
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Apply configuration to serial port and decoder
  applyConfig();
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/saveconfig", HTTP_POST, handleSaveConfig);
  server.on("/status", handleStatus);
  server.on("/data", handleData);
  server.on("/restart", handleRestart);
  
  // Start web server
  server.begin();
  Serial.println("Web server started");
  Serial.print("Access configuration at: http://");
  Serial.println(WiFi.localIP());
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  server.handleClient();
  vbus.loop();
}

// ============================================================================
// Configuration Management
// ============================================================================
void loadConfig() {
#ifdef USE_PREFERENCES
  preferences.begin("viessmann", false);
  
  if (preferences.getUInt("magic", 0) == CONFIG_MAGIC) {
    config.protocol = preferences.getUChar("protocol", PROTOCOL_VBUS);
    config.baudRate = preferences.getUInt("baudRate", 9600);
    config.serialConfig = preferences.getUChar("serialCfg", 0);
    config.rxPin = preferences.getUChar("rxPin", 16);
    config.txPin = preferences.getUChar("txPin", 17);
    Serial.println("Configuration loaded from preferences");
  } else {
    Serial.println("Using default configuration");
  }
  
  preferences.end();
#elif defined(USE_EEPROM)
  EEPROM.begin(sizeof(Config));
  Config tempConfig;
  EEPROM.get(0, tempConfig);
  
  if (tempConfig.magic == CONFIG_MAGIC) {
    config = tempConfig;
    Serial.println("Configuration loaded from EEPROM");
  } else {
    Serial.println("Using default configuration");
  }
#endif
}

void saveConfig() {
  config.magic = CONFIG_MAGIC;
  
#ifdef USE_PREFERENCES
  preferences.begin("viessmann", false);
  preferences.putUInt("magic", config.magic);
  preferences.putUChar("protocol", config.protocol);
  preferences.putUInt("baudRate", config.baudRate);
  preferences.putUChar("serialCfg", config.serialConfig);
  preferences.putUChar("rxPin", config.rxPin);
  preferences.putUChar("txPin", config.txPin);
  preferences.end();
  Serial.println("Configuration saved to preferences");
#elif defined(USE_EEPROM)
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Configuration saved to EEPROM");
#endif
}

void applyConfig() {
  Serial.println("Applying configuration...");
  Serial.print("Protocol: ");
  
  switch(config.protocol) {
    case PROTOCOL_VBUS:
      Serial.println("VBUS");
      break;
    case PROTOCOL_KW:
      Serial.println("KW-Bus");
      break;
    case PROTOCOL_P300:
      Serial.println("P300");
      break;
    case PROTOCOL_KM:
      Serial.println("KM-Bus");
      break;
  }
  
  Serial.print("Baud Rate: ");
  Serial.println(config.baudRate);
  Serial.print("Serial Config: ");
  Serial.println(config.serialConfig == 0 ? "8N1" : "8E2");
  
#if defined(ESP32)
  // Configure ESP32 hardware serial
  vbusSerial.begin(
    config.baudRate, 
    config.serialConfig == 0 ? SERIAL_8N1 : SERIAL_8E2,
    config.rxPin,
    config.txPin
  );
#elif defined(ESP8266)
  // Configure ESP8266 serial
  vbusSerial.begin(config.baudRate);
  if (config.serialConfig == 1) {
    // 8E2 not fully supported on ESP8266, use hardware serial directly
    vbusSerial.begin(config.baudRate, SERIAL_8E2);
  }
  // Swap pins if using alternate UART pins
  if (config.rxPin == 13 && config.txPin == 15) {
    Serial.swap();
  }
#endif
  
  // Initialize decoder with selected protocol
  vbus.begin((ProtocolType)config.protocol);
  
  Serial.println("Configuration applied");
}

// ============================================================================
// Web Server Handlers
// ============================================================================
void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void handleConfig() {
  server.send(200, "text/html", getConfigHTML());
}

void handleSaveConfig() {
  // Parse form data
  if (server.hasArg("protocol")) {
    config.protocol = server.arg("protocol").toInt();
  }
  if (server.hasArg("baudRate")) {
    config.baudRate = server.arg("baudRate").toInt();
  }
  if (server.hasArg("serialConfig")) {
    config.serialConfig = server.arg("serialConfig").toInt();
  }
  if (server.hasArg("rxPin")) {
    config.rxPin = server.arg("rxPin").toInt();
  }
  if (server.hasArg("txPin")) {
    config.txPin = server.arg("txPin").toInt();
  }
  
  // Save configuration
  saveConfig();
  
  // Send response
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Configuration Saved</title>";
  html += "<style>body{font-family:Arial;max-width:800px;margin:50px auto;padding:20px;}";
  html += ".success{background:#d4edda;border:1px solid #c3e6cb;padding:15px;border-radius:5px;}</style>";
  html += "</head><body>";
  html += "<div class='success'><h2>✓ Configuration Saved</h2>";
  html += "<p>The configuration has been saved. Please restart the device for changes to take effect.</p>";
  html += "<p><a href='/config'>← Back to Configuration</a> | ";
  html += "<a href='/restart'>Restart Device</a></p></div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  server.send(200, "text/html", getStatusHTML());
}

void handleData() {
  // Return JSON data for AJAX updates
  String json = "{";
  json += "\"ready\":" + String(vbus.isReady() ? "true" : "false") + ",";
  json += "\"status\":\"" + String(vbus.getVbusStat() ? "OK" : "Error") + "\",";
  json += "\"protocol\":" + String(config.protocol) + ",";
  json += "\"temperatures\":[";
  
  if (vbus.isReady()) {
    uint8_t tempNum = vbus.getTempNum();
    for (uint8_t i = 0; i < tempNum; i++) {
      if (i > 0) json += ",";
      json += String(vbus.getTemp(i), 1);
    }
  }
  
  json += "],\"pumps\":[";
  
  if (vbus.isReady()) {
    uint8_t pumpNum = vbus.getPumpNum();
    for (uint8_t i = 0; i < pumpNum; i++) {
      if (i > 0) json += ",";
      json += String(vbus.getPump(i));
    }
  }
  
  json += "],\"relays\":[";
  
  if (vbus.isReady()) {
    uint8_t relayNum = vbus.getRelayNum();
    for (uint8_t i = 0; i < relayNum; i++) {
      if (i > 0) json += ",";
      json += vbus.getRelay(i) ? "true" : "false";
    }
  }
  
  json += "]}";
  
  server.send(200, "application/json", json);
}

void handleRestart() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='10;url=/'>";
  html += "<title>Restarting...</title>";
  html += "<style>body{font-family:Arial;max-width:800px;margin:50px auto;padding:20px;text-align:center;}</style>";
  html += "</head><body>";
  html += "<h2>Restarting Device...</h2>";
  html += "<p>Please wait, the device will restart and you will be redirected to the home page.</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
}

// ============================================================================
// HTML Generation
// ============================================================================
String getHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Viessmann Decoder - Dashboard</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:0;background:#f5f5f5;}";
  html += "header{background:#0066cc;color:white;padding:20px;text-align:center;}";
  html += ".container{max-width:1200px;margin:20px auto;padding:20px;}";
  html += ".card{background:white;border-radius:8px;padding:20px;margin-bottom:20px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
  html += ".card h2{margin-top:0;color:#333;}";
  html += ".nav{display:flex;justify-content:center;gap:20px;padding:20px;}";
  html += ".nav a{text-decoration:none;color:white;background:#0066cc;padding:10px 20px;border-radius:5px;}";
  html += ".nav a:hover{background:#0052a3;}";
  html += ".status{font-size:18px;margin:10px 0;}";
  html += ".status.ok{color:green;}";
  html += ".status.error{color:red;}";
  html += "table{width:100%;border-collapse:collapse;}";
  html += "th,td{text-align:left;padding:10px;border-bottom:1px solid #ddd;}";
  html += "th{background:#f0f0f0;}";
  html += "</style>";
  html += "<script>";
  html += "function updateData(){";
  html += "fetch('/data').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('status').textContent=d.status;";
  html += "document.getElementById('status').className='status '+(d.status==='OK'?'ok':'error');";
  html += "let protocols=['VBUS','KW-Bus','P300','KM-Bus'];";
  html += "document.getElementById('protocol').textContent=protocols[d.protocol];";
  html += "let tbody=document.getElementById('data');";
  html += "tbody.innerHTML='';";
  html += "if(d.temperatures.length>0){";
  html += "d.temperatures.forEach((t,i)=>{";
  html += "tbody.innerHTML+='<tr><td>Temperature '+(i+1)+'</td><td>'+t.toFixed(1)+' °C</td></tr>';";
  html += "});";
  html += "}";
  html += "if(d.pumps.length>0){";
  html += "d.pumps.forEach((p,i)=>{";
  html += "tbody.innerHTML+='<tr><td>Pump '+(i+1)+'</td><td>'+p+' %</td></tr>';";
  html += "});";
  html += "}";
  html += "if(d.relays.length>0){";
  html += "d.relays.forEach((r,i)=>{";
  html += "tbody.innerHTML+='<tr><td>Relay '+(i+1)+'</td><td>'+(r?'ON':'OFF')+'</td></tr>';";
  html += "});";
  html += "}";
  html += "if(!d.ready){tbody.innerHTML='<tr><td colspan=\"2\">Waiting for data...</td></tr>';}";
  html += "});";
  html += "}";
  html += "setInterval(updateData,2000);";
  html += "window.onload=updateData;";
  html += "</script>";
  html += "</head><body>";
  html += "<header><h1>Viessmann Decoder Dashboard</h1></header>";
  html += "<div class='nav'>";
  html += "<a href='/'>Dashboard</a>";
  html += "<a href='/config'>Configuration</a>";
  html += "<a href='/status'>Status</a>";
  html += "</div>";
  html += "<div class='container'>";
  
  html += "<div class='card'>";
  html += "<h2>System Status</h2>";
  html += "<p>Communication Status: <span id='status' class='status'>Checking...</span></p>";
  html += "<p>Active Protocol: <span id='protocol'>-</span></p>";
  html += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<h2>Live Data</h2>";
  html += "<table><thead><tr><th>Parameter</th><th>Value</th></tr></thead>";
  html += "<tbody id='data'><tr><td colspan='2'>Loading...</td></tr></tbody></table>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  return html;
}

String getConfigHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Viessmann Decoder - Configuration</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:0;background:#f5f5f5;}";
  html += "header{background:#0066cc;color:white;padding:20px;text-align:center;}";
  html += ".container{max-width:800px;margin:20px auto;padding:20px;}";
  html += ".card{background:white;border-radius:8px;padding:20px;margin-bottom:20px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
  html += ".card h2{margin-top:0;color:#333;}";
  html += ".nav{display:flex;justify-content:center;gap:20px;padding:20px;}";
  html += ".nav a{text-decoration:none;color:white;background:#0066cc;padding:10px 20px;border-radius:5px;}";
  html += ".nav a:hover{background:#0052a3;}";
  html += ".form-group{margin-bottom:20px;}";
  html += "label{display:block;margin-bottom:5px;font-weight:bold;color:#333;}";
  html += "select,input{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;font-size:16px;}";
  html += "button{background:#0066cc;color:white;padding:12px 30px;border:none;border-radius:5px;font-size:16px;cursor:pointer;}";
  html += "button:hover{background:#0052a3;}";
  html += ".info{background:#d1ecf1;border:1px solid #bee5eb;padding:15px;border-radius:5px;margin-bottom:20px;}";
  html += "</style>";
  html += "</head><body>";
  html += "<header><h1>Configuration</h1></header>";
  html += "<div class='nav'>";
  html += "<a href='/'>Dashboard</a>";
  html += "<a href='/config'>Configuration</a>";
  html += "<a href='/status'>Status</a>";
  html += "</div>";
  html += "<div class='container'>";
  
  html += "<div class='info'>";
  html += "<strong>⚠️ Important:</strong> After changing configuration, you must restart the device for changes to take effect.";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<h2>Protocol Settings</h2>";
  html += "<form action='/saveconfig' method='POST'>";
  
  html += "<div class='form-group'>";
  html += "<label for='protocol'>Protocol Type:</label>";
  html += "<select name='protocol' id='protocol'>";
  html += "<option value='0'" + String(config.protocol == 0 ? " selected" : "") + ">VBUS (RESOL) - Solar Controllers</option>";
  html += "<option value='1'" + String(config.protocol == 1 ? " selected" : "") + ">KW-Bus (VS1) - Old Vitotronic</option>";
  html += "<option value='2'" + String(config.protocol == 2 ? " selected" : "") + ">P300 (VS2/Optolink) - Modern Vitodens</option>";
  html += "<option value='3'" + String(config.protocol == 3 ? " selected" : "") + ">KM-Bus - Remote Controls</option>";
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='baudRate'>Baud Rate:</label>";
  html += "<select name='baudRate' id='baudRate'>";
  html += "<option value='2400'" + String(config.baudRate == 2400 ? " selected" : "") + ">2400</option>";
  html += "<option value='4800'" + String(config.baudRate == 4800 ? " selected" : "") + ">4800</option>";
  html += "<option value='9600'" + String(config.baudRate == 9600 ? " selected" : "") + ">9600</option>";
  html += "<option value='19200'" + String(config.baudRate == 19200 ? " selected" : "") + ">19200</option>";
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='serialConfig'>Serial Configuration:</label>";
  html += "<select name='serialConfig' id='serialConfig'>";
  html += "<option value='0'" + String(config.serialConfig == 0 ? " selected" : "") + ">8N1 (8 data bits, no parity, 1 stop bit)</option>";
  html += "<option value='1'" + String(config.serialConfig == 1 ? " selected" : "") + ">8E2 (8 data bits, even parity, 2 stop bits)</option>";
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='rxPin'>RX Pin (GPIO):</label>";
  html += "<input type='number' name='rxPin' id='rxPin' value='" + String(config.rxPin) + "' min='0' max='39'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='txPin'>TX Pin (GPIO):</label>";
  html += "<input type='number' name='txPin' id='txPin' value='" + String(config.txPin) + "' min='0' max='39'>";
  html += "</div>";
  
  html += "<button type='submit'>Save Configuration</button>";
  html += "</form>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<h2>Quick Configuration Presets</h2>";
  html += "<p><strong>VBUS (Vitosolic 200, DeltaSol):</strong> Protocol=VBUS, Baud=9600, Serial=8N1</p>";
  html += "<p><strong>KW-Bus (Vitotronic 200):</strong> Protocol=KW-Bus, Baud=4800, Serial=8E2</p>";
  html += "<p><strong>P300 (Vitodens):</strong> Protocol=P300, Baud=4800, Serial=8E2</p>";
  html += "<p><strong>KM-Bus:</strong> Protocol=KM-Bus, Baud varies (9600 typical), Serial=8N1</p>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  return html;
}

String getStatusHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Viessmann Decoder - Status</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:0;background:#f5f5f5;}";
  html += "header{background:#0066cc;color:white;padding:20px;text-align:center;}";
  html += ".container{max-width:800px;margin:20px auto;padding:20px;}";
  html += ".card{background:white;border-radius:8px;padding:20px;margin-bottom:20px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
  html += ".card h2{margin-top:0;color:#333;}";
  html += ".nav{display:flex;justify-content:center;gap:20px;padding:20px;}";
  html += ".nav a{text-decoration:none;color:white;background:#0066cc;padding:10px 20px;border-radius:5px;}";
  html += ".nav a:hover{background:#0052a3;}";
  html += "table{width:100%;border-collapse:collapse;}";
  html += "th,td{text-align:left;padding:10px;border-bottom:1px solid #ddd;}";
  html += "th{background:#f0f0f0;font-weight:bold;}";
  html += "</style>";
  html += "</head><body>";
  html += "<header><h1>System Status</h1></header>";
  html += "<div class='nav'>";
  html += "<a href='/'>Dashboard</a>";
  html += "<a href='/config'>Configuration</a>";
  html += "<a href='/status'>Status</a>";
  html += "</div>";
  html += "<div class='container'>";
  
  html += "<div class='card'>";
  html += "<h2>Current Configuration</h2>";
  html += "<table>";
  
  html += "<tr><th>Parameter</th><th>Value</th></tr>";
  
  String protocolName;
  switch(config.protocol) {
    case 0: protocolName = "VBUS (RESOL)"; break;
    case 1: protocolName = "KW-Bus (VS1)"; break;
    case 2: protocolName = "P300 (VS2/Optolink)"; break;
    case 3: protocolName = "KM-Bus"; break;
    default: protocolName = "Unknown";
  }
  html += "<tr><td>Protocol</td><td>" + protocolName + "</td></tr>";
  html += "<tr><td>Baud Rate</td><td>" + String(config.baudRate) + "</td></tr>";
  html += "<tr><td>Serial Config</td><td>" + String(config.serialConfig == 0 ? "8N1" : "8E2") + "</td></tr>";
  html += "<tr><td>RX Pin</td><td>GPIO" + String(config.rxPin) + "</td></tr>";
  html += "<tr><td>TX Pin</td><td>GPIO" + String(config.txPin) + "</td></tr>";
  
  html += "</table>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<h2>Network Information</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Value</th></tr>";
  html += "<tr><td>WiFi SSID</td><td>" + String(ssid) + "</td></tr>";
  html += "<tr><td>IP Address</td><td>" + WiFi.localIP().toString() + "</td></tr>";
  html += "<tr><td>MAC Address</td><td>" + WiFi.macAddress() + "</td></tr>";
  html += "<tr><td>Signal Strength</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
  html += "</table>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<h2>System Information</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Value</th></tr>";
  
#if defined(ESP32)
  html += "<tr><td>Board</td><td>ESP32</td></tr>";
  html += "<tr><td>Chip Model</td><td>ESP32</td></tr>";
  html += "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
#elif defined(ESP8266)
  html += "<tr><td>Board</td><td>ESP8266</td></tr>";
  html += "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
#endif
  
  html += "<tr><td>Communication Status</td><td>" + String(vbus.getVbusStat() ? "OK" : "Error") + "</td></tr>";
  html += "<tr><td>Data Ready</td><td>" + String(vbus.isReady() ? "Yes" : "No") + "</td></tr>";
  
  html += "</table>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  return html;
}
