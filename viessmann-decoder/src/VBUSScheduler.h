/*
 * Viessmann Multi-Protocol Library - Scheduler
 * Provides advanced scheduling and automation rules
 */

#pragma once
#ifndef VBUSScheduler_h
#define VBUSScheduler_h

#include <Arduino.h>
#include "vbusdecoder.h"

// Rule types
enum RuleType {
  RULE_TIME_BASED,           // Time-based trigger (e.g., daily at 6:00)
  RULE_TEMPERATURE_BASED,    // Temperature threshold trigger
  RULE_CONDITION_BASED       // Complex condition with multiple factors
};

// Action types
enum ActionType {
  ACTION_SET_MODE,           // Set operating mode
  ACTION_SET_SETPOINT,       // Set temperature setpoint
  ACTION_ENABLE_ECO,         // Enable eco mode
  ACTION_DISABLE_ECO,        // Disable eco mode
  ACTION_ENABLE_PARTY,       // Enable party mode
  ACTION_DISABLE_PARTY,      // Disable party mode
  ACTION_CALLBACK            // Call user-defined callback
};

// Time-based schedule
struct TimeSchedule {
  uint8_t hour;              // Hour (0-23)
  uint8_t minute;            // Minute (0-59)
  uint8_t daysOfWeek;        // Bitmap: bit 0=Sunday, bit 1=Monday, etc.
  bool enabled;              // Is this schedule enabled?
};

// Temperature-based condition
struct TemperatureCondition {
  uint8_t sensorIndex;       // Which temperature sensor to monitor
  float threshold;           // Temperature threshold
  bool aboveThreshold;       // Trigger when above (true) or below (false)
};

// Schedule rule
struct ScheduleRule {
  uint8_t id;                // Unique rule ID
  RuleType type;             // Type of rule
  ActionType action;         // Action to perform
  bool enabled;              // Is this rule enabled?
  uint8_t priority;          // Priority (higher = more important)
  
  // Type-specific data
  TimeSchedule timeSchedule;
  TemperatureCondition tempCondition;
  
  // Action parameters
  uint8_t actionValue1;      // First parameter (e.g., mode, circuit)
  float actionValue2;        // Second parameter (e.g., temperature)
  
  // Callback for custom actions
  void (*callback)(VBUSDecoder*);
  
  // State tracking
  uint32_t lastTriggered;    // When was this rule last triggered
  bool wasActive;            // Was the condition active last check
};

class VBUSScheduler {
  public:
    VBUSScheduler(VBUSDecoder* decoder, uint8_t maxRules = 16);
    ~VBUSScheduler();
    
    // Configuration
    void begin();
    void setCurrentTime(uint8_t hour, uint8_t minute, uint8_t dayOfWeek);
    
    // Rule management
    uint8_t addTimeRule(uint8_t hour, uint8_t minute, uint8_t daysOfWeek, 
                        ActionType action, uint8_t actionValue1 = 0, float actionValue2 = 0);
    uint8_t addTemperatureRule(uint8_t sensorIndex, float threshold, bool aboveThreshold,
                               ActionType action, uint8_t actionValue1 = 0, float actionValue2 = 0);
    uint8_t addCallbackRule(RuleType type, void (*callback)(VBUSDecoder*));
    bool removeRule(uint8_t ruleId);
    bool enableRule(uint8_t ruleId, bool enable = true);
    bool disableRule(uint8_t ruleId);
    void clearAllRules();
    
    // Rule queries
    uint8_t getRuleCount();
    ScheduleRule* getRule(uint8_t ruleId);
    
    // Execution
    void loop();
    void checkRules();
    void executeRule(uint8_t ruleId);
    
    // Status
    uint8_t getActiveRuleCount();
    uint32_t getLastExecutionTime();
    
  private:
    VBUSDecoder* _decoder;
    ScheduleRule* _rules;
    uint8_t _maxRules;
    uint8_t _ruleCount;
    uint8_t _nextRuleId;
    
    // Current time (must be updated externally)
    uint8_t _currentHour;
    uint8_t _currentMinute;
    uint8_t _currentDayOfWeek;
    uint32_t _lastCheck;
    uint32_t _lastExecution;
    
    // Helper methods
    bool _checkTimeRule(const ScheduleRule& rule);
    bool _checkTemperatureRule(const ScheduleRule& rule);
    void _executeAction(const ScheduleRule& rule);
    int8_t _findRuleIndex(uint8_t ruleId);
};

#endif
