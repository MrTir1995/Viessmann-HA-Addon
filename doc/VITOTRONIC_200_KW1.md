# Vitotronic 200 KW1 Controller Documentation

## Overview

The Vitotronic 200 Type KW1 is a weather-compensated digital boiler control system from Viessmann. This document provides technical details for integration with the VBUS Decoder Library.

## Device Information

**Full Name:** Vitotronic 200, Type KW1
**Application:** Weather-compensated, digital boiler circuit control
**Protocols Supported:** KW-Bus (VS1), KM-Bus
**Part Numbers:** Best.-Nr. 7450 740 to Best.-Nr. 7450 743

## Physical Connection

### KW-Bus Connection

The Vitotronic 200 KW1 uses the KW-Bus protocol for communication with boiler systems.

**Communication Parameters:**
- **Baud Rate:** 4800 baud
- **Data Format:** 8 data bits, Even parity, 2 stop bits (8E2)
- **Interface:** Serial UART connection
- **Protocol Type:** `PROTOCOL_KW` in library

**Frame Format:**
```
Start: 0x01
Length: <byte>
Address: <byte>
Data: <n bytes>
Checksum: XOR of all previous bytes
```

**Arduino Setup Example:**
```cpp
#include <SoftwareSerial.h>
#include <vbusdecoder.h>

SoftwareSerial kwSerial(8, 9);  // RX, TX
VBUSDecoder decoder(&kwSerial);

void setup() {
  kwSerial.begin(4800, SERIAL_8E2);  // 4800 baud, 8E2 format
  decoder.begin(PROTOCOL_KW);
  Serial.begin(115200);
}

void loop() {
  decoder.loop();
  
  if (decoder.isReady()) {
    // Read temperatures
    for (uint8_t i = 0; i < decoder.getTempNum(); i++) {
      Serial.print("Temperature ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(decoder.getTemp(i));
    }
  }
}
```

### KM-Bus Connection

The KM-Bus is used for connecting remote controls (Vitotrol), expansion modules, and other peripherals.

**Communication Parameters:**
- **Physical Layer:** 2-wire bus (similar to M-Bus)
- **Voltage:** 24V DC
- **Frame Format:** M-Bus long frame compatible
- **Protocol Type:** `PROTOCOL_KM` in library

**Connection Requirements:**
- For multiple KM-Bus participants, a KM-Bus distributor is required
- Maximum cable length: typically up to 50 meters
- May require M-Bus transceiver chip (e.g., NCN5150) for proper level conversion

**Frame Format:**
```
Start: 0x68
Length: <byte>
Length: <byte> (repeated)
Start: 0x68 (repeated)
Control: <byte>
Address: <byte>
Data: <n bytes>
Checksum: Sum of control + address + data
Stop: 0x16
```

## Sensor Configuration

### Temperature Sensors

The Vitotronic 200 KW1 supports multiple temperature sensors:

1. **Outside Temperature Sensor (ATS)**
   - Connection: 2-wire, polarity can be swapped
   - Maximum cable length: 35m with 1.5mm² copper wire
   - Installation location:
     - North or northwest wall
     - 2 to 2.5m above ground
     - Not above windows, doors, or air vents
     - Not directly under balcony or gutter
   - Do not plaster in

2. **Boiler Temperature Sensor (KTS)**
   - Pre-installed on boiler
   - Measures boiler water temperature

3. **Storage Tank Temperature Sensor (STS)**
   - Optional, for hot water storage tanks
   - Same connection as other temperature sensors

4. **Exhaust Gas Temperature Sensor (AGS)**
   - Optional accessory
   - Used for advanced boiler control

### Pumps and Relays

**Available Pump Connections:**
- **Heating Circuit Pump A1 (sÖ):** Main heating circulation pump
- **Storage Charging Pump (sA):** Hot water tank charging pump (optional)
- **Circulation Pump (sK):** Domestic hot water circulation pump (user-installed)

**Electrical Specifications:**
- Rated current: 4 (2) A~
- Nominal voltage: 230 V~
- Recommended cable: H05VV-F 3G 0.75 mm² or H05RN-F 3G 0.75 mm²

**For 400V 3-phase pumps:**
- Use external contactor
- Contactor coil: 230V~, 4(2)A~

## Boiler Coding Plug

The Vitotronic 200 KW1 uses a boiler coding plug to configure the system for specific boiler types. The coding plug must match the boiler model and is inserted at position "X7" on the controller.

**Important:** Only use the coding plug supplied with the boiler. Refer to the boiler documentation for the correct coding plug information.

## Safety Features

### Safety Temperature Limiter (STB)

- **Factory Setting:** 110°C
- **Alternative Setting:** 100°C (field adjustable)
- **Important:** If set to 100°C, the temperature regulator must not be set above 75°C

### Temperature Regulator (TR)

- **Factory Setting:** 75°C
- **Alternative Settings:** 87°C or 95°C (by breaking out stops)
- **Note:** When operating with a hot water storage tank, ensure the maximum permissible domestic water temperature is not exceeded

## External Connections (Connector aBÖ)

The external connection terminal provides interfaces for:

1. **External Lockout (TR - TR terminals)**
   - Potentia-free contact
   - Open contact = control shutdown
   - Used for safety shutdowns only
   - **Warning:** No frost protection during lockout, boiler not maintained at minimum temperature

2. **External Start-up (EIN/TR terminals)**
   - Potential-free contact
   - Closed contact = first burner stage on
   - Boiler temperature controlled by temperature regulator

3. **External Safety Devices (STB - STB terminals)**
   - For additional safety equipment
   - Connected in series
   - Examples: Minimum pressure switch, additional safety temperature limiters

