#pragma once

#include "Arduino.h"

class KMBusVitotrol {
public:
    KMBusVitotrol(Stream* serial, uint8_t modelId, uint8_t slot);

    void loop();
    bool setCurrentRoomTemperature(float temperature);
    bool setDesiredRoomTemperature(float temperature);
    bool setOperatingMode(const char* mode);

    bool isOnline() const;
    float getCurrentRoomTemperature() const;
    float getDesiredRoomTemperature() const;
    uint8_t getOperatingMode() const;
    uint8_t getLastMasterDataset() const;
    uint32_t getLastMessageMillis() const;

private:
    static const uint8_t MAX_FRAME_SIZE = 255;
    static const uint8_t DATASET_QUEUE_SIZE = 4;
    static const uint8_t MAX_DATASET_DATA = 29;

    struct QueuedDataset {
        uint8_t id;
        uint8_t length;
        uint8_t data[MAX_DATASET_DATA];
    };

    Stream* _serial;
    uint8_t _modelId;
    uint8_t _slot;
    uint8_t _serialNumber[2];
    uint8_t _registers[256];
    uint8_t _datasets[19][MAX_DATASET_DATA];
    uint8_t _datasetLengths[19];
    QueuedDataset _queue[DATASET_QUEUE_SIZE];
    uint8_t _queueHead;
    uint8_t _queueCount;
    uint8_t _frame[MAX_FRAME_SIZE];
    uint8_t _frameLength;
    uint8_t _expectedLength;
    float _currentRoomTemperature;
    float _desiredRoomTemperature;
    uint8_t _operatingMode;
    uint8_t _lastMasterDataset;
    uint32_t _lastMessageMillis;
    uint32_t _lastRoomTemperatureSend;
    bool _hasReceivedMessage;

    void processFrame();
    bool queueDataset(uint8_t id, const uint8_t* data, uint8_t length);
    bool sendFrame(uint8_t command, const uint8_t* data, uint8_t length);
    bool sendQueuedDataset();
    uint16_t calculateCRC(const uint8_t* data, uint8_t length) const;
    void recordMasterDataset(const uint8_t* data, uint8_t length);
    void applyMasterModeCommand(uint8_t command);
};
