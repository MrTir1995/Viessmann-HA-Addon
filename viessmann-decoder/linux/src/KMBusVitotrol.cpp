#include "KMBusVitotrol.h"

#include <math.h>
#include <string.h>

namespace {
const uint8_t XOR_MASK = 0xAA;
const uint32_t INTERBYTE_TIMEOUT_MS = 500;
// Transport retry safeguard, not a measured KM-Bus response deadline.
const uint32_t TX_RETRY_TIMEOUT_MS = 500;
const uint32_t ROOM_TEMPERATURE_INTERVAL_MS = 30000;

bool temperatureValid(float value, float minimum, float maximum) {
    return isfinite(value) && value >= minimum && value <= maximum;
}

bool modeCommand(const char* mode, uint8_t& command) {
    if (!mode) return false;
    const char* names[] = {"off", "water", "heat_water", "party_on", "party_off",
                           "economy_on", "economy_off"};
    const uint8_t commands[] = {0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xDC, 0xDD};
    for (uint8_t i = 0; i < 7; ++i) {
        if (strcmp(mode, names[i]) == 0) {
            command = commands[i];
            return true;
        }
    }
    return false;
}

bool readOnlyRegister(uint8_t address) {
    return address >= 0xF8 && address <= 0xFB;
}
}

KMBusVitotrol::KMBusVitotrol(Stream* serial, uint8_t modelId, uint8_t slot)
    : _serial(serial), _modelId(modelId), _slot(slot),
      _registers{}, _datasets{}, _datasetLengths{}, _datasetPresent{},
      _datasetReceivedAt{}, _queue{}, _queueHead(0), _queueCount(0),
      _frame{}, _frameLength(0), _lastByteMillis(0), _txFrame{},
      _txLength(0), _txOffset(0), _txStartedMillis(0), _txQueued(false),
      _currentRoomTemperature(20), _desiredRoomTemperature(20),
      _reducedRoomTemperature(16), _operatingMode(0xCA), _lastMasterDataset(0),
      _lastMessageMillis(0), _lastRoomTemperatureSend(0), _crcErrors(0),
      _malformedFrames(0), _unknownCommands(0), _hasReceivedMessage(false),
      _openv(false), _party(false), _economy(false) {
    _registers[0] = 0x12;
    _registers[0xF8] = 0x11;
    _registers[0xF9] = modelId;
    _registers[0xFA] = 0;
    _registers[0xFB] = modelId == 0x38 ? 0x11 : 0x05;
    const uint8_t room[3] = {200, 0, 0};
    queueDataset(ROOM, room, sizeof(room));
}