4. **Emergency Operation Mode**
   - Move bridge from "EIN/TR" to "EIN" terminal for emergency burner operation

## Operating Programs

### Winter Operation (Heating and Hot Water)
- Space heating with alternating normal and reduced room temperature according to time program
- Hot water preparation according to time program
- Frost protection for boiler and hot water tank

### Summer Operation (Hot Water Only)
- No space heating
- Hot water preparation according to time program
- Frost protection for boiler and hot water tank

### Shutdown Mode
- No space heating
- No hot water preparation
- Frost protection for boiler and hot water tank
- Circulation pumps activated briefly every 24 hours to prevent seizing

## Time Programs

The controller supports up to 4 time phases per day for:
- Space heating (normal vs. reduced temperature)
- Hot water preparation
- Circulation pump operation

**Factory Default Settings:**
- Heating: Normal temperature 6:00-22:00, Reduced (frost protection) 22:00-6:00
- Hot water: 5:30-22:00
- Circulation pump: 5:30-22:00

## Service Functions

### Service Level Access

Various service levels provide diagnostic information:

1. **Temperatures and Quick Queries** (Press K + G for 2 seconds)
   - Outside temperature (damped and actual)
   - Boiler temperature (setpoint and actual)
   - Exhaust gas temperature (max and actual)
   - Hot water temperature (setpoint and actual)
   - Room temperature (setpoint and actual)
   - Boiler coding plug information
   - Quick queries 1-4 (system configuration info)

2. **Relay Test** (Press K + d for 2 seconds)
   - Manual control of all relay outputs
   - Test burner stages, pumps, and other outputs

3. **Operating Status Query** (Press c)
   - Holiday program status
   - All temperature readings
   - Burner operating hours
   - Burner starts
   - Fuel consumption (if configured)
   - Current time and date
   - Relay states (burner, pumps, etc.)

### Maintenance Indication

The controller can display "Wartung" (Maintenance) when:
- Preset operating hours are reached
- Preset time interval has elapsed
- Maximum exhaust gas temperature exceeded

## Troubleshooting

### Common Fault Codes

The controller displays fault codes when errors occur. Common indicators:
- Red fault LED blinking or steady
- Error code displayed on screen
- System goes into safety shutdown

**Important:** Fault codes and detailed troubleshooting procedures are documented in the service manual (Montage- und Serviceanleitung).

### What to Check First

1. **Boiler water pressure** - Check manometer, refill if below red marking
2. **Fuel supply** - Ensure oil tank valves or gas shut-off valve are open
3. **Power supply** - Check main power switch and fuses
4. **Safety temperature limiter** - May need to be reset by pressing the "E" button

## Hardware Wiring

### Connector Pin Assignments

**230V~ Connections:**
- **sÖ:** Heating circuit pump A1
- **sA:** Storage charging pump (optional)
- **sK:** Circulation pump (user-installed)
- **fÖ:** Mains connection (230V~ 50Hz)
- **fA:** Burner connection

**Low Voltage Connections:**
- **! (ATS):** Outside temperature sensor
- **§ (KTS):** Boiler temperature sensor
- **% (STS):** Storage temperature sensor
- **aG (AGS):** Exhaust gas temperature sensor (optional)
- **aVG:** KM-Bus participants (Vitotrol remote, Vitocom 100)
- **a:A:** Two-stage/modulating burner expansion

### Cable Specifications

**Mains Connection:**
- 3-core cable: H05VV-F 3G 0.75mm² or H05RN-F 3G 0.75mm²
- Maximum fuse rating: 16A~
- Colors (DIN/IEC 757):
  - L1 (Phase): Brown
  - N (Neutral): Blue
  - PE (Ground): Green/Yellow

**Important Safety Notes:**
- Installation must comply with IEC 364, VDE regulations, and local utility requirements
- Main switch (if required) must disconnect all non-grounded conductors with minimum 3mm contact separation
- Only qualified heating technicians should perform installation and service work

## Heating Curve Settings

The heating curve determines the relationship between outside temperature and boiler/flow temperature.

**Factory Settings:**
- Slope: 1.4
- Level: 0

**Typical Ranges:**
- Low-temperature heating systems: Slope 0.8-1.6 (Range A)
- Standard heating systems (>75°C): Slope 1.6-2.8 (Range B)

**Adjustment Examples:**
- Well-insulated house, sheltered location: Slope = 1.2
- House in exposed location or older heating system: Slope = 1.6

## Additional Resources

For complete wiring diagrams, detailed troubleshooting procedures, and coding address tables, refer to the full service manual:
- **Vitotronic-200-KW1-Montage-und-Serviceanleitung.pdf** (Installation and Service Manual)
- **Vitotronic 200 Bedienungsanleitung.pdf** (Operating Manual)

## Integration Notes for Developers

When implementing support for the Vitotronic 200 KW1:

1. **Protocol Selection:** Use `PROTOCOL_KW` for boiler communication
2. **Baud Rate:** Ensure serial port is configured for 4800 baud, 8E2
3. **Data Decoding:** KW-Bus frames contain device-specific data structures
4. **Temperature Scaling:** Most temperature values are transmitted as 16-bit values with 0.1°C resolution
5. **Status Information:** Relay and pump states may need to be inferred from control states
6. **Remote Control:** If Vitotrol or similar remote is present, it communicates via KM-Bus

## Version Information

This documentation is based on:
- Vitotronic 200 KW1 Service Manual dated 10/2002
- Library version 2.0.0+
- Applicable to Part Numbers: Best.-Nr. 7450 740 through Best.-Nr. 7450 743
