/*
 * Vitotronic 200 KW1 Example
 * 
 * This example demonstrates how to interface with a Viessmann Vitotronic 200 
 * Type KW1 controller using the KW-Bus protocol.
 * 
 * NOTE: The Vitotronic 200 KW1 also supports KM-Bus protocol for connecting
 * remote controls (Vitotrol) and expansion modules. To use KM-Bus instead:
 * 1. Change baud rate to 9600 (or device-specific rate)
 * 2. Change protocol to PROTOCOL_KM
 * 3. Use standard 8N1 format instead of 8E2
 * 4. See PROTOCOLS.md for complete KM-Bus documentation
 * 
 * Hardware Setup:
 * - Connect Arduino RX (Pin 8) to Vitotronic KW-Bus TX
 * - Connect Arduino TX (Pin 9) to Vitotronic KW-Bus RX
 * - Connect GND between Arduino and Vitotronic
 * - Use proper level conversion if needed (RS-232 to TTL)
 * - Consider using electrical isolation for safety
 * 
 * For KM-Bus connections:
 * - Connect to aVG terminals on Vitotronic 200 KW1
 * - May require M-Bus transceiver IC (e.g., NCN5150)
 * - See doc/HARDWARE_SETUP.md for detailed connection diagrams
 * 
 * See doc/HARDWARE_SETUP.md for detailed connection diagrams
 * See doc/VITOTRONIC_200_KW1.md for device-specific information
 * 
 * Protocol: KW-Bus (VS1)
 * Baud Rate: 4800
 * Format: 8 data bits, Even parity, 2 stop bits (8E2)
 * 
 * Compatible with:
 * - Vitotronic 200 Type KW1 (Part Numbers: 7450 740 to 7450 743)
 * - Other Vitotronic 100/200/300 series controllers
 * 
 * Author: Viessmann Multi-Protocol Library Contributors
 * Date: January 2026
 */

#include <SoftwareSerial.h>
#include <vbusdecoder.h>

// Pin configuration
// RX Pin 8 - receives data from Vitotronic
// TX Pin 9 - sends data to Vitotronic (if needed for future bidirectional communication)
SoftwareSerial kwSerial(8, 9);
VBUSDecoder decoder(&kwSerial);

// Update interval for serial output
const unsigned long UPDATE_INTERVAL = 5000; // 5 seconds
unsigned long lastUpdate = 0;

// Temperature validation constants
const float MIN_VALID_TEMP = -50.0; // Minimum reasonable temperature in °C
const float MAX_VALID_TEMP = 200.0; // Maximum reasonable temperature in °C

// Status tracking
bool firstConnection = true;
unsigned long connectionAttempts = 0;

void setup() {
  // Initialize USB serial for debugging/output
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Wait up to 3 seconds for Serial
  
  Serial.println(F("========================================"));
  Serial.println(F("Vitotronic 200 KW1 Interface"));
  Serial.println(F("========================================"));
  Serial.println();
  
  // Initialize KW-Bus serial communication
  // IMPORTANT: KW-Bus requires 4800 baud, 8 data bits, Even parity, 2 stop bits
  // NOTE: SERIAL_8E2 (even parity) is not supported by all Arduino cores
  // For boards without even parity support in SoftwareSerial, use hardware serial (Serial1, Serial2, etc.)
  kwSerial.begin(4800, SERIAL_8E2);
  
  // Initialize decoder with KW-Bus protocol
  decoder.begin(PROTOCOL_KW);
  
  Serial.println(F("Configuration:"));
  Serial.println(F("  Protocol: KW-Bus (VS1)"));
  Serial.println(F("  Baud Rate: 4800"));
  Serial.println(F("  Format: 8E2 (8 data bits, Even parity, 2 stop bits)"));
  Serial.println(F("  RX Pin: 8"));
  Serial.println(F("  TX Pin: 9"));
  Serial.println();
  Serial.println(F("Waiting for data from Vitotronic 200..."));
  Serial.println();
}

void loop() {
  // Main decoder loop - must be called frequently
  decoder.loop();
  
  // Check if new data is available
  if (decoder.isReady()) {
    if (firstConnection) {
      Serial.println(F("*** Connection established! ***"));
      Serial.println();
      firstConnection = false;
    }
    
    // Update display at regular intervals
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      displayData();
      lastUpdate = millis();
    }
  } else {
    // No data ready yet
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      connectionAttempts++;
      
      // Check communication status
      if (!decoder.getVbusStat()) {
        Serial.print(F("Waiting for data... (attempt "));
        Serial.print(connectionAttempts);
        Serial.println(F(")"));
        
        if (connectionAttempts % 6 == 0) {
          Serial.println(F("Troubleshooting tips:"));
          Serial.println(F("  1. Verify RX/TX connections"));
          Serial.println(F("  2. Check ground connection"));
          Serial.println(F("  3. Ensure Vitotronic is powered on"));
          Serial.println(F("  4. Verify baud rate (should be 4800)"));
          Serial.println(F("  5. Check serial format (should be 8E2)"));
          Serial.println();
        }
      }
      
      lastUpdate = millis();
    }
  }
}