bool KMBusVitotrol::applyControlUpdate(const ControlUpdate& update) {
    if (!update.hasRoomTemperature && !update.hasDesiredRoomTemperature &&
        !update.hasReducedRoomTemperature && !update.hasMode && !update.hasProfile)
        return false;
    uint8_t command = 0;
    bool openv = _openv;
    if (update.hasProfile) {
        if (!update.profile) return false;
        if (strcmp(update.profile, "wifi") == 0) openv = false;
        else if (strcmp(update.profile, "openv") == 0) openv = true;
        else return false;
    }
    if (openv && _slot > 3) return false;
    if ((update.hasRoomTemperature &&
         !temperatureValid(update.roomTemperature, -20, 50)) ||
        (update.hasDesiredRoomTemperature &&
         (!temperatureValid(update.desiredRoomTemperature, 5, 35) ||
          truncf(update.desiredRoomTemperature) != update.desiredRoomTemperature)) ||
        (update.hasReducedRoomTemperature &&
         (!temperatureValid(update.reducedRoomTemperature, 5, 35) ||
          truncf(update.reducedRoomTemperature) != update.reducedRoomTemperature)) ||
        (update.hasMode && !modeCommand(update.mode, command))) return false;
    const unsigned needed = unsigned(update.hasRoomTemperature) +
        unsigned(update.hasDesiredRoomTemperature) +
        unsigned(update.hasReducedRoomTemperature) + unsigned(update.hasMode);
    if (needed > unsigned(DATASET_QUEUE_SIZE - _queueCount)) return false;

    // A partially transmitted telegram cannot safely be rewritten mid-frame.
    if (openv != _openv && _txQueued && _txOffset != 0) return false;
    const bool profileChanged = openv != _openv;
    _openv = openv;
    if (profileChanged) {
        _registers[0] = _openv ? 0 : 0x12;
        for (uint8_t i = 0; i < _queueCount; ++i) {
            QueuedDataset& item = _queue[(_queueHead + i) % DATASET_QUEUE_SIZE];
            item.id = datasetId(item.kind);
        }
    }
    if (profileChanged && _txQueued && _txOffset == 0) {
        // No byte has left yet: discard the encoded copy, not the command.
        abortPendingWrite();
    }
    if (update.hasRoomTemperature) {
        _currentRoomTemperature = update.roomTemperature;
        const int16_t tenths = static_cast<int16_t>(lroundf(update.roomTemperature * 10));
        const uint16_t encoded = static_cast<uint16_t>(tenths);
        const uint8_t data[3] = {static_cast<uint8_t>(encoded),
                                static_cast<uint8_t>(encoded >> 8), 0};
        queueDataset(ROOM, data, sizeof(data));
    }
    if (update.hasDesiredRoomTemperature) {
        _desiredRoomTemperature = update.desiredRoomTemperature;
        const uint8_t data[5] = {0x0C, _slot, 0xCD,
            static_cast<uint8_t>(lroundf(update.desiredRoomTemperature)), 0};
        queueDataset(DESIRED, data, sizeof(data));
    }
    if (update.hasReducedRoomTemperature) {
        _reducedRoomTemperature = update.reducedRoomTemperature;
        const uint8_t data[5] = {0x0C, _slot, 0xCE,
            static_cast<uint8_t>(lroundf(update.reducedRoomTemperature)), 0};
        queueDataset(REDUCED, data, sizeof(data));
    }
    if (update.hasMode) {
        const uint8_t data[8] = {0, _slot, command, 0, 0, 0, 0, 0};
        queueDataset(MODE, data, sizeof(data));
        if (command <= 0xCA) _operatingMode = command;
        else if (command == 0xCB || command == 0xCC) _party = command == 0xCB;
        else _economy = command == 0xDC;
    }
    return true;
}

bool KMBusVitotrol::setCurrentRoomTemperature(float value) {
    ControlUpdate update; update.hasRoomTemperature = true; update.roomTemperature = value;
    return applyControlUpdate(update);
}
bool KMBusVitotrol::setDesiredRoomTemperature(float value) {
    ControlUpdate update; update.hasDesiredRoomTemperature = true; update.desiredRoomTemperature = value;
    return applyControlUpdate(update);
}
bool KMBusVitotrol::setReducedRoomTemperature(float value) {
    ControlUpdate update; update.hasReducedRoomTemperature = true; update.reducedRoomTemperature = value;
    return applyControlUpdate(update);
}
bool KMBusVitotrol::setOperatingMode(const char* value) {
    ControlUpdate update; update.hasMode = true; update.mode = value;
    return applyControlUpdate(update);
}

void KMBusVitotrol::loop() {
    if (!_serial) return;
    const uint32_t now = millis();
    if (_frameLength && uint32_t(now - _lastByteMillis) >= INTERBYTE_TIMEOUT_MS) {
        ++_malformedFrames;
        // A response from a buffered suffix would use an expired bus grant.
        _frameLength = 0;
    }
    // Exactly one continuation attempt per loop; a zero/EAGAIN result does not spin.
    if (_txLength) continueWrite();
    while (_serial->available() > 0) {
        int value = _serial->read();
        if (value < 0) break;
        if (_frameLength == MAX_FRAME_SIZE) {
            ++_malformedFrames;
            discardBytes(1);
        }
        _frame[_frameLength++] = static_cast<uint8_t>(value);
        _lastByteMillis = millis();
        parseFrames();
    }
    if (!_queueCount && uint32_t(millis() - _lastRoomTemperatureSend) >=
        ROOM_TEMPERATURE_INTERVAL_MS) {
        const uint16_t tenths = static_cast<uint16_t>(
            static_cast<int16_t>(lroundf(_currentRoomTemperature * 10)));
        const uint8_t data[3] = {static_cast<uint8_t>(tenths),
                                static_cast<uint8_t>(tenths >> 8), 0};
        queueDataset(ROOM, data, sizeof(data));
    }
}

void KMBusVitotrol::discardBytes(uint16_t count) {
    _frameLength -= count;
    memmove(_frame, _frame + count, _frameLength);
}

