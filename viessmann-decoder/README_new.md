# Viessmann Decoder - Home Assistant Add-on

[![Add repository to Home Assistant](https://img.shields.io/badge/Add%20repository%20to-Home%20Assistant-blue?logo=home-assistant&logoColor=white)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https://github.com/MrTir1995/Viessmann-Home-Assistant-Addon-)
[![GitHub Release](https://img.shields.io/github/v/release/MrTir1995/Viessmann-Home-Assistant-Addon-?logo=github)](https://github.com/MrTir1995/Viessmann-Home-Assistant-Addon-/releases)
[![Build Status](https://img.shields.io/github/actions/workflow/status/MrTir1995/developers.home-assistant/addon-build-test.yml?logo=github-actions)](https://github.com/MrTir1995/developers.home-assistant/actions)

Monitor and control your Viessmann heating system directly from Home Assistant with professional-grade protocol support!

This add-on provides a comprehensive web interface to communicate with Viessmann heating controllers using multiple industry-standard protocols (VBUS, KW-Bus, P300/Optolink, KM-Bus).

## ✨ Features

- 🔄 **Multi-Protocol Support**: Works with VBUS, KW-Bus, P300, and KM-Bus protocols
- 📊 **Real-Time Monitoring**: Live temperature sensors, pump states, and relay status
- 🖥️ **Web Interface**: Clean, responsive dashboard with ingress support
- 🔍 **Auto-Discovery**: Automatically detects and identifies devices on the bus
- ⚙️ **Easy Configuration**: Intuitive setup through Home Assistant UI
- 🪶 **Lightweight**: Optimized Alpine Linux container with minimal resource usage
- 📈 **Data Logging**: Historical data collection and export capabilities
- 🔧 **Advanced Diagnostics**: Protocol analyzer and debugging tools
- 🏠 **Home Assistant Integration**: Native sensor and entity creation
- 🔒 **Secure**: Runs with appropriate permissions and security context

## 🎯 Supported Devices

### VBUS Protocol Devices

- Viessmann Vitosolic 200 solar controllers
- RESOL DeltaSol BX Plus/BX/MX controllers
- Generic RESOL solar and heating controllers
- Third-party VBUS-compatible devices

### KW-Bus (VS1) Protocol Devices

- Viessmann Vitotronic 100/200/300 series
- Vitodens and Vitocrossal legacy models
- Older Viessmann control units

### P300 (VS2/Optolink) Protocol Devices

- Modern Viessmann Vitodens condensing boilers
- Vitocrossal 300 series
- Current generation Vitotronic controllers

### KM-Bus Protocol Devices

- Vitotrol 200/300 remote controls
- Communication modules

## 🚀 Installation

1. Add this repository to Home Assistant by clicking the button above or manually adding the repository URL
2. Install the "Viessmann Decoder" add-on from the Supervisor Add-on Store
3. Configure the add-on with your serial port and protocol settings
4. Start the add-on and access the web interface through the "Open Web UI" button

## ⚙️ Configuration Options

### serial_port (required)

Path to the serial device connected to your heating system.

Common values:

- `/dev/ttyUSB0` - USB-to-Serial adapter (most common)
- `/dev/ttyACM0` - Direct USB connection
- `/dev/ttyAMA0` - Raspberry Pi GPIO UART

To find available serial ports:

1. Go to Home Assistant Settings → System → Hardware
2. Look for devices under "Serial" section

### baud_rate (required)

Communication speed matching your device:

- `9600` - VBUS protocol (Vitosolic, DeltaSol)
- `4800` - KW-Bus protocol (Vitotronic 100/200/300)
- `4800` - P300 protocol (Modern Vitodens via Optolink)

### protocol (required)

Protocol used by your heating system:

- `vbus` - RESOL VBUS protocol (Vitosolic 200, DeltaSol controllers)
- `kw` - KW-Bus protocol (Vitotronic 100/200/300)
- `p300` - P300 protocol (Modern Vitodens via Optolink)
- `km` - KM-Bus protocol (Remote controls)

### serial_config (required)

Serial communication parameters:

- `8N1` - 8 data bits, no parity, 1 stop bit (for VBUS, KM-Bus)
- `8E2` - 8 data bits, even parity, 2 stop bits (for KW-Bus, P300)

## 📋 Example Configurations

### Example 1: Vitosolic 200 (Solar Controller)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 9600,
  "protocol": "vbus",
  "serial_config": "8N1"
}
```

### Example 2: Vitotronic 200 (KW-Bus)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 4800,
  "protocol": "kw",
  "serial_config": "8E2"
}
```

### Example 3: Modern Vitodens (Optolink)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 4800,
  "protocol": "p300",
  "serial_config": "8E2"
}
```

## 🔌 Hardware Setup

### USB-to-Serial Adapter

Most common setup for existing heating systems:

- Connect adapter RX to bus TX
- Connect adapter TX to bus RX
- Connect GND to common ground
- No external power needed for most adapters

### Raspberry Pi GPIO

For direct connection to Pi GPIO pins:

- GPIO 14 (TX) to bus RX
- GPIO 15 (RX) to bus TX
- Enable UART in Pi configuration
- Disable console on serial port

## 🖥️ Web Interface

After starting the add-on, access the web interface through:

- Home Assistant: Click "Open Web UI" button in add-on configuration
- Direct access: `http://your-homeassistant:8099`

The interface provides:

- Real-time sensor readings
- Device status indicators
- Configuration options
- Diagnostic tools
- Data export functions

## 🏠 Home Assistant Integration

The add-on automatically creates Home Assistant entities for:

- Temperature sensors (collector, storage, etc.)
- Pump and relay states
- System status indicators
- Energy production/consumption metrics

Entities appear in Home Assistant as `sensor.viessmann_*` and can be:

- Added to dashboards
- Used in automations
- Integrated with other systems
- Monitored for alerts

## 🔧 Troubleshooting

### Common Issues

**Add-on won't start:**

- Check serial port path in configuration
- Verify serial adapter is connected
- Review add-on logs for error messages

**No data received:**

- Confirm protocol selection matches your device
- Check baud rate and serial configuration
- Verify wiring connections
- Try different serial adapters if available

**Connection drops frequently:**

- Check for loose connections
- Try a different USB port
- Consider USB hub power issues
- Review system logs for hardware errors

### Debug Mode

Enable debug logging by adding to configuration:

```json
{
  "log_level": "debug"
}
```

### Getting Help

1. Check the [documentation repository](../doc/) for detailed guides
2. Review existing [GitHub issues](https://github.com/MrTir1995/Viessmann-Home-Assistant-Addon-/issues)
3. Create a new issue with:
   - Add-on configuration
   - Hardware setup details
   - Complete error logs
   - Home Assistant version

## 📚 Documentation

For detailed documentation, visit the [doc folder](../doc/) which includes:

- [Hardware Setup Guide](../doc/HARDWARE_SETUP.md)
- [Protocol Specifications](../doc/BUS_PARTICIPANT_DISCOVERY.md)
- [MQTT Integration](../doc/MQTT_SETUP.md)
- [Control Commands](../doc/CONTROL_COMMANDS.md)
- [Troubleshooting Guide](../doc/WEBSERVER_SETUP.md)

## 🤝 Contributing

Contributions are welcome! Please read the contributing guidelines before submitting pull requests.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🔄 Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history and updates.

## ⭐ Support

If this add-on helps you monitor your heating system, please consider starring the repository!