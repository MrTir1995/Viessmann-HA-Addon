#include "KMBusVitotrol.h"

#include <math.h>
#include <string.h>

namespace {
const uint8_t VITOTROL_CLASS = 0x11;
const uint8_t MASTER_CLASS = 0x00;
const uint8_t BROADCAST_CLASS = 0xFF;
const uint8_t CMD_PING = 0x00;
const uint8_t CMD_PONG = 0x80;
const uint8_t CMD_READ_ONE = 0x31;
const uint8_t CMD_READ_MANY = 0x33;
const uint8_t CMD_WRITE_ONE = 0xB1;
const uint8_t CMD_WRITE_MANY = 0xB3;
const uint8_t CMD_WRITE_DATASET = 0xBF;
const uint8_t XOR_MASK = 0xAA;
const uint32_t ROOM_TEMPERATURE_INTERVAL_MS = 30000;
}

KMBusVitotrol::KMBusVitotrol(Stream* serial, uint8_t modelId, uint8_t slot)
    : _serial(serial),
      _modelId(modelId),
      _slot(slot),
      _serialNumber{0x00, static_cast<uint8_t>(modelId == 0x38 ? 0x11 : 0x05)},
      _registers{0},
      _datasets{0},
      _datasetLengths{0},
      _queue{},
      _queueHead(0),
      _queueCount(0),
      _frame{0},
      _frameLength(0),
      _expectedLength(0),
      _currentRoomTemperature(20.0f),
      _desiredRoomTemperature(20.0f),
      _operatingMode(0xCA),
      _lastMasterDataset(0),
      _lastMessageMillis(0),
      _lastRoomTemperatureSend(0),
      _hasReceivedMessage(false) {
    _registers[0x00] = 0x12;
    _registers[0xF8] = VITOTROL_CLASS;
    _registers[0xF9] = _modelId;
    _registers[0xFA] = _serialNumber[0];
    _registers[0xFB] = _serialNumber[1];

    uint8_t roomTemperature[3] = {0, 0, 0};
    queueDataset(0x20, roomTemperature, sizeof(roomTemperature));
}

void KMBusVitotrol::loop() {
    if (!_serial) return;

    while (_serial->available() > 0) {
        const int value = _serial->read();
        if (value < 0) break;

        if (_frameLength == 0) {
            _expectedLength = 0;
        } else if (_frameLength >= MAX_FRAME_SIZE) {
            _frameLength = 0;
            _expectedLength = 0;
        }

        _frame[_frameLength++] = static_cast<uint8_t>(value);
        if (_frameLength == 4) {
            _expectedLength = _frame[3];
            if (_expectedLength < 8) {
                _frameLength = 0;
                _expectedLength = 0;
            }
        }

        if (_expectedLength >= 8 && _frameLength == _expectedLength) {
            processFrame();
            _frameLength = 0;
            _expectedLength = 0;
        }
    }

    if (_queueCount == 0 &&
        millis() - _lastRoomTemperatureSend >= ROOM_TEMPERATURE_INTERVAL_MS) {
        const int16_t tenths = static_cast<int16_t>(lroundf(_currentRoomTemperature * 10.0f));
        const uint8_t data[3] = {
            static_cast<uint8_t>(tenths & 0xFF),
            static_cast<uint8_t>((tenths >> 8) & 0xFF),
            0
        };
        queueDataset(0x20, data, sizeof(data));
    }
}

bool KMBusVitotrol::setCurrentRoomTemperature(float temperature) {
    if (!isfinite(temperature) || temperature < -20.0f || temperature > 50.0f) {
        return false;
    }
    _currentRoomTemperature = temperature;
    const int16_t tenths = static_cast<int16_t>(lroundf(temperature * 10.0f));
    const uint8_t data[3] = {
        static_cast<uint8_t>(tenths & 0xFF),
        static_cast<uint8_t>((tenths >> 8) & 0xFF),
        0
    };
    return queueDataset(0x20, data, sizeof(data));
}

