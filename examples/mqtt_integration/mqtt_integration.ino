/*
 * Viessmann Multi-Protocol Library - MQTT Integration Example
 * 
 * This example demonstrates MQTT integration with Home Assistant auto-discovery.
 * All temperature sensors, pump states, and relay states are automatically
 * published to MQTT and discovered by Home Assistant.
 * 
 * Hardware Requirements:
 * - ESP32 or ESP8266 board with WiFi
 * - Serial connection to Viessmann heating system
 * - MQTT broker (e.g., Mosquitto, Home Assistant built-in)
 * 
 * Dependencies:
 * - Viessmann Multi-Protocol Library
 * - PubSubClient library (install via Library Manager)
 * 
 * Configuration:
 * 1. Update WiFi credentials below
 * 2. Update MQTT broker settings
 * 3. Select protocol type
 * 4. Upload to ESP32/ESP8266
 */

#if defined(ESP32)
  #include <WiFi.h>
  #define debugSerial Serial
  HardwareSerial vbusSerial(2);  // Use Serial2 on ESP32
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  HardwareSerial& vbusSerial = Serial;
  #define debugSerial Serial1  // TX-only diagnostics on GPIO2, separate from the bus UART.
#else
  #error "This example requires ESP32 or ESP8266"
#endif

#include "vbusdecoder.h"
#include "VBUSMqttClient.h"

// ============================================================================
// Configuration
// ============================================================================

// WiFi Settings
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// MQTT Settings
const char* MQTT_BROKER = "192.168.1.100";    // MQTT broker IP address
const uint16_t MQTT_PORT = 1883;              // MQTT broker port
const char* MQTT_USERNAME = "";                // MQTT username (leave empty if not required)
const char* MQTT_PASSWORD = "";                // MQTT password (leave empty if not required)
const char* MQTT_CLIENT_ID = "viessmann";      // MQTT client ID
const char* MQTT_BASE_TOPIC = "viessmann";     // Base topic for all messages

// Home Assistant Integration
const bool USE_HOME_ASSISTANT = true;          // Enable Home Assistant auto-discovery
const char* HA_DISCOVERY_PREFIX = "homeassistant";  // Home Assistant discovery prefix

// Serial Communication Settings
const ProtocolType PROTOCOL = PROTOCOL_VBUS;   // Protocol type
const uint32_t BAUD_RATE = 9600;               // Baud rate (9600 for VBUS, 4800 for KW/P300)

// Publishing Interval
const uint16_t PUBLISH_INTERVAL = 30;          // Publish interval in seconds

// ============================================================================
// Global Objects
// ============================================================================

VBUSDecoder vbus(&vbusSerial);

#if defined(ESP32) || defined(ESP8266)
WiFiClient wifiClient;
VBUSMqttClient mqttClient(&vbus, &wifiClient);
#endif

// ============================================================================
// Setup
// ============================================================================

