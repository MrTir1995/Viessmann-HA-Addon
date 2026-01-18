# Control Commands Guide

This guide explains how to use control commands to adjust Viessmann heating system settings programmatically.

## Overview

The Viessmann Multi-Protocol Library now supports **bidirectional communication** with KM-Bus protocol devices, allowing you to:

- Adjust temperature setpoints
- Change operating modes (Day, Night, Eco, Party)
- Enable/disable eco and party modes
- Monitor the results of your commands

## ⚠️ Safety Warning

**IMPORTANT:** Control commands directly affect your heating system operation!

- **Test in a safe environment** before deploying to production
- **Understand each command** and its effects on your system
- **Monitor system response** after sending commands
- **Have physical access** to override commands if needed
- **Use electrical isolation** for hardware connections
- **Comply with local regulations** and safety codes
- **Backup your current settings** before making changes

## Supported Protocols

Currently, control commands are **only available for KM-Bus protocol** devices, including:

- Vitotronic controllers with KM-Bus terminals
- Vitotrol 200/300 remote controls
- Vitocom 100 gateways
- Various expansion modules

## Available Commands

### 1. Set Operating Mode

Change the heating system operating mode.

```cpp
bool setKMBusMode(uint8_t mode);
```

**Parameters:**
- `mode`: Operating mode constant
  - `KMBUS_MODE_OFF` - Turn off heating
  - `KMBUS_MODE_NIGHT` - Night/reduced mode
  - `KMBUS_MODE_DAY` - Day/comfort mode
  - `KMBUS_MODE_ECO` - Economy mode
  - `KMBUS_MODE_PARTY` - Party mode (temporary comfort)

**Returns:** `true` if command was sent successfully, `false` otherwise

**Example:**
```cpp
// Set to day mode
if (vbus.setKMBusMode(KMBUS_MODE_DAY)) {
  Serial.println("Day mode activated");
} else {
  Serial.println("Command failed");
}
```

### 2. Set Temperature Setpoint

Adjust the target temperature for a heating circuit.

```cpp
bool setKMBusSetpoint(uint8_t circuit, float temperature);
```

**Parameters:**
- `circuit`: Heating circuit number (0-2)
- `temperature`: Target temperature in °C (5.0 - 35.0)

**Returns:** `true` if command was sent successfully, `false` otherwise

**Example:**
```cpp
// Set circuit 0 to 21.5°C
if (vbus.setKMBusSetpoint(0, 21.5)) {
  Serial.println("Setpoint updated");
}
```

**Notes:**
- Temperature must be between 5.0°C and 35.0°C (safety limits)
- Temperature is encoded with 0.5°C resolution
- Different circuits may have different supported ranges

### 3. Enable/Disable Eco Mode

Toggle economy mode for energy savings.

```cpp
bool setKMBusEcoMode(bool enable);
```

**Parameters:**
- `enable`: `true` to enable eco mode, `false` to disable

**Returns:** `true` if command was sent successfully, `false` otherwise

**Example:**
```cpp
// Enable eco mode
vbus.setKMBusEcoMode(true);

// Disable eco mode
vbus.setKMBusEcoMode(false);
```

### 4. Enable/Disable Party Mode

Toggle party mode for temporary comfort override.

```cpp
bool setKMBusPartyMode(bool enable);
```

**Parameters:**
- `enable`: `true` to enable party mode, `false` to disable

**Returns:** `true` if command was sent successfully, `false` otherwise

**Example:**
```cpp
// Enable party mode for a gathering
vbus.setKMBusPartyMode(true);

// Disable after event
vbus.setKMBusPartyMode(false);
```

## Command Rate Limiting

**Important:** Do not send commands too frequently!

- Minimum interval: **5 seconds between commands**
- Allow time for system to respond and apply changes
- Monitor status after each command
- Wait for confirmation before sending another command

```cpp
uint32_t lastCommand = 0;
const uint32_t COMMAND_INTERVAL = 5000; // 5 seconds

void sendCommand() {
  uint32_t now = millis();
  if (now - lastCommand >= COMMAND_INTERVAL) {
    if (vbus.setKMBusMode(KMBUS_MODE_DAY)) {
      lastCommand = now;
    }
  }
}
```

## Verifying Commands

After sending a command, verify that it was applied:

```cpp
// Send command
if (vbus.setKMBusSetpoint(0, 22.0)) {
  // Wait for system to process
  delay(1000);
  
  // Verify
  float currentSetpoint = vbus.getKMBusSetpointTemp();
  if (abs(currentSetpoint - 22.0) < 0.6) {
    Serial.println("Command verified");
  } else {
    Serial.println("Command may have failed");
  }
}
```

## Error Handling

Always check return values and handle errors:

