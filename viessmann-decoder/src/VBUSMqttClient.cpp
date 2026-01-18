/*
 * Viessmann Multi-Protocol Library - MQTT Client Implementation
 */

#include "VBUSMqttClient.h"

#if defined(ESP32) || defined(ESP8266)

VBUSMqttClient::VBUSMqttClient(VBUSDecoder* decoder, Client* networkClient) :
  _decoder(decoder),
  _lastPublish(0),
  _discoveryPublished(false)
{
  _mqttClient = new PubSubClient(*networkClient);
}

VBUSMqttClient::~VBUSMqttClient() {
  delete _mqttClient;
}

void VBUSMqttClient::begin(const MqttConfig& config) {
  _config = config;
  _mqttClient->setServer(_config.broker, _config.port);
}

void VBUSMqttClient::setConfig(const MqttConfig& config) {
  _config = config;
  _mqttClient->setServer(_config.broker, _config.port);
}

bool VBUSMqttClient::connect() {
  if (_mqttClient->connected()) return true;
  
  bool connected = false;
  if (_config.username && _config.password) {
    connected = _mqttClient->connect(_config.clientId, _config.username, _config.password);
  } else {
    connected = _mqttClient->connect(_config.clientId);
  }
  
  if (connected && _config.useHomeAssistant && !_discoveryPublished) {
    publishHomeAssistantDiscovery();
    _discoveryPublished = true;
  }
  
  return connected;
}

void VBUSMqttClient::disconnect() {
  _mqttClient->disconnect();
}

bool VBUSMqttClient::isConnected() {
  return _mqttClient->connected();
}

void VBUSMqttClient::loop() {
  if (!_mqttClient->connected()) {
    _reconnect();
  }
  
  _mqttClient->loop();
  
  // Check if it's time to publish
  uint32_t now = millis();
  if (now - _lastPublish >= (_config.publishInterval * 1000)) {
    publishAll();
    _lastPublish = now;
  }
}

void VBUSMqttClient::publishAll() {
  if (!_decoder->isReady()) return;
  
  publishTemperatures();
  publishPumps();
  publishRelays();
  publishStatus();
  
  if (_decoder->getProtocol() == PROTOCOL_KM) {
    publishKMBusData();
  }
}

void VBUSMqttClient::publishTemperatures() {
  uint8_t tempCount = _decoder->getTempNum();
  for (uint8_t i = 0; i < tempCount; i++) {
    float temp = _decoder->getTemp(i);
    if (temp > -99.0 && temp < 999.0) {  // Sanity check
      char topic[64];
      snprintf(topic, sizeof(topic), "%s/temperature/%d", _config.baseTopic, i);
      _publishFloat(topic, temp);
    }
  }
}

void VBUSMqttClient::publishPumps() {
  uint8_t pumpCount = _decoder->getPumpNum();
  for (uint8_t i = 0; i < pumpCount; i++) {
    uint8_t power = _decoder->getPump(i);
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/pump/%d", _config.baseTopic, i);
    _publishInt(topic, power);
  }
}

void VBUSMqttClient::publishRelays() {
  uint8_t relayCount = _decoder->getRelayNum();
  for (uint8_t i = 0; i < relayCount; i++) {
    bool state = _decoder->getRelay(i);
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/relay/%d", _config.baseTopic, i);
    _publishBool(topic, state);
  }
}

void VBUSMqttClient::publishStatus() {
  char topic[64];
  
  // Protocol
  snprintf(topic, sizeof(topic), "%s/status/protocol", _config.baseTopic);
  _publishInt(topic, _decoder->getProtocol());
  
  // Connection status
  snprintf(topic, sizeof(topic), "%s/status/ready", _config.baseTopic);
  _publishBool(topic, _decoder->isReady());
  
  // Error mask
  snprintf(topic, sizeof(topic), "%s/status/error_mask", _config.baseTopic);
  _publishInt(topic, _decoder->getErrorMask());
  
  // System time
  snprintf(topic, sizeof(topic), "%s/status/system_time", _config.baseTopic);
  _publishInt(topic, _decoder->getSystemTime());
  
  // Heat quantity
  snprintf(topic, sizeof(topic), "%s/energy/heat_quantity", _config.baseTopic);
  _publishInt(topic, _decoder->getHeatQuantity());
}