void KMBusVitotrol::parseFrames() {
    while (_frameLength >= 4) {
        if ((_frame[0] != 0x11 && _frame[0] != 0xFF) || _frame[1] != 0) {
            discardBytes(1);
            continue;
        }
        const uint16_t length = _frame[3];
        const uint8_t command = _frame[2];
        const bool impossibleLength =
            (command == 0x00 && length != 8) ||
            (command == 0x31 && length != 9) ||
            ((command == 0x33 || command == 0xB1) && length != 10) ||
            (command == 0xB3 && length < 10) ||
            (command == 0xBF && (length < 9 || length > 38)) ||
            (command == 0x3F && (length < 9 || length > 41));
        if (length < 8 || impossibleLength) {
            ++_malformedFrames;
            discardBytes(1);
            continue;
        }
        // Reject impossible partial headers without waiting for their claimed length.
        // Otherwise an uncorrupted outer frame owns its payload, even if that payload
        // contains a complete CRC-valid telegram.
        bool impossiblePayload = false;
        if (_frameLength >= 7) {
            if (command == 0xB3 && ((length - 8) & 1))
                impossiblePayload = uint16_t(_frame[6]) * 2 + 9 != length;
            if (command == 0xBF) {
                const uint8_t id = _frame[6];
                impossiblePayload = id < 0x10 || id > 0x22 ||
                    (id == datasetId(ROOM) && length != 12);
            }
            if (command == 0x3F && length > 9 && _frame[6] == 0x34)
                impossiblePayload = length < 12;
        }
        if (impossiblePayload) {
            ++_malformedFrames;
            discardBytes(1);
            continue;
        }
        if (_frameLength < length) return;
        const uint16_t received = uint16_t(_frame[length - 2]) |
                                  (uint16_t(_frame[length - 1]) << 8);
        if (calculateCRC(_frame, length - 2) != received) {
            ++_crcErrors;
            // Slide rather than lose the next frame after a corrupt length/CRC.
            discardBytes(1);
            continue;
        }
        processFrame();
        discardBytes(length);
    }
}

void KMBusVitotrol::processFrame() {
    if (_frame[4] != _slot && _frame[4] != 0) return;
    const uint8_t command = _frame[2];
    const uint8_t length = _frame[3] - 8;
    const uint8_t* data = _frame + 6;
    const bool broadcast = _frame[0] == 0xFF || _frame[4] == 0;
    bool valid = true;
    bool write = false;
    switch (command) {
        case 0x00:
            valid = length == 0;
            if (valid && !broadcast && !_txLength) {
                if (_queueCount) sendQueuedDataset();
                else sendFrame(0x80, nullptr, 0);
            }
            break;
        case 0x31:
            valid = length == 1;
            if (valid && !broadcast) {
                const uint8_t response[2] = {data[0], _registers[data[0]]};
                sendFrame(0xB1, response, sizeof(response));
            }
            break;
        case 0x33:
            valid = length == 2 && data[1] > 0 && data[1] <= 123 &&
                    uint16_t(data[0]) + data[1] <= 256;
            if (valid && !broadcast) {
                uint8_t response[246];
                for (uint16_t i = 0; i < data[1]; ++i) {
                    const uint8_t address = static_cast<uint8_t>(data[0] + i);
                    response[2 * i] = address;
                    response[2 * i + 1] = _registers[address];
                }
                sendFrame(0xB3, response, 2 * data[1]);
            }
            break;
        case 0xB1:
            write = true;
            valid = length == 2 && !readOnlyRegister(data[0]);
            if (valid) _registers[data[0]] = data[1];
            break;
        case 0xB3: {
            write = true;
            const uint8_t offset = length & 1;
            valid = length >= 2 &&
                    (!offset || uint16_t(data[0]) * 2 + 1 == length);
            if (valid) {
                for (uint16_t i = offset; i < length; i += 2)
                    if (readOnlyRegister(data[i])) valid = false;
            }
            if (valid) {
                for (uint16_t i = offset; i < length; i += 2)
                    _registers[data[i]] = data[i + 1];
            }
            break;
        }
        case 0xBF:
            write = true;
            valid = recordMasterDataset(data, length, false);
            break;
        case 0x3F:
            if (length == 1) {
                // Only locally originated commands may be returned, never master status.
                if (!broadcast && !_txLength) {
                    bool found = false;
                    for (uint8_t i = 0; i < _queueCount; ++i) {
                        const QueuedDataset& item =
                            _queue[(_queueHead + i) % DATASET_QUEUE_SIZE];
                        if (item.id == data[0]) {
                            sendDataset(item, false);
                            found = true;
                            break;
                        }
                    }
                    if (!found && data[0] == datasetId(ROOM)) {
                        const uint16_t tenths = static_cast<uint16_t>(
                            static_cast<int16_t>(lroundf(_currentRoomTemperature * 10)));
                        QueuedDataset room = {};
                        room.id = datasetId(ROOM); room.length = 3;
                        room.data[0] = static_cast<uint8_t>(tenths);
                        room.data[1] = static_cast<uint8_t>(tenths >> 8);
                        sendDataset(room, false);
                    }
                }
            } else {
                write = true;
                valid = !_openv && recordMasterDataset(data, length, true);
            }
            break;
        default:
            ++_unknownCommands;
            return;
    }
    if (!valid) {
        ++_malformedFrames;
        return;
    }
    _hasReceivedMessage = true;
    _lastMessageMillis = millis();
    if (write && !_openv && !broadcast) sendFrame(0x80, nullptr, 0);
}