bool KMBusVitotrol::setDesiredRoomTemperature(float temperature) {
    if (!isfinite(temperature) || temperature < 5.0f || temperature > 35.0f) {
        return false;
    }
    _desiredRoomTemperature = temperature;

    const uint8_t degrees = static_cast<uint8_t>(lroundf(temperature));
    const uint8_t data[5] = {
        0x0C,
        _slot,
        0xCD,
        degrees,
        0x00
    };
    return queueDataset(0x15, data, sizeof(data));
}

bool KMBusVitotrol::setOperatingMode(const char* mode) {
    if (!mode) return false;

    uint8_t command;
    if (strcmp(mode, "off") == 0) {
        command = 0xC8;
    } else if (strcmp(mode, "water") == 0) {
        command = 0xC9;
    } else if (strcmp(mode, "heat_water") == 0) {
        command = 0xCA;
    } else if (strcmp(mode, "party_on") == 0) {
        command = 0xCB;
    } else if (strcmp(mode, "party_off") == 0) {
        command = 0xCC;
    } else if (strcmp(mode, "economy_on") == 0) {
        command = 0xDC;
    } else if (strcmp(mode, "economy_off") == 0) {
        command = 0xDD;
    } else {
        return false;
    }

    const uint8_t data[8] = {0x00, _slot, command, 0, 0, 0, 0, 0};
    if (!queueDataset(0x14, data, sizeof(data))) return false;
    _operatingMode = command;
    return true;
}

bool KMBusVitotrol::isOnline() const {
    return _hasReceivedMessage && millis() - _lastMessageMillis < 60000;
}

float KMBusVitotrol::getCurrentRoomTemperature() const {
    return _currentRoomTemperature;
}

float KMBusVitotrol::getDesiredRoomTemperature() const {
    return _desiredRoomTemperature;
}

uint8_t KMBusVitotrol::getOperatingMode() const {
    return _operatingMode;
}

uint8_t KMBusVitotrol::getLastMasterDataset() const {
    return _lastMasterDataset;
}

uint32_t KMBusVitotrol::getLastMessageMillis() const {
    return _lastMessageMillis;
}

void KMBusVitotrol::processFrame() {
    if (_frameLength < 8 || _frame[3] != _frameLength) return;

    const uint16_t expectedCrc = calculateCRC(_frame, _frameLength - 2);
    const uint16_t receivedCrc = static_cast<uint16_t>(_frame[_frameLength - 2]) |
                                 (static_cast<uint16_t>(_frame[_frameLength - 1]) << 8);
    if (expectedCrc != receivedCrc) return;

    if (_frame[0] != VITOTROL_CLASS && _frame[0] != BROADCAST_CLASS) return;
    if (_frame[1] != MASTER_CLASS || (_frame[4] != _slot && _frame[4] != 0)) return;

    _hasReceivedMessage = true;
    _lastMessageMillis = millis();
    const uint8_t command = _frame[2];
    const uint8_t dataLength = _frameLength - 8;
    const uint8_t* data = _frame + 6;

    switch (command) {
        case CMD_PING:
            if (!sendQueuedDataset()) sendFrame(CMD_PONG, nullptr, 0);
            break;
        case CMD_READ_ONE:
            if (dataLength >= 1) {
                const uint8_t response[2] = {data[0], _registers[data[0]]};
                sendFrame(CMD_WRITE_ONE, response, sizeof(response));
            }
            break;
        case CMD_READ_MANY:
            if (dataLength >= 2 && data[1] > 0 && data[1] <= 64) {
                uint8_t response[128];
                uint8_t responseLength = 0;
                const uint8_t count = data[1];
                for (uint16_t i = 0; i < count; ++i) {
                    const uint8_t address = static_cast<uint8_t>(data[0] + i);
                    response[responseLength++] = address;
                    response[responseLength++] = _registers[address];
                }
                sendFrame(CMD_WRITE_MANY, response, responseLength);
            }
            break;
        case CMD_WRITE_ONE:
            if (dataLength >= 2) _registers[data[0]] = data[1];
            sendFrame(CMD_PONG, nullptr, 0);
            break;
        case CMD_WRITE_MANY:
            if (dataLength > 0) {
                const uint8_t count = data[0];
                for (uint8_t i = 0; i < count && 1 + 2 * i + 1 < dataLength; ++i) {
                    _registers[data[1 + 2 * i]] = data[2 + 2 * i];
                }
            }
            sendFrame(CMD_PONG, nullptr, 0);
            break;
        case CMD_WRITE_DATASET:
            recordMasterDataset(data, dataLength);
            sendFrame(CMD_PONG, nullptr, 0);
            break;
        default:
            break;
    }
}