void VBUSMqttClient::publishKMBusData() {
  char topic[64];
  
  // Burner status
  snprintf(topic, sizeof(topic), "%s/kmbus/burner", _config.baseTopic);
  _publishBool(topic, _decoder->getKMBusBurnerStatus());
  
  // Main pump status
  snprintf(topic, sizeof(topic), "%s/kmbus/main_pump", _config.baseTopic);
  _publishBool(topic, _decoder->getKMBusMainPumpStatus());
  
  // Loop pump status
  snprintf(topic, sizeof(topic), "%s/kmbus/loop_pump", _config.baseTopic);
  _publishBool(topic, _decoder->getKMBusLoopPumpStatus());
  
  // Operating mode
  snprintf(topic, sizeof(topic), "%s/kmbus/mode", _config.baseTopic);
  _publishInt(topic, _decoder->getKMBusMode());
  
  // Temperatures
  snprintf(topic, sizeof(topic), "%s/kmbus/boiler_temp", _config.baseTopic);
  _publishFloat(topic, _decoder->getKMBusBoilerTemp());
  
  snprintf(topic, sizeof(topic), "%s/kmbus/hotwater_temp", _config.baseTopic);
  _publishFloat(topic, _decoder->getKMBusHotWaterTemp());
  
  snprintf(topic, sizeof(topic), "%s/kmbus/outdoor_temp", _config.baseTopic);
  _publishFloat(topic, _decoder->getKMBusOutdoorTemp());
  
  snprintf(topic, sizeof(topic), "%s/kmbus/setpoint_temp", _config.baseTopic);
  _publishFloat(topic, _decoder->getKMBusSetpointTemp());
  
  snprintf(topic, sizeof(topic), "%s/kmbus/departure_temp", _config.baseTopic);
  _publishFloat(topic, _decoder->getKMBusDepartureTemp());
}

void VBUSMqttClient::publishHomeAssistantDiscovery() {
  if (!_config.useHomeAssistant) return;
  
  // Publish temperature sensors
  uint8_t tempCount = _decoder->getTempNum();
  for (uint8_t i = 0; i < tempCount; i++) {
    char objectId[32];
    snprintf(objectId, sizeof(objectId), "viessmann_temp_%d", i);
    char name[32];
    snprintf(name, sizeof(name), "Temperature %d", i);
    char valueTopic[64];
    snprintf(valueTopic, sizeof(valueTopic), "%s/temperature/%d", _config.baseTopic, i);
    _publishSensor(name, "temperature", "°C", valueTopic);
  }
  
  // Publish pump sensors
  uint8_t pumpCount = _decoder->getPumpNum();
  for (uint8_t i = 0; i < pumpCount; i++) {
    char objectId[32];
    snprintf(objectId, sizeof(objectId), "viessmann_pump_%d", i);
    char name[32];
    snprintf(name, sizeof(name), "Pump %d Power", i);
    char valueTopic[64];
    snprintf(valueTopic, sizeof(valueTopic), "%s/pump/%d", _config.baseTopic, i);
    _publishSensor(name, "power_factor", "%", valueTopic);
  }
  
  // Publish relay binary sensors
  uint8_t relayCount = _decoder->getRelayNum();
  for (uint8_t i = 0; i < relayCount; i++) {
    char objectId[32];
    snprintf(objectId, sizeof(objectId), "viessmann_relay_%d", i);
    char name[32];
    snprintf(name, sizeof(name), "Relay %d", i);
    char valueTopic[64];
    snprintf(valueTopic, sizeof(valueTopic), "%s/relay/%d", _config.baseTopic, i);
    _publishBinarySensor(name, "power", valueTopic);
  }
  
  // Publish heat quantity sensor
  char valueTopic[64];
  snprintf(valueTopic, sizeof(valueTopic), "%s/energy/heat_quantity", _config.baseTopic);
  _publishSensor("Heat Quantity", "energy", "Wh", valueTopic);
}