uint8_t KMBusVitotrol::datasetId(DatasetKind kind) const {
    if (kind == MODE) return 0x14;
    if (kind == ROOM) return _openv ? 0x1F + _slot : 0x20;
    return _openv ? 0x14 + _slot : 0x15;
}

bool KMBusVitotrol::queueDataset(DatasetKind kind, const uint8_t* data, uint8_t length) {
    const uint8_t id = datasetId(kind);
    if (id < 0x10 || id > 0x22 || length > MAX_DATASET_DATA ||
        (length && !data) || _queueCount == DATASET_QUEUE_SIZE) return false;
    QueuedDataset& item = _queue[(_queueHead + _queueCount) % DATASET_QUEUE_SIZE];
    item.id = id; item.length = length; item.kind = kind;
    if (length) memcpy(item.data, data, length);
    ++_queueCount;
    return true;
}

bool KMBusVitotrol::sendDataset(const QueuedDataset& item, bool queued) {
    uint8_t data[MAX_DATASET_DATA + 1];
    data[0] = item.id;
    for (uint8_t i = 0; i < item.length; ++i) data[i + 1] = item.data[i] ^ XOR_MASK;
    return sendFrame(0xBF, data, item.length + 1, queued);
}
bool KMBusVitotrol::sendQueuedDataset() {
    return _queueCount && sendDataset(_queue[_queueHead], true);
}

bool KMBusVitotrol::sendFrame(uint8_t command, const uint8_t* data,
                            uint8_t length, bool queued) {
    if (!_serial || _txLength || length > MAX_FRAME_SIZE - 8 || (length && !data))
        return false;
    _txLength = uint16_t(length) + 8;
    _txOffset = 0; _txStartedMillis = millis(); _txQueued = queued;
    _txFrame[0] = 0; _txFrame[1] = 0x11; _txFrame[2] = command;
    _txFrame[3] = static_cast<uint8_t>(_txLength);
    _txFrame[4] = _slot; _txFrame[5] = 1;
    if (length) memcpy(_txFrame + 6, data, length);
    const uint16_t crc = calculateCRC(_txFrame, _txLength - 2);
    _txFrame[_txLength - 2] = static_cast<uint8_t>(crc);
    _txFrame[_txLength - 1] = static_cast<uint8_t>(crc >> 8);
    continueWrite();
    return true;
}

void KMBusVitotrol::continueWrite() {
    if (!_txLength) return;
    if (_serial->available() > 0 ||
        uint32_t(millis() - _txStartedMillis) >= TX_RETRY_TIMEOUT_MS) {
        // RX ends the old grant. Keep the command for a later explicit master poll,
        // but never append its stale remainder to a subsequent exchange.
        abortPendingWrite();
        return;
    }
    const size_t remaining = _txLength - _txOffset;
    const size_t accepted = _serial->write(_txFrame + _txOffset, remaining);
    // Some Stream implementations cast negative errors to size_t.
    if (accepted > remaining) return;
    _txOffset += accepted;
    if (_txOffset != _txLength) return;
    _txLength = 0; _txOffset = 0;
    if (_txQueued) {
        if (_queue[_queueHead].kind == ROOM) _lastRoomTemperatureSend = millis();
        _queueHead = (_queueHead + 1) % DATASET_QUEUE_SIZE;
        --_queueCount;
    }
    _txQueued = false;
}

