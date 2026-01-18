# Viessmann Decoder - Home Assistant Add-on

[![Add repository to Home Assistant](https://img.shields.io/badge/Add%20repository%20to-Home%20Assistant-blue?logo=home-assistant&logoColor=white)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https://github.com/MrTir1995/Viessmann-HA-Addon)

Monitor and configure your Viessmann heating system directly from Home Assistant!

This add-on provides a web interface to communicate with Viessmann heating controllers using multiple protocols (VBUS, KW-Bus, P300/Optolink, KM-Bus).

## Features

- **Multi-Protocol Support**: Works with VBUS, KW-Bus, P300, and KM-Bus protocols
- **Real-Time Monitoring**: View temperature sensors, pump states, and relay status
- **Web Interface**: Clean, responsive dashboard accessible from Home Assistant
- **Auto-Discovery**: Automatically detects devices on the bus
- **Easy Configuration**: Simple setup through Home Assistant UI
- **Lightweight**: Built on Alpine Linux for minimal resource usage

## Supported Devices

### VBUS Protocol Devices
- Viessmann Vitosolic 200
- RESOL DeltaSol BX Plus/BX/MX
- Generic RESOL solar controllers

### KW-Bus (VS1) Protocol Devices
- Viessmann Vitotronic 100/200/300 series
- Older Vitodens and Vitocrossal models

### P300 (VS2/Optolink) Protocol Devices
- Modern Viessmann Vitodens condensing boilers
- Viessmann Vitocrossal commercial systems

### KM-Bus Protocol Devices
- Vitotrol 200/300 remote controls
- Vitocom 100 internet gateway
- Expansion modules and switching modules

## Installation

1. Add this repository to your Home Assistant add-on store
2. Install the "Viessmann Decoder" add-on
3. Configure your serial port and protocol settings
4. Start the add-on
5. Access the web interface through Home Assistant

## Configuration

The add-on can be configured through the Home Assistant UI with the following options:

### serial_port (required)
The serial device connected to your Viessmann system.

**Common values:**
- `/dev/ttyUSB0` - USB-to-Serial adapter (most common)
- `/dev/ttyUSB1` - Second USB-to-Serial adapter
- `/dev/ttyACM0` - Some USB devices
- `/dev/ttyAMA0` - Raspberry Pi GPIO UART

**How to find your serial port:**
1. Go to Home Assistant Settings → System → Hardware
2. Look under "Serial" section for connected devices
3. Or use SSH/Terminal to run: `ls -la /dev/tty*`

### baud_rate (required)
The communication speed for your protocol.

**Common values:**
- `9600` - VBUS protocol (Vitosolic, DeltaSol)
- `4800` - KW-Bus and P300 protocols (Vitotronic, Vitodens)

### protocol (required)
The protocol used by your heating system.

**Options:**
- `vbus` - RESOL VBUS protocol (Vitosolic 200, DeltaSol controllers)
- `kw` - KW-Bus (VS1) protocol (Vitotronic 100/200/300, older systems)
- `p300` - P300/VS2 (Optolink) protocol (modern Vitodens boilers)
- `km` - KM-Bus protocol (remote controls, expansion modules)

### serial_config (required)
The serial port configuration.

**Options:**
- `8N1` - 8 data bits, no parity, 1 stop bit (for VBUS, KM-Bus)
- `8E2` - 8 data bits, even parity, 2 stop bits (for KW-Bus, P300)

## Configuration Examples

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

## Hardware Setup

### USB-to-Serial Adapter
The most common setup uses a USB-to-Serial adapter (FTDI, CH340, CP2102, etc.) connected to your Viessmann system's data bus.

**Wiring:**
- Connect adapter RX to bus TX
- Connect adapter TX to bus RX
- Connect GND to bus GND
- Consider using an optocoupler for electrical isolation

### Raspberry Pi GPIO
You can also use the Raspberry Pi's built-in UART:
- Enable UART in Raspberry Pi configuration
- Connect GPIO 14 (TX) and GPIO 15 (RX)
- Set `serial_port` to `/dev/ttyAMA0`

⚠️ **Important:** Always ensure proper electrical isolation when connecting to your heating system. Follow local electrical codes and regulations.

## Using the Add-on

### Web Interface
After starting the add-on, access the web interface:

1. Click "OPEN WEB UI" in the add-on info page
2. Or navigate to `http://homeassistant.local:8099`

The web interface provides:
- **Dashboard**: Real-time view of all sensor data
- **Status**: System and configuration information

### Data Updates
The dashboard automatically refreshes data every 2 seconds, showing:
- Temperature sensors (°C)
- Pump power levels (%)
- Relay states (ON/OFF)
- Communication status

## Troubleshooting

### Serial Port Not Found
**Symptom:** Add-on fails to start with "Serial port not found" error

**Solutions:**
1. Verify the serial device is connected: Settings → System → Hardware
2. Check the `serial_port` configuration matches your actual device
3. Ensure the device is properly recognized by the system
4. Try unplugging and replugging the USB adapter

### No Data Received
**Symptom:** Dashboard shows "Waiting for data..."

**Solutions:**
1. Verify physical connections to your heating system
2. Check that `protocol` matches your device
3. Ensure `baud_rate` and `serial_config` are correct
4. Verify the heating system is powered and communicating
5. Check for reversed RX/TX connections

### Communication Status: Error
**Symptom:** Status shows "Error" instead of "OK"

**Solutions:**
1. Double-check protocol settings
2. Verify baud rate is correct for your device
3. Check serial configuration (8N1 vs 8E2)
4. Ensure no other software is using the serial port
5. Try restarting the add-on

### Permission Denied
**Symptom:** Cannot access serial port due to permissions

**Solution:** This should be handled automatically by the add-on's privileged access. If issues persist, try restarting Home Assistant.

## Integration with Home Assistant

### Sensors
The add-on exposes data via HTTP API at `/data` endpoint. You can create Home Assistant sensors using the RESTful integration:

```yaml
sensor:
  - platform: rest
    resource: http://localhost:8099/data
    name: Viessmann Data
    json_attributes:
      - temperatures
      - pumps
      - relays
    value_template: '{{ value_json.status }}'
    scan_interval: 10

template:
  - sensor:
      - name: "Boiler Temperature"
        unique_id: viessmann_temp_1
        unit_of_measurement: "°C"
        state: '{{ state_attr("sensor.viessmann_data", "temperatures")[0] }}'
      - name: "Circulation Pump"
        unique_id: viessmann_pump_1
        unit_of_measurement: "%"
        state: '{{ state_attr("sensor.viessmann_data", "pumps")[0] }}'
```

### Automation Examples

**Example: Alert on low temperature**
```yaml
automation:
  - alias: "Low boiler temperature alert"
    trigger:
      platform: numeric_state
      entity_id: sensor.viessmann_temp_1
      below: 30
    action:
      service: notify.notify
      data:
        message: "Warning: Boiler temperature is below 30°C"
```

## Support

For issues, questions, or contributions:
- GitHub: https://github.com/MrTir1995/Viessmann-Home-Assistant-Addon-
- Issues: https://github.com/MrTir1995/Viessmann-Home-Assistant-Addon-/issues

## Troubleshooting

### Common Issues

#### "[FATAL tini (7)] exec /init failed: Permission denied" Error

This error has been fixed in the latest version of the addon by properly configuring S6-Overlay v3. If you encounter this error:

1. Update the addon to the latest version (v2.1.1 or later)
2. Restart the addon

**Technical Details**: Home Assistant's base images use S6-Overlay v3, which requires the `/init` process to run as PID 1. The error occurs when Docker's default init system (tini) conflicts with S6-Overlay. 

The fix is to add `init: false` to the addon's config.yaml. This tells the Supervisor not to inject Docker's init, allowing S6-Overlay to manage the service correctly. 

The addon uses the proper S6-Overlay service directory structure (`/etc/services.d/viessmann-decoder/run`). See the [Home Assistant S6-Overlay migration guide](https://developers.home-assistant.io/blog/2022/05/12/s6-overlay-base-images/) for more information.

#### Serial Port Not Found

If the addon reports that the serial port is not found:
1. Verify that your USB-to-serial adapter is properly connected
2. Check that the device appears in `/dev` (e.g., `/dev/ttyUSB0`)
3. Ensure the addon has permission to access the serial device
4. Try a different USB port
5. Check the addon logs for specific error messages

#### Connection Issues

If the addon starts but cannot communicate with the heating system:
1. Verify the protocol selection matches your device
2. Check the baud rate setting (9600 for VBUS, 4800 for KW/P300/KM)
3. Verify the serial configuration (8N1 for VBUS, 8E2 for KW/P300)
4. Test the serial adapter with another tool to confirm it works
5. Check your hardware wiring and connections

## License

See the main repository LICENSE file for details.

## Disclaimer

⚠️ **WARNING**: This add-on interfaces with your heating system. Use at your own risk. Always follow proper electrical safety procedures and local regulations when interfacing with heating systems. The authors assume no liability for any damages.