void VBUSMqttClient::publishHomeAssistantSensors() {
  publishHomeAssistantDiscovery();
}

bool VBUSMqttClient::publish(const char* topic, const char* payload, bool retained) {
  return _mqttClient->publish(topic, payload, retained);
}

void VBUSMqttClient::setCallback(void (*callback)(char*, uint8_t*, unsigned int)) {
  _mqttClient->setCallback(callback);
}

// Private helper methods

void VBUSMqttClient::_reconnect() {
  static uint32_t lastAttempt = 0;
  uint32_t now = millis();
  
  // Try to reconnect every 5 seconds
  if (now - lastAttempt > 5000) {
    connect();
    lastAttempt = now;
  }
}

void VBUSMqttClient::_publishSensor(const char* name, const char* deviceClass, 
                                   const char* unit, const char* valueTopic) {
  char discoveryTopic[128];
  snprintf(discoveryTopic, sizeof(discoveryTopic), 
           "%s/sensor/%s/config", _config.haDiscoveryPrefix, valueTopic);
  
  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"device_class\":\"%s\",\"unit_of_measurement\":\"%s\","
           "\"state_topic\":\"%s\",\"unique_id\":\"%s\","
           "\"device\":{\"identifiers\":[\"viessmann_%s\"],\"name\":\"Viessmann Heating\","
           "\"model\":\"Multi-Protocol\",\"manufacturer\":\"Viessmann\"}}",
           name, deviceClass, unit, valueTopic, valueTopic, _config.clientId);
  
  _mqttClient->publish(discoveryTopic, payload, true);
}

void VBUSMqttClient::_publishBinarySensor(const char* name, const char* deviceClass, 
                                         const char* valueTopic) {
  char discoveryTopic[128];
  snprintf(discoveryTopic, sizeof(discoveryTopic), 
           "%s/binary_sensor/%s/config", _config.haDiscoveryPrefix, valueTopic);
  
  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"device_class\":\"%s\","
           "\"state_topic\":\"%s\",\"payload_on\":\"true\",\"payload_off\":\"false\","
           "\"unique_id\":\"%s\","
           "\"device\":{\"identifiers\":[\"viessmann_%s\"],\"name\":\"Viessmann Heating\","
           "\"model\":\"Multi-Protocol\",\"manufacturer\":\"Viessmann\"}}",
           name, deviceClass, valueTopic, valueTopic, _config.clientId);
  
  _mqttClient->publish(discoveryTopic, payload, true);
}

String VBUSMqttClient::_buildTopic(const char* suffix) {
  String topic = _config.baseTopic;
  topic += "/";
  topic += suffix;
  return topic;
}

String VBUSMqttClient::_buildStateTopic(const char* suffix) {
  return _buildTopic(suffix);
}

String VBUSMqttClient::_buildDiscoveryTopic(const char* component, const char* objectId) {
  String topic = _config.haDiscoveryPrefix;
  topic += "/";
  topic += component;
  topic += "/";
  topic += objectId;
  topic += "/config";
  return topic;
}

void VBUSMqttClient::_publishFloat(const char* topic, float value) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%.2f", value);
  _mqttClient->publish(topic, buffer);
}

void VBUSMqttClient::_publishInt(const char* topic, int value) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%d", value);
  _mqttClient->publish(topic, buffer);
}

void VBUSMqttClient::_publishBool(const char* topic, bool value) {
  _mqttClient->publish(topic, value ? "true" : "false");
}

#endif // ESP32 || ESP8266
