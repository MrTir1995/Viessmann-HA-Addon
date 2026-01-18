# Bus Participant Discovery and Configuration

This document describes the automatic discovery and manual configuration features for bus participants in the Viessmann Multi-Protocol Library v2.1.

## Overview

The library now supports:
- **Automatic discovery** of all devices on the bus
- **Manual configuration** of participants with custom settings
- **Real-time monitoring** of participant status
- **Device identification** with known device types

## Features

### Automatic Discovery

By default, the library automatically discovers all devices communicating on the bus. Each time a packet is received, the library:

1. Extracts the source address from the packet
2. Checks if the device is already known
3. If new, adds it to the participant list
4. Updates the "last seen" timestamp
5. Auto-configures channels based on known device addresses

### Known Device Types

The library automatically recognizes these devices:

| Address | Device Name | Temp | Pump | Relay |
|---------|-------------|------|------|-------|
| 0x1060 | Vitosolic 200 | 12 | 0 | 7 |
| 0x7E11 | DeltaSol BX Plus | 6 | 2 | 0 |
| 0x7E21 | DeltaSol BX | 6 | 2 | 0 |
| 0x7E31 | DeltaSol MX | 4 | 4 | 0 |

Unknown devices are assigned default values (4 temp, 2 pump, 2 relay channels).

### Manual Configuration

You can manually add or configure devices before or during operation. Manual configurations take precedence over auto-detection.

## API Reference

### Enable/Disable Auto-Discovery

```cpp
void enableAutoDiscovery(bool enable = true);
bool isAutoDiscoveryEnabled() const;
```

Enable or disable automatic discovery. Default is enabled.

**Example:**
```cpp
vbus.enableAutoDiscovery(false);  // Disable auto-discovery
```

### Query Participants

```cpp
uint8_t getParticipantCount() const;
const BusParticipant* getParticipant(uint8_t idx) const;
const BusParticipant* getParticipantByAddress(uint16_t address) const;
uint16_t getCurrentSourceAddress() const;
```

Get information about discovered or configured participants.

**Example:**
```cpp
uint8_t count = vbus.getParticipantCount();
for (uint8_t i = 0; i < count; i++) {
    const BusParticipant* p = vbus.getParticipant(i);
    Serial.print("Device: ");
    Serial.print(p->name);
    Serial.print(" at 0x");
    Serial.println(p->address, HEX);
}
```

### Add Participant Manually

```cpp
bool addParticipant(uint16_t address, const char* name = nullptr,
                   uint8_t tempChannels = 0, uint8_t pumpChannels = 0,
                   uint8_t relayChannels = 0);
```

Manually add or configure a participant. Returns `true` on success.

**Parameters:**
- `address`: Device address (e.g., 0x1060)
- `name`: Optional device name (auto-generated if nullptr)
- `tempChannels`: Number of temperature channels (0 = auto-detect)
- `pumpChannels`: Number of pump channels (0 = auto-detect)
- `relayChannels`: Number of relay channels (0 = auto-detect)

**Example:**
```cpp
// Add custom device with manual configuration
vbus.addParticipant(0x2000, "Custom Device", 8, 4, 2);

// Add device with auto-detected channels
vbus.addParticipant(0x1060);  // Vitosolic 200 will be auto-configured
```

### Remove Participant

```cpp
bool removeParticipant(uint16_t address);
void clearParticipants();
```

Remove a specific participant or clear all participants.

**Example:**
```cpp
vbus.removeParticipant(0x2000);  // Remove specific device
vbus.clearParticipants();         // Remove all devices
```

## BusParticipant Structure

```cpp
struct BusParticipant {
    uint16_t address;         // Device address
    uint32_t lastSeen;        // Last time packet was received (millis)
    uint8_t tempChannels;     // Number of temperature channels
    uint8_t pumpChannels;     // Number of pump channels
    uint8_t relayChannels;    // Number of relay channels
    bool autoDetected;        // True if auto-detected
    char name[32];            // Device name/description
    bool active;              // True if participant is active
};
```

## Usage Examples

### Example 1: Monitor All Devices