void KMBusVitotrol::abortPendingWrite() {
    _txLength = 0;
    _txOffset = 0;
    _txStartedMillis = 0;
    _txQueued = false;
}

uint16_t KMBusVitotrol::calculateCRC(const uint8_t* data, uint16_t length) const {
    uint16_t crc = 0;
    for (uint16_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : crc >> 1;
    }
    return crc;
}

bool KMBusVitotrol::recordMasterDataset(const uint8_t* data, uint8_t length,
                                      bool wrapped) {
    if (!data || !length) return false;
    uint8_t id = data[0];
    if (wrapped && id == 0x34) {
        if (length < 4) return false;
        data += 3; length -= 3;
        id = data[0] ^ XOR_MASK;
    }
    if (id < 0x10 || id > 0x22) return false;
    const uint8_t payloadLength = length - 1;
    if (payloadLength > MAX_DATASET_DATA ||
        (id == datasetId(ROOM) && payloadLength != 3)) return false;
    const uint8_t index = id - 0x10;
    for (uint8_t i = 0; i < payloadLength; ++i)
        _datasets[index][i] = data[i + 1] ^ XOR_MASK;
    _datasetLengths[index] = payloadLength;
    _datasetPresent[index] = true;
    _datasetReceivedAt[index] = millis();
    _lastMasterDataset = id;
    return true;
}

bool KMBusVitotrol::isOnline() const {
    return _hasReceivedMessage && uint32_t(millis() - _lastMessageMillis) < 60000;
}
float KMBusVitotrol::getCurrentRoomTemperature() const { return _currentRoomTemperature; }
float KMBusVitotrol::getDesiredRoomTemperature() const { return _desiredRoomTemperature; }
float KMBusVitotrol::getReducedRoomTemperature() const { return _reducedRoomTemperature; }
const char* KMBusVitotrol::getProtocolProfile() const { return _openv ? "openv" : "wifi"; }
uint8_t KMBusVitotrol::getPendingCommandCount() const { return _queueCount; }
uint8_t KMBusVitotrol::getOperatingMode() const { return _operatingMode; }
bool KMBusVitotrol::getPartyEnabled() const { return _party; }
bool KMBusVitotrol::getEconomyEnabled() const { return _economy; }
uint8_t KMBusVitotrol::getLastMasterDataset() const { return _lastMasterDataset; }
uint32_t KMBusVitotrol::getLastMessageMillis() const { return _lastMessageMillis; }
uint32_t KMBusVitotrol::getCrcErrorCount() const { return _crcErrors; }
uint32_t KMBusVitotrol::getMalformedFrameCount() const { return _malformedFrames; }
uint32_t KMBusVitotrol::getUnknownCommandCount() const { return _unknownCommands; }

bool KMBusVitotrol::getDataset(uint8_t id, const uint8_t*& data, uint8_t& length,
                             uint32_t& receivedAt) const {
    data = nullptr; length = 0; receivedAt = 0;
    if (id < 0x10 || id > 0x22 || !_datasetPresent[id - 0x10]) return false;
    const uint8_t index = id - 0x10;
    data = _datasets[index]; length = _datasetLengths[index];
    receivedAt = _datasetReceivedAt[index];
    return true;
}
bool KMBusVitotrol::getOutsideTemperature(float& value) const {
    const uint8_t* data; uint8_t length; uint32_t receivedAt;
    const uint8_t id = _openv ? 0x1C + _slot : 0x1D;
    if (!getDataset(id, data, length, receivedAt) || length <= 6) return false;
    value = data[6] < 128 ? float(data[6]) : float(int(data[6]) - 256);
    return true;
}
bool KMBusVitotrol::getHeatingEnabled(bool& value) const {
    const uint8_t* data; uint8_t length; uint32_t receivedAt;
    const uint8_t id = _openv ? 0x1C + _slot : 0x1D;
    if (!getDataset(id, data, length, receivedAt) || length <= 10) return false;
    value = (data[10] & 0x40) != 0;
    return true;
}
