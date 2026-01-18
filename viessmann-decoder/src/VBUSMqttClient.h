/*
 * Viessmann Multi-Protocol Library - MQTT Client Wrapper
 * Provides MQTT integration with Home Assistant auto-discovery
 */

#pragma once
#ifndef VBUSMqttClient_h
#define VBUSMqttClient_h

#include <Arduino.h>
#include "vbusdecoder.h"

#if defined(ESP32) || defined(ESP8266)
  #include <PubSubClient.h>
#endif

// MQTT Configuration
struct MqttConfig {
  const char* broker;          // MQTT broker address
  uint16_t port;               // MQTT broker port (default: 1883)
  const char* username;        // MQTT username (optional)
  const char* password;        // MQTT password (optional)
  const char* clientId;        // MQTT client ID
  const char* baseTopic;       // Base topic (e.g., "viessmann")
  uint16_t publishInterval;    // Publish interval in seconds (default: 30)
  bool useHomeAssistant;       // Enable Home Assistant auto-discovery
  const char* haDiscoveryPrefix; // HA discovery prefix (default: "homeassistant")
};

class VBUSMqttClient {
  public:
    VBUSMqttClient(VBUSDecoder* decoder, Client* networkClient);
    ~VBUSMqttClient();
    
    // Configuration
    void begin(const MqttConfig& config);
    void setConfig(const MqttConfig& config);
    
    // Connection management
    bool connect();
    void disconnect();
    bool isConnected();
    void loop();
    
    // Publishing
    void publishAll();
    void publishTemperatures();
    void publishPumps();
    void publishRelays();
    void publishStatus();
    void publishKMBusData();
    
    // Home Assistant integration
    void publishHomeAssistantDiscovery();
    void publishHomeAssistantSensors();
    
    // Manual publishing
    bool publish(const char* topic, const char* payload, bool retained = false);
    
    // Callbacks
    void setCallback(void (*callback)(char*, uint8_t*, unsigned int));
    
  private:
    VBUSDecoder* _decoder;
    PubSubClient* _mqttClient;
    MqttConfig _config;
    uint32_t _lastPublish;
    bool _discoveryPublished;
    
    // Helper methods
    void _reconnect();
    void _publishSensor(const char* name, const char* deviceClass, 
                       const char* unit, const char* valueTopic);
    void _publishBinarySensor(const char* name, const char* deviceClass, 
                             const char* valueTopic);
    String _buildTopic(const char* suffix);
    String _buildStateTopic(const char* suffix);
    String _buildDiscoveryTopic(const char* component, const char* objectId);
    void _publishFloat(const char* topic, float value);
    void _publishInt(const char* topic, int value);
    void _publishBool(const char* topic, bool value);
};

#endif
