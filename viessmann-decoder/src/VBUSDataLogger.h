/*
 * Viessmann Multi-Protocol Library - Data Logger
 * Provides historical data logging with circular buffer
 */

#pragma once
#ifndef VBUSDataLogger_h
#define VBUSDataLogger_h

#include <Arduino.h>
#include "vbusdecoder.h"

// Data point structure
struct DataPoint {
  uint32_t timestamp;      // Unix timestamp or millis()
  float temperatures[8];   // Store up to 8 temperature sensors
  uint8_t pumps[4];        // Store up to 4 pump power levels
  bool relays[4];          // Store up to 4 relay states
  uint16_t errorMask;      // Error mask
  uint16_t heatQuantity;   // Heat quantity in Wh
};

// Statistical data
struct DataStats {
  float tempMin[8];
  float tempMax[8];
  float tempAvg[8];
  uint32_t pumpRuntime[4];
  uint32_t relayRuntime[4];
  uint32_t totalHeat;
};

class VBUSDataLogger {
  public:
    VBUSDataLogger(VBUSDecoder* decoder, uint16_t bufferSize = 288);  // 24h at 5min intervals
    ~VBUSDataLogger();
    
    // Configuration
    void begin();
    void setLogInterval(uint32_t intervalSeconds);
    void setMaxDataPoints(uint16_t maxPoints);
    
    // Logging control
    void loop();
    void logNow();
    void clear();
    void pause();
    void resume();
    bool isPaused();
    
    // Data access
    uint16_t getDataPointCount();
    DataPoint* getDataPoint(uint16_t index);
    DataPoint* getLatestDataPoint();
    DataPoint* getOldestDataPoint();
    
    // Statistics
    DataStats getStatistics(uint32_t startTime, uint32_t endTime);
    DataStats getStatisticsLastHours(uint8_t hours);
    DataStats getStatisticsAll();
    
    // Export
    String exportCSV(uint32_t startTime, uint32_t endTime);
    String exportJSON(uint32_t startTime, uint32_t endTime);
    
  private:
    VBUSDecoder* _decoder;
    DataPoint* _buffer;
    uint16_t _bufferSize;
    uint16_t _writeIndex;
    uint16_t _count;
    uint32_t _logInterval;
    uint32_t _lastLog;
    bool _paused;
    
    // Helper methods
    void _addDataPoint(const DataPoint& point);
    uint16_t _getCircularIndex(uint16_t offset);
    void _calculateStats(DataStats& stats, uint16_t startIdx, uint16_t count);
};

#endif
