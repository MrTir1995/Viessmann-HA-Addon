/*
 * Viessmann Multi-Protocol Library - Control Commands Example
 * 
 * This example demonstrates how to use control commands to adjust
 * heating system setpoints and modes (KM-Bus protocol only).
 * 
 * ⚠️ WARNING: Control commands can affect your heating system operation!
 * - Test carefully in a safe environment
 * - Understand the commands before using them
 * - Monitor system response
 * - Have physical access to override if needed
 * 
 * Hardware Requirements:
 * - Arduino board or ESP32/ESP8266
 * - Serial connection to Viessmann heating system (KM-Bus)
 * - Proper electrical isolation recommended
 * 
 * KM-Bus Protocol:
 * - Baud rate: Varies (typically 4800)
 * - Frame format: M-Bus long frame
 * - Bidirectional communication
 */

#include <SoftwareSerial.h>
#include "vbusdecoder.h"

// ============================================================================
// Configuration
// ============================================================================

// Serial Communication (adjust for your hardware)
const uint8_t RX_PIN = 8;
const uint8_t TX_PIN = 9;
const uint32_t BAUD_RATE = 4800;

// Control Settings
const uint8_t HEATING_CIRCUIT = 0;         // Heating circuit (0-2)
const float DEFAULT_SETPOINT = 20.0;       // Default room temperature (°C)
const float ECO_SETPOINT = 18.0;           // Eco mode temperature (°C)
const float COMFORT_SETPOINT = 22.0;       // Comfort mode temperature (°C)

// ============================================================================
// Global Objects
// ============================================================================

SoftwareSerial kmSerial(RX_PIN, TX_PIN);
VBUSDecoder vbus(&kmSerial);

// State tracking
uint8_t currentMode = KMBUS_MODE_DAY;
float currentSetpoint = DEFAULT_SETPOINT;
uint32_t lastCommand = 0;
const uint32_t COMMAND_INTERVAL = 5000;    // Minimum time between commands (ms)

// ============================================================================
// Setup
// ============================================================================

void setup() {
  // Initialize debug serial
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nViessmann Multi-Protocol Library - Control Commands Example");
  Serial.println("=============================================================");
  Serial.println("\n⚠️  WARNING: This example will send control commands to your heating system!");
  Serial.println("Make sure you understand what each command does before proceeding.\n");
  
  // Initialize KM-Bus serial
  kmSerial.begin(BAUD_RATE);
  vbus.begin(PROTOCOL_KM);
  
  Serial.println("Initializing KM-Bus communication...");
  Serial.println("Waiting for data from heating system...\n");
  
  printHelp();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Process decoder
  vbus.loop();
  
  // Print status every 10 seconds
  static uint32_t lastStatus = 0;
  uint32_t now = millis();
  if (now - lastStatus >= 10000) {
    printStatus();
    lastStatus = now;
  }
  
  // Handle serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    handleCommand(cmd);
  }
}

// ============================================================================
// Command Handler
// ============================================================================

