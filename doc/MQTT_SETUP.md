# MQTT Integration Guide

This guide explains how to integrate the Viessmann Multi-Protocol Library with MQTT for home automation and monitoring.

## Overview

MQTT (Message Queuing Telemetry Transport) is a lightweight messaging protocol perfect for IoT applications. This library provides:

- **Automatic publishing** of sensor data
- **Home Assistant auto-discovery**
- **Bidirectional control** (subscribe to commands)
- **Configurable topics** and intervals
- **TLS support** (with compatible MQTT clients)

## Quick Start

### 1. Install Dependencies

Install the PubSubClient library:

**Arduino IDE:**
1. Go to Sketch → Include Library → Manage Libraries
2. Search for "PubSubClient"
3. Install "PubSubClient by Nick O'Leary"

**PlatformIO:**
```ini
[env:esp32]
lib_deps = 
    knolleary/PubSubClient@^2.8
```

### 2. Basic Example

```cpp
#include <WiFi.h>
#include "vbusdecoder.h"
#include "VBUSMqttClient.h"

const char* WIFI_SSID = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* MQTT_BROKER = "192.168.1.100";

WiFiClient wifiClient;
VBUSDecoder vbus(&Serial2);
VBUSMqttClient mqttClient(&vbus, &wifiClient);

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  
  vbus.begin(PROTOCOL_VBUS);
  
  MqttConfig config;
  config.broker = MQTT_BROKER;
  config.port = 1883;
  config.clientId = "viessmann";
  config.baseTopic = "viessmann";
  config.publishInterval = 30;
  config.useHomeAssistant = true;
  config.haDiscoveryPrefix = "homeassistant";
  
  mqttClient.begin(config);
}

void loop() {
  vbus.loop();
  mqttClient.loop();
}
```

## Configuration

### MQTT Broker Settings

```cpp
MqttConfig config;

// Required settings
config.broker = "192.168.1.100";     // MQTT broker IP or hostname
config.port = 1883;                  // MQTT port (default: 1883)
config.clientId = "viessmann";       // Unique client ID

// Optional authentication
config.username = "mqtt_user";       // Leave as nullptr if not required
config.password = "mqtt_pass";       // Leave as nullptr if not required

// Topic configuration
config.baseTopic = "viessmann";      // Base topic for all messages

// Publishing
config.publishInterval = 30;         // Seconds between updates

// Home Assistant
config.useHomeAssistant = true;      // Enable HA auto-discovery
config.haDiscoveryPrefix = "homeassistant";  // HA discovery prefix
```

## MQTT Topics

### Published Topics

All topics are prefixed with the base topic (default: `viessmann`).

#### Temperature Sensors
```
viessmann/temperature/0   → 45.5
viessmann/temperature/1   → 32.2
viessmann/temperature/2   → 15.8
```

#### Pump Power Levels
```
viessmann/pump/0   → 75
viessmann/pump/1   → 50
```

#### Relay States
```
viessmann/relay/0   → true
viessmann/relay/1   → false
viessmann/relay/2   → true
```

#### System Status
```
viessmann/status/protocol     → 0
viessmann/status/ready        → true
viessmann/status/error_mask   → 0
viessmann/status/system_time  → 1234
```

#### Energy Data
```
viessmann/energy/heat_quantity   → 15680
```

#### KM-Bus Specific Data
```
viessmann/kmbus/burner         → false
viessmann/kmbus/main_pump      → true
viessmann/kmbus/loop_pump      → false
viessmann/kmbus/mode           → 132
viessmann/kmbus/boiler_temp    → 72.5
viessmann/kmbus/hotwater_temp  → 55.0
viessmann/kmbus/outdoor_temp   → 12.3
viessmann/kmbus/setpoint_temp  → 21.0
viessmann/kmbus/departure_temp → 45.8
```

### Subscribed Topics (Control)

Subscribe to control topics to send commands:

```
viessmann/control/mode      → "day" / "night" / "eco"
viessmann/control/setpoint  → "21.5"
viessmann/control/eco       → "on" / "off"
viessmann/control/party     → "on" / "off"
```

**Example implementation:**

