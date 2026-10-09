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
  #define debugSerial Serial
  #include <time.h>
  HardwareSerial vbusSerial(2);
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <time.h>
  HardwareSerial& vbusSerial = Serial;
  #define debugSerial Serial1  // TX-only diagnostics on GPIO2, separate from the bus UART.
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
  debugSerial.begin(115200);
  delay(1000);
  debugSerial.println("\n\nViessmann Multi-Protocol Library - Advanced Automation Example");
  debugSerial.println("===============================================================");
  
  // Connect to WiFi for NTP
  debugSerial.print("Connecting to WiFi: ");
  debugSerial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    debugSerial.print(".");
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    debugSerial.println("\nWiFi connected!");
    debugSerial.print("IP address: ");
    debugSerial.println(WiFi.localIP());
    
    // Configure NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    debugSerial.println("NTP time sync configured");
  } else {
    debugSerial.println("\nWiFi connection failed. Continuing without time sync.");
  }
  
  // Initialize decoder
  debugSerial.println("\nInitializing KM-Bus decoder...");
  vbusSerial.begin(BAUD_RATE);
  vbus.begin(PROTOCOL);
  
  // Initialize data logger
  debugSerial.println("Initializing data logger...");
  logger.begin();
  logger.setLogInterval(LOG_INTERVAL);
  debugSerial.print("Buffer size: ");
  debugSerial.print(LOG_BUFFER_SIZE);
  debugSerial.print(" data points (");
  debugSerial.print((LOG_BUFFER_SIZE * LOG_INTERVAL) / 3600);
  debugSerial.println(" hours)");
  
  // Initialize scheduler
  debugSerial.println("Initializing scheduler...");
  scheduler.begin();
  
  // Configure heating schedule
  setupSchedule();
  
  debugSerial.println("\n===============================================================");
  debugSerial.println("Setup complete. Starting automation...");
  debugSerial.println("===============================================================\n");
  
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
#if defined(ESP32)
  if (debugSerial.available()) {
    handleCommand(debugSerial.read());
  }
#endif
}

// ============================================================================
// Schedule Configuration
// ============================================================================

void setupSchedule() {
  debugSerial.println("\nConfiguring heating schedule:");
  
  // Weekday morning: Set day mode at 6:00 AM
  uint8_t weekdays = 0x3E;  // Monday to Friday (bits 1-5)
  scheduler.addTimeRule(6, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_DAY);
  debugSerial.println("  ✓ Weekdays 6:00 AM - Day mode");
  
  // Weekday evening: Set night mode at 10:00 PM
  scheduler.addTimeRule(22, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_NIGHT);
  debugSerial.println("  ✓ Weekdays 10:00 PM - Night mode");
  
  // Weekend morning: Set day mode at 8:00 AM
  uint8_t weekend = 0x41;  // Saturday and Sunday (bits 0 and 6)
  scheduler.addTimeRule(8, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_DAY);
  debugSerial.println("  ✓ Weekend 8:00 AM - Day mode");
  
  // Weekend evening: Set night mode at 11:00 PM
  scheduler.addTimeRule(23, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_NIGHT);
  debugSerial.println("  ✓ Weekend 11:00 PM - Night mode");
  
  // Temperature-based rule: Enable eco mode if outdoor temp > 15°C
  // Assuming outdoor temp is on sensor index 2
  scheduler.addTemperatureRule(2, 15.0, true, ACTION_ENABLE_ECO);
  debugSerial.println("  ✓ Auto eco mode when outdoor > 15°C");
  
  // Temperature-based rule: Disable eco mode if outdoor temp < 10°C
  scheduler.addTemperatureRule(2, 10.0, false, ACTION_DISABLE_ECO);
  debugSerial.println("  ✓ Disable eco mode when outdoor < 10°C");
  
  debugSerial.print("\nTotal rules configured: ");
  debugSerial.println(scheduler.getRuleCount());
}

// ============================================================================
// Time Management
// ============================================================================

bool readLocalTime(struct tm* timeinfo) {
#if defined(ESP8266)
  time_t now = time(nullptr);
  // Match getLocalTime's synchronized-clock check (year later than 2016).
  return localtime_r(&now, timeinfo) != nullptr && timeinfo->tm_year > 116;
#else
  return getLocalTime(timeinfo);
#endif
}

void updateSchedulerTime() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  
  // Update every 10 seconds
  if (now - lastUpdate >= 10000) {
    struct tm timeinfo;
    if (readLocalTime(&timeinfo)) {
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
      debugSerial.println("✓ Data log cleared");
      break;
    
    case 'p':
    case 'P':
      if (logger.isPaused()) {
        logger.resume();
        debugSerial.println("✓ Data logging resumed");
      } else {
        logger.pause();
        debugSerial.println("✓ Data logging paused");
      }
      break;
    
    default:
      debugSerial.println("Unknown command. Press 'h' for help.");
  }
}

// ============================================================================
// Display Functions
// ============================================================================

