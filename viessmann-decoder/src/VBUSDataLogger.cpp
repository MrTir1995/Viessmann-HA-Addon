/*
 * Viessmann Multi-Protocol Library - Data Logger Implementation
 */

#include "VBUSDataLogger.h"

VBUSDataLogger::VBUSDataLogger(VBUSDecoder* decoder, uint16_t bufferSize) :
  _decoder(decoder),
  _bufferSize(bufferSize),
  _writeIndex(0),
  _count(0),
  _logInterval(300),  // 5 minutes default
  _lastLog(0),
  _paused(false)
{
  _buffer = new DataPoint[_bufferSize];
}

VBUSDataLogger::~VBUSDataLogger() {
  delete[] _buffer;
}

void VBUSDataLogger::begin() {
  clear();
  _lastLog = millis();
}

void VBUSDataLogger::setLogInterval(uint32_t intervalSeconds) {
  _logInterval = intervalSeconds;
}

void VBUSDataLogger::setMaxDataPoints(uint16_t maxPoints) {
  if (maxPoints != _bufferSize) {
    delete[] _buffer;
    _bufferSize = maxPoints;
    _buffer = new DataPoint[_bufferSize];
    clear();
  }
}

void VBUSDataLogger::loop() {
  if (_paused) return;
  if (!_decoder->isReady()) return;
  
  uint32_t now = millis();
  if (now - _lastLog >= (_logInterval * 1000)) {
    logNow();
    _lastLog = now;
  }
}

void VBUSDataLogger::logNow() {
  if (!_decoder->isReady()) return;
  
  DataPoint point;
  point.timestamp = millis() / 1000;  // Convert to seconds
  
  // Log temperatures
  uint8_t tempCount = min((uint8_t)8, _decoder->getTempNum());
  for (uint8_t i = 0; i < tempCount; i++) {
    point.temperatures[i] = _decoder->getTemp(i);
  }
  for (uint8_t i = tempCount; i < 8; i++) {
    point.temperatures[i] = -999.0;  // Invalid marker
  }
  
  // Log pump power
  uint8_t pumpCount = min((uint8_t)4, _decoder->getPumpNum());
  for (uint8_t i = 0; i < pumpCount; i++) {
    point.pumps[i] = _decoder->getPump(i);
  }
  for (uint8_t i = pumpCount; i < 4; i++) {
    point.pumps[i] = 0;
  }
  
  // Log relay states
  uint8_t relayCount = min((uint8_t)4, _decoder->getRelayNum());
  for (uint8_t i = 0; i < relayCount; i++) {
    point.relays[i] = _decoder->getRelay(i);
  }
  for (uint8_t i = relayCount; i < 4; i++) {
    point.relays[i] = false;
  }
  
  // Log error mask and heat quantity
  point.errorMask = _decoder->getErrorMask();
  point.heatQuantity = _decoder->getHeatQuantity();
  
  _addDataPoint(point);
}

void VBUSDataLogger::clear() {
  _writeIndex = 0;
  _count = 0;
  memset(_buffer, 0, sizeof(DataPoint) * _bufferSize);
}

void VBUSDataLogger::pause() {
  _paused = true;
}

void VBUSDataLogger::resume() {
  _paused = false;
  _lastLog = millis();
}

bool VBUSDataLogger::isPaused() {
  return _paused;
}

uint16_t VBUSDataLogger::getDataPointCount() {
  return _count;
}

DataPoint* VBUSDataLogger::getDataPoint(uint16_t index) {
  if (index >= _count) return nullptr;
  return &_buffer[_getCircularIndex(index)];
}

DataPoint* VBUSDataLogger::getLatestDataPoint() {
  if (_count == 0) return nullptr;
  uint16_t index = (_writeIndex + _bufferSize - 1) % _bufferSize;
  return &_buffer[index];
}

DataPoint* VBUSDataLogger::getOldestDataPoint() {
  if (_count == 0) return nullptr;
  if (_count < _bufferSize) {
    return &_buffer[0];
  } else {
    return &_buffer[_writeIndex];
  }
}

