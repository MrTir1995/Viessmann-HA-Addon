# Web Server Configuration Example

This example demonstrates an optional web-based configuration interface for the Viessmann Multi-Protocol Library.

## Features

- **Web-based Configuration**: Configure protocol, baud rate, serial settings via web browser
- **Real-time Monitoring**: View temperatures, pump states, and relay status
- **Persistent Storage**: Configuration saved to flash memory (auto-loads on boot)
- **Responsive UI**: Mobile-friendly interface
- **REST API**: JSON endpoint for integration with home automation systems

## Hardware Requirements

- ESP32 or ESP8266 board with WiFi
- Serial connection to Viessmann heating system

## Quick Start

1. **Install Libraries**:
   - Viessmann Multi-Protocol Library (this library)
   - WiFi library (included with ESP32/ESP8266 core)

2. **Update WiFi Credentials**:
   ```cpp
   const char* ssid = "YourWiFiSSID";
   const char* password = "YourWiFiPassword";
   ```

3. **Upload to Board**:
   - Select ESP32 or ESP8266 board in Arduino IDE
   - Upload the sketch

4. **Access Web Interface**:
   - Open Serial Monitor (115200 baud)
   - Note the IP address displayed
   - Open browser to `http://<ip-address>`

## Usage

### Dashboard
- View real-time heating system data
- Check communication status
- Monitor sensor values

### Configuration
- Select protocol (VBUS, KW-Bus, P300, KM-Bus)
- Set baud rate (2400, 4800, 9600, 19200)
- Configure serial settings (8N1, 8E2)
- Set GPIO pins for RX/TX

### Status
- View current configuration
- Check network information
- Monitor system resources

## Default Settings

```
Protocol: VBUS
Baud Rate: 9600
Serial Config: 8N1
RX Pin: GPIO16
TX Pin: GPIO17
```

## Web Interface Routes

- `/` - Dashboard (main page)
- `/config` - Configuration page
- `/status` - Status information
- `/data` - JSON API endpoint
- `/saveconfig` - Save configuration (POST)
- `/restart` - Restart device

## API Example

Get current data as JSON:

```bash
curl http://192.168.1.100/data
```

Response:
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

## Documentation

For complete documentation, see [WEBSERVER_SETUP.md](../../doc/WEBSERVER_SETUP.md).

For hardware setup information, see [HARDWARE_SETUP.md](../../doc/HARDWARE_SETUP.md).

## Security Notice

⚠️ **This web server has no authentication.** Only use on trusted local networks. See security considerations in the full documentation.

## Troubleshooting

**Cannot connect to WiFi:**
- Verify SSID and password
- Check that WiFi is 2.4GHz (ESP8266 requirement)

**No data from heating system:**
- Verify physical connections
- Check protocol and baud rate settings
- Ensure RX/TX pins are correct

**Web page not loading:**
- Clear browser cache
- Check device IP address
- Verify device is on same network

## License

Part of the Viessmann Multi-Protocol Library. See main LICENSE file.
