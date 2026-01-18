# Scheduler Guide

This guide explains how to use the advanced scheduling system to automate your heating system based on time and conditions.

## Overview

The VBUSScheduler provides powerful automation capabilities:

- **Time-based rules** - Execute actions at specific times
- **Temperature-based rules** - React to temperature changes
- **Condition-based rules** - Complex multi-factor triggers (future)
- **Priority system** - Control execution order
- **Callback support** - Custom actions
- **Multiple circuits** - Independent control

## Quick Start

```cpp
#include "vbusdecoder.h"
#include "VBUSScheduler.h"

VBUSDecoder vbus(&Serial2);
VBUSScheduler scheduler(&vbus, 16);  // Up to 16 rules

void setup() {
  vbus.begin(PROTOCOL_KM);
  scheduler.begin();
  
  // Add a simple time-based rule
  // Every day at 6:00 AM, set day mode
  uint8_t everyday = 0x7F;  // All 7 days
  scheduler.addTimeRule(6, 0, everyday, ACTION_SET_MODE, KMBUS_MODE_DAY);
  
  // Update time (typically from NTP or RTC)
  scheduler.setCurrentTime(8, 30, 1);  // 8:30 AM, Monday
}

void loop() {
  vbus.loop();
  scheduler.loop();  // Checks and executes rules
}
```

## Rule Types

### 1. Time-Based Rules

Execute actions at specific times on specific days.

```cpp
uint8_t addTimeRule(uint8_t hour, uint8_t minute, uint8_t daysOfWeek,
                    ActionType action, uint8_t actionValue1 = 0, float actionValue2 = 0);
```

**Parameters:**
- `hour`: Hour (0-23)
- `minute`: Minute (0-59)
- `daysOfWeek`: Bitmap of days (bit 0=Sunday, bit 6=Saturday)
- `action`: Action to perform
- `actionValue1`, `actionValue2`: Action-specific parameters

**Day Bitmap:**
```cpp
0x01 = Sunday     (0b00000001)
0x02 = Monday     (0b00000010)
0x04 = Tuesday    (0b00000100)
0x08 = Wednesday  (0b00001000)
0x10 = Thursday   (0b00010000)
0x20 = Friday     (0b00100000)
0x40 = Saturday   (0b01000000)

// Combinations
0x3E = Weekdays (Mon-Fri)  (0b00111110)
0x41 = Weekend (Sun+Sat)    (0b01000001)
0x7F = Every day            (0b01111111)
```

**Examples:**

```cpp
// Weekday morning: Day mode at 6:00 AM
uint8_t weekdays = 0x3E;
scheduler.addTimeRule(6, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_DAY);

// Weekday evening: Night mode at 10:00 PM
scheduler.addTimeRule(22, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_NIGHT);

// Weekend morning: Day mode at 8:00 AM
uint8_t weekend = 0x41;
scheduler.addTimeRule(8, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_DAY);

// Every day at noon: Set setpoint to 21°C
scheduler.addTimeRule(12, 0, 0x7F, ACTION_SET_SETPOINT, 0, 21.0);
```

### 2. Temperature-Based Rules

Execute actions when temperature crosses a threshold.

```cpp
uint8_t addTemperatureRule(uint8_t sensorIndex, float threshold, bool aboveThreshold,
                          ActionType action, uint8_t actionValue1 = 0, float actionValue2 = 0);
```

**Parameters:**
- `sensorIndex`: Temperature sensor to monitor (0-31)
- `threshold`: Temperature threshold in °C
- `aboveThreshold`: `true` = trigger when above, `false` = when below
- `action`: Action to perform
- `actionValue1`, `actionValue2`: Action-specific parameters

**Examples:**

```cpp
// Enable eco mode when outdoor temp > 15°C
scheduler.addTemperatureRule(2, 15.0, true, ACTION_ENABLE_ECO);

// Disable eco mode when outdoor temp < 10°C
scheduler.addTemperatureRule(2, 10.0, false, ACTION_DISABLE_ECO);

// Boost heating when indoor temp < 18°C
scheduler.addTemperatureRule(0, 18.0, false, ACTION_SET_SETPOINT, 0, 23.0);

// Reduce heating when indoor temp > 24°C
scheduler.addTemperatureRule(0, 24.0, true, ACTION_SET_SETPOINT, 0, 20.0);
```

### 3. Callback Rules

Execute custom code when conditions are met.

```cpp
uint8_t addCallbackRule(RuleType type, void (*callback)(VBUSDecoder*));
```

**Example:**

