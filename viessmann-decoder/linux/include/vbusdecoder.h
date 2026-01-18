/*
 * Viessmann Multi-Protocol Library v2.0
 * Created by Martin Saidl January, 2018
 * Extended for multi-protocol support January, 2026
 */
 
#pragma once
#ifndef VBUSDecoder_h
#define VBUSDecoder_h

#include <Arduino.h>

// Protocol types supported by the library
enum ProtocolType: uint8_t {
  PROTOCOL_VBUS = 0,    // RESOL VBUS Protocol (default)
  PROTOCOL_KW = 1,      // Viessmann KW-Bus (VS1)
  PROTOCOL_P300 = 2,    // Viessmann P300/VS2 (Optolink)
  PROTOCOL_KM = 3       // Viessmann KM-Bus
};

// Bus participant information structure
struct BusParticipant {
  uint16_t address;           // Device address
  uint32_t lastSeen;          // Last time packet was received (millis)
  uint8_t tempChannels;       // Number of temperature channels
  uint8_t pumpChannels;       // Number of pump channels
  uint8_t relayChannels;      // Number of relay channels
  bool autoDetected;          // True if auto-detected, false if manually configured
  char name[32];              // Device name/description
  bool active;                // True if participant is active
};

// KM-Bus Protocol Constants and Structures
// Device class identifiers for KM-Bus protocol
enum KMBusDeviceClass: uint8_t {
  KMBUS_CLASS_VITOTRONIC = 0x00,    // Vitotronic controller (master)
  KMBUS_CLASS_INT_EXTENSION = 0x04, // Internal extension module
  KMBUS_CLASS_VITOTROL = 0x11,      // Vitotrol remote control
  KMBUS_CLASS_BROADCAST = 0xFF       // Broadcast address
};

// KM-Bus command types
enum KMBusCommand: uint8_t {
  KMBUS_CMD_PING = 0x00,      // Ping request
  KMBUS_CMD_PONG = 0x80,      // Pong response
  KMBUS_CMD_RD1_REQ = 0x31,   // Read 1 byte request
  KMBUS_CMD_RDN_REQ = 0x33,   // Read N bytes request
  KMBUS_CMD_RDR_REQ = 0x3F,   // Read record request
  KMBUS_CMD_WR1_DAT = 0xB1,   // Write 1 byte data
  KMBUS_CMD_WRN_DAT = 0xB3,   // Write N bytes data
  KMBUS_CMD_WRR_DAT = 0xBF    // Write record data
};

// KM-Bus register addresses for status records
enum KMBusStatusAddress: uint8_t {
  KMBUS_ADDR_MASTER_STATUS = 0x1C,  // Master status record
  KMBUS_ADDR_CIR1_STATUS = 0x1D,    // Circuit 1 status record
  KMBUS_ADDR_CIR2_STATUS = 0x1E,    // Circuit 2 status record
  KMBUS_ADDR_CIR3_STATUS = 0x1F     // Circuit 3 status record
};

// KM-Bus register addresses for command records
enum KMBusCommandAddress: uint8_t {
  KMBUS_ADDR_MASTER_CMD = 0x14,     // Master command record
  KMBUS_ADDR_CIR1_CMD = 0x15,       // Circuit 1 command record
  KMBUS_ADDR_CIR2_CMD = 0x16,       // Circuit 2 command record
  KMBUS_ADDR_CIR3_CMD = 0x17        // Circuit 3 command record
};

// KM-Bus register addresses for ambient temperature
enum KMBusAmbientTempAddress: uint8_t {
  KMBUS_ADDR_CIR1_AMBIENT = 0x20,   // Circuit 1 ambient temperature
  KMBUS_ADDR_CIR2_AMBIENT = 0x21,   // Circuit 2 ambient temperature
  KMBUS_ADDR_CIR3_AMBIENT = 0x22    // Circuit 3 ambient temperature
};