```cpp
#include "vbusdecoder.h"

VBUSDecoder vbus(&serial);

void setup() {
    Serial.begin(115200);
    serial.begin(9600);
    vbus.begin(PROTOCOL_VBUS);
    
    // Auto-discovery is enabled by default
}

void loop() {
    vbus.loop();
    
    // Display all discovered devices every 10 seconds
    static unsigned long lastDisplay = 0;
    if (millis() - lastDisplay > 10000) {
        Serial.println("=== Discovered Devices ===");
        uint8_t count = vbus.getParticipantCount();
        for (uint8_t i = 0; i < count; i++) {
            const BusParticipant* p = vbus.getParticipant(i);
            Serial.print("Device ");
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(p->name);
            Serial.print(" (0x");
            Serial.print(p->address, HEX);
            Serial.println(")");
            Serial.print("  Channels: T=");
            Serial.print(p->tempChannels);
            Serial.print(", P=");
            Serial.print(p->pumpChannels);
            Serial.print(", R=");
            Serial.println(p->relayChannels);
            Serial.print("  Last seen: ");
            Serial.print((millis() - p->lastSeen) / 1000);
            Serial.println(" sec ago");
        }
        lastDisplay = millis();
    }
}
```

### Example 2: Manual Configuration Only

```cpp
#include "vbusdecoder.h"

VBUSDecoder vbus(&serial);

void setup() {
    Serial.begin(115200);
    serial.begin(9600);
    vbus.begin(PROTOCOL_VBUS);
    
    // Disable auto-discovery
    vbus.enableAutoDiscovery(false);
    
    // Manually configure known devices
    vbus.addParticipant(0x1060, "Vitosolic 200", 12, 0, 7);
    vbus.addParticipant(0x7E11, "DeltaSol BX Plus", 6, 2, 0);
}

void loop() {
    vbus.loop();
    
    if (vbus.isReady()) {
        // Get current device
        uint16_t addr = vbus.getCurrentSourceAddress();
        const BusParticipant* p = vbus.getParticipantByAddress(addr);
        
        if (p != nullptr) {
            Serial.print("Data from: ");
            Serial.println(p->name);
            // Process data...
        }
    }
}
```

### Example 3: Mixed Mode

```cpp
#include "vbusdecoder.h"

VBUSDecoder vbus(&serial);

void setup() {
    Serial.begin(115200);
    serial.begin(9600);
    vbus.begin(PROTOCOL_VBUS);
    
    // Auto-discovery enabled (default)
    // But pre-configure known devices with custom settings
    vbus.addParticipant(0x1060, "Solar Controller", 12, 0, 7);
    
    // Unknown devices will still be auto-discovered
}

void loop() {
    vbus.loop();
    // Process data...
}
```

## Windows and Linux Examples

The example applications (`vbusdecoder_windows.exe` and `vbusdecoder_linux`) automatically display all discovered participants:

```
=== Discovered Bus Participants: 2 ===
  [1] Address: 0x1060, Name: Vitosolic 200
      Channels: Temp=12, Pump=0, Relay=7
      Status: Auto-detected, Last seen: 125 ms ago
  [2] Address: 0x7E11, Name: DeltaSol BX Plus
      Channels: Temp=6, Pump=2, Relay=0
      Status: Auto-detected, Last seen: 3421 ms ago

=== Current Device (0x1060) ===
Temperature sensors [12]: 25.3°C, 23.1°C, ...
```

## Best Practices

1. **Use auto-discovery** for initial setup and troubleshooting
2. **Switch to manual configuration** for production deployments
3. **Pre-configure known devices** even with auto-discovery enabled
4. **Monitor participant list** to detect new or missing devices
5. **Check lastSeen timestamp** to detect communication issues

## Limitations

- Maximum 16 participants can be tracked simultaneously
- Participant list is not persistent (lost on restart)
- Auto-detection requires receiving at least one packet from each device
- Channel configuration is based on device address, not actual capabilities

## Future Enhancements

Planned features for future versions:
- Persistent storage of participants
- Capability negotiation protocol
- Device health monitoring
- Automatic participant prioritization
- Support for more than 16 participants
