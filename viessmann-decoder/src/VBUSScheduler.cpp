/*
 * Viessmann Multi-Protocol Library - Scheduler Implementation
 */

#include "VBUSScheduler.h"

VBUSScheduler::VBUSScheduler(VBUSDecoder* decoder, uint8_t maxRules) :
  _decoder(decoder),
  _maxRules(maxRules),
  _ruleCount(0),
  _nextRuleId(1),
  _currentHour(0),
  _currentMinute(0),
  _currentDayOfWeek(0),
  _lastCheck(0),
  _lastExecution(0)
{
  _rules = new ScheduleRule[_maxRules];
  memset(_rules, 0, sizeof(ScheduleRule) * _maxRules);
}

VBUSScheduler::~VBUSScheduler() {
  delete[] _rules;
}

void VBUSScheduler::begin() {
  _lastCheck = millis();
}

void VBUSScheduler::setCurrentTime(uint8_t hour, uint8_t minute, uint8_t dayOfWeek) {
  _currentHour = hour;
  _currentMinute = minute;
  _currentDayOfWeek = dayOfWeek;
}

uint8_t VBUSScheduler::addTimeRule(uint8_t hour, uint8_t minute, uint8_t daysOfWeek, 
                                   ActionType action, uint8_t actionValue1, float actionValue2) {
  if (_ruleCount >= _maxRules) return 0;
  
  ScheduleRule& rule = _rules[_ruleCount++];
  rule.id = _nextRuleId++;
  rule.type = RULE_TIME_BASED;
  rule.action = action;
  rule.enabled = true;
  rule.priority = 50;  // Default priority
  
  rule.timeSchedule.hour = hour;
  rule.timeSchedule.minute = minute;
  rule.timeSchedule.daysOfWeek = daysOfWeek;
  rule.timeSchedule.enabled = true;
  
  rule.actionValue1 = actionValue1;
  rule.actionValue2 = actionValue2;
  rule.callback = nullptr;
  rule.lastTriggered = 0;
  rule.wasActive = false;
  
  return rule.id;
}

uint8_t VBUSScheduler::addTemperatureRule(uint8_t sensorIndex, float threshold, bool aboveThreshold,
                                          ActionType action, uint8_t actionValue1, float actionValue2) {
  if (_ruleCount >= _maxRules) return 0;
  
  ScheduleRule& rule = _rules[_ruleCount++];
  rule.id = _nextRuleId++;
  rule.type = RULE_TEMPERATURE_BASED;
  rule.action = action;
  rule.enabled = true;
  rule.priority = 50;  // Default priority
  
  rule.tempCondition.sensorIndex = sensorIndex;
  rule.tempCondition.threshold = threshold;
  rule.tempCondition.aboveThreshold = aboveThreshold;
  
  rule.actionValue1 = actionValue1;
  rule.actionValue2 = actionValue2;
  rule.callback = nullptr;
  rule.lastTriggered = 0;
  rule.wasActive = false;
  
  return rule.id;
}

uint8_t VBUSScheduler::addCallbackRule(RuleType type, void (*callback)(VBUSDecoder*)) {
  if (_ruleCount >= _maxRules) return 0;
  if (callback == nullptr) return 0;
  
  ScheduleRule& rule = _rules[_ruleCount++];
  rule.id = _nextRuleId++;
  rule.type = type;
  rule.action = ACTION_CALLBACK;
  rule.enabled = true;
  rule.priority = 50;
  rule.callback = callback;
  rule.lastTriggered = 0;
  rule.wasActive = false;
  
  return rule.id;
}

bool VBUSScheduler::removeRule(uint8_t ruleId) {
  int8_t index = _findRuleIndex(ruleId);
  if (index < 0) return false;
  
  // Shift remaining rules
  for (uint8_t i = index; i < _ruleCount - 1; i++) {
    _rules[i] = _rules[i + 1];
  }
  _ruleCount--;
  
  return true;
}

bool VBUSScheduler::enableRule(uint8_t ruleId, bool enable) {
  int8_t index = _findRuleIndex(ruleId);
  if (index < 0) return false;
  
  _rules[index].enabled = enable;
  return true;
}

bool VBUSScheduler::disableRule(uint8_t ruleId) {
  return enableRule(ruleId, false);
}

void VBUSScheduler::clearAllRules() {
  _ruleCount = 0;
  memset(_rules, 0, sizeof(ScheduleRule) * _maxRules);
}

uint8_t VBUSScheduler::getRuleCount() {
  return _ruleCount;
}