// KM-Bus WRR command bytes for mode control
enum KMBusWRRCommand: uint8_t {
  KMBUS_WRR_MODE_OFF = 0x62,        // Shutdown mode
  KMBUS_WRR_MODE_HEAT_WATER = 0x60, // Heating + hot water mode
  KMBUS_WRR_MODE_WATER = 0x63,      // Hot water only mode
  KMBUS_WRR_ECO_ON = 0x76,          // Eco mode on
  KMBUS_WRR_ECO_OFF = 0x77,         // Eco mode off
  KMBUS_WRR_PARTY_ON = 0x61,        // Party mode on
  KMBUS_WRR_PARTY_OFF = 0x66,       // Party mode off
  KMBUS_WRR_SETPOINT_NORM = 0x67,   // Normal temperature setpoint
  KMBUS_WRR_SETPOINT_ECO = 0x64,    // Eco temperature setpoint
  KMBUS_WRR_SETPOINT_PARTY1 = 0x65, // Party 1 temperature setpoint
  KMBUS_WRR_SETPOINT_PARTY2 = 0x66, // Party 2 temperature setpoint
  KMBUS_WRR_SET_TIME = 0x7E,        // Set time command
  KMBUS_WRR_SET_DATE = 0x7F         // Set date command
};

// KM-Bus status flags
enum KMBusStatusFlags: uint8_t {
  KMBUS_STATUS_BURNER = 0x04,       // Burner active
  KMBUS_STATUS_MAIN_PUMP = 0x80,    // Main circulation pump active
  KMBUS_STATUS_LOOP_PUMP = 0x40     // Hot water loop pump active
};

// KM-Bus mode byte values
enum KMBusMode: uint8_t {
  KMBUS_MODE_OFF = 0x00,            // Off/standby mode
  KMBUS_MODE_NIGHT = 0x08,          // Night/reduced mode
  KMBUS_MODE_DAY = 0x84,            // Day/comfort mode
  KMBUS_MODE_ECO = 0xC6,            // Eco mode
  KMBUS_MODE_PARTY = 0x86           // Party mode
};

// KM-Bus data XOR mask for WRR data encoding
#define KMBUS_XOR_MASK 0xAA

// KM-Bus maximum number of heating circuits
#define KMBUS_MAX_CIRCUITS 3

class VBUSDecoder {
  
  public:
    VBUSDecoder(Stream* serial);
    ~VBUSDecoder();
    
    void begin(ProtocolType protocol = PROTOCOL_VBUS);
    void loop();
    float const getTemp(uint8_t idx) const;
    uint8_t const getPump(uint8_t idx) const;
    bool const getRelay(uint8_t idx) const;
    uint8_t const getTempNum() const;
    uint8_t const getRelayNum() const;
    uint8_t const getPumpNum() const;
    bool const getVbusStat() const;
    bool const isReady() const;
    uint16_t const getErrorMask() const;
    uint16_t const getSystemTime() const;
    uint32_t const getOperatingHours(uint8_t idx) const;
    uint16_t const getHeatQuantity() const;
    uint8_t const getSystemVariant() const;
    ProtocolType const getProtocol() const;
    
    // Bus participant discovery and management
    void enableAutoDiscovery(bool enable = true);
    bool isAutoDiscoveryEnabled() const;
    uint8_t getParticipantCount() const;
    const BusParticipant* getParticipant(uint8_t idx) const;
    const BusParticipant* getParticipantByAddress(uint16_t address) const;
    bool addParticipant(uint16_t address, const char* name = nullptr, 
                       uint8_t tempChannels = 0, uint8_t pumpChannels = 0, 
                       uint8_t relayChannels = 0);
    bool removeParticipant(uint16_t address);
    void clearParticipants();
    uint16_t getCurrentSourceAddress() const;
    
    // KM-Bus specific methods (only available when PROTOCOL_KM is active)
    bool getKMBusBurnerStatus() const;      // Get burner on/off status
    bool getKMBusMainPumpStatus() const;    // Get main circulation pump status
    bool getKMBusLoopPumpStatus() const;    // Get hot water loop pump status
    uint8_t getKMBusMode() const;           // Get current operating mode
    float getKMBusBoilerTemp() const;       // Get boiler temperature
    float getKMBusHotWaterTemp() const;     // Get hot water temperature
    float getKMBusOutdoorTemp() const;      // Get outdoor temperature
    float getKMBusSetpointTemp() const;     // Get setpoint temperature
    float getKMBusDepartureTemp() const;    // Get departure/flow temperature
    