void displayData() {
  Serial.println(F("========================================"));
  Serial.print(F("Update: "));
  Serial.print(millis() / 1000);
  Serial.println(F(" seconds"));
  Serial.println(F("========================================"));
  
  // Communication status
  Serial.print(F("Communication: "));
  Serial.println(decoder.getVbusStat() ? F("OK") : F("ERROR"));
  
  // Temperature sensors
  uint8_t tempCount = decoder.getTempNum();
  if (tempCount > 0) {
    Serial.println();
    Serial.println(F("Temperature Sensors:"));
    Serial.println(F("------------------"));
    
    for (uint8_t i = 0; i < tempCount; i++) {
      float temp = decoder.getTemp(i);
      
      // Skip invalid temperatures
      if (temp < MIN_VALID_TEMP || temp > MAX_VALID_TEMP) {
        continue;
      }
      
      Serial.print(F("  Sensor "));
      Serial.print(i + 1);
      Serial.print(F(": "));
      
      if (temp > -40.0) {
        Serial.print(temp, 1);
        Serial.print(F(" °C"));
        
        // Add sensor description based on position
        switch(i) {
          case 0:
            Serial.print(F(" (Boiler/Flow)"));
            break;
          case 1:
            Serial.print(F(" (Return)"));
            break;
          case 2:
            Serial.print(F(" (Hot Water)"));
            break;
          case 3:
            Serial.print(F(" (Outside)"));
            break;
        }
      } else {
        Serial.print(F("Not connected"));
      }
      Serial.println();
    }
  }
  
  // Pump status
  uint8_t pumpCount = decoder.getPumpNum();
  if (pumpCount > 0) {
    Serial.println();
    Serial.println(F("Pump Status:"));
    Serial.println(F("------------"));
    
    for (uint8_t i = 0; i < pumpCount; i++) {
      uint8_t pumpPower = decoder.getPump(i);
      
      Serial.print(F("  Pump "));
      Serial.print(i + 1);
      Serial.print(F(": "));
      
      if (pumpPower > 0) {
        Serial.print(pumpPower);
        Serial.print(F("% "));
        
        // Add pump description
        switch(i) {
          case 0:
            Serial.print(F("(Heating Circuit)"));
            break;
          case 1:
            Serial.print(F("(Storage Charging)"));
            break;
        }
      } else {
        Serial.print(F("OFF"));
      }
      Serial.println();
    }
  }
  
  // Relay status
  uint8_t relayCount = decoder.getRelayNum();
  if (relayCount > 0) {
    Serial.println();
    Serial.println(F("Relay Status:"));
    Serial.println(F("-------------"));
    
    for (uint8_t i = 0; i < relayCount; i++) {
      Serial.print(F("  Relay "));
      Serial.print(i + 1);
      Serial.print(F(": "));
      Serial.print(decoder.getRelay(i) ? F("ON") : F("OFF"));
      
      // Add relay description
      switch(i) {
        case 0:
          Serial.print(F(" (Burner Stage 1)"));
          break;
        case 1:
          Serial.print(F(" (Burner Stage 2)"));
          break;
      }
      Serial.println();
    }
  }
  
  // Extended information
  displayExtendedInfo();
  
  Serial.println(F("========================================"));
  Serial.println();
}

void displayExtendedInfo() {
  // System variant
  uint8_t variant = decoder.getSystemVariant();
  if (variant != 0) {
    Serial.println();
    Serial.print(F("System Variant: 0x"));
    Serial.println(variant, HEX);
  }
  
  // Error mask
  uint16_t errorMask = decoder.getErrorMask();
  if (errorMask != 0) {
    Serial.println();
    Serial.print(F("ERROR: Error mask = 0x"));
    Serial.println(errorMask, HEX);
    Serial.println(F("Check system for faults!"));
  }
  
  // Operating hours
  uint32_t opHours = decoder.getOperatingHours(0);
  if (opHours > 0) {
    Serial.println();
    Serial.print(F("Operating Hours: "));
    Serial.print(opHours);
    Serial.println(F(" hours"));
  }
  
  // Heat quantity
  uint16_t heatQty = decoder.getHeatQuantity();
  if (heatQty > 0) {
    Serial.print(F("Heat Quantity: "));
    Serial.print(heatQty);
    Serial.println(F(" Wh"));
  }
  
  // System time
  uint16_t sysTime = decoder.getSystemTime();
  if (sysTime > 0) {
    Serial.print(F("System Time: "));
    
    // Convert minutes to hours:minutes
    uint16_t hours = sysTime / 60;
    uint8_t minutes = sysTime % 60;
    
    if (hours < 10) Serial.print(F("0"));
    Serial.print(hours);
    Serial.print(F(":"));
    if (minutes < 10) Serial.print(F("0"));
    Serial.println(minutes);
  }
}

/*
 * Additional Notes:
 * 
 * 1. Data Update Rate:
 *    The Vitotronic 200 KW1 typically sends data every 1-10 seconds depending
 *    on the system configuration. This example prints data every 5 seconds.
 * 
 * 2. Temperature Sensor Mapping:
 *    - Sensor 0: Usually boiler/flow temperature
 *    - Sensor 1: Usually return temperature
 *    - Sensor 2: Usually hot water storage temperature
 *    - Sensor 3: Usually outside temperature
 *    (Actual mapping may vary based on system configuration)
 * 
 * 3. Error Handling:
 *    If communication errors occur, check:
 *    - Physical connections (RX/TX, GND)
 *    - Voltage levels (may need level converter)
 *    - Electrical interference (use shielded cables)
 *    - Serial configuration (must be 4800 baud, 8E2)
 * 
 * 4. Safety:
 *    - This example only reads data (monitoring mode)
 *    - Never interfere with safety systems
 *    - Use electrical isolation for permanent installations
 *    - Consult qualified technician for installation
 * 
 * 5. Power Supply:
 *    - Use separate power supply for Arduino
 *    - Do not power Arduino from Vitotronic
 *    - Ensure common ground connection
 * 
 * 6. Further Information:
 *    - Full documentation: doc/VITOTRONIC_200_KW1.md
 *    - Hardware guide: doc/HARDWARE_SETUP.md
 *    - Protocol details: PROTOCOLS.md
 */