ScheduleRule* VBUSScheduler::getRule(uint8_t ruleId) {
  int8_t index = _findRuleIndex(ruleId);
  if (index < 0) return nullptr;
  return &_rules[index];
}

void VBUSScheduler::loop() {
  uint32_t now = millis();
  
  // Check rules every second
  if (now - _lastCheck >= 1000) {
    checkRules();
    _lastCheck = now;
  }
}

void VBUSScheduler::checkRules() {
  if (!_decoder->isReady()) return;
  
  for (uint8_t i = 0; i < _ruleCount; i++) {
    ScheduleRule& rule = _rules[i];
    if (!rule.enabled) continue;
    
    bool shouldTrigger = false;
    
    switch (rule.type) {
      case RULE_TIME_BASED:
        shouldTrigger = _checkTimeRule(rule);
        break;
      
      case RULE_TEMPERATURE_BASED:
        shouldTrigger = _checkTemperatureRule(rule);
        break;
      
      case RULE_CONDITION_BASED:
        // Complex conditions - future implementation
        break;
    }
    
    // Execute if condition is met and wasn't active before (edge trigger)
    if (shouldTrigger && !rule.wasActive) {
      executeRule(rule.id);
      rule.lastTriggered = millis();
    }
    
    rule.wasActive = shouldTrigger;
  }
}

void VBUSScheduler::executeRule(uint8_t ruleId) {
  int8_t index = _findRuleIndex(ruleId);
  if (index < 0) return;
  
  _executeAction(_rules[index]);
  _lastExecution = millis();
}

uint8_t VBUSScheduler::getActiveRuleCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < _ruleCount; i++) {
    if (_rules[i].enabled) count++;
  }
  return count;
}

uint32_t VBUSScheduler::getLastExecutionTime() {
  return _lastExecution;
}

// Private helper methods

bool VBUSScheduler::_checkTimeRule(const ScheduleRule& rule) {
  if (!rule.timeSchedule.enabled) return false;
  
  // Check if current time matches
  if (rule.timeSchedule.hour != _currentHour) return false;
  if (rule.timeSchedule.minute != _currentMinute) return false;
  
  // Check day of week (bitmap)
  uint8_t dayBit = 1 << _currentDayOfWeek;
  if (!(rule.timeSchedule.daysOfWeek & dayBit)) return false;
  
  return true;
}

bool VBUSScheduler::_checkTemperatureRule(const ScheduleRule& rule) {
  float temp = _decoder->getTemp(rule.tempCondition.sensorIndex);
  
  // Check for invalid temperature
  if (temp < -99.0 || temp > 999.0) return false;
  
  if (rule.tempCondition.aboveThreshold) {
    return temp > rule.tempCondition.threshold;
  } else {
    return temp < rule.tempCondition.threshold;
  }
}

void VBUSScheduler::_executeAction(const ScheduleRule& rule) {
  bool success = false;
  
  switch (rule.action) {
    case ACTION_SET_MODE:
      if (_decoder->getProtocol() == PROTOCOL_KM) {
        success = _decoder->setKMBusMode(rule.actionValue1);
      }
      break;
    
    case ACTION_SET_SETPOINT:
      if (_decoder->getProtocol() == PROTOCOL_KM) {
        success = _decoder->setKMBusSetpoint(rule.actionValue1, rule.actionValue2);
      }
      break;
    
    case ACTION_ENABLE_ECO:
      if (_decoder->getProtocol() == PROTOCOL_KM) {
        success = _decoder->setKMBusEcoMode(true);
      }
      break;
    
    case ACTION_DISABLE_ECO:
      if (_decoder->getProtocol() == PROTOCOL_KM) {
        success = _decoder->setKMBusEcoMode(false);
      }
      break;
    
    case ACTION_ENABLE_PARTY:
      if (_decoder->getProtocol() == PROTOCOL_KM) {
        success = _decoder->setKMBusPartyMode(true);
      }
      break;
    
    case ACTION_DISABLE_PARTY:
      if (_decoder->getProtocol() == PROTOCOL_KM) {
        success = _decoder->setKMBusPartyMode(false);
      }
      break;
    
    case ACTION_CALLBACK:
      if (rule.callback) {
        rule.callback(_decoder);
        success = true;  // Assume callback succeeded
      }
      break;
  }
  
  // Log result (optional, can be disabled to save memory)
  // Users can check success in their own code if needed
  (void)success;  // Suppress unused variable warning
}

int8_t VBUSScheduler::_findRuleIndex(uint8_t ruleId) {
  for (uint8_t i = 0; i < _ruleCount; i++) {
    if (_rules[i].id == ruleId) {
      return i;
    }
  }
  return -1;
}
