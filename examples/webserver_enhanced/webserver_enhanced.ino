/*
 * Viessmann Multi-Protocol Library - Enhanced Web Server
 * 
 * Features:
 * - Real-time monitoring dashboard
 * - Historical data graphing with Chart.js
 * - Multi-language support (English, German)
 * - Data logging and export
 * - Control commands interface
 * - Energy dashboard integration
 * - Mobile-responsive design
 * 
 * Hardware Requirements:
 * - ESP32 (recommended) or ESP8266 with at least 4MB flash
 * - Serial connection to Viessmann heating system
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
#include "VBUSDataLogger.h"
#include "VBUSScheduler.h"

// ============================================================================
// Configuration
// ============================================================================

const char* ssid = "YourWiFiSSID";
const char* password = "YourWiFiPassword";

struct Config {
  uint8_t protocol;
  uint32_t baudRate;
  uint8_t serialConfig;
  uint8_t rxPin;
  uint8_t txPin;
  uint8_t language;      // 0=English, 1=German
  uint32_t magic;
};

const uint32_t CONFIG_MAGIC = 0xDEADBEEF;

Config config = {
  PROTOCOL_VBUS,
  9600,
  0,
  16,
  17,
  0,  // Default language: English
  CONFIG_MAGIC
};

// ============================================================================
// Global Objects
// ============================================================================

#if defined(ESP32)
  HardwareSerial vbusSerial(2);
#elif defined(ESP8266)
  #define vbusSerial Serial
#endif

VBUSDecoder vbus(&vbusSerial);
VBUSDataLogger logger(&vbus, 288);  // 24 hours at 5-minute intervals
WEBSERVER_CLASS server(80);

#ifdef USE_PREFERENCES
Preferences preferences;
#endif

// ============================================================================
// Language Strings
// ============================================================================

struct LanguageStrings {
  const char* dashboard;
  const char* graphs;
  const char* settings;
  const char* temperature;
  const char* pump;
  const char* relay;
  const char* status;
  const char* ready;
  const char* waiting;
  const char* protocol;
  const char* save;
  const char* export;
  const char* lastUpdate;
};

const LanguageStrings lang_en = {
  "Dashboard", "Graphs", "Settings", "Temperature", "Pump", "Relay",
  "Status", "Ready", "Waiting for data", "Protocol", "Save", "Export",
  "Last Update"
};

const LanguageStrings lang_de = {
  "Übersicht", "Diagramme", "Einstellungen", "Temperatur", "Pumpe", "Relais",
  "Status", "Bereit", "Warte auf Daten", "Protokoll", "Speichern", "Exportieren",
  "Letzte Aktualisierung"
};

const LanguageStrings* lang = &lang_en;

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nViessmann Enhanced Web Server");
  
  loadConfig();
  
  // Set language
  lang = (config.language == 1) ? &lang_de : &lang_en;
  
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
  
  // Apply configuration
  applyConfig();
  
  // Initialize logger
  logger.begin();
  logger.setLogInterval(300);  // 5 minutes
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.on("/graph-data", handleGraphData);
  server.on("/config", handleConfig);
  server.on("/saveconfig", HTTP_POST, handleSaveConfig);
  server.on("/export", handleExport);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/language", HTTP_POST, handleLanguage);
  
  server.begin();
  Serial.println("Web server started");
  Serial.print("Access at: http://");
  Serial.println(WiFi.localIP());
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  server.handleClient();
  vbus.loop();
  logger.loop();
}

// ============================================================================
// Web Handlers
// ============================================================================

void handleRoot() {
  String html = getHTMLHeader();
  html += "<div class='container'>";
  html += "<h1>" + String(lang->dashboard) + "</h1>";
  html += "<div id='status-panel'></div>";
  html += "<div id='data-panel'></div>";
  html += "<canvas id='tempChart' width='400' height='200'></canvas>";
  html += "</div>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += getJavaScript();
  html += getHTMLFooter();
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"ready\":" + String(vbus.isReady() ? "true" : "false") + ",";
  json += "\"protocol\":" + String(vbus.getProtocol()) + ",";
  
  if (vbus.isReady()) {
    json += "\"temperatures\":[";
    for (uint8_t i = 0; i < vbus.getTempNum(); i++) {
      if (i > 0) json += ",";
      json += String(vbus.getTemp(i), 2);
    }
    json += "],\"pumps\":[";
    for (uint8_t i = 0; i < vbus.getPumpNum(); i++) {
      if (i > 0) json += ",";
      json += String(vbus.getPump(i));
    }
    json += "],\"relays\":[";
    for (uint8_t i = 0; i < vbus.getRelayNum(); i++) {
      if (i > 0) json += ",";
      json += vbus.getRelay(i) ? "true" : "false";
    }
    json += "],\"errorMask\":" + String(vbus.getErrorMask());
    json += ",\"heatQuantity\":" + String(vbus.getHeatQuantity());
    
    if (vbus.getProtocol() == PROTOCOL_KM) {
      json += ",\"kmbus\":{";
      json += "\"burner\":" + String(vbus.getKMBusBurnerStatus() ? "true" : "false");
      json += ",\"mainPump\":" + String(vbus.getKMBusMainPumpStatus() ? "true" : "false");
      json += ",\"boilerTemp\":" + String(vbus.getKMBusBoilerTemp(), 2);
      json += ",\"outdoorTemp\":" + String(vbus.getKMBusOutdoorTemp(), 2);
      json += ",\"setpointTemp\":" + String(vbus.getKMBusSetpointTemp(), 2);
      json += "}";
    }
  }
  
  json += "}";
  server.send(200, "application/json", json);
}

void handleHistory() {
  String html = getHTMLHeader();
  html += "<div class='container'>";
  html += "<h1>" + String(lang->graphs) + "</h1>";
  html += "<canvas id='historyChart' width='800' height='400'></canvas>";
  html += "<button onclick='exportData()'>" + String(lang->export) + "</button>";
  html += "</div>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += getHistoryJavaScript();
  html += getHTMLFooter();
  server.send(200, "text/html", html);
}

void handleGraphData() {
  uint32_t now = millis() / 1000;
  uint32_t startTime = now - 86400;  // Last 24 hours
  
  String json = logger.exportJSON(startTime, now);
  server.send(200, "application/json", json);
}

void handleConfig() {
  String html = getHTMLHeader();
  html += "<div class='container'>";
  html += "<h1>" + String(lang->settings) + "</h1>";
  html += "<form action='/saveconfig' method='POST'>";
  html += "<label>Protocol:</label>";
  html += "<select name='protocol'>";
  html += "<option value='0'" + String(config.protocol == 0 ? " selected" : "") + ">VBUS</option>";
  html += "<option value='1'" + String(config.protocol == 1 ? " selected" : "") + ">KW-Bus</option>";
  html += "<option value='2'" + String(config.protocol == 2 ? " selected" : "") + ">P300</option>";
  html += "<option value='3'" + String(config.protocol == 3 ? " selected" : "") + ">KM-Bus</option>";
  html += "</select><br>";
  html += "<label>Baud Rate:</label>";
  html += "<input type='number' name='baudrate' value='" + String(config.baudRate) + "'><br>";
  html += "<button type='submit'>" + String(lang->save) + "</button>";
  html += "</form>";
  html += "</div>";
  html += getHTMLFooter();
  server.send(200, "text/html", html);
}

void handleSaveConfig() {
  if (server.hasArg("protocol")) {
    config.protocol = server.arg("protocol").toInt();
  }
  if (server.hasArg("baudrate")) {
    config.baudRate = server.arg("baudrate").toInt();
  }
  saveConfig();
  server.sendHeader("Location", "/config");
  server.send(303);
}

void handleExport() {
  uint32_t now = millis() / 1000;
  uint32_t startTime = now - 86400;
  String csv = logger.exportCSV(startTime, now);
  server.send(200, "text/csv", csv);
}

void handleControl() {
  if (vbus.getProtocol() != PROTOCOL_KM) {
    server.send(400, "text/plain", "Control only available for KM-Bus");
    return;
  }
  
  String response = "{\"success\":false}";
  
  if (server.hasArg("mode")) {
    uint8_t mode = server.arg("mode").toInt();
    if (vbus.setKMBusMode(mode)) {
      response = "{\"success\":true}";
    }
  } else if (server.hasArg("setpoint")) {
    float setpoint = server.arg("setpoint").toFloat();
    uint8_t circuit = server.hasArg("circuit") ? server.arg("circuit").toInt() : 0;
    if (vbus.setKMBusSetpoint(circuit, setpoint)) {
      response = "{\"success\":true}";
    }
  }
  
  server.send(200, "application/json", response);
}

void handleLanguage() {
  if (server.hasArg("lang")) {
    config.language = server.arg("lang").toInt();
    lang = (config.language == 1) ? &lang_de : &lang_en;
    saveConfig();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// ============================================================================
// HTML Helpers
// ============================================================================

String getHTMLHeader() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Viessmann</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f0f0f0}";
  html += ".container{max-width:1200px;margin:0 auto;background:white;padding:20px;border-radius:8px}";
  html += "canvas{max-width:100%;height:auto}";
  html += "button{padding:10px 20px;margin:5px;background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer}";
  html += "button:hover{background:#0056b3}";
  html += "</style></head><body>";
  return html;
}

String getHTMLFooter() {
  return "</body></html>";
}

String getJavaScript() {
  String js = "<script>";
  js += "function updateData(){";
  js += "fetch('/data').then(r=>r.json()).then(data=>{";
  js += "document.getElementById('status-panel').innerHTML=";
  js += "'<h2>" + String(lang->status) + ": '+(data.ready?'" + String(lang->ready) + "':'" + String(lang->waiting) + "')+'</h2>';";
  js += "if(data.ready){";
  js += "let html='<h3>" + String(lang->temperature) + "s:</h3><ul>';";
  js += "data.temperatures.forEach((t,i)=>html+=`<li>Sensor ${i}: ${t.toFixed(1)}°C</li>`);";
  js += "html+='</ul>';";
  js += "document.getElementById('data-panel').innerHTML=html;";
  js += "}});";
  js += "}";
  js += "setInterval(updateData,2000);";
  js += "updateData();";
  js += "</script>";
  return js;
}

String getHistoryJavaScript() {
  String js = "<script>";
  js += "fetch('/graph-data').then(r=>r.json()).then(data=>{";
  js += "const ctx=document.getElementById('historyChart').getContext('2d');";
  js += "const labels=data.dataPoints.map(p=>new Date(p.timestamp*1000).toLocaleTimeString());";
  js += "const temps=data.dataPoints.map(p=>p.temperatures[0]);";
  js += "new Chart(ctx,{";
  js += "type:'line',";
  js += "data:{labels:labels,datasets:[{label:'" + String(lang->temperature) + " 1',data:temps,borderColor:'rgb(255,99,132)',tension:0.1}]},";
  js += "options:{responsive:true,scales:{y:{beginAtZero:false}}}";
  js += "});";
  js += "});";
  js += "function exportData(){window.location='/export';}";
  js += "</script>";
  return js;
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
    config.language = preferences.getUChar("language", 0);
    Serial.println("Configuration loaded");
  }
  preferences.end();
#elif defined(USE_EEPROM)
  EEPROM.begin(sizeof(Config));
  Config tempConfig;
  EEPROM.get(0, tempConfig);
  if (tempConfig.magic == CONFIG_MAGIC) {
    config = tempConfig;
    Serial.println("Configuration loaded");
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
  preferences.putUChar("language", config.language);
  preferences.end();
#elif defined(USE_EEPROM)
  EEPROM.put(0, config);
  EEPROM.commit();
#endif
}

void applyConfig() {
#if defined(ESP32)
  vbusSerial.begin(config.baudRate, 
                  config.serialConfig == 0 ? SERIAL_8N1 : SERIAL_8E2,
                  config.rxPin, config.txPin);
#elif defined(ESP8266)
  vbusSerial.begin(config.baudRate);
#endif
  vbus.begin((ProtocolType)config.protocol);
}
