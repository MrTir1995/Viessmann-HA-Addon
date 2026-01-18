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
  HardwareSerial vbusSerial(2);  // Use Serial2 on ESP32
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #define vbusSerial Serial
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
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nViessmann Multi-Protocol Library - MQTT Integration Example");
  Serial.println("==============================================================");
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize VBUS decoder
  Serial.println("\nInitializing decoder...");
  vbusSerial.begin(BAUD_RATE);
  vbus.begin(PROTOCOL);
  
  Serial.print("Protocol: ");
  switch (PROTOCOL) {
    case PROTOCOL_VBUS:
      Serial.println("VBUS (RESOL)");
      break;
    case PROTOCOL_KW:
      Serial.println("KW-Bus (VS1)");
      break;
    case PROTOCOL_P300:
      Serial.println("P300 (VS2/Optolink)");
      break;
    case PROTOCOL_KM:
      Serial.println("KM-Bus");
      break;
  }
  
  // Configure MQTT client
  Serial.println("\nConfiguring MQTT...");
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
  
  Serial.println("MQTT configuration complete");
  Serial.print("Broker: ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  Serial.print("Base topic: ");
  Serial.println(MQTT_BASE_TOPIC);
  Serial.print("Home Assistant: ");
  Serial.println(USE_HOME_ASSISTANT ? "Enabled" : "Disabled");
  
  Serial.println("\n==============================================================");
  Serial.println("Setup complete. Starting main loop...");
  Serial.println("==============================================================\n");
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
  Serial.println("\n--- Status Update ---");
  
  // WiFi status
  Serial.print("WiFi: ");
  Serial.print(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.print(" (RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm)");
  
  // MQTT status
  Serial.print("MQTT: ");
  Serial.println(mqttClient.isConnected() ? "Connected" : "Disconnected");
  
  // Decoder status
  Serial.print("Decoder: ");
  if (vbus.isReady()) {
    Serial.println("Ready");
    
    // Print sensor data
    Serial.print("Temperatures: ");
    for (uint8_t i = 0; i < vbus.getTempNum(); i++) {
      if (i > 0) Serial.print(", ");
      Serial.print(vbus.getTemp(i), 1);
      Serial.print("°C");
    }
    Serial.println();
    
    Serial.print("Pumps: ");
    for (uint8_t i = 0; i < vbus.getPumpNum(); i++) {
      if (i > 0) Serial.print(", ");
      Serial.print(vbus.getPump(i));
      Serial.print("%");
    }
    Serial.println();
    
    Serial.print("Relays: ");
    for (uint8_t i = 0; i < vbus.getRelayNum(); i++) {
      if (i > 0) Serial.print(", ");
      Serial.print(vbus.getRelay(i) ? "ON" : "OFF");
    }
    Serial.println();
    
    // KM-Bus specific data
    if (vbus.getProtocol() == PROTOCOL_KM) {
      Serial.println("\nKM-Bus Data:");
      Serial.print("  Burner: ");
      Serial.println(vbus.getKMBusBurnerStatus() ? "ON" : "OFF");
      Serial.print("  Main Pump: ");
      Serial.println(vbus.getKMBusMainPumpStatus() ? "ON" : "OFF");
      Serial.print("  Boiler: ");
      Serial.print(vbus.getKMBusBoilerTemp(), 1);
      Serial.println("°C");
    }
  } else {
    Serial.println("Waiting for data...");
  }
  
  Serial.println("--------------------\n");
}