void printHelp() {
#if defined(ESP32)
  debugSerial.println("\n=== Commands ===");
  debugSerial.println("  s - Show status");
  debugSerial.println("  h - Show this help");
  debugSerial.println("  l - Show log statistics");
  debugSerial.println("  e - Export data (last 1 hour)");
  debugSerial.println("  r - Show schedule rules");
  debugSerial.println("  c - Clear data log");
  debugSerial.println("  p - Pause/resume logging");
  debugSerial.println("================\n");
#else
  debugSerial.println("ESP8266 diagnostics are TX-only on GPIO2; console input is disabled.");
#endif
}

void printStatus() {
  debugSerial.println("\n=== System Status ===");
  
  // Time
  struct tm timeinfo;
  if (readLocalTime(&timeinfo)) {
    debugSerial.print("Time: ");
    debugSerial.printf("%02d:%02d:%02d ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    debugSerial.println(days[timeinfo.tm_wday]);
  }
  
  // Decoder
  debugSerial.print("Decoder: ");
  debugSerial.println(vbus.isReady() ? "Ready" : "Waiting...");
  
  if (vbus.isReady()) {
    // Current temperatures
    debugSerial.print("Boiler: ");
    debugSerial.print(vbus.getKMBusBoilerTemp(), 1);
    debugSerial.println("°C");
    
    debugSerial.print("Outdoor: ");
    debugSerial.print(vbus.getKMBusOutdoorTemp(), 1);
    debugSerial.println("°C");
    
    debugSerial.print("Setpoint: ");
    debugSerial.print(vbus.getKMBusSetpointTemp(), 1);
    debugSerial.println("°C");
    
    // Operating mode
    debugSerial.print("Mode: ");
    uint8_t mode = vbus.getKMBusMode();
    switch (mode) {
      case KMBUS_MODE_DAY: debugSerial.println("Day"); break;
      case KMBUS_MODE_NIGHT: debugSerial.println("Night"); break;
      case KMBUS_MODE_ECO: debugSerial.println("Eco"); break;
      default: debugSerial.println("Unknown");
    }
  }
  
  // Data logger
  debugSerial.print("\nData Points: ");
  debugSerial.print(logger.getDataPointCount());
  debugSerial.print(" / ");
  debugSerial.println(LOG_BUFFER_SIZE);
  debugSerial.print("Logging: ");
  debugSerial.println(logger.isPaused() ? "Paused" : "Active");
  
  // Scheduler
  debugSerial.print("\nActive Rules: ");
  debugSerial.print(scheduler.getActiveRuleCount());
  debugSerial.print(" / ");
  debugSerial.println(scheduler.getRuleCount());
  
  debugSerial.println("====================\n");
}

void printLogStatistics() {
  debugSerial.println("\n=== Log Statistics (Last 24h) ===");
  
  DataStats stats = logger.getStatisticsLastHours(24);
  
  debugSerial.println("Temperatures:");
  for (uint8_t i = 0; i < 3; i++) {
    debugSerial.print("  Sensor ");
    debugSerial.print(i);
    debugSerial.print(": Min=");
    debugSerial.print(stats.tempMin[i], 1);
    debugSerial.print("°C, Max=");
    debugSerial.print(stats.tempMax[i], 1);
    debugSerial.print("°C, Avg=");
    debugSerial.print(stats.tempAvg[i], 1);
    debugSerial.println("°C");
  }
  
  debugSerial.println("\nRuntime:");
  for (uint8_t i = 0; i < 2; i++) {
    debugSerial.print("  Pump ");
    debugSerial.print(i);
    debugSerial.print(": ");
    debugSerial.print(stats.pumpRuntime[i] / 3600);
    debugSerial.println(" hours");
  }
  
  debugSerial.print("\nTotal Heat: ");
  debugSerial.print(stats.totalHeat);
  debugSerial.println(" Wh");
  
  debugSerial.println("=================================\n");
}

void printScheduleRules() {
  debugSerial.println("\n=== Schedule Rules ===");
  
  for (uint8_t i = 0; i < scheduler.getRuleCount(); i++) {
    ScheduleRule* rule = scheduler.getRule(i + 1);
    if (rule) {
      debugSerial.print("Rule ");
      debugSerial.print(rule->id);
      debugSerial.print(": ");
      
      if (rule->type == RULE_TIME_BASED) {
        debugSerial.printf("Time %02d:%02d - ", rule->timeSchedule.hour, rule->timeSchedule.minute);
      } else if (rule->type == RULE_TEMPERATURE_BASED) {
        debugSerial.print("Temp sensor ");
        debugSerial.print(rule->tempCondition.sensorIndex);
        debugSerial.print(rule->tempCondition.aboveThreshold ? " > " : " < ");
        debugSerial.print(rule->tempCondition.threshold, 1);
        debugSerial.print("°C - ");
      }
      
      debugSerial.print(rule->enabled ? "Enabled" : "Disabled");
      debugSerial.println();
    }
  }
  
  debugSerial.println("======================\n");
}

void exportData() {
  debugSerial.println("\n=== Exporting Data (Last 1 hour) ===");
  
  uint32_t now = millis() / 1000;
  uint32_t startTime = now >= 3600 ? now - 3600 : 0;  // 1 hour ago
  
  String csv = logger.exportCSV(startTime, now);
  
  debugSerial.println(csv);
  debugSerial.println("====================================\n");
}