void handleCommand(char cmd) {
  uint32_t now = millis();
  
  // Rate limiting
  if (now - lastCommand < COMMAND_INTERVAL) {
    Serial.println("⚠️  Please wait before sending another command");
    return;
  }
  
  bool success = false;
  
  switch (cmd) {
    case '1':  // Set Day mode
      Serial.println("\n→ Setting Day mode...");
      success = vbus.setKMBusMode(KMBUS_MODE_DAY);
      if (success) {
        currentMode = KMBUS_MODE_DAY;
        Serial.println("✓ Day mode set");
      }
      break;
    
    case '2':  // Set Night mode
      Serial.println("\n→ Setting Night mode...");
      success = vbus.setKMBusMode(KMBUS_MODE_NIGHT);
      if (success) {
        currentMode = KMBUS_MODE_NIGHT;
        Serial.println("✓ Night mode set");
      }
      break;
    
    case '3':  // Set Eco mode
      Serial.println("\n→ Enabling Eco mode...");
      success = vbus.setKMBusEcoMode(true);
      if (success) {
        Serial.println("✓ Eco mode enabled");
      }
      break;
    
    case '4':  // Disable Eco mode
      Serial.println("\n→ Disabling Eco mode...");
      success = vbus.setKMBusEcoMode(false);
      if (success) {
        Serial.println("✓ Eco mode disabled");
      }
      break;
    
    case '5':  // Set Party mode
      Serial.println("\n→ Enabling Party mode...");
      success = vbus.setKMBusPartyMode(true);
      if (success) {
        Serial.println("✓ Party mode enabled");
      }
      break;
    
    case '6':  // Disable Party mode
      Serial.println("\n→ Disabling Party mode...");
      success = vbus.setKMBusPartyMode(false);
      if (success) {
        Serial.println("✓ Party mode disabled");
      }
      break;
    
    case '7':  // Increase setpoint
      currentSetpoint += 0.5;
      if (currentSetpoint > 30.0) currentSetpoint = 30.0;
      Serial.print("\n→ Setting setpoint to ");
      Serial.print(currentSetpoint, 1);
      Serial.println("°C...");
      success = vbus.setKMBusSetpoint(HEATING_CIRCUIT, currentSetpoint);
      if (success) {
        Serial.println("✓ Setpoint updated");
      }
      break;
    
    case '8':  // Decrease setpoint
      currentSetpoint -= 0.5;
      if (currentSetpoint < 5.0) currentSetpoint = 5.0;
      Serial.print("\n→ Setting setpoint to ");
      Serial.print(currentSetpoint, 1);
      Serial.println("°C...");
      success = vbus.setKMBusSetpoint(HEATING_CIRCUIT, currentSetpoint);
      if (success) {
        Serial.println("✓ Setpoint updated");
      }
      break;
    
    case '9':  // Set default setpoint
      currentSetpoint = DEFAULT_SETPOINT;
      Serial.print("\n→ Resetting setpoint to ");
      Serial.print(currentSetpoint, 1);
      Serial.println("°C...");
      success = vbus.setKMBusSetpoint(HEATING_CIRCUIT, currentSetpoint);
      if (success) {
        Serial.println("✓ Setpoint reset");
      }
      break;
    
    case 'h':
    case 'H':
      printHelp();
      return;
    
    case 's':
    case 'S':
      printStatus();
      return;
    
    default:
      Serial.println("Unknown command. Press 'h' for help.");
      return;
  }
  
  if (!success) {
    Serial.println("✗ Command failed! Check connection and protocol.");
  } else {
    lastCommand = now;
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

void printHelp() {
  Serial.println("\n=== Control Commands ===");
  Serial.println("Mode Control:");
  Serial.println("  1 - Set Day mode");
  Serial.println("  2 - Set Night mode");
  Serial.println("  3 - Enable Eco mode");
  Serial.println("  4 - Disable Eco mode");
  Serial.println("  5 - Enable Party mode");
  Serial.println("  6 - Disable Party mode");
  Serial.println();
  Serial.println("Setpoint Control:");
  Serial.println("  7 - Increase setpoint (+0.5°C)");
  Serial.println("  8 - Decrease setpoint (-0.5°C)");
  Serial.println("  9 - Reset to default setpoint");
  Serial.println();
  Serial.println("Information:");
  Serial.println("  h - Show this help");
  Serial.println("  s - Show current status");
  Serial.println("========================\n");
}

void printStatus() {
  Serial.println("\n=== System Status ===");
  
  if (vbus.isReady()) {
    Serial.println("Connection: ✓ Connected");
    
    // Operating mode
    Serial.print("Mode: ");
    uint8_t mode = vbus.getKMBusMode();
    switch (mode) {
      case KMBUS_MODE_OFF:
        Serial.println("Off");
        break;
      case KMBUS_MODE_NIGHT:
        Serial.println("Night");
        break;
      case KMBUS_MODE_DAY:
        Serial.println("Day");
        break;
      case KMBUS_MODE_ECO:
        Serial.println("Eco");
        break;
      case KMBUS_MODE_PARTY:
        Serial.println("Party");
        break;
      default:
        Serial.print("Unknown (0x");
        Serial.print(mode, HEX);
        Serial.println(")");
    }
    
    // Temperatures
    Serial.print("Boiler: ");
    Serial.print(vbus.getKMBusBoilerTemp(), 1);
    Serial.println("°C");
    
    Serial.print("Hot Water: ");
    Serial.print(vbus.getKMBusHotWaterTemp(), 1);
    Serial.println("°C");
    
    Serial.print("Outdoor: ");
    Serial.print(vbus.getKMBusOutdoorTemp(), 1);
    Serial.println("°C");
    
    Serial.print("Setpoint: ");
    Serial.print(vbus.getKMBusSetpointTemp(), 1);
    Serial.println("°C");
    
    Serial.print("Departure: ");
    Serial.print(vbus.getKMBusDepartureTemp(), 1);
    Serial.println("°C");
    
    // Status flags
    Serial.print("Burner: ");
    Serial.println(vbus.getKMBusBurnerStatus() ? "ON" : "OFF");
    
    Serial.print("Main Pump: ");
    Serial.println(vbus.getKMBusMainPumpStatus() ? "ON" : "OFF");
    
    Serial.print("Loop Pump: ");
    Serial.println(vbus.getKMBusLoopPumpStatus() ? "ON" : "OFF");
    
    // Local setpoint tracking
    Serial.print("\nLocal Setpoint: ");
    Serial.print(currentSetpoint, 1);
    Serial.println("°C");
  } else {
    Serial.println("Connection: ✗ Waiting for data...");
  }
  
  Serial.println("====================\n");
}
