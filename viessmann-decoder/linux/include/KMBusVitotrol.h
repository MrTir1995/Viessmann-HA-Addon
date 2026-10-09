#pragma once

#include "Arduino.h"

class KMBusVitotrol {
public:
    struct ControlUpdate {
        bool hasRoomTemperature = false;
        bool hasDesiredRoomTemperature = false;
        bool hasReducedRoomTemperature = false;
        bool hasMode = false;
        bool hasProfile = false;
        float roomTemperature = 0;
        float desiredRoomTemperature = 0;
        float reducedRoomTemperature = 0;
        const char* mode = nullptr;
        const char* profile = nullptr;
    };

    KMBusVitotrol(Stream* serial, uint8_t modelId, uint8_t slot);
    void loop();
    bool applyControlUpdate(const ControlUpdate& update);
    bool setCurrentRoomTemperature(float temperature);
    bool setDesiredRoomTemperature(float temperature);
    bool setReducedRoomTemperature(float temperature);
    bool setOperatingMode(const char* mode);
    bool isOnline() const;
    float getCurrentRoomTemperature() const;
    float getDesiredRoomTemperature() const;
    float getReducedRoomTemperature() const;
    const char* getProtocolProfile() const;
    uint8_t getPendingCommandCount() const;
    uint8_t getOperatingMode() const;
    bool getPartyEnabled() const;
    bool getEconomyEnabled() const;
    uint8_t getLastMasterDataset() const;
    uint32_t getLastMessageMillis() const;
    bool getDataset(uint8_t id, const uint8_t*& data, uint8_t& length,
                    uint32_t& receivedAt) const;
    bool getOutsideTemperature(float& value) const;
    bool getHeatingEnabled(bool& value) const;
    uint32_t getCrcErrorCount() const;
    uint32_t getMalformedFrameCount() const;
    uint32_t getUnknownCommandCount() const;

private:
    static const uint16_t MAX_FRAME_SIZE = 255;
    static const uint8_t DATASET_QUEUE_SIZE = 4;
    static const uint8_t MAX_DATASET_DATA = 29;
    enum DatasetKind : uint8_t { ROOM, DESIRED, REDUCED, MODE };
    struct QueuedDataset {
        uint8_t id;
        uint8_t length;
        uint8_t data[MAX_DATASET_DATA];
        DatasetKind kind;
    };

    Stream* _serial;
    uint8_t _modelId, _slot;
    uint8_t _registers[256];
    uint8_t _datasets[19][MAX_DATASET_DATA];
    uint8_t _datasetLengths[19];
    bool _datasetPresent[19];
    uint32_t _datasetReceivedAt[19];
    QueuedDataset _queue[DATASET_QUEUE_SIZE];
    uint8_t _queueHead, _queueCount;
    uint8_t _frame[MAX_FRAME_SIZE];
    uint16_t _frameLength;
    uint32_t _lastByteMillis;
    uint8_t _txFrame[MAX_FRAME_SIZE];
    uint16_t _txLength, _txOffset;
    uint32_t _txStartedMillis;
    bool _txQueued;
    float _currentRoomTemperature, _desiredRoomTemperature, _reducedRoomTemperature;
    uint8_t _operatingMode, _lastMasterDataset;
    uint32_t _lastMessageMillis, _lastRoomTemperatureSend;
    uint32_t _crcErrors, _malformedFrames, _unknownCommands;
    bool _hasReceivedMessage, _openv, _party, _economy;

    void parseFrames();
    void discardBytes(uint16_t count);
    void processFrame();
    uint8_t datasetId(DatasetKind kind) const;
    bool queueDataset(DatasetKind kind, const uint8_t* data, uint8_t length);
    bool sendFrame(uint8_t command, const uint8_t* data, uint8_t length,
                   bool queued = false);
    bool sendQueuedDataset();
    bool sendDataset(const QueuedDataset& dataset, bool queued);
    void continueWrite();
    void abortPendingWrite();
    uint16_t calculateCRC(const uint8_t* data, uint16_t length) const;
    bool recordMasterDataset(const uint8_t* data, uint8_t length, bool wrapped);
};