DataStats VBUSDataLogger::getStatistics(uint32_t startTime, uint32_t endTime) {
  DataStats stats;
  memset(&stats, 0, sizeof(DataStats));
  
  // Initialize min/max
  for (uint8_t i = 0; i < 8; i++) {
    stats.tempMin[i] = 999.0;
    stats.tempMax[i] = -999.0;
  }
  
  uint16_t validCount = 0;
  float tempSum[8] = {0};
  
  for (uint16_t i = 0; i < _count; i++) {
    DataPoint* point = getDataPoint(i);
    if (point->timestamp >= startTime && point->timestamp <= endTime) {
      validCount++;
      
      // Temperature statistics
      for (uint8_t t = 0; t < 8; t++) {
        if (point->temperatures[t] > -99.0 && point->temperatures[t] < 999.0) {
          if (point->temperatures[t] < stats.tempMin[t]) {
            stats.tempMin[t] = point->temperatures[t];
          }
          if (point->temperatures[t] > stats.tempMax[t]) {
            stats.tempMax[t] = point->temperatures[t];
          }
          tempSum[t] += point->temperatures[t];
        }
      }
      
      // Runtime statistics (approximate based on interval)
      for (uint8_t p = 0; p < 4; p++) {
        stats.pumpRuntime[p] += (point->pumps[p] * _logInterval) / 100;
      }
      for (uint8_t r = 0; r < 4; r++) {
        if (point->relays[r]) {
          stats.relayRuntime[r] += _logInterval;
        }
      }
      
      // Heat accumulation
      stats.totalHeat += point->heatQuantity;
    }
  }
  
  // Calculate averages
  if (validCount > 0) {
    for (uint8_t t = 0; t < 8; t++) {
      stats.tempAvg[t] = tempSum[t] / validCount;
    }
  }
  
  return stats;
}

DataStats VBUSDataLogger::getStatisticsLastHours(uint8_t hours) {
  uint32_t now = millis() / 1000;
  uint32_t startTime = now - (hours * 3600);
  return getStatistics(startTime, now);
}

DataStats VBUSDataLogger::getStatisticsAll() {
  return getStatistics(0, 0xFFFFFFFF);
}

String VBUSDataLogger::exportCSV(uint32_t startTime, uint32_t endTime) {
  String csv = "Timestamp,Temp0,Temp1,Temp2,Temp3,Temp4,Temp5,Temp6,Temp7,";
  csv += "Pump0,Pump1,Pump2,Pump3,Relay0,Relay1,Relay2,Relay3,ErrorMask,HeatQuantity\n";
  
  for (uint16_t i = 0; i < _count; i++) {
    DataPoint* point = getDataPoint(i);
    if (point->timestamp >= startTime && point->timestamp <= endTime) {
      csv += String(point->timestamp) + ",";
      
      // Temperatures
      for (uint8_t t = 0; t < 8; t++) {
        csv += String(point->temperatures[t], 2) + ",";
      }
      
      // Pumps
      for (uint8_t p = 0; p < 4; p++) {
        csv += String(point->pumps[p]) + ",";
      }
      
      // Relays
      for (uint8_t r = 0; r < 4; r++) {
        csv += String(point->relays[r]) + ",";
      }
      
      csv += String(point->errorMask) + ",";
      csv += String(point->heatQuantity) + "\n";
    }
  }
  
  return csv;
}

String VBUSDataLogger::exportJSON(uint32_t startTime, uint32_t endTime) {
  String json = "{\"dataPoints\":[";
  bool first = true;
  
  for (uint16_t i = 0; i < _count; i++) {
    DataPoint* point = getDataPoint(i);
    if (point->timestamp >= startTime && point->timestamp <= endTime) {
      if (!first) json += ",";
      first = false;
      
      json += "{\"timestamp\":" + String(point->timestamp) + ",";
      json += "\"temperatures\":[";
      for (uint8_t t = 0; t < 8; t++) {
        if (t > 0) json += ",";
        json += String(point->temperatures[t], 2);
      }
      json += "],\"pumps\":[";
      for (uint8_t p = 0; p < 4; p++) {
        if (p > 0) json += ",";
        json += String(point->pumps[p]);
      }
      json += "],\"relays\":[";
      for (uint8_t r = 0; r < 4; r++) {
        if (r > 0) json += ",";
        json += point->relays[r] ? "true" : "false";
      }
      json += "],\"errorMask\":" + String(point->errorMask) + ",";
      json += "\"heatQuantity\":" + String(point->heatQuantity) + "}";
    }
  }
  
  json += "]}";
  return json;
}

// Private helper methods

void VBUSDataLogger::_addDataPoint(const DataPoint& point) {
  _buffer[_writeIndex] = point;
  _writeIndex = (_writeIndex + 1) % _bufferSize;
  if (_count < _bufferSize) {
    _count++;
  }
}

uint16_t VBUSDataLogger::_getCircularIndex(uint16_t offset) {
  if (_count < _bufferSize) {
    return offset;
  } else {
    return (_writeIndex + offset) % _bufferSize;
  }
}

// Note: This method is currently unused but reserved for future enhancements
// that may require more complex statistical calculations beyond what getStatistics() provides
void VBUSDataLogger::_calculateStats(DataStats& stats, uint16_t startIdx, uint16_t count) {
  // Reserved for future use
  (void)stats;
  (void)startIdx;
  (void)count;
}
