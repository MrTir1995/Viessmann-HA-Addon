# Viessmann Decoder Add-on

Monitor your Viessmann heating system from Home Assistant

![Supports aarch64 Architecture][aarch64-shield]
![Supports amd64 Architecture][amd64-shield]
![Supports armhf Architecture][armhf-shield]
![Supports armv7 Architecture][armv7-shield]
![Supports i386 Architecture][i386-shield]

## About

This add-on enables monitoring and data collection from Viessmann heating systems using various protocols (VBUS, KW-Bus, P300, KM-Bus). It provides a clean web interface and REST API for integration with Home Assistant.

### Features

- 🌡️ Real-time temperature monitoring
- 🔄 Pump status and power levels
- 💡 Relay states
- 📊 Live dashboard with auto-refresh
- 🔌 Multiple protocol support
- 🐳 Docker-based for easy deployment
- 📱 Responsive web interface

## Installation

1. Navigate to **Supervisor > Add-on Store** in Home Assistant
2. Click the three dots in the top right corner
3. Select **Repositories**
4. Add this repository URL: `https://github.com/MrTir1995/Viessmann-HA-Addon`
5. Click **Add**, then close
6. Find "Viessmann Decoder" in the add-on list and click **Install**

## Configuration

### Options

| Option | Description | Default |
| ------ | ----------- | ------- |
| `serial_port` | Serial port for connection | `/dev/ttyUSB0` |
| `baud_rate` | Communication speed | `9600` |
| `protocol` | Protocol type | `vbus` |
| `serial_config` | Serial configuration | `8N1` |
| `log_level` | Logging verbosity | `info` |

### Protocol Options

- **vbus**: RESOL VBUS protocol (most common)
- **kw**: KW-Bus (VS1) protocol
- **p300**: P300 (VS2/Optolink) protocol
- **km**: KM-Bus protocol

### Baud Rate Options

- 2400, 4800, 9600 (default), 19200, 38400, 115200

### Serial Configuration

- **8N1**: 8 data bits, no parity, 1 stop bit (default)
- **8E2**: 8 data bits, even parity, 2 stop bits

## Hardware Setup

### Required Hardware

1. USB-to-TTL adapter (3.3V or 5V depending on your Viessmann controller)
2. Connection cable to the Viessmann bus interface

### Wiring

Connect your USB adapter to the appropriate bus port on your Viessmann controller:

- GND → Ground
- RX → Data line (TX on adapter)
- TX → Data line (RX on adapter)

**Note:** Check your specific Viessmann controller documentation for exact pinouts.

## Web Interface

Access the web interface at: `http://homeassistant.local:8099`

Or use the **Ingress** panel directly in Home Assistant.

### API Endpoints

| Endpoint | Description |
| -------- | ----------- |
| `/` | Main dashboard |
| `/data` | JSON data endpoint |
| `/health` | Health check endpoint |
| `/settings` | Configuration page |
| `/devices` | Device management |
| `/status` | System status |

### Example Data Response

```json
{
  "serialConnected": true,
  "compatible": true,
  "serialPort": "/dev/ttyUSB0",
  "ready": true,
  "status": "OK",
  "protocol": 0,
  "temperatures": [45.2, 38.1, 22.5],
  "pumps": [100, 0],
  "relays": [true, false, false]
}
```

## Troubleshooting

### Serial Port Not Found

- Verify the USB adapter is connected
- Check the serial port name: `ls /dev/ttyUSB*`
- Ensure the correct port is configured

### No Data Received

- Verify the protocol setting matches your Viessmann system
- Check baud rate settings
- Verify wiring connections

### Permission Denied

- The add-on requires `uart: true` and `privileged: SYS_RAWIO`
- These are configured automatically in the add-on

## Support

For issues and feature requests, visit:
<https://github.com/MrTir1995/Viessmann-HA-Addon/issues>

[aarch64-shield]: https://img.shields.io/badge/aarch64-yes-green.svg
[amd64-shield]: https://img.shields.io/badge/amd64-yes-green.svg
[armhf-shield]: https://img.shields.io/badge/armhf-yes-green.svg
[armv7-shield]: https://img.shields.io/badge/armv7-yes-green.svg
[i386-shield]: https://img.shields.io/badge/i386-yes-green.svg