```cpp
void customAction(VBUSDecoder* vbus) {
  Serial.println("Custom action triggered!");
  
  // Read current state
  float temp = vbus->getKMBusBoilerTemp();
  
  // Make decision
  if (temp > 80.0) {
    vbus->setKMBusEcoMode(true);
  }
}

void setup() {
  // Add callback rule
  scheduler.addCallbackRule(RULE_TEMPERATURE_BASED, customAction);
}
```

## Action Types

### ACTION_SET_MODE

Change operating mode.

```cpp
// Parameters: mode value
scheduler.addTimeRule(6, 0, 0x7F, ACTION_SET_MODE, KMBUS_MODE_DAY);
```

**Mode values:**
- `KMBUS_MODE_OFF` - Off/standby
- `KMBUS_MODE_NIGHT` - Night/reduced
- `KMBUS_MODE_DAY` - Day/comfort
- `KMBUS_MODE_ECO` - Economy
- `KMBUS_MODE_PARTY` - Party

### ACTION_SET_SETPOINT

Change temperature setpoint.

```cpp
// Parameters: circuit number, temperature
scheduler.addTimeRule(6, 0, 0x7F, ACTION_SET_SETPOINT, 0, 21.5);
```

### ACTION_ENABLE_ECO / ACTION_DISABLE_ECO

Toggle eco mode.

```cpp
// Enable eco mode
scheduler.addTimeRule(23, 0, 0x7F, ACTION_ENABLE_ECO);

// Disable eco mode
scheduler.addTimeRule(6, 0, 0x7F, ACTION_DISABLE_ECO);
```

### ACTION_ENABLE_PARTY / ACTION_DISABLE_PARTY

Toggle party mode.

```cpp
// Enable party mode on Friday evening
scheduler.addTimeRule(18, 0, 0x20, ACTION_ENABLE_PARTY);

// Disable party mode on Sunday evening
scheduler.addTimeRule(22, 0, 0x01, ACTION_DISABLE_PARTY);
```

### ACTION_CALLBACK

Execute custom function.

```cpp
scheduler.addCallbackRule(RULE_TIME_BASED, myCustomFunction);
```

## Rule Management

### Enable/Disable Rules

```cpp
// Get rule ID when creating
uint8_t ruleId = scheduler.addTimeRule(6, 0, 0x7F, ACTION_SET_MODE, KMBUS_MODE_DAY);

// Disable temporarily
scheduler.disableRule(ruleId);

// Re-enable
scheduler.enableRule(ruleId);
```

### Remove Rules

```cpp
// Remove specific rule
scheduler.removeRule(ruleId);

// Clear all rules
scheduler.clearAllRules();
```

### Query Rules

```cpp
// Get total rule count
uint8_t total = scheduler.getRuleCount();

// Get active rule count
uint8_t active = scheduler.getActiveRuleCount();

// Get specific rule
ScheduleRule* rule = scheduler.getRule(ruleId);
if (rule) {
  Serial.print("Rule enabled: ");
  Serial.println(rule->enabled);
}
```

## Time Management

The scheduler needs current time to function. Update regularly:

### With NTP (ESP32/ESP8266)

```cpp
#include <time.h>

void setup() {
  // Configure NTP
  configTime(0, 0, "pool.ntp.org");
  
  // ... other setup ...
}

void loop() {
  // Update scheduler time every 10 seconds
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= 10000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      scheduler.setCurrentTime(
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_wday
      );
    }
    lastUpdate = millis();
  }
  
  vbus.loop();
  scheduler.loop();
}
```

### With RTC Module

```cpp
#include <RTClib.h>

RTC_DS3231 rtc;

void setup() {
  rtc.begin();
  // ... other setup ...
}

void loop() {
  DateTime now = rtc.now();
  scheduler.setCurrentTime(now.hour(), now.minute(), now.dayOfWeek());
  
  vbus.loop();
  scheduler.loop();
}
```

### Manual Time Setting

```cpp
// Set time manually (hour, minute, day of week)
scheduler.setCurrentTime(14, 30, 3);  // 2:30 PM, Wednesday
```

## Complete Examples

### Basic Weekly Schedule

```cpp
void setupBasicSchedule() {
  // Weekday schedule
  uint8_t weekdays = 0x3E;
  
  // Morning
  scheduler.addTimeRule(6, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_DAY);
  scheduler.addTimeRule(6, 0, weekdays, ACTION_SET_SETPOINT, 0, 22.0);
  
  // Daytime eco
  scheduler.addTimeRule(9, 0, weekdays, ACTION_ENABLE_ECO);
  
  // Evening comfort
  scheduler.addTimeRule(17, 0, weekdays, ACTION_DISABLE_ECO);
  scheduler.addTimeRule(17, 0, weekdays, ACTION_SET_SETPOINT, 0, 23.0);
  
  // Night
  scheduler.addTimeRule(22, 0, weekdays, ACTION_SET_MODE, KMBUS_MODE_NIGHT);
  scheduler.addTimeRule(22, 0, weekdays, ACTION_SET_SETPOINT, 0, 18.0);
  
  // Weekend schedule
  uint8_t weekend = 0x41;
  
  scheduler.addTimeRule(8, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_DAY);
  scheduler.addTimeRule(8, 0, weekend, ACTION_SET_SETPOINT, 0, 22.0);
  scheduler.addTimeRule(23, 0, weekend, ACTION_SET_MODE, KMBUS_MODE_NIGHT);
}
```