```cpp
bool success = vbus.setKMBusMode(KMBUS_MODE_DAY);
if (!success) {
  Serial.println("Command failed - possible reasons:");
  Serial.println("- Not using KM-Bus protocol");
  Serial.println("- Communication error");
  Serial.println("- Invalid parameters");
  Serial.println("- Device not responding");
}
```

## Integration Examples

### With Scheduler

Combine with the scheduler for automated control:

```cpp
VBUSScheduler scheduler(&vbus, 16);

// Morning: Set day mode at 6:00 AM
scheduler.addTimeRule(6, 0, 0x3E, ACTION_SET_MODE, KMBUS_MODE_DAY);

// Evening: Set night mode at 10:00 PM
scheduler.addTimeRule(22, 0, 0x3E, ACTION_SET_MODE, KMBUS_MODE_NIGHT);

// Temperature-based: Enable eco if outdoor > 15°C
scheduler.addTemperatureRule(2, 15.0, true, ACTION_ENABLE_ECO);
```

### With MQTT

Control via MQTT messages:

```cpp
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, "viessmann/control/mode") == 0) {
    char mode[10];
    memcpy(mode, payload, min(length, 9));
    mode[length] = '\0';
    
    if (strcmp(mode, "day") == 0) {
      vbus.setKMBusMode(KMBUS_MODE_DAY);
    } else if (strcmp(mode, "night") == 0) {
      vbus.setKMBusMode(KMBUS_MODE_NIGHT);
    }
  }
}
```

### With Web Interface

Control via HTTP POST:

```cpp
void handleControl() {
  if (server.hasArg("mode")) {
    uint8_t mode = server.arg("mode").toInt();
    bool success = vbus.setKMBusMode(mode);
    
    String response = "{\"success\":";
    response += success ? "true" : "false";
    response += "}";
    
    server.send(200, "application/json", response);
  }
}
```

## Troubleshooting

### Commands Not Working

1. **Check protocol**: Control commands only work with KM-Bus
   ```cpp
   if (vbus.getProtocol() != PROTOCOL_KM) {
     Serial.println("Switch to KM-Bus protocol");
   }
   ```

2. **Check connection**: Ensure decoder is receiving data
   ```cpp
   if (!vbus.isReady()) {
     Serial.println("No data from heating system");
   }
   ```

3. **Check hardware**: Verify TX pin is connected and working

4. **Check timing**: Ensure sufficient delay between commands

### System Not Responding

- Verify baud rate and serial configuration
- Check TX/RX pin connections
- Ensure proper voltage levels (3.3V or 5V)
- Test with known-good hardware
- Monitor serial traffic with logic analyzer

### Unexpected Behavior

- Read current state before sending commands
- Log all command attempts and responses
- Compare with manual control via physical interface
- Check for interfering devices on bus
- Verify command parameters are within valid ranges

## Protocol Details

### KM-Bus Frame Format

Commands use the KM-Bus long frame format:

```
0x68 L L 0x68 Ctrl Addr Data... CS 0x16
```

Where:
- `0x68`: Start bytes
- `L`: Length (repeated)
- `Ctrl`: Control byte (command type)
- `Addr`: Target address/register
- `Data`: Command-specific data
- `CS`: Checksum
- `0x16`: End byte

### Command Types

- `KMBUS_CMD_WRN_DAT` (0xB3): Write N bytes
- `KMBUS_CMD_WRR_DAT` (0xBF): Write record

### Data Encoding

- Temperatures: XOR encoded with 0xAA mask
- Modes: Specific command bytes (see enum values)
- Resolution: 0.5°C for temperature setpoints

## Best Practices

1. **Test incrementally**: Start with read-only monitoring, then add one command at a time

2. **Log everything**: Keep detailed logs of all commands and responses

3. **Use feedback**: Always read back the current state after commands

4. **Implement safeguards**:
   - Minimum/maximum setpoint limits
   - Rate limiting
   - Timeout handling
   - Error recovery

5. **Plan for failures**: System should work safely even if control commands fail

6. **Document your setup**: Note which circuits, sensors, and devices are connected

## Examples

See complete examples in the repository:

- `examples/control_commands/` - Basic control interface
- `examples/advanced_automation/` - Scheduling integration
- `examples/mqtt_integration/` - MQTT control
- `examples/webserver_enhanced/` - Web interface control

## Further Reading

- [KM-Bus Protocol Specification](KMBUS_PROTOCOL.md)
- [Hardware Setup Guide](HARDWARE_SETUP.md)
- [Vitotronic 200 KW1 Documentation](VITOTRONIC_200_KW1.md)
- [Scheduler Guide](SCHEDULER_GUIDE.md)

## Support

For issues with control commands:

1. Check this guide and examples
2. Verify hardware connections
3. Review protocol documentation
4. Test with simpler commands first
5. Open an issue on GitHub with detailed logs

## License

Part of the Viessmann Multi-Protocol Library. See main LICENSE file.