void setup() {
  // Initialize debug serial
  debugSerial.begin(115200);
  delay(1000);
  debugSerial.println("\n\nViessmann Multi-Protocol Library - MQTT Integration Example");
  debugSerial.println("==============================================================");
  
  // Connect to WiFi
  debugSerial.print("Connecting to WiFi: ");
  debugSerial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debugSerial.print(".");
  }
  
  debugSerial.println("\nWiFi connected!");
  debugSerial.print("IP address: ");
  debugSerial.println(WiFi.localIP());
  
  // Initialize VBUS decoder
  debugSerial.println("\nInitializing decoder...");
  vbusSerial.begin(BAUD_RATE);
  vbus.begin(PROTOCOL);
  
  debugSerial.print("Protocol: ");
  switch (PROTOCOL) {
    case PROTOCOL_VBUS:
      debugSerial.println("VBUS (RESOL)");
      break;
    case PROTOCOL_KW:
      debugSerial.println("KW-Bus (VS1)");
      break;
    case PROTOCOL_P300:
      debugSerial.println("P300 (VS2/Optolink)");
      break;
    case PROTOCOL_KM:
      debugSerial.println("KM-Bus");
      break;
  }
  
  // Configure MQTT client
  debugSerial.println("\nConfiguring MQTT...");
  MqttConfig mqttConfig;
  mqttConfig.broker = MQTT_BROKER;
  mqttConfig.port = MQTT_PORT;
  mqttConfig.username = MQTT_USERNAME[0] ? MQTT_USERNAME : nullptr;
  mqttConfig.password = MQTT_PASSWORD[0] ? MQTT_PASSWORD : nullptr;
  mqttConfig.clientId = MQTT_CLIENT_ID;
  mqttConfig.baseTopic = MQTT_BASE_TOPIC;
  mqttConfig.publishInterval = PUBLISH_INTERVAL;
  mqttConfig.useHomeAssistant = USE_HOME_ASSISTANT;
  mqttConfig.haDiscoveryPrefix = HA_DISCOVERY_PREFIX;
  
  mqttClient.begin(mqttConfig);
  
  debugSerial.println("MQTT configuration complete");
  debugSerial.print("Broker: ");
  debugSerial.print(MQTT_BROKER);
  debugSerial.print(":");
  debugSerial.println(MQTT_PORT);
  debugSerial.print("Base topic: ");
  debugSerial.println(MQTT_BASE_TOPIC);
  debugSerial.print("Home Assistant: ");
  debugSerial.println(USE_HOME_ASSISTANT ? "Enabled" : "Disabled");
  
  debugSerial.println("\n==============================================================");
  debugSerial.println("Setup complete. Starting main loop...");
  debugSerial.println("==============================================================\n");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Process decoder
  vbus.loop();
  
  // Process MQTT (handles auto-reconnect and periodic publishing)
  mqttClient.loop();
  
  // Print status every 10 seconds
  static uint32_t lastStatus = 0;
  uint32_t now = millis();
  if (now - lastStatus >= 10000) {
    printStatus();
    lastStatus = now;
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

void printStatus() {
  debugSerial.println("\n--- Status Update ---");
  
  // WiFi status
  debugSerial.print("WiFi: ");
  debugSerial.print(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  debugSerial.print(" (RSSI: ");
  debugSerial.print(WiFi.RSSI());
  debugSerial.println(" dBm)");
  
  // MQTT status
  debugSerial.print("MQTT: ");
  debugSerial.println(mqttClient.isConnected() ? "Connected" : "Disconnected");
  
  // Decoder status
  debugSerial.print("Decoder: ");
  if (vbus.isReady()) {
    debugSerial.println("Ready");
    
    // Print sensor data
    debugSerial.print("Temperatures: ");
    for (uint8_t i = 0; i < vbus.getTempNum(); i++) {
      if (i > 0) debugSerial.print(", ");
      debugSerial.print(vbus.getTemp(i), 1);
      debugSerial.print("°C");
    }
    debugSerial.println();
    
    debugSerial.print("Pumps: ");
    for (uint8_t i = 0; i < vbus.getPumpNum(); i++) {
      if (i > 0) debugSerial.print(", ");
      debugSerial.print(vbus.getPump(i));
      debugSerial.print("%");
    }
    debugSerial.println();
    
    debugSerial.print("Relays: ");
    for (uint8_t i = 0; i < vbus.getRelayNum(); i++) {
      if (i > 0) debugSerial.print(", ");
      debugSerial.print(vbus.getRelay(i) ? "ON" : "OFF");
    }
    debugSerial.println();
    
    // KM-Bus specific data
    if (vbus.getProtocol() == PROTOCOL_KM) {
      debugSerial.println("\nKM-Bus Data:");
      debugSerial.print("  Burner: ");
      debugSerial.println(vbus.getKMBusBurnerStatus() ? "ON" : "OFF");
      debugSerial.print("  Main Pump: ");
      debugSerial.println(vbus.getKMBusMainPumpStatus() ? "ON" : "OFF");
      debugSerial.print("  Boiler: ");
      debugSerial.print(vbus.getKMBusBoilerTemp(), 1);
      debugSerial.println("°C");
    }
  } else {
    debugSerial.println("Waiting for data...");
  }
  
  debugSerial.println("--------------------\n");
}
