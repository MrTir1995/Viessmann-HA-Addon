/*
 * Viessmann Multi-Protocol Library - Advanced Automation Example
 * 
 * This example demonstrates:
 * - Historical data logging with circular buffer
 * - Advanced scheduling with time and temperature-based rules
 * - Data export (CSV and JSON)
 * - Statistical analysis
 * 
 * Use case: Automatically adjust heating based on schedule and outdoor temperature,
 * while logging all data for analysis.
 * 
 * Hardware Requirements:
 * - ESP32 (recommended for memory) or ESP8266
 * - Serial connection to Viessmann heating system
 * - RTC module or NTP for accurate time (optional)
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <time.h>
  HardwareSerial vbusSerial(2);
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <time.h>
  #define vbusSerial Serial
#else
  #error "This example requires ESP32 or ESP8266"
#endif

#include "vbusdecoder.h"
#include "VBUSDataLogger.h"
#include "VBUSScheduler.h"

// ============================================================================
// Configuration
// ============================================================================

// WiFi Settings (for NTP time sync)
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// NTP Settings
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 0;           // Your timezone offset in seconds
const int DAYLIGHT_OFFSET_SEC = 3600;    // Daylight saving time offset

// Serial Communication
const ProtocolType PROTOCOL = PROTOCOL_KM;  // KM-Bus for control commands
const uint32_t BAUD_RATE = 4800;

// Data Logging
const uint16_t LOG_BUFFER_SIZE = 576;    // 48 hours at 5-minute intervals
const uint32_t LOG_INTERVAL = 300;       // 5 minutes

// Heating Automation Schedule
const uint8_t HEATING_CIRCUIT = 0;

// ============================================================================
// Global Objects
// ============================================================================

VBUSDecoder vbus(&vbusSerial);
VBUSDataLogger logger(&vbus, LOG_BUFFER_SIZE);
VBUSScheduler scheduler(&vbus, 16);

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nViessmann Multi-Protocol Library - Advanced Automation Example");
  Serial.println("===============================================================");
  
  // Connect to WiFi for NTP
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Configure NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println("NTP time sync configured");
  } else {
    Serial.println("\nWiFi connection failed. Continuing without time sync.");
  }
  
  // Initialize decoder
  Serial.println("\nInitializing KM-Bus decoder...");
  vbusSerial.begin(BAUD_RATE);
  vbus.begin(PROTOCOL);
  
  // Initialize data logger
  Serial.println("Initializing data logger...");
  logger.begin();
  logger.setLogInterval(LOG_INTERVAL);
  Serial.print("Buffer size: ");
  Serial.print(LOG_BUFFER_SIZE);
  Serial.print(" data points (");
  Serial.print((LOG_BUFFER_SIZE * LOG_INTERVAL) / 3600);
  Serial.println(" hours)");
  
  // Initialize scheduler
  Serial.println("Initializing scheduler...");
  scheduler.begin();
  
  // Configure heating schedule
  setupSchedule();
  
  Serial.println("\n===============================================================");
  Serial.println("Setup complete. Starting automation...");
  Serial.println("===============================================================\n");
  
  printHelp();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Update current time for scheduler
  updateSchedulerTime();
  
  // Process all components
  vbus.loop();
  logger.loop();
  scheduler.loop();
  
  // Print status every 30 seconds
  static uint32_t lastStatus = 0;
  uint32_t now = millis();
  if (now - lastStatus >= 30000) {
    printStatus();
    lastStatus = now;
  }
  
  // Handle serial commands
  if (Serial.available()) {
    handleCommand(Serial.read());
  }
}

// ============================================================================
// Schedule Configuration
// ============================================================================

void setupSchedule() {
  Serial.println("\nConfiguring heating schedule:");
  
  // Weekday morning: Set day mode at 6:00 AM
  uint8_t weekdays = 0x3E;  // Monday to Friday (bits 1-5)
  scheduler.addTimeRule(6, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_DAY);
  Serial.println("  ✓ Weekdays 6:00 AM - Day mode");
  
  // Weekday evening: Set night mode at 10:00 PM
  scheduler.addTimeRule(22, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_NIGHT);
  Serial.println("  ✓ Weekdays 10:00 PM - Night mode");
  
  // Weekend morning: Set day mode at 8:00 AM
  uint8_t weekend = 0x41;  // Saturday and Sunday (bits 0 and 6)
  scheduler.addTimeRule(8, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_DAY);
  Serial.println("  ✓ Weekend 8:00 AM - Day mode");
  
  // Weekend evening: Set night mode at 11:00 PM
  scheduler.addTimeRule(23, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_NIGHT);
  Serial.println("  ✓ Weekend 11:00 PM - Night mode");
  
  // Temperature-based rule: Enable eco mode if outdoor temp > 15°C
  // Assuming outdoor temp is on sensor index 2
  scheduler.addTemperatureRule(2, 15.0, true, ACTION_ENABLE_ECO);
  Serial.println("  ✓ Auto eco mode when outdoor > 15°C");
  
  // Temperature-based rule: Disable eco mode if outdoor temp < 10°C
  scheduler.addTemperatureRule(2, 10.0, false, ACTION_DISABLE_ECO);
  Serial.println("  ✓ Disable eco mode when outdoor < 10°C");
  
  Serial.print("\nTotal rules configured: ");
  Serial.println(scheduler.getRuleCount());
}

// ============================================================================
// Time Management
// ============================================================================

void updateSchedulerTime() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  
  // Update every 10 seconds
  if (now - lastUpdate >= 10000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      scheduler.setCurrentTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_wday);
    }
    lastUpdate = now;
  }
}

// ============================================================================
// Command Handler
// ============================================================================

void handleCommand(char cmd) {
  switch (cmd) {
    case 's':
    case 'S':
      printStatus();
      break;
    
    case 'h':
    case 'H':
      printHelp();
      break;
    
    case 'l':
    case 'L':
      printLogStatistics();
      break;
    
    case 'e':
    case 'E':
      exportData();
      break;
    
    case 'r':
    case 'R':
      printScheduleRules();
      break;
    
    case 'c':
    case 'C':
      logger.clear();
      Serial.println("✓ Data log cleared");
      break;
    
    case 'p':
    case 'P':
      if (logger.isPaused()) {
        logger.resume();
        Serial.println("✓ Data logging resumed");
      } else {
        logger.pause();
        Serial.println("✓ Data logging paused");
      }
      break;
    
    default:
      Serial.println("Unknown command. Press 'h' for help.");
  }
}

// ============================================================================
// Display Functions
// ============================================================================

void printHelp() {
  Serial.println("\n=== Commands ===");
  Serial.println("  s - Show status");
  Serial.println("  h - Show this help");
  Serial.println("  l - Show log statistics");
  Serial.println("  e - Export data (last 1 hour)");
  Serial.println("  r - Show schedule rules");
  Serial.println("  c - Clear data log");
  Serial.println("  p - Pause/resume logging");
  Serial.println("================\n");
}

void printStatus() {
  Serial.println("\n=== System Status ===");
  
  // Time
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.print("Time: ");
    Serial.printf("%02d:%02d:%02d ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    Serial.println(days[timeinfo.tm_wday]);
  }
  
  // Decoder
  Serial.print("Decoder: ");
  Serial.println(vbus.isReady() ? "Ready" : "Waiting...");
  
  if (vbus.isReady()) {
    // Current temperatures
    Serial.print("Boiler: ");
    Serial.print(vbus.getKMBusBoilerTemp(), 1);
    Serial.println("°C");
    
    Serial.print("Outdoor: ");
    Serial.print(vbus.getKMBusOutdoorTemp(), 1);
    Serial.println("°C");
    
    Serial.print("Setpoint: ");
    Serial.print(vbus.getKMBusSetpointTemp(), 1);
    Serial.println("°C");
    
    // Operating mode
    Serial.print("Mode: ");
    uint8_t mode = vbus.getKMBusMode();
    switch (mode) {
      case KMBUS_MODE_DAY: Serial.println("Day"); break;
      case KMBUS_MODE_NIGHT: Serial.println("Night"); break;
      case KMBUS_MODE_ECO: Serial.println("Eco"); break;
      default: Serial.println("Unknown");
    }
  }
  
  // Data logger
  Serial.print("\nData Points: ");
  Serial.print(logger.getDataPointCount());
  Serial.print(" / ");
  Serial.println(LOG_BUFFER_SIZE);
  Serial.print("Logging: ");
  Serial.println(logger.isPaused() ? "Paused" : "Active");
  
  // Scheduler
  Serial.print("\nActive Rules: ");
  Serial.print(scheduler.getActiveRuleCount());
  Serial.print(" / ");
  Serial.println(scheduler.getRuleCount());
  
  Serial.println("====================\n");
}

void printLogStatistics() {
  Serial.println("\n=== Log Statistics (Last 24h) ===");
  
  DataStats stats = logger.getStatisticsLastHours(24);
  
  Serial.println("Temperatures:");
  for (uint8_t i = 0; i < 3; i++) {
    Serial.print("  Sensor ");
    Serial.print(i);
    Serial.print(": Min=");
    Serial.print(stats.tempMin[i], 1);
    Serial.print("°C, Max=");
    Serial.print(stats.tempMax[i], 1);
    Serial.print("°C, Avg=");
    Serial.print(stats.tempAvg[i], 1);
    Serial.println("°C");
  }
  
  Serial.println("\nRuntime:");
  for (uint8_t i = 0; i < 2; i++) {
    Serial.print("  Pump ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(stats.pumpRuntime[i] / 3600);
    Serial.println(" hours");
  }
  
  Serial.print("\nTotal Heat: ");
  Serial.print(stats.totalHeat);
  Serial.println(" Wh");
  
  Serial.println("=================================\n");
}

void printScheduleRules() {
  Serial.println("\n=== Schedule Rules ===");
  
  for (uint8_t i = 0; i < scheduler.getRuleCount(); i++) {
    ScheduleRule* rule = scheduler.getRule(i + 1);
    if (rule) {
      Serial.print("Rule ");
      Serial.print(rule->id);
      Serial.print(": ");
      
      if (rule->type == RULE_TIME_BASED) {
        Serial.printf("Time %02d:%02d - ", rule->timeSchedule.hour, rule->timeSchedule.minute);
      } else if (rule->type == RULE_TEMPERATURE_BASED) {
        Serial.print("Temp sensor ");
        Serial.print(rule->tempCondition.sensorIndex);
        Serial.print(rule->tempCondition.aboveThreshold ? " > " : " < ");
        Serial.print(rule->tempCondition.threshold, 1);
        Serial.print("°C - ");
      }
      
      Serial.print(rule->enabled ? "Enabled" : "Disabled");
      Serial.println();
    }
  }
  
  Serial.println("======================\n");
}

void exportData() {
  Serial.println("\n=== Exporting Data (Last 1 hour) ===");
  
  uint32_t now = millis() / 1000;
  uint32_t startTime = now - 3600;  // 1 hour ago
  
  String csv = logger.exportCSV(startTime, now);
  
  Serial.println(csv);
  Serial.println("====================================\n");
}
