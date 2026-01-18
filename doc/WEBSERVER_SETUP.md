# Web Server Configuration Guide

This guide explains how to use the optional web-based configuration interface for the Viessmann Multi-Protocol Library. The web server allows you to configure and monitor your heating system remotely through a web browser.

## Table of Contents
- [Overview](#overview)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Features](#features)
- [Configuration](#configuration)
- [Web Interface](#web-interface)
- [API Endpoints](#api-endpoints)
- [Troubleshooting](#troubleshooting)
- [Security Considerations](#security-considerations)

## Overview

The web server example provides a complete web-based interface for:
- **Configuration Management**: Select protocol, baud rate, serial settings, and GPIO pins
- **Real-time Monitoring**: View temperatures, pump states, and relay status
- **Persistent Storage**: Configuration saved to flash memory (EEPROM/Preferences)
- **Remote Access**: Configure and monitor from any device with a web browser

## Requirements

### Hardware
- **ESP32** or **ESP8266** board with WiFi capability
- Serial connection to Viessmann heating system (see [HARDWARE_SETUP.md](./HARDWARE_SETUP.md))
- Stable WiFi network

### Software
- Arduino IDE with ESP32/ESP8266 board support
- Viessmann Multi-Protocol Library (this library)

## Quick Start

### 1. Install Required Libraries

Install the Viessmann Multi-Protocol Library through the Arduino Library Manager or manually.

### 2. Open the Example

Open the example sketch:
```
File → Examples → Viessmann Multi-Protocol Library → webserver_config
```

### 3. Configure WiFi Credentials

Edit the WiFi credentials in the sketch:

```cpp
const char* ssid = "YourWiFiSSID";          // Your WiFi network name
const char* password = "YourWiFiPassword";  // Your WiFi password
```

### 4. Upload to Board

1. Select your board (ESP32 or ESP8266) in Tools → Board
2. Select the correct COM port
3. Click Upload

### 5. Access Web Interface

1. Open the Serial Monitor (115200 baud)
2. Wait for WiFi connection
3. Note the IP address printed (e.g., `192.168.1.100`)
4. Open a web browser and navigate to `http://192.168.1.100`

## Features

### Dashboard
- **Live Data Display**: Real-time temperature, pump power, and relay states
- **Communication Status**: Monitor connection health
- **Auto-Refresh**: Data updates every 2 seconds

### Configuration Page
- **Protocol Selection**: Choose between VBUS, KW-Bus, P300, or KM-Bus
- **Baud Rate**: Set communication speed (2400, 4800, 9600, 19200)
- **Serial Configuration**: Select 8N1 or 8E2
- **GPIO Pins**: Configure RX and TX pins
- **Quick Presets**: Pre-configured settings for common devices

### Status Page
- **Current Configuration**: View active settings
- **Network Information**: WiFi SSID, IP, MAC, signal strength
- **System Information**: Board type, free memory, communication status

## Configuration

### Protocol Settings

#### VBUS (RESOL Protocol)
```
Protocol: VBUS (RESOL)
Baud Rate: 9600
Serial Config: 8N1
Use for: Vitosolic 200, DeltaSol BX/MX
```

#### KW-Bus (VS1)
```
Protocol: KW-Bus (VS1)
Baud Rate: 4800
Serial Config: 8E2
Use for: Vitotronic 100/200/300 (older models)
```

#### P300 (VS2/Optolink)
```
Protocol: P300 (VS2/Optolink)
Baud Rate: 4800
Serial Config: 8E2
Use for: Vitodens, modern condensing boilers
```

#### KM-Bus
```
Protocol: KM-Bus
Baud Rate: 9600 (varies by device)
Serial Config: 8N1
Use for: Vitotrol remotes, expansion modules
```

### GPIO Pin Configuration

#### ESP32 Default Pins
```
RX Pin: GPIO16
TX Pin: GPIO17
```

You can use any available GPIO pins on the ESP32. Common alternatives:
- Serial2: RX=16, TX=17 (default)
- Serial1: RX=9, TX=10
- Custom: Any available GPIO

#### ESP8266 Default Pins
```
Standard UART: RX=3, TX=1
Alternate UART: RX=13, TX=15
```

⚠️ **Note**: ESP8266 has limited hardware serial options. Use `Serial.swap()` for alternate pins.

### Saving Configuration

1. Navigate to the Configuration page
2. Select your protocol and settings
3. Click "Save Configuration"
4. Click "Restart Device" to apply changes

Configuration is automatically saved to flash memory and persists across reboots.

## Web Interface

### Dashboard Page (`/`)

The main dashboard displays:
- Communication status (OK/Error)
- Active protocol
- Live sensor data in a table
- Auto-refreshing data (2-second interval)

### Configuration Page (`/config`)

Configure all communication parameters:
- Drop-down menus for protocol, baud rate, serial config
- Number inputs for GPIO pins
- Quick reference presets for common devices
- Save button to persist changes

### Status Page (`/status`)

View detailed system information:
- Current configuration table
- Network details (SSID, IP, MAC, signal)
- System info (board type, free memory)
- Communication status

## API Endpoints

The web server provides REST API endpoints for programmatic access:

### GET `/`
Returns the main dashboard HTML page.

### GET `/config`
Returns the configuration page HTML.

### POST `/saveconfig`
Saves configuration parameters.

**Parameters**:
- `protocol` (0-3): Protocol type
- `baudRate` (number): Baud rate in bps
- `serialConfig` (0-1): 0=8N1, 1=8E2
- `rxPin` (number): RX GPIO pin
- `txPin` (number): TX GPIO pin

### GET `/status`
Returns the status page HTML.

### GET `/data`
Returns JSON with current sensor data.

**Response**:
```json
{
  "ready": true,
  "status": "OK",
  "protocol": 0,
  "temperatures": [25.5, 30.2, 45.8],
  "pumps": [75, 50],
  "relays": [true, false, true]
}
```

### GET `/restart`
Restarts the ESP32/ESP8266 device.

## Troubleshooting

### Cannot Connect to WiFi

**Problem**: Device doesn't connect to WiFi network.

**Solutions**:
1. Verify SSID and password are correct
2. Check WiFi signal strength
3. Ensure WiFi is 2.4GHz (ESP8266 doesn't support 5GHz)
4. Check serial monitor for error messages

### Cannot Access Web Interface

**Problem**: Browser cannot connect to device IP address.

**Solutions**:
1. Verify device is connected to WiFi (check serial monitor)
2. Ensure computer/phone is on same WiFi network
3. Try pinging the device IP address
4. Check firewall settings
5. Use device hostname if mDNS is supported

### Configuration Not Saving

**Problem**: Settings reset after reboot.

**Solutions**:
1. Wait for "Configuration saved" message before restarting
2. On ESP8266, ensure EEPROM is properly initialized
3. On ESP32, check Preferences namespace is accessible
4. Verify flash memory is not corrupted

### No Data from Heating System

**Problem**: Web interface shows "Waiting for data..."

**Solutions**:
1. Verify physical connections (see [HARDWARE_SETUP.md](./HARDWARE_SETUP.md))
2. Check protocol selection matches your device
3. Verify baud rate and serial configuration
4. Check RX/TX pins are correct and not swapped
5. Monitor serial debug output for errors
6. Ensure heating system is powered and communicating

### Web Page Not Loading Properly

**Problem**: HTML/CSS doesn't display correctly.

**Solutions**:
1. Clear browser cache
2. Try a different browser
3. Check serial monitor for memory issues
4. ESP8266 has limited memory; consider reducing HTML content

## Security Considerations

⚠️ **Important Security Information**

### Network Security

1. **Local Network Only**: The web server has no authentication. Only use on trusted local networks.

2. **No HTTPS**: Communication is unencrypted. Do not transmit sensitive information.

3. **WiFi Security**: Use WPA2/WPA3 encryption on your WiFi network.

4. **Access Control**: Consider using a separate SSID/VLAN for IoT devices.

### Recommended Security Measures

For production deployments, consider adding:
- **Authentication**: HTTP basic auth or form-based login
- **HTTPS**: SSL/TLS encryption for web traffic
- **API Keys**: Token-based access for API endpoints
- **Firewall Rules**: Restrict access to specific IP addresses
- **Password Protection**: For configuration changes

### Example: Adding Basic Authentication

```cpp
// In setup()
server.on("/config", []() {
  if (!server.authenticate("admin", "password")) {
    return server.requestAuthentication();
  }
  handleConfig();
});
```

### Isolation from Heating System

The library provides **read-only** monitoring by default. It does not send commands or control the heating system. However:

1. Keep monitoring separate from heating system control
2. Use electrical isolation (see [HARDWARE_SETUP.md](./HARDWARE_SETUP.md))
3. Do not bypass safety equipment
4. Follow local regulations for heating system modifications

## Advanced Configuration

### Static IP Address

To use a static IP instead of DHCP:

```cpp
IPAddress local_IP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);

WiFi.config(local_IP, gateway, subnet, primaryDNS);
WiFi.begin(ssid, password);
```

### Custom Port

To use a port other than 80:

```cpp
WEBSERVER_CLASS server(8080);  // Use port 8080
```

Access at: `http://192.168.1.100:8080`

### mDNS Hostname

Enable mDNS to access by hostname:

```cpp
#include <ESPmDNS.h>  // ESP32
// or
#include <ESP8266mDNS.h>  // ESP8266

void setup() {
  // After WiFi connects:
  if (!MDNS.begin("viessmann")) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
}

void loop() {
  MDNS.update();  // ESP8266 only
}
```

Access at: `http://viessmann.local`

## Integration with Home Automation

The JSON API endpoint (`/data`) can be integrated with home automation systems:

### Home Assistant

```yaml
sensor:
  - platform: rest
    resource: http://192.168.1.100/data
    name: Viessmann Heating
    json_attributes:
      - temperatures
      - pumps
      - relays
    value_template: '{{ value_json.status }}'
    scan_interval: 10
```

### Node-RED

Use an HTTP request node to fetch `/data` endpoint and parse JSON.

### MQTT Bridge

Add MQTT publishing to the main loop:

```cpp
#include <PubSubClient.h>

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void publishData() {
  if (vbus.isReady()) {
    String topic = "viessmann/temperature/1";
    String payload = String(vbus.getTemp(0));
    mqttClient.publish(topic.c_str(), payload.c_str());
  }
}
```

## Performance Considerations

### Memory Usage

- **ESP32**: ~50KB RAM used, plenty of headroom
- **ESP8266**: ~25KB RAM used, monitor free heap

Check free memory:
```cpp
Serial.println(ESP.getFreeHeap());
```

### Response Time

- HTML pages: ~500ms load time
- JSON data: ~50ms response time
- Auto-refresh: 2-second interval (configurable)

### Concurrent Connections

The web server handles one connection at a time. For multiple users:
- ESP32: Better performance, can handle 2-3 concurrent users
- ESP8266: Limited to 1 user, add connection queuing if needed

## Additional Resources

- **Library Documentation**: [README.md](../README.md)
- **Hardware Setup**: [HARDWARE_SETUP.md](./HARDWARE_SETUP.md)
- **Protocol Details**: [PROTOCOLS.md](../PROTOCOLS.md)
- **Example Code**: [examples/webserver_config/](../examples/webserver_config/)

## Contributing

Have improvements or bug fixes? Contributions are welcome!

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This example is part of the Viessmann Multi-Protocol Library. See the main LICENSE file for details.

## Disclaimer

**⚠️ WARNING**: This is an experimental feature for advanced users. Use at your own risk. Always follow proper electrical safety procedures and local regulations when interfacing with heating systems.

The authors and contributors assume no liability for any damages or issues arising from the use of this software.
