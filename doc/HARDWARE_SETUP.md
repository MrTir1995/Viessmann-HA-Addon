# Hardware Setup Guide

This guide provides detailed hardware connection information for interfacing Arduino/microcontrollers with Viessmann heating systems using different protocols.

## Table of Contents
- [General Considerations](#general-considerations)
- [KW-Bus Hardware Setup](#kw-bus-hardware-setup)
- [KM-Bus Hardware Setup](#km-bus-hardware-setup)
- [VBUS Hardware Setup](#vbus-hardware-setup)
- [P300/Optolink Hardware Setup](#p300optolink-hardware-setup)
- [Safety Guidelines](#safety-guidelines)

## General Considerations

### Electrical Isolation
**IMPORTANT:** When connecting your microcontroller to heating system controllers, proper electrical isolation is strongly recommended to:
- Protect your microcontroller from voltage spikes
- Prevent ground loops
- Ensure safety compliance
- Avoid damaging expensive heating equipment

Consider using:
- Optocouplers for signal lines
- Isolated DC-DC converters for power
- Galvanic isolation modules

### Power Supply
- Never power your Arduino/microcontroller directly from the heating controller
- Use a separate, isolated power supply
- Ensure proper ground connections (but through isolation when necessary)

### Cable Quality
- Use shielded cables for communication lines in electrically noisy environments
- Keep cable runs as short as practical
- Avoid running signal cables parallel to power cables
- Use twisted pair for differential signals

## KW-Bus Hardware Setup

### Overview
KW-Bus is a serial protocol used in older Viessmann Vitotronic controllers. It operates at 4800 baud with 8E2 (8 data bits, even parity, 2 stop bits).

### Connection Method

**Option 1: Direct TTL Connection (if available)**
```
Vitotronic KW-Bus TX  →  Arduino RX (Pin 8)
Vitotronic KW-Bus RX  ←  Arduino TX (Pin 9)
Common Ground          ―  Arduino GND
```

**Option 2: RS-232 to TTL Level Converter**

If the Vitotronic uses RS-232 voltage levels, you need a level converter:

```
Vitotronic RS-232 TX  →  MAX232 (or similar)  →  Arduino RX (Pin 8)
Vitotronic RS-232 RX  ←  MAX232 (or similar)  ←  Arduino TX (Pin 9)
```

**Recommended Circuit (with isolation):**

```
Vitotronic                    Isolated Interface                 Arduino
-----------                   ------------------                 -------
KW-Bus TX  → [Optocoupler] → Level Shifter → Arduino RX
KW-Bus RX  ← [Optocoupler] ← Level Shifter ← Arduino TX
GND        → Isolated DC/DC →                 Arduino GND
```

### Parts List
- **MAX232** or similar RS-232 to TTL converter (if needed)
- **Optocouplers**: 6N137 or similar (for isolation)
- **DC-DC Isolated Converter**: B0505S or similar (for power isolation)
- **Resistors**: Various for optocoupler circuits
- **Capacitors**: As required by MAX232 datasheet

### Arduino Code Example
```cpp
#include <SoftwareSerial.h>
#include "vbusdecoder.h"

// RX = Pin 8, TX = Pin 9
SoftwareSerial kwSerial(8, 9);
VBUSDecoder decoder(&kwSerial);

void setup() {
  Serial.begin(115200);
  
  // KW-Bus: 4800 baud, 8 data bits, even parity, 2 stop bits
  kwSerial.begin(4800, SERIAL_8E2);
  decoder.begin(PROTOCOL_KW);
  
  Serial.println("KW-Bus interface initialized");
}

void loop() {
  decoder.loop();
  
  if (decoder.isReady()) {
    // Read data
    for (uint8_t i = 0; i < decoder.getTempNum(); i++) {
      Serial.print("Temp ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(decoder.getTemp(i));
    }
  }
}
```

### Vitotronic 200 KW1 Specific Connections

For the Vitotronic 200 KW1, the KW-Bus signals are available at specific terminals. Refer to the wiring diagram in the service manual (page 85-87 of Vitotronic-200-KW1-Montage-und-Serviceanleitung.pdf).

**Connection Points:**
- Located on the main controller board
- Usually labeled as serial interface or service port
- May require opening the controller housing

**CAUTION:** 
- Ensure mains power is disconnected before opening the controller
- Only qualified personnel should access internal connections
- Document your connections for future reference

## KM-Bus Hardware Setup

### Overview
KM-Bus is a 2-wire bus system similar to M-Bus, carrying both power and data. It operates at 24V DC.

### Connection Requirements

**WARNING:** KM-Bus combines power and data on the same wires. Incorrect connections can damage devices.

### Recommended Interface

Use an M-Bus master transceiver IC, such as:
- **NCN5150** (Onsemi/Texas Instruments)
- **TSS721** (Texas Instruments)

**Basic Connection Diagram:**

```
Vitotronic KM-Bus         M-Bus Transceiver           Arduino
-----------------         -----------------           -------
KM+ (24V + Data) → [NCN5150 or similar] → Arduino RX (Pin 8)
KM- (GND + Data) ←                       ← Arduino TX (Pin 9)
```

### NCN5150 Circuit Example

```
         +5V
          |
          R1 (10k)
          |
KM+ ─────┤1  NCN5150  8├──── +5V
         │             │
KM- ─────┤2          7├──── GND
         │             │
         │          6├──── Arduino TX (Pin 9)
         │             │
    ┌────┤3          5├──── Arduino RX (Pin 8)
    │    │             │
   GND   └─────────────┘
              4
```

### Arduino Code Example
```cpp
#include <SoftwareSerial.h>
#include "vbusdecoder.h"

SoftwareSerial kmSerial(8, 9);  // RX, TX
VBUSDecoder decoder(&kmSerial);

void setup() {
  Serial.begin(115200);
  
  // KM-Bus baud rate varies; common values: 2400, 4800, 9600
  kmSerial.begin(9600);
  decoder.begin(PROTOCOL_KM);
  
  Serial.println("KM-Bus interface initialized");
}

void loop() {
  decoder.loop();
  
  if (decoder.isReady()) {
    Serial.println("KM-Bus frame received");
    // Process KM-Bus data
  }
}
```

### Multi-Device Setup

When connecting multiple KM-Bus devices (e.g., Vitotrol remote + Vitocom 100), use a KM-Bus distributor:

```
Vitotronic 200
    │
    │ KM-Bus (2-wire)
    │
    ├──── KM-Bus Distributor
    │          ├──── Vitotrol 200 Remote
    │          ├──── Arduino Interface
    │          └──── Expansion Module
```

**Important:**
- Each device needs a unique bus address
- Total cable length should not exceed 50 meters
- Distributor provides proper bus termination and power distribution

## VBUS Hardware Setup

### Overview
VBUS (RESOL protocol) is used in Vitosolic solar controllers and DeltaSol devices. It operates at 9600 baud, 8N1.

### Connection Methods

**Option 1: Direct 3-wire Connection (if TTL levels)**
```
VBUS Controller          Arduino
---------------          -------
GND        ─────────────  GND
+          ─────────────  (Not connected typically)
-          ─────────────  RX (Pin 8)
```

**Option 2: RS-485 Transceiver**

Many VBUS controllers use RS-485 differential signaling:

```
VBUS Controller       MAX485/SN75176      Arduino
---------------       ---------------     -------
GND      ──────────── GND    ────────────  GND
A (+)    ──────────── A
B (-)    ──────────── B
                      RO ─────────────────  RX (Pin 8)
                      DI ─────────────────  TX (Pin 9)
                      DE/RE ──────────────  Digital Pin (control)
```

### Arduino Code Example
```cpp
#include <SoftwareSerial.h>
#include "vbusdecoder.h"

SoftwareSerial vbusSerial(8, 9);
VBUSDecoder decoder(&vbusSerial);

void setup() {
  Serial.begin(115200);
  vbusSerial.begin(9600);  // VBUS: 9600 baud, 8N1
  decoder.begin(PROTOCOL_VBUS);  // Default protocol
  
  Serial.println("VBUS interface initialized");
}

void loop() {
  decoder.loop();
  
  if (decoder.isReady()) {
    Serial.print("Temp1: ");
    Serial.print(decoder.getTemp(0));
    Serial.println(" °C");
  }
}
```

### Connection Notes
- VBUS typically uses 3-wire connection (GND, +, -)
- Voltage levels vary by device (check specifications)
- Some devices provide galvanic isolation built-in
- Cable: Use shielded twisted pair, max length ~50m

## P300/Optolink Hardware Setup

### Overview
P300 protocol (VS2/Optolink) is accessed via an optical interface on Viessmann boilers. It requires an infrared transceiver.

### Required Components

1. **IR LED** (transmitter): SFH487-2 or similar
2. **IR Phototransistor** (receiver): SFH309FA or similar
3. **Resistors**: For current limiting and biasing
4. **Optolink Adapter**: Custom circuit or commercial module

### Circuit Diagram

```
         +5V
          │
          ├──── 220Ω ──── IR LED (SFH487-2) ──── Arduino TX (Pin 9)
          │                     │
          │                    GND
          │
          ├──── 10kΩ ──── Phototransistor (SFH309FA) ──── Arduino RX (Pin 8)
                               │
                              GND
```

### Physical Mounting

The Optolink interface is a small circular opening on the boiler, usually marked with the Viessmann logo or "Optolink" label.

**Mounting Steps:**
1. Locate the Optolink port (typically on the front panel)
2. Align IR LED and phototransistor with the optical window
3. Use adhesive or mechanical mount to hold in place
4. Ensure good optical coupling (minimize ambient light)
5. May need light-blocking shield around components

### Arduino Code Example
```cpp
#include <SoftwareSerial.h>
#include <vbusdecoder.h>

SoftwareSerial optoSerial(8, 9);  // RX, TX
VBUSDecoder decoder(&optoSerial);

void setup() {
  Serial.begin(115200);
  
  // P300: 4800 baud, 8E2
  // NOTE: Even parity (8E2) may require hardware serial on some boards
  optoSerial.begin(4800, SERIAL_8E2);
  decoder.begin(PROTOCOL_P300);
  
  Serial.println("P300/Optolink interface initialized");
}

void loop() {
  decoder.loop();
  
  if (decoder.isReady()) {
    Serial.println("P300 data received");
    // Read temperatures and other data
    for (uint8_t i = 0; i < decoder.getTempNum(); i++) {
      Serial.print("Temp ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(decoder.getTemp(i));
    }
  }
}
```

**Parity Support Note:**
The SERIAL_8E2 configuration (8 data bits, even parity, 2 stop bits) is required for P300 protocol. Not all Arduino cores support even parity in SoftwareSerial:
- **Supported:** Most AVR boards (Uno, Mega, Nano) with newer cores
- **Hardware Serial Recommended:** For ESP32, ESP8266, and other platforms, use hardware serial ports (Serial1, Serial2, etc.) which typically support all parity modes
- **Fallback:** If even parity is not available, protocol communication will not work correctly

### Commercial Optolink Adapters

Several commercial adapters are available:
- Viessmann official Optolink adapter
- Third-party USB Optolink interfaces
- DIY kits with PCB and components

These often include proper optical alignment and shielding.

## Safety Guidelines

### Electrical Safety

1. **Mains Voltage:** 
   - Heating controllers operate on 230V AC or higher
   - Never work on live equipment
   - Disconnect mains power before making connections
   - Use a qualified electrician if unsure

2. **Low Voltage Safety:**
   - Even 24V systems can be dangerous under certain conditions
   - Observe proper polarity
   - Don't short-circuit power supplies

3. **Isolation:**
   - Always use electrical isolation between Arduino and heating system
   - Galvanic isolation protects both systems
   - Consider safety requirements and local codes

### Regulatory Compliance

1. **Installation:**
   - In many jurisdictions, modifications to heating systems must be performed by licensed professionals
   - Check local regulations before proceeding
   - Document all modifications

2. **Safety Equipment:**
   - Don't bypass or interfere with safety equipment
   - Safety temperature limiters must remain functional
   - Don't modify critical control signals

3. **Warranty:**
   - Modifications may void manufacturer warranty
   - Consult with manufacturer before making changes
   - Keep heating system and monitoring system separate

### Best Practices

1. **Testing:**
   - Test connections with multimeter before applying power
   - Start with monitoring only (read-only access)
   - Verify proper operation before relying on automation

2. **Monitoring:**
   - Include error detection in your code
   - Log communication errors
   - Implement watchdog timers for reliability

3. **Documentation:**
   - Document all connections and modifications
   - Take photos before and after
   - Keep wiring diagrams up to date
   - Label all wires clearly

4. **Maintenance:**
   - Periodically check connections
   - Clean optical interfaces (for Optolink)
   - Update software as needed
   - Monitor for communication errors

## Troubleshooting

### No Communication

1. **Check Physical Connections:**
   - Verify RX/TX are not swapped
   - Check ground connection
   - Measure voltages on communication lines

2. **Check Serial Configuration:**
   - Verify baud rate matches protocol
   - Confirm parity and stop bits are correct
   - Try different Arduino pins if using SoftwareSerial

3. **Check Signal Levels:**
   - Use oscilloscope or logic analyzer if available
   - Verify voltage levels match expectations
   - Check for noise or interference

### Intermittent Communication

1. **Power Supply Issues:**
   - Check for voltage drops
   - Ensure adequate current capacity
   - Add decoupling capacitors

2. **Interference:**
   - Move cables away from power lines
   - Use shielded cables
   - Add ferrite beads on communication lines

3. **Timing Issues:**
   - Increase buffer sizes if needed
   - Check for Arduino processing delays
   - Verify no interrupt conflicts

### Incorrect Data

1. **Protocol Mismatch:**
   - Verify correct protocol type is selected
   - Check device compatibility
   - Consult device documentation

2. **Checksum Errors:**
   - Usually indicates noise or incorrect levels
   - Check connection quality
   - Verify proper termination (for bus systems)

3. **Byte Alignment:**
   - Clear buffers and restart communication
   - Check for sync byte detection
   - Verify no bytes are dropped

## Additional Resources

- **Vitotronic 200 KW1:** See doc/VITOTRONIC_200_KW1.md for device-specific information
- **Protocol Details:** See PROTOCOLS.md for protocol specifications
- **Example Code:** See examples/ directory for working examples
- **Community Forums:** Search for Arduino Viessmann projects for additional tips
- **Official Documentation:** Viessmann service manuals (available from Viessmann technical support)

## Disclaimer

**WARNING:** Interfacing with heating systems involves working with potentially dangerous voltages and critical safety equipment. Improper modifications can result in:
- Equipment damage
- Fire hazard
- Personal injury
- Voided warranty
- Violation of safety regulations

This documentation is provided for informational purposes only. The authors and contributors assume no liability for any damages or injuries resulting from the use of this information. Always consult qualified professionals and follow all applicable safety regulations and codes.

If you are not qualified to work on electrical systems and heating equipment, please hire a licensed professional.