### Weather-Responsive Heating

```cpp
void setupWeatherResponsive() {
  // Temperature-based adjustments
  // Outdoor sensor on index 2
  
  // Cold weather: boost heating
  scheduler.addTemperatureRule(2, 0.0, false, ACTION_SET_SETPOINT, 0, 23.0);
  
  // Mild weather: normal heating
  scheduler.addTemperatureRule(2, 10.0, true, ACTION_SET_SETPOINT, 0, 21.0);
  
  // Warm weather: reduce heating
  scheduler.addTemperatureRule(2, 15.0, true, ACTION_ENABLE_ECO);
  scheduler.addTemperatureRule(2, 15.0, true, ACTION_SET_SETPOINT, 0, 19.0);
  
  // Very warm: minimal heating
  scheduler.addTemperatureRule(2, 20.0, true, ACTION_SET_SETPOINT, 0, 17.0);
}
```

### Multi-Zone Control

```cpp
void setupMultiZone() {
  // Living room (circuit 0) - main schedule
  scheduler.addTimeRule(6, 0, 0x7F, ACTION_SET_SETPOINT, 0, 22.0);
  scheduler.addTimeRule(22, 0, 0x7F, ACTION_SET_SETPOINT, 0, 18.0);
  
  // Bedrooms (circuit 1) - cooler
  scheduler.addTimeRule(6, 0, 0x7F, ACTION_SET_SETPOINT, 1, 19.0);
  scheduler.addTimeRule(22, 0, 0x7F, ACTION_SET_SETPOINT, 1, 16.0);
  
  // Bathroom (circuit 2) - warmer in morning
  scheduler.addTimeRule(6, 0, 0x7F, ACTION_SET_SETPOINT, 2, 24.0);
  scheduler.addTimeRule(8, 0, 0x7F, ACTION_SET_SETPOINT, 2, 20.0);
  scheduler.addTimeRule(22, 0, 0x7F, ACTION_SET_SETPOINT, 2, 18.0);
}
```

## Troubleshooting

### Rules Not Triggering

1. **Check time is set:**
   ```cpp
   if (scheduler.getLastExecutionTime() == 0) {
     Serial.println("No rules have executed");
   }
   ```

2. **Verify rule is enabled:**
   ```cpp
   ScheduleRule* rule = scheduler.getRule(ruleId);
   if (rule && !rule->enabled) {
     Serial.println("Rule is disabled");
   }
   ```

3. **Check day of week bitmap:**
   ```cpp
   // Debug current time
   Serial.print("Hour: ");
   Serial.print(hour);
   Serial.print(", Day: ");
   Serial.println(dayOfWeek);
   ```

### Temperature Rules Not Working

1. **Verify sensor index:**
   ```cpp
   float temp = vbus.getTemp(sensorIndex);
   Serial.print("Sensor ");
   Serial.print(sensorIndex);
   Serial.print(": ");
   Serial.println(temp);
   ```

2. **Check threshold logic:**
   ```cpp
   // Remember: rules trigger on edge (change from inactive to active)
   // Not continuously while active
   ```

## Best Practices

1. **Start simple** - Add one rule at a time
2. **Test thoroughly** - Verify each rule works before adding more
3. **Use priorities** - Set priorities for conflicting rules
4. **Log executions** - Track when rules fire
5. **Provide overrides** - Allow manual control
6. **Set safe defaults** - Fallback if automation fails

## Integration with Other Features

### With Data Logger

```cpp
// Log when rules execute
void ruleCallback(VBUSDecoder* vbus) {
  logger.logNow();  // Force immediate log
  Serial.println("Rule executed, data logged");
}
```

### With MQTT

```cpp
// Publish when rules change settings
void publishRuleExecution(uint8_t ruleId) {
  String topic = "viessmann/scheduler/executed";
  String payload = String(ruleId);
  mqttClient.publish(topic.c_str(), payload.c_str());
}
```

## Further Reading

- [Control Commands Guide](CONTROL_COMMANDS.md)
- [MQTT Setup Guide](MQTT_SETUP.md)
- [Data Logger Guide](DATA_LOGGER.md)

## License

Part of the Viessmann Multi-Protocol Library. See main LICENSE file.