```cpp
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String payloadStr = "";
  for (unsigned int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }
  
  if (topicStr == "viessmann/control/mode") {
    if (payloadStr == "day") {
      vbus.setKMBusMode(KMBUS_MODE_DAY);
    } else if (payloadStr == "night") {
      vbus.setKMBusMode(KMBUS_MODE_NIGHT);
    }
  } else if (topicStr == "viessmann/control/setpoint") {
    float setpoint = payloadStr.toFloat();
    vbus.setKMBusSetpoint(0, setpoint);
  }
}

void setup() {
  // ... other setup code ...
  mqttClient.setCallback(mqttCallback);
  // Subscribe after connection
  mqttClient.subscribe("viessmann/control/#");
}
```

## Home Assistant Integration

### Automatic Discovery

When `useHomeAssistant` is enabled, the library automatically publishes discovery messages for all sensors.

**Discovery topic format:**
```
homeassistant/sensor/viessmann_temp_0/config
homeassistant/sensor/viessmann_pump_0/config
homeassistant/binary_sensor/viessmann_relay_0/config
```

**Discovery payload example:**
```json
{
  "name": "Temperature 0",
  "device_class": "temperature",
  "unit_of_measurement": "°C",
  "state_topic": "viessmann/temperature/0",
  "unique_id": "viessmann_temp_0",
  "device": {
    "identifiers": ["viessmann_heating"],
    "name": "Viessmann Heating",
    "model": "Multi-Protocol",
    "manufacturer": "Viessmann"
  }
}
```

### Manual Sensor Configuration

If you prefer manual configuration, add to `configuration.yaml`:

```yaml
sensor:
  # Temperature sensors
  - platform: mqtt
    name: "Boiler Temperature"
    state_topic: "viessmann/kmbus/boiler_temp"
    unit_of_measurement: "°C"
    device_class: temperature
    
  - platform: mqtt
    name: "Outdoor Temperature"
    state_topic: "viessmann/kmbus/outdoor_temp"
    unit_of_measurement: "°C"
    device_class: temperature
    
  # Pump power
  - platform: mqtt
    name: "Circulation Pump"
    state_topic: "viessmann/pump/0"
    unit_of_measurement: "%"
    icon: mdi:pump

binary_sensor:
  # Burner status
  - platform: mqtt
    name: "Burner"
    state_topic: "viessmann/kmbus/burner"
    payload_on: "true"
    payload_off: "false"
    device_class: heat
```

### Control Switches

Add control switches to Home Assistant:

```yaml
switch:
  - platform: mqtt
    name: "Eco Mode"
    command_topic: "viessmann/control/eco"
    state_topic: "viessmann/kmbus/mode"
    payload_on: "on"
    payload_off: "off"
    optimistic: false
    
  - platform: mqtt
    name: "Party Mode"
    command_topic: "viessmann/control/party"
    payload_on: "on"
    payload_off: "off"
```

### Input Number for Setpoint

```yaml
input_number:
  heating_setpoint:
    name: "Heating Setpoint"
    min: 15
    max: 25
    step: 0.5
    unit_of_measurement: "°C"
    icon: mdi:thermometer

automation:
  - alias: "Update heating setpoint"
    trigger:
      platform: state
      entity_id: input_number.heating_setpoint
    action:
      service: mqtt.publish
      data:
        topic: "viessmann/control/setpoint"
        payload: "{{ states('input_number.heating_setpoint') }}"
```

## Energy Dashboard Integration

### Configure Energy Sensor

```yaml
sensor:
  - platform: mqtt
    name: "Heating Energy"
    state_topic: "viessmann/energy/heat_quantity"
    unit_of_measurement: "Wh"
    device_class: energy
    state_class: total_increasing
```

### Add to Energy Dashboard

1. Go to Settings → Dashboards → Energy
2. Click "Add Consumption"
3. Select "Heating Energy" sensor
4. Configure as needed

### Calculate Daily Energy

```yaml
sensor:
  - platform: template
    sensors:
      heating_energy_daily:
        friendly_name: "Daily Heating Energy"
        unit_of_measurement: "kWh"
        value_template: >
          {{ (states('sensor.heating_energy') | float / 1000) | round(2) }}
```