    // Control commands (KM-Bus protocol)
    bool setKMBusMode(uint8_t mode);        // Set operating mode (off/night/day/eco/party)
    bool setKMBusSetpoint(uint8_t circuit, float temperature);  // Set temperature setpoint
    bool setKMBusEcoMode(bool enable);      // Enable/disable eco mode
    bool setKMBusPartyMode(bool enable);    // Enable/disable party mode

  private:
    Stream* _stream;
    ProtocolType _protocol;
    enum T_state: uint8_t {
      SYNC,
      RECEIVE,
      DECODE,
      ERROR
    } _state;
    uint16_t _dstAddr;
    uint16_t _srcAddr;
    uint8_t _protocolVer;
    uint16_t _cmd;
    uint8_t _frameCnt;
    uint16_t _frameLen;
    static const uint16_t MAX_BUFFER_SIZE = 255;
    uint8_t _rcvBuffer[MAX_BUFFER_SIZE];
    uint8_t _rcvBufferIdx;
    bool _errorFlag;
    bool _readyFlag;
    float _temp[32];
    uint8_t _pump[32];
    bool _relay[32];
    uint8_t _tempNum;
    uint8_t _relayNum;
    uint8_t _pumpNum;
    uint32_t _lastMillis;
    uint16_t _errorMask;
    uint16_t _systemTime;
    uint32_t _operatingHours[8];
    uint16_t _heatQuantity;
    uint8_t _systemVariant;
    
    // Bus participant discovery
    static const uint8_t MAX_PARTICIPANTS = 16;
    BusParticipant _participants[MAX_PARTICIPANTS];
    uint8_t _participantCount;
    bool _autoDiscoveryEnabled;
    
    // KM-Bus specific data storage
    uint8_t _kmBusMode;               // Current operating mode
    bool _kmBusBurnerStatus;          // Burner on/off
    bool _kmBusMainPumpStatus;        // Main circulation pump on/off
    bool _kmBusLoopPumpStatus;        // Hot water loop pump on/off
    float _kmBusBoilerTemp;           // Boiler temperature
    float _kmBusHotWaterTemp;         // Hot water temperature
    float _kmBusOutdoorTemp;          // Outdoor temperature
    float _kmBusSetpointTemp;         // Setpoint temperature
    float _kmBusDepartureTemp;        // Departure/flow temperature
    
    void _updateParticipant(uint16_t address);
    int8_t _findParticipantIndex(uint16_t address) const;
    void _configureParticipantChannels(BusParticipant* participant, uint16_t address);
    
    // Common utility functions
    uint8_t _calcCRC(const uint8_t *Buffer, uint8_t Offset, uint8_t Length);
    void _septetInject(uint8_t *Buffer, uint8_t Offset, uint8_t Length);
    float _calcTemp(uint8_t Byte1, uint8_t Byte2);
    void _headerDecoder();

    // VBUS protocol handlers
    void _vbusSyncHandler();
    void _vbusReceiveHandler();
    void _vbusDecodeHandler();
    void _errorHandler();

    // KW-Bus protocol handlers
    void _kwSyncHandler();
    void _kwReceiveHandler();
    void _kwDecodeHandler();
    
    // P300 protocol handlers
    void _p300SyncHandler();
    void _p300ReceiveHandler();
    void _p300DecodeHandler();
    
    // KM-Bus protocol handlers
    void _kmSyncHandler();
    void _kmReceiveHandler();
    void _kmDecodeHandler();
    
    // KM-Bus helper functions
    uint16_t _kmCalcCRC16(const uint8_t *data, uint8_t start, uint16_t length);
    uint8_t _kmReflect8(uint8_t data);
    uint16_t _kmReflect16(uint16_t data);
    void _kmDecodeStatusRecord(const uint8_t *buffer, uint8_t bufferLen);
    float _kmDecodeTemperature(uint8_t encodedTemp);
    bool _kmSendCommand(uint8_t address, uint8_t command, const uint8_t* data, uint8_t dataLen);

    // VBUS device decoders
    void _defaultDecoder();
    void _vitosolic200Decoder();
    void _deltaSolBXDecoder();
    void _deltaSolMXDecoder();
    
    // KW-Bus device decoders
    void _kwDefaultDecoder();
    
    // P300 device decoders
    void _p300DefaultDecoder();
    
    // KM-Bus device decoders
    void _kmDefaultDecoder();
};

#endif