bool KMBusVitotrol::queueDataset(uint8_t id, const uint8_t* data, uint8_t length) {
    if (id < 0x10 || id > 0x22 || length > MAX_DATASET_DATA ||
        (length > 0 && !data) || _queueCount >= DATASET_QUEUE_SIZE) {
        return false;
    }

    const uint8_t index = (_queueHead + _queueCount) % DATASET_QUEUE_SIZE;
    _queue[index].id = id;
    _queue[index].length = length;
    if (length > 0) memcpy(_queue[index].data, data, length);
    ++_queueCount;
    return true;
}

bool KMBusVitotrol::sendQueuedDataset() {
    if (_queueCount == 0) return false;

    const QueuedDataset& queued = _queue[_queueHead];
    uint8_t payload[MAX_DATASET_DATA + 1];
    payload[0] = queued.id;
    for (uint8_t i = 0; i < queued.length; ++i) {
        payload[i + 1] = queued.data[i] ^ XOR_MASK;
    }
    if (!sendFrame(CMD_WRITE_DATASET, payload, queued.length + 1)) return false;

    _queueHead = (_queueHead + 1) % DATASET_QUEUE_SIZE;
    --_queueCount;
    if (queued.id == 0x20) _lastRoomTemperatureSend = millis();
    return true;
}

bool KMBusVitotrol::sendFrame(uint8_t command, const uint8_t* data, uint8_t length) {
    if (!_serial || length > MAX_FRAME_SIZE - 8) return false;

    uint8_t frame[MAX_FRAME_SIZE];
    const uint8_t frameLength = length + 8;
    frame[0] = MASTER_CLASS;
    frame[1] = VITOTROL_CLASS;
    frame[2] = command;
    frame[3] = frameLength;
    frame[4] = _slot;
    frame[5] = 0x01;
    if (length > 0 && data) memcpy(frame + 6, data, length);

    const uint16_t crc = calculateCRC(frame, frameLength - 2);
    frame[frameLength - 2] = static_cast<uint8_t>(crc & 0xFF);
    frame[frameLength - 1] = static_cast<uint8_t>(crc >> 8);
    if (_serial->write(frame, frameLength) != frameLength) return false;
    _serial->flush();
    return true;
}

uint16_t KMBusVitotrol::calculateCRC(const uint8_t* data, uint8_t length) const {
    uint16_t crc = 0;
    for (uint8_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : crc >> 1;
        }
    }
    return crc;
}

void KMBusVitotrol::recordMasterDataset(const uint8_t* data, uint8_t length) {
    if (!data || length < 1) return;

    const uint8_t id = data[0];
    if (id < 0x10 || id > 0x22) return;

    const uint8_t index = id - 0x10;
    const uint8_t payloadLength = length - 1;
    if (payloadLength > MAX_DATASET_DATA) return;

    _datasetLengths[index] = payloadLength;
    for (uint8_t i = 0; i < payloadLength; ++i) {
        _datasets[index][i] = data[i + 1] ^ XOR_MASK;
    }
    _lastMasterDataset = id;

    if (id >= 0x14 && id <= 0x17 && payloadLength >= 3) {
        applyMasterModeCommand(_datasets[index][2]);
    }
}

void KMBusVitotrol::applyMasterModeCommand(uint8_t command) {
    if (command == 0xC8 || command == 0xC9 || command == 0xCA ||
        command == 0xCB || command == 0xCC || command == 0xDC || command == 0xDD) {
        _operatingMode = command;
    }
}