## Advanced Features

### Retained Messages

Publish important state with retain flag:

```cpp
mqttClient.publish("viessmann/status/online", "true", true);
```

### Last Will and Testament

Set up LWT for connection monitoring:

```cpp
void setup() {
  // ... connection code ...
  
  // Set last will before connecting
  mqttClient.setLastWill("viessmann/status/online", "false", true);
  mqttClient.connect();
}
```

### TLS/SSL Connection

For secure MQTT over TLS:

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
VBUSMqttClient mqttClient(&vbus, &secureClient);

void setup() {
  // Set CA certificate
  secureClient.setCACert(ca_cert);
  
  // Configure MQTT for TLS
  MqttConfig config;
  config.broker = "mqtt.example.com";
  config.port = 8883;  // TLS port
  // ... rest of config ...
  
  mqttClient.begin(config);
}
```

### Custom Topics

Publish custom data:

```cpp
void loop() {
  vbus.loop();
  mqttClient.loop();
  
  // Custom publishing
  if (someCondition) {
    String topic = "viessmann/custom/status";
    String payload = "custom_value";
    mqttClient.publish(topic.c_str(), payload.c_str());
  }
}
```

## Troubleshooting

### Connection Issues

```cpp
void loop() {
  if (!mqttClient.isConnected()) {
    Serial.println("MQTT disconnected, reconnecting...");
    if (!mqttClient.connect()) {
      Serial.println("Connection failed");
      delay(5000);
    }
  }
  
  mqttClient.loop();
  vbus.loop();
}
```

### Debug Logging

Enable PubSubClient debug output:

```cpp
// In setup()
Serial.begin(115200);
mqttClient.setDebug(true);  // If available in your version
```

### Message Size Limits

Default max message size is 128 bytes. Increase if needed:

```cpp
// Before creating PubSubClient
#define MQTT_MAX_PACKET_SIZE 512
```

### QoS Levels

Choose appropriate QoS:
- QoS 0: At most once (fast, no guarantee)
- QoS 1: At least once (reliable, may duplicate)
- QoS 2: Exactly once (slow, guaranteed)

```cpp
// Publish with QoS 1
mqttClient.publish(topic, payload, false);  // QoS 0
mqttClient.publish(topic, payload, true);   // QoS 1 with retain
```

## Performance Optimization

### Reduce Publishing Frequency

```cpp
// Only publish changes
float lastTemp = -999.0;

void loop() {
  vbus.loop();
  
  float currentTemp = vbus.getTemp(0);
  if (abs(currentTemp - lastTemp) > 0.5) {  // 0.5°C threshold
    mqttClient.publishTemperatures();
    lastTemp = currentTemp;
  }
}
```

### Batch Updates

Publish multiple values in one message:

```cpp
String json = "{";
json += "\"temp\":" + String(vbus.getTemp(0), 2) + ",";
json += "\"pump\":" + String(vbus.getPump(0)) + ",";
json += "\"relay\":" + String(vbus.getRelay(0) ? "true" : "false");
json += "}";

mqttClient.publish("viessmann/data/all", json.c_str());
```

## Security Best Practices

1. **Use authentication**: Always set username and password
2. **Enable TLS**: Use encrypted connections
3. **Restrict topics**: Use ACLs on broker
4. **Unique client IDs**: Prevent conflicts
5. **Validate inputs**: Check all received commands
6. **Rate limiting**: Prevent command flooding

## Examples

Complete examples available:
- `examples/mqtt_integration/` - Full MQTT setup
- `examples/webserver_enhanced/` - MQTT + Web interface
- `examples/advanced_automation/` - MQTT + Scheduler

## Further Reading

- [PubSubClient Documentation](https://pubsubclient.knolleary.net/)
- [Home Assistant MQTT Integration](https://www.home-assistant.io/integrations/mqtt/)
- [MQTT Specification](https://mqtt.org/mqtt-specification/)
- [Control Commands Guide](CONTROL_COMMANDS.md)

## License

Part of the Viessmann Multi-Protocol Library. See main LICENSE file.
