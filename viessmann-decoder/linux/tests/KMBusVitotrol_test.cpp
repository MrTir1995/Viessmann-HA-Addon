#include "KMBusVitotrol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
uint32_t clockMillis = 0;
unsigned assertions = 0;
#define CHECK(condition) do { ++assertions; if (!(condition)) \
    throw std::runtime_error(std::string(__func__) + ":" + \
    std::to_string(__LINE__) + ": " #condition); } while (false)
using Bytes = std::vector<uint8_t>;

class FakeStream : public Stream {
public:
    std::deque<uint8_t> input;
    Bytes output;
    std::deque<size_t> writeResults;
    unsigned writeCalls = 0;
    unsigned flushCalls = 0;
    uint32_t writeAdvanceMillis = 0;
    int available() override { return static_cast<int>(input.size()); }
    int read() override {
        if (input.empty()) return -1;
        const int value = input.front(); input.pop_front(); return value;
    }
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* data, size_t length) override {
        ++writeCalls;
        clockMillis += writeAdvanceMillis;
        size_t result = length;
        if (!writeResults.empty()) {
            result = writeResults.front(); writeResults.pop_front();
        }
        if (result > length) return result;
        output.insert(output.end(), data, data + result);
        return result;
    }
    void flush() override { ++flushCalls; }
    void feed(const Bytes& data) { input.insert(input.end(), data.begin(), data.end()); }
};

// Independent, bit-by-bit polynomial division (CRC-16/KERMIT, reflected).
uint16_t crc(const Bytes& bytes) {
    uint16_t remainder = 0;
    for (uint8_t byte : bytes) {
        for (unsigned bit = 0; bit != 8; ++bit) {
            const bool feedback = (remainder ^ (byte >> bit)) & 1;
            remainder >>= 1;
            if (feedback) remainder ^= 0x8408;
        }
    }
    return remainder;
}
Bytes frame(uint8_t command, const Bytes& payload = {}, uint8_t slot = 1,
            uint8_t destination = 0x11, uint8_t source = 0) {
    Bytes result = {destination, source, command,
                    static_cast<uint8_t>(payload.size() + 8), slot, 1};
    result.insert(result.end(), payload.begin(), payload.end());
    const uint16_t checksum = crc(result);
    result.push_back(checksum & 255); result.push_back(checksum >> 8);
    return result;
}
Bytes dataset(uint8_t id, const Bytes& decoded) {
    Bytes result = {id};
    for (uint8_t value : decoded) result.push_back(value ^ 0xAA);
    return result;
}
Bytes exchange(KMBusVitotrol& device, FakeStream& stream, uint8_t command,
               const Bytes& payload = {}, uint8_t slot = 1, uint8_t destination = 0x11) {
    stream.output.clear();
    stream.feed(frame(command, payload, slot, destination));
    device.loop();
    return stream.output;
}
Bytes reply(uint8_t command, const Bytes& payload = {}, uint8_t slot = 1) {
    return frame(command, payload, slot, 0, 0x11);
}
void profile(KMBusVitotrol& device, const char* value) {
    KMBusVitotrol::ControlUpdate update;
    update.hasProfile = true; update.profile = value;
    CHECK(device.applyControlUpdate(update));
}

void sourceFixturesAndInitialTemperature() {
    CHECK(crc(Bytes{'1','2','3','4','5','6','7','8','9'}) == 0x2189);
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    const Bytes ping = {0x11,0,0,8,1,1,0x08,0x88};
    stream.feed(ping); device.loop();
    CHECK(stream.output == Bytes({0,0x11,0xBF,0x0C,1,1,0x20,0x62,0xAA,0xAA,0x3D,0xFC}));
    CHECK(device.getCurrentRoomTemperature() == 20);
    CHECK(device.getPendingCommandCount() == 0);
    CHECK(stream.flushCalls == 0);
    CHECK(exchange(device, stream, 0) == reply(0x80));
    stream.output.clear();
    stream.feed({0x11,0,0x31,9,1,1,0xF8,0x29,0x34}); device.loop();
    CHECK(stream.output == reply(0xB1, {0xF8,0x11}));
    // Published party-on capture: destination slot2, circuit1, 20 degrees.
    const Bytes sourceParty = {0,0x11,0xBF,0x11,2,1,0x14,0xAA,0xAB,0x65,
                               0xBE,0xAA,0xAA,0xAA,0xAA,0x0B,0x5D};
    CHECK(crc(Bytes(sourceParty.begin(), sourceParty.end() - 2)) == 0x5D0B);
    CHECK((sourceParty[9] ^ 0xAA) == 0xCF);
    CHECK((sourceParty[10] ^ 0xAA) == 20);
    CHECK(device.getPartyRoomTemperature() == 20);
    CHECK(device.setOperatingMode("party_on"));
    const Bytes requestedParty = exchange(device, stream, 0);
    CHECK(requestedParty == reply(0xBF, dataset(0x14, {0,1,0xCB,20,0,0,0,0})));
    CHECK(requestedParty[10] == sourceParty[10]);
}

void registerCommandsAndVariants() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    CHECK(exchange(device, stream, 0x31, {0}) == reply(0xB1, {0,0x12}));
    CHECK(exchange(device, stream, 0x31, {0xF9}) == reply(0xB1, {0xF9,0x38}));
    CHECK(exchange(device, stream, 0x31, {0xFB}) == reply(0xB1, {0xFB,0x11}));
    CHECK(exchange(device, stream, 0x31, {0x70}) == reply(0xB1, {0x70,0}));
    CHECK(exchange(device, stream, 0xB1, {0x70,0x55}) == reply(0x80));
    CHECK(exchange(device, stream, 0x31, {0x70}) == reply(0xB1, {0x70,0x55}));
    CHECK(exchange(device, stream, 0xB3, {0x70,0x66,0x71,0x77}) == reply(0x80));
    CHECK(exchange(device, stream, 0x33, {0x70,2}) ==
          reply(0xB3, {0x70,0x66,0x71,0x77}));
    CHECK(exchange(device, stream, 0xB3, {2,0x70,0x88,0x71,0x99}) == reply(0x80));
    CHECK(exchange(device, stream, 0x33, {0x70,2}) ==
          reply(0xB3, {0x70,0x88,0x71,0x99}));
    CHECK(exchange(device, stream, 0xB1, {0,0x42}) == reply(0x80));
    CHECK(device.setCurrentRoomTemperature(21));
    CHECK(exchange(device, stream, 0x31, {0}) == reply(0xB1, {0,0x42}));
    profile(device, "wifi");
    CHECK(exchange(device, stream, 0x31, {0}) == reply(0xB1, {0,0x42}));
    profile(device, "openv");
    CHECK(exchange(device, stream, 0x31, {0}) == reply(0xB1, {0,0}));
    CHECK(exchange(device, stream, 0x33, {0xF8,4}) ==
          reply(0xB3, {0xF8,0x11,0xF9,0x38,0xFA,0,0xFB,5}));
    CHECK(exchange(device, stream, 0xB1, {0xFB,0x11}).empty());
    CHECK(exchange(device, stream, 0x31, {0xFB}) == reply(0xB1, {0xFB,5}));
    CHECK(exchange(device, stream, 0xB1, {0x70,0x44}).empty());
    CHECK(exchange(device, stream, 0xB3, {1,0x71,0x33}).empty());
    CHECK(exchange(device, stream, 0x33, {0x70,2}) ==
          reply(0xB3, {0x70,0x44,0x71,0x33}));
    profile(device, "wifi");
    CHECK(exchange(device, stream, 0x33, {0xF8,4}) ==
          reply(0xB3, {0xF8,0x11,0xF9,0x38,0xFA,0,0xFB,0x11}));
    KMBusVitotrol alternate(&stream, 0x34, 1);
    CHECK(exchange(alternate, stream, 0x31, {0xFB}) == reply(0xB1, {0xFB,5}));
    profile(alternate, "openv");
    CHECK(exchange(alternate, stream, 0x31, {0xFB}) == reply(0xB1, {0xFB,5}));
    profile(alternate, "wifi");
    CHECK(exchange(alternate, stream, 0x31, {0xFB}) == reply(0xB1, {0xFB,5}));
}

void invalidWritesAreAtomicAndSilent() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    CHECK(exchange(device, stream, 0xB1, {0xF8,0}).empty());
    CHECK(exchange(device, stream, 0x31, {0xF8}) == reply(0xB1, {0xF8,0x11}));
    CHECK(exchange(device, stream, 0xB3, {0x70,0x55,0xF9,0}).empty());
    CHECK(exchange(device, stream, 0x31, {0x70}) == reply(0xB1, {0x70,0}));
    CHECK(exchange(device, stream, 0xB3, {2,0x70,0x55}).empty());
    CHECK(exchange(device, stream, 0xB3, {0}).empty());
    CHECK(exchange(device, stream, 0xB1, {0x70}).empty());
    CHECK(exchange(device, stream, 0x31, {0x70,0x55}).empty());
    CHECK(exchange(device, stream, 0xB3, {2,0x70,1,0xF8,0}).empty());
    CHECK(device.getMalformedFrameCount() >= 7);
    CHECK(exchange(device, stream, 0x31, {0x70}) == reply(0xB1, {0x70,0}));
}

void registerReadLimitsAndFullFrame() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    Bytes response = exchange(device, stream, 0x33, {0,123});
    CHECK(response.size() == 254);
    CHECK(response[2] == 0xB3 && response[3] == 254);
    CHECK(response[6] == 0 && response[7] == 0x12 && response[250] == 122);
    CHECK(exchange(device, stream, 0x33, {0,124}).empty());
    CHECK(exchange(device, stream, 0x33, {0xF0,17}).empty());
    CHECK(exchange(device, stream, 0x33, {0,0}).empty());
    CHECK(exchange(device, stream, 0x33, {0xFF,1}) == reply(0xB3, {0xFF,0}));
    Bytes pairs = {123};
    for (unsigned i = 0; i < 123; ++i) {
        pairs.push_back(i); pairs.push_back(i ^ 0x35);
    }
    CHECK(frame(0xB3, pairs).size() == 255);
    CHECK(exchange(device, stream, 0xB3, pairs) == reply(0x80));
    CHECK(exchange(device, stream, 0x31, {122}) == reply(0xB1, {122,122 ^ 0x35}));
    CHECK(device.getCrcErrorCount() == 0);
}

void broadcastsAndAddressFiltering() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 2);
    CHECK(exchange(device, stream, 0, {}, 2, 0xFF).empty());
    CHECK(device.getPendingCommandCount() == 1);
    CHECK(exchange(device, stream, 0xB1, {0x70,0x33}, 2, 0xFF).empty());
    CHECK(exchange(device, stream, 0x31, {0x70}, 2) == reply(0xB1, {0x70,0x33}, 2));
    CHECK(exchange(device, stream, 0xB1, {0x70,0x44}, 0).empty());
    CHECK(exchange(device, stream, 0x31, {0x70}, 2) == reply(0xB1, {0x70,0x44}, 2));
    CHECK(exchange(device, stream, 0xB1, {0x70,0x55}, 1).empty());
    CHECK(exchange(device, stream, 0x31, {0x70}, 2) == reply(0xB1, {0x70,0x44}, 2));
    stream.output.clear();
    stream.feed(frame(0xB1, {0x70,0x66}, 2, 0x11, 0x12)); device.loop();
    CHECK(stream.output.empty());
    CHECK(exchange(device, stream, 0x31, {0x70}, 2) == reply(0xB1, {0x70,0x44}, 2));
    CHECK(exchange(device, stream, 0x77, {}, 2).empty());
    CHECK(device.getUnknownCommandCount() == 1);
    CHECK(exchange(device, stream, 0x1D, {}, 2).empty());
    CHECK(device.getUnknownCommandCount() == 2);
}

void rawDatasetsAndStatusGuards() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(1);
    uint8_t length = 99; uint32_t received = 99;
    CHECK(!device.getDataset(0x1D, raw, length, received));
    CHECK(raw == nullptr && length == 0 && received == 0);
    float outside = 123; bool heating = true;
    CHECK(!device.getOutsideTemperature(outside) && outside == 123);
    CHECK(!device.getHeatingEnabled(heating) && heating);
    clockMillis = 1234;
    CHECK(exchange(device, stream, 0xBF, {0x10}) == reply(0x80));
    CHECK(device.getDataset(0x10, raw, length, received));
    CHECK(raw != nullptr && length == 0 && received == 1234);
    CHECK(!device.getDataset(0x0F, raw, length, received));
    CHECK(!device.getDataset(0x23, raw, length, received));
    Bytes status(11, 0); status[6] = 0xFB; status[10] = 0x40;
    CHECK(exchange(device, stream, 0xBF, dataset(0x1D, status)) == reply(0x80));
    CHECK(device.getOutsideTemperature(outside) && outside == -5);
    CHECK(device.getHeatingEnabled(heating) && heating);
    CHECK(device.getDataset(0x1D, raw, length, received));
    CHECK(length == 11 && received == 1234 && raw[6] == 0xFB);
    CHECK(device.getLastMasterDataset() == 0x1D);
    CHECK(exchange(device, stream, 0xBF, dataset(0x14, {0,1,0xC8})) == reply(0x80));
    CHECK(device.getOperatingMode() == 0xCA);
    CHECK(exchange(device, stream, 0x3F, {0x1D}).empty());
    CHECK(exchange(device, stream, 0xBF, dataset(0x1D, Bytes(30, 0))).empty());
    CHECK(exchange(device, stream, 0xBF, dataset(0x20, {1,2})).empty());
    CHECK(exchange(device, stream, 0xBF, {0xFE}).empty());
    CHECK(exchange(device, stream, 0xBF, {0xFF}).empty());
    CHECK(exchange(device, stream, 0xBF, {}).empty());
    CHECK(exchange(device, stream, 0xBF, dataset(0x1D, Bytes(6,0))) == reply(0x80));
    CHECK(!device.getOutsideTemperature(outside));
    CHECK(!device.getHeatingEnabled(heating));
    CHECK(exchange(device, stream, 0xBF, dataset(0x1D, Bytes(7,0))) == reply(0x80));
    CHECK(device.getOutsideTemperature(outside) && outside == 0);
    CHECK(!device.getHeatingEnabled(heating));
    CHECK(exchange(device, stream, 0xBF, dataset(0x22, Bytes(29,0))) == reply(0x80));
    CHECK(device.getDataset(0x22, raw, length, received) && length == 29);
}

void wifiDatasetRequestAndWrappedWrite() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    CHECK(exchange(device, stream, 0x3F, {0x20}) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 1);
    CHECK(exchange(device, stream, 0x3F, dataset(0x1D, {1,2,3,4,5,6,7})) == reply(0x80));
    const uint8_t* raw; uint8_t length; uint32_t received;
    CHECK(device.getDataset(0x1D, raw, length, received));
    CHECK(length == 7 && raw[6] == 7);
    CHECK(exchange(device, stream, 0x3F,
                   {0x34,0x91,0x82,uint8_t(0x1D ^ 0xAA),0xAB,0xA8,0xA9}) == reply(0x80));
    CHECK(device.getDataset(0x1D, raw, length, received));
    CHECK(length == 3 && raw[0] == 1 && raw[2] == 3);
    CHECK(exchange(device, stream, 0x3F, {0x34,0,0}).empty());
    CHECK(exchange(device, stream, 0x3F, {0x34,0}).empty());
    CHECK(exchange(device, stream, 0x3F, {}).empty());
    CHECK(exchange(device, stream, 0x3F, {0x34,0,0,uint8_t(0xFE ^ 0xAA),0}).empty());
    CHECK(device.setDesiredRoomTemperature(23));
    CHECK(exchange(device, stream, 0x3F, {0x15}) ==
          reply(0xBF, dataset(0x15, {0x0C,1,0xCD,23,0})));
    CHECK(device.getPendingCommandCount() == 2);
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x15, {0x0C,1,0xCD,23,0})));
    CHECK(exchange(device, stream, 0x3F, {0x20}) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(exchange(device, stream, 0x3F, {0x15}).empty());
}

void wifiRawDatasetRangeAndBothWrapperCommands() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    const uint8_t* raw; uint8_t length; uint32_t received;
    clockMillis = 123;
    CHECK(exchange(device, stream, 0x3F, {0x34,0xA8,0xA8,0xA2,0xAB,0xA8}) == reply(0x80));
    CHECK(device.getDataset(0x08, raw, length, received));
    CHECK(length == 2 && raw[0] == 1 && raw[1] == 2 && received == 123);
    CHECK(exchange(device, stream, 0xBF, {0x34,0xA8,0xA8,0xA2,0xA9,0xAE}) == reply(0x80));
    CHECK(device.getDataset(0x08, raw, length, received));
    CHECK(length == 2 && raw[0] == 3 && raw[1] == 4);
    CHECK(exchange(device, stream, 0xBF, dataset(0xAD, {0,1,0xFF,0x34})) == reply(0x80));
    CHECK(device.getDataset(0xAD, raw, length, received));
    CHECK(length == 4 && raw[0] == 0 && raw[2] == 0xFF && raw[3] == 0x34);
    CHECK(exchange(device, stream, 0xBF, dataset(0xBE, {0x55,0x66})) == reply(0x80));
    CHECK(device.getDataset(0xBE, raw, length, received));
    CHECK(length == 2 && raw[0] == 0x55 && raw[1] == 0x66);
    CHECK(exchange(device, stream, 0xBF, {0}) == reply(0x80));
    CHECK(device.getDataset(0, raw, length, received) && length == 0 && received == 123);

    Bytes maximum = {0x34,0xA8,0xA8,uint8_t(0xFD ^ 0xAA)};
    maximum.insert(maximum.end(), 29, 0xAA);
    CHECK(frame(0xBF, maximum).size() == 41);
    CHECK(exchange(device, stream, 0xBF, maximum) == reply(0x80));
    CHECK(device.getDataset(0xFD, raw, length, received) && length == 29 && raw[28] == 0);
    maximum.push_back(0xAA);
    CHECK(exchange(device, stream, 0xBF, maximum).empty());
    CHECK(exchange(device, stream, 0xBF, dataset(0xAD, Bytes(30,0))).empty());
    CHECK(exchange(device, stream, 0xBF, {0x34,0xA8,0xA8}).empty());
    CHECK(exchange(device, stream, 0xBF,
                   {0x34,0xA8,0xA8,uint8_t(0xFE ^ 0xAA)}).empty());
    CHECK(!device.getDataset(0xFE, raw, length, received));
    CHECK(raw == nullptr && length == 0 && received == 0);
    CHECK(!device.getDataset(0xFF, raw, length, received));
    for (uint8_t id : Bytes{0,0x08,0xAD,0xBE,0xFD})
        CHECK(exchange(device, stream, 0x3F, {id}).empty());
    CHECK(exchange(device, stream, 0xBF,
                   {0x34,0xA8,0xA8,uint8_t(0x20 ^ 0xAA),uint8_t(99 ^ 0xAA),0xAA,0xAA}) ==
          reply(0x80));
    CHECK(device.getDataset(0x20, raw, length, received) && length == 3 && raw[0] == 99);
    CHECK(exchange(device, stream, 0x3F, {0x20}) ==
          reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getCurrentRoomTemperature() == 20);

    FakeStream openvStream; KMBusVitotrol openv(&openvStream, 0x38, 1);
    profile(openv, "openv");
    CHECK(exchange(openv, openvStream, 0xBF, dataset(0xAD, {1,2})).empty());
    CHECK(!openv.getDataset(0xAD, raw, length, received));
    CHECK(exchange(openv, openvStream, 0xBF,
                   {0x34,0xA8,0xA8,0xA2,0xAB,0xA8}).empty());
    CHECK(!openv.getDataset(0x08, raw, length, received));
}

void profilesMigratePendingCommandsAndStatus() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 2);
    CHECK(device.setDesiredRoomTemperature(21));
    CHECK(device.setReducedRoomTemperature(17));
    CHECK(device.setOperatingMode("water"));
    CHECK(device.getPendingCommandCount() == 4);
    profile(device, "openv");
    CHECK(std::string(device.getProtocolProfile()) == "openv");
    CHECK(exchange(device, stream, 0, {}, 2) == reply(0xBF, dataset(0x21, {200,0,0}), 2));
    CHECK(exchange(device, stream, 0, {}, 2) == reply(0xBF, dataset(0x16, {0x0C,2,0xCD,21,0}), 2));
    CHECK(exchange(device, stream, 0, {}, 2) == reply(0xBF, dataset(0x16, {0x0C,2,0xCE,17,0}), 2));
    CHECK(exchange(device, stream, 0, {}, 2) == reply(0xBF, dataset(0x14, {0,2,0xC9,0,0,0,0,0}), 2));
    CHECK(device.getPendingCommandCount() == 0);
    Bytes status(11,0); status[6] = 12; status[10] = 0x40;
    CHECK(exchange(device, stream, 0xBF, dataset(0x1D, status), 2).empty());
    float outside; bool heating;
    CHECK(!device.getOutsideTemperature(outside));
    CHECK(!device.getHeatingEnabled(heating));
    CHECK(exchange(device, stream, 0xBF, dataset(0x1E, status), 2).empty());
    CHECK(device.getOutsideTemperature(outside) && outside == 12);
    CHECK(device.getHeatingEnabled(heating) && heating);
    CHECK(exchange(device, stream, 0x3F, dataset(0x1E, {1,2}), 2).empty());
    CHECK(exchange(device, stream, 0x3F, {0x21}, 2) ==
          reply(0xBF, dataset(0x21, {200,0,0}), 2));
    CHECK(device.setCurrentRoomTemperature(-2.5f));
    profile(device, "wifi");
    CHECK(exchange(device, stream, 0, {}, 2) == reply(0xBF, dataset(0x20, {0xE7,0xFF,0}), 2));
}

void allModeCommandsAndIndependentFlags() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    exchange(device, stream, 0);
    const char* names[] = {"off","water","heat_water","party_on","party_off",
                           "economy_on","economy_off"};
    const uint8_t commands[] = {0xC8,0xC9,0xCA,0xCB,0xCC,0xDC,0xDD};
    for (unsigned i = 0; i < 7; ++i) {
        CHECK(device.setOperatingMode(names[i]));
        CHECK(exchange(device, stream, 0) ==
              reply(0xBF, dataset(0x14,
                  {0,1,commands[i],uint8_t(commands[i] == 0xCB ? 20 : 0),0,0,0,0})));
        CHECK(device.getOperatingMode() == (i < 3 ? commands[i] : 0xCA));
        if (i == 3) CHECK(device.getPartyEnabled());
        if (i == 4) CHECK(!device.getPartyEnabled());
        if (i == 5) CHECK(device.getEconomyEnabled());
        if (i == 6) CHECK(!device.getEconomyEnabled());
    }
    CHECK(!device.setOperatingMode(nullptr));
    CHECK(!device.setOperatingMode("invented"));
    CHECK(device.setOperatingMode("water"));
    CHECK(device.setOperatingMode("party_on"));
    CHECK(device.setOperatingMode("economy_on"));
    CHECK(device.getOperatingMode() == 0xC9);
    CHECK(device.getPartyEnabled() && device.getEconomyEnabled());
}

void partyTemperatureIsAtomicAndPartOfModeCommand() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    CHECK(device.getPartyRoomTemperature() == 20);
    KMBusVitotrol::ControlUpdate update;
    update.hasPartyRoomTemperature = true; update.partyRoomTemperature = 22;
    CHECK(!device.applyControlUpdate(update));
    update.hasMode = true; update.mode = "water";
    CHECK(!device.applyControlUpdate(update));
    update.mode = "party_on"; update.partyRoomTemperature = 22.5f;
    CHECK(!device.applyControlUpdate(update));
    update.partyRoomTemperature = std::numeric_limits<float>::quiet_NaN();
    CHECK(!device.applyControlUpdate(update));
    update.partyRoomTemperature = std::numeric_limits<float>::infinity();
    CHECK(!device.applyControlUpdate(update));
    update.partyRoomTemperature = 36;
    CHECK(!device.applyControlUpdate(update));
    update.partyRoomTemperature = 4;
    CHECK(!device.applyControlUpdate(update));
    CHECK(device.getPartyRoomTemperature() == 20 && !device.getPartyEnabled());
    update.partyRoomTemperature = 22;
    CHECK(device.applyControlUpdate(update));
    CHECK(device.getPartyRoomTemperature() == 22 && device.getPartyEnabled());
    CHECK(device.getOperatingMode() == 0xCA && device.getPendingCommandCount() == 2);
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(exchange(device, stream, 0) ==
          reply(0xBF, dataset(0x14, {0,1,0xCB,22,0,0,0,0})));
    CHECK(device.setOperatingMode("party_on"));
    CHECK(exchange(device, stream, 0) ==
          reply(0xBF, dataset(0x14, {0,1,0xCB,22,0,0,0,0})));

    FakeStream fullStream; KMBusVitotrol full(&fullStream, 0x38, 1);
    CHECK(full.setOperatingMode("off"));
    CHECK(full.setOperatingMode("water"));
    CHECK(full.setOperatingMode("heat_water"));
    CHECK(full.getPendingCommandCount() == 4);
    update.hasProfile = true; update.profile = "openv";
    CHECK(!full.applyControlUpdate(update));
    CHECK(full.getPartyRoomTemperature() == 20 && !full.getPartyEnabled());
    CHECK(full.getOperatingMode() == 0xCA);
    CHECK(std::string(full.getProtocolProfile()) == "wifi");
}

void atomicUpdatesAndCapacity() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    KMBusVitotrol::ControlUpdate update;
    CHECK(!device.applyControlUpdate(update));
    CHECK(device.getPendingCommandCount() == 1);
    update.hasRoomTemperature = true; update.roomTemperature = 22;
    update.hasDesiredRoomTemperature = true; update.desiredRoomTemperature = 99;
    update.hasProfile = true; update.profile = "openv";
    CHECK(!device.applyControlUpdate(update));
    CHECK(device.getCurrentRoomTemperature() == 20);
    CHECK(std::string(device.getProtocolProfile()) == "wifi");
    CHECK(device.getPendingCommandCount() == 1);
    update.desiredRoomTemperature = 25;
    update.hasReducedRoomTemperature = true; update.reducedRoomTemperature = 17;
    update.hasMode = true; update.mode = "water";
    CHECK(!device.applyControlUpdate(update));
    CHECK(device.getCurrentRoomTemperature() == 20 && device.getOperatingMode() == 0xCA);
    update.hasMode = false;
    CHECK(device.applyControlUpdate(update));
    CHECK(device.getCurrentRoomTemperature() == 22);
    CHECK(device.getDesiredRoomTemperature() == 25);
    CHECK(device.getReducedRoomTemperature() == 17);
    CHECK(device.getPendingCommandCount() == 4);
    CHECK(!device.setDesiredRoomTemperature(30));
    CHECK(device.getDesiredRoomTemperature() == 25);
    CHECK(!device.setCurrentRoomTemperature(10));
    CHECK(device.getCurrentRoomTemperature() == 22);
    CHECK(!device.setReducedRoomTemperature(20));
    CHECK(device.getReducedRoomTemperature() == 17);
    profile(device, "wifi"); // Pure profile migration needs no queue capacity.
    CHECK(device.getPendingCommandCount() == 4);
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {220,0,0})));
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x15, {0x0C,1,0xCD,25,0})));
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x15, {0x0C,1,0xCE,17,0})));
    CHECK(!device.setCurrentRoomTemperature(std::numeric_limits<float>::quiet_NaN()));
    CHECK(!device.setCurrentRoomTemperature(std::numeric_limits<float>::infinity()));
    CHECK(!device.setCurrentRoomTemperature(-21));
    CHECK(!device.setDesiredRoomTemperature(4));
    CHECK(!device.setReducedRoomTemperature(36));
    CHECK(!device.setDesiredRoomTemperature(20.5f));
    CHECK(!device.setReducedRoomTemperature(16.5f));
    CHECK(device.getDesiredRoomTemperature() == 25);
    CHECK(device.getReducedRoomTemperature() == 17);
    update = {};
    update.hasProfile = true; update.profile = nullptr;
    CHECK(!device.applyControlUpdate(update));
    update.profile = "unknown";
    CHECK(!device.applyControlUpdate(update));
}

void partialAndFailedWrites() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    stream.writeResults = {3,0,2,7};
    exchange(device, stream, 0);
    CHECK(stream.output.size() == 3 && device.getPendingCommandCount() == 1);
    CHECK(stream.writeCalls == 1);
    const Bytes prefix = stream.output;
    device.loop();
    CHECK(stream.output == prefix && stream.writeCalls == 2);
    CHECK(device.getPendingCommandCount() == 1);
    KMBusVitotrol::ControlUpdate update;
    update.hasProfile = true; update.profile = "openv";
    CHECK(!device.applyControlUpdate(update)); // Cannot migrate a frame already on wire.
    CHECK(std::string(device.getProtocolProfile()) == "wifi");
    device.loop();
    CHECK(stream.output.size() == 5 && device.getPendingCommandCount() == 1);
    device.loop();
    CHECK(stream.output == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 0 && stream.flushCalls == 0);

    FakeStream blocked; KMBusVitotrol retry(&blocked, 0x38, 2);
    blocked.writeResults = {0,0};
    CHECK(exchange(retry, blocked, 0, {}, 2).empty());
    CHECK(retry.getPendingCommandCount() == 1);
    retry.loop();
    CHECK(blocked.writeCalls == 2 && blocked.output.empty());
    profile(retry, "openv"); // Zero accepted bytes can be safely re-encoded.
    CHECK(retry.getPendingCommandCount() == 1);
    CHECK(exchange(retry, blocked, 0, {}, 2) == reply(0xBF, dataset(0x21, {200,0,0}), 2));

    FakeStream failed; KMBusVitotrol negative(&failed, 0x38, 1);
    failed.writeResults = {std::numeric_limits<size_t>::max()};
    CHECK(exchange(negative, failed, 0).empty());
    CHECK(negative.getPendingCommandCount() == 1);
    negative.loop();
    CHECK(failed.output == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(negative.getPendingCommandCount() == 0);
}

void newMasterTrafficAbortsPendingTransmission() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    stream.writeResults = {3};
    CHECK(exchange(device, stream, 0).size() == 3);
    CHECK(device.getPendingCommandCount() == 1 && stream.writeCalls == 1);
    CHECK(exchange(device, stream, 0x31, {0xF9}) == reply(0xB1, {0xF9,0x38}));
    CHECK(device.getPendingCommandCount() == 1 && stream.writeCalls == 2);
    stream.output.clear(); device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 2);
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 0);

    FakeStream stalled; KMBusVitotrol blocked(&stalled, 0x38, 1);
    stalled.writeResults = {0};
    CHECK(exchange(blocked, stalled, 0x31, {0xF8}).empty());
    CHECK(stalled.writeCalls == 1);
    const Bytes fresh = frame(0x31, {0xF9});
    stalled.feed({fresh[0]}); blocked.loop();
    CHECK(stalled.output.empty() && stalled.writeCalls == 1);
    stalled.feed(Bytes(fresh.begin() + 1, fresh.end())); blocked.loop();
    CHECK(stalled.output == reply(0xB1, {0xF9,0x38}));
    CHECK(stalled.writeCalls == 2);

    FakeStream repolled; KMBusVitotrol retry(&repolled, 0x38, 1);
    repolled.writeResults = {3};
    CHECK(exchange(retry, repolled, 0).size() == 3);
    CHECK(exchange(retry, repolled, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(retry.getPendingCommandCount() == 0 && repolled.writeCalls == 2);
}

void transportRetryTimeoutKeepsQueuedCommands() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 2);
    stream.writeResults = {0,0};
    CHECK(exchange(device, stream, 0, {}, 2).empty());
    clockMillis += 499; device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 2);
    clockMillis += 1; device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 2);
    CHECK(device.getPendingCommandCount() == 1);
    clockMillis += 1000; device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 2);
    profile(device, "openv");
    CHECK(exchange(device, stream, 0, {}, 2) ==
          reply(0xBF, dataset(0x21, {200,0,0}), 2));
    CHECK(device.getPendingCommandCount() == 0);

    FakeStream partial; KMBusVitotrol truncated(&partial, 0x38, 2);
    partial.writeResults = {3};
    CHECK(exchange(truncated, partial, 0, {}, 2).size() == 3);
    KMBusVitotrol::ControlUpdate update;
    update.hasProfile = true; update.profile = "openv";
    CHECK(!truncated.applyControlUpdate(update));
    partial.output.clear();
    clockMillis += 500; truncated.loop();
    CHECK(partial.output.empty() && partial.writeCalls == 1);
    CHECK(truncated.getPendingCommandCount() == 1);
    CHECK(truncated.applyControlUpdate(update));
    CHECK(exchange(truncated, partial, 0, {}, 2) ==
          reply(0xBF, dataset(0x21, {200,0,0}), 2));
    CHECK(truncated.getPendingCommandCount() == 0);
}

void parserNoiseCrcAndTimeouts() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    Bytes bad = frame(0); bad.back() ^= 1;
    stream.feed({0x03,0x11,0x81,0x22,0xFF,0x99});
    stream.feed(bad); stream.feed(frame(0x31, {0xF8})); device.loop();
    CHECK(stream.output == reply(0xB1, {0xF8,0x11}));
    CHECK(device.getCrcErrorCount() == 1);
    const Bytes ambiguousPrefixes[] = {
        {0x11,0,0,0xFF,1,1},
        {0x11,0,0xB3,255,1,1,255},
        {0x11,0,0xBF,38,1,1,0x20},
    };
    for (const Bytes& prefix : ambiguousPrefixes) {
        stream.output.clear();
        stream.feed(prefix); stream.feed(frame(0x31, {0xF8})); device.loop();
        CHECK(stream.output.empty());
        clockMillis += 500; device.loop();
        CHECK(stream.output.empty());
        CHECK(exchange(device, stream, 0x31, {0xF8}) == reply(0xB1, {0xF8,0x11}));
    }
    stream.output.clear();
    stream.feed({0x11,0,0xBF,38,1,1,0x10}); device.loop();
    CHECK(stream.output.empty());
    clockMillis += 500;
    stream.feed(frame(0x31, {0xFA})); device.loop();
    CHECK(stream.output == reply(0xB1, {0xFA,0}));

    stream.output.clear();
    const Bytes good = frame(0x31, {0xFB});
    stream.feed(Bytes(good.begin(), good.begin() + 4)); device.loop();
    clockMillis += 499;
    stream.feed(Bytes(good.begin() + 4, good.end())); device.loop();
    CHECK(stream.output == reply(0xB1, {0xFB,0x11}));
    stream.output.clear();
    stream.feed({0x11,0,0xBF,38,1,1,0x10});
    stream.feed(frame(0x31, {0xF8})); device.loop();
    CHECK(stream.output.empty());
    clockMillis += 500; device.loop(); // Buffered suffix has lost its bus grant.
    CHECK(stream.output.empty());
    stream.feed(frame(0x31, {0xF8})); device.loop();
    CHECK(stream.output == reply(0xB1, {0xF8,0x11}));
}

void parserDoesNotProcessEmbeddedValidFrame() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    const Bytes embedded = frame(0x31, {0xF9});
    // Master datasets are XOR encoded, so any valid-looking bytes may be payload.
    Bytes payload = {0x1D};
    payload.insert(payload.end(), embedded.begin(), embedded.end());
    payload.insert(payload.end(), {0xAA,0xAA});
    const Bytes outer = frame(0xBF, payload);
    stream.feed(Bytes(outer.begin(), outer.end() - 3)); device.loop();
    CHECK(stream.output.empty());
    stream.feed(Bytes(outer.end() - 3, outer.end())); device.loop();
    CHECK(stream.output == reply(0x80));
    const uint8_t* data; uint8_t length; uint32_t received;
    CHECK(device.getDataset(0x1D, data, length, received) && length == 11);
    CHECK(data[0] == (embedded[0] ^ 0xAA));

    stream.output.clear();
    Bytes corrupt = frame(0xBF, payload);
    corrupt[3] = 9; // CRC no longer valid; following frame is inside old payload.
    stream.feed(corrupt); stream.feed(frame(0x31, {0xF8})); device.loop();
    CHECK(stream.output.size() >= 10);
    CHECK(Bytes(stream.output.end() - 10, stream.output.end()) == reply(0xB1, {0xF8,0x11}));
}

void unrelatedFramesAndEchoesOwnTheirPayload() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    const Bytes embeddedPing = frame(0);
    const Bytes echo = frame(0xBF, embeddedPing, 1, 0x00, 0x11);
    stream.feed(Bytes(echo.begin(), echo.end() - 2)); device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 0);
    CHECK(!device.isOnline() && device.getPendingCommandCount() == 1);
    stream.feed(Bytes(echo.end() - 2, echo.end())); device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 0);
    CHECK(!device.isOnline());
    const Bytes unrelated[] = {
        frame(0x00, embeddedPing, 1, 0x11, 0x11),
        frame(0x00, embeddedPing, 1, 0x11, 0xFF),
        frame(0x00, embeddedPing, 2, 0x11, 0x00),
        frame(0x90, embeddedPing, 1, 0x00, 0x00),
        frame(0x00, embeddedPing, 1, 0x04, 0x00),
        frame(0x00, embeddedPing, 1, 0x11, 0x04),
    };
    for (const Bytes& outer : unrelated) {
        stream.feed(outer); device.loop();
        CHECK(stream.output.empty() && stream.writeCalls == 0);
        CHECK(!device.isOnline() && device.getPendingCommandCount() == 1);
    }
    CHECK(device.getMalformedFrameCount() == 0);
    CHECK(device.getUnknownCommandCount() == 0);
    CHECK(device.getCrcErrorCount() == 0);
    CHECK(exchange(device, stream, 0x31, {0xF8}) == reply(0xB1, {0xF8,0x11}));
    CHECK(device.getPendingCommandCount() == 1 && device.isOnline());
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 0);
}

void corruptOuterFrameCannotFabricateBusGrant() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    Bytes payload = {0x1D};
    const Bytes ping = frame(0);
    payload.insert(payload.end(), ping.begin(), ping.end());
    Bytes bad = frame(0xBF, payload); bad.back() ^= 1;
    stream.feed(bad); device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 0);
    CHECK(device.getCrcErrorCount() == 1 && device.getPendingCommandCount() == 1);
    CHECK(!device.isOnline());
    stream.feed(Bytes(bad.begin(), bad.end() - 2)); device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 0);
    stream.feed(Bytes(bad.end() - 2, bad.end())); device.loop();
    CHECK(stream.output.empty() && stream.writeCalls == 0);
    CHECK(device.getCrcErrorCount() == 2 && device.getPendingCommandCount() == 1);
    stream.feed(bad); stream.feed(frame(0x31, {0xF8})); device.loop();
    CHECK(stream.output == reply(0xB1, {0xF8,0x11}));
    CHECK(device.getCrcErrorCount() == 3 && device.getPendingCommandCount() == 1);
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 0);
}

void malformedValidCrcPayloadCannotFabricateBusGrant() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    const Bytes ping = frame(0);
    Bytes badRoom = {0x20}; badRoom.insert(badRoom.end(), ping.begin(), ping.end());
    Bytes badCount = {2}; badCount.insert(badCount.end(), ping.begin(), ping.end());
    Bytes badWrapper = {0x34,0xA8,0xA8,uint8_t(0xFE ^ 0xAA)};
    badWrapper.insert(badWrapper.end(), ping.begin(), ping.end());
    const Bytes invalid[] = {
        frame(0xBF, badRoom), frame(0xB3, badCount), frame(0, ping),
        frame(0x31, ping), frame(0xBF, badWrapper),
    };
    for (const Bytes& outer : invalid) {
        stream.feed(Bytes(outer.begin(), outer.end() - 2)); device.loop();
        CHECK(stream.output.empty() && stream.writeCalls == 0);
        CHECK(device.getPendingCommandCount() == 1 && !device.isOnline());
        stream.feed(Bytes(outer.end() - 2, outer.end())); device.loop();
        CHECK(stream.output.empty() && stream.writeCalls == 0);
        CHECK(device.getPendingCommandCount() == 1 && !device.isOnline());
    }
    CHECK(device.getMalformedFrameCount() == 5);
    CHECK(device.getCrcErrorCount() == 0);
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 0 && device.isOnline());
}

void schedulingAndOnlineWraparound() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x38, 1);
    device.loop();
    CHECK(stream.output.empty() && !device.isOnline());
    stream.writeAdvanceMillis = 1;
    CHECK(exchange(device, stream, 0) == reply(0xBF, dataset(0x20, {200,0,0})));
    CHECK(device.getPendingCommandCount() == 0);
    stream.writeAdvanceMillis = 0;
    CHECK(device.isOnline());
    stream.output.clear();
    clockMillis += 30000; device.loop();
    CHECK(stream.output.empty() && device.getPendingCommandCount() == 1);
    clockMillis += 30000; device.loop();
    CHECK(!device.isOnline() && stream.output.empty());
    clockMillis = 0xFFFFFF00;
    exchange(device, stream, 0x31, {0});
    CHECK(device.getLastMessageMillis() == 0xFFFFFF00 && device.isOnline());
    clockMillis = 0x100;
    CHECK(device.isOnline());
    clockMillis = 0x100 + 60000;
    CHECK(!device.isOnline());
    KMBusVitotrol noSerial(nullptr,0x38,1);
    noSerial.loop();
    CHECK(!noSerial.isOnline());
}

void boundaryTemperaturesAndSlotThree() {
    FakeStream stream; KMBusVitotrol device(&stream, 0x34, 3);
    profile(device, "openv");
    CHECK(exchange(device, stream, 0, {}, 3) ==
          reply(0xBF, dataset(0x22, {200,0,0}), 3));
    CHECK(device.setCurrentRoomTemperature(-20));
    CHECK(exchange(device, stream, 0, {}, 3) ==
          reply(0xBF, dataset(0x22, {0x38,0xFF,0}), 3));
    CHECK(device.setCurrentRoomTemperature(50));
    CHECK(exchange(device, stream, 0, {}, 3) ==
          reply(0xBF, dataset(0x22, {0xF4,1,0}), 3));
    CHECK(device.setDesiredRoomTemperature(5));
    CHECK(exchange(device, stream, 0, {}, 3) ==
          reply(0xBF, dataset(0x17, {0x0C,3,0xCD,5,0}), 3));
    CHECK(device.setReducedRoomTemperature(35));
    CHECK(exchange(device, stream, 0, {}, 3) ==
          reply(0xBF, dataset(0x17, {0x0C,3,0xCE,35,0}), 3));
    Bytes status(11,0); status[6] = 127;
    CHECK(exchange(device, stream, 0xBF, dataset(0x1F, status), 3).empty());
    float outside; bool heating = true;
    CHECK(device.getOutsideTemperature(outside) && outside == 127);
    CHECK(device.getHeatingEnabled(heating) && !heating);
    CHECK(exchange(device, stream, 0x3F, {0x34,0,0,uint8_t(0x1F ^ 0xAA)}, 3).empty());
    CHECK(device.getOutsideTemperature(outside) && outside == 127);
}
}

unsigned long millis() { return clockMillis; }

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"source fixtures and initial temperature", sourceFixturesAndInitialTemperature},
        {"register commands and source variants", registerCommandsAndVariants},
        {"invalid writes are atomic and silent", invalidWritesAreAtomicAndSilent},
        {"register read limits and full 255-byte frame", registerReadLimitsAndFullFrame},
        {"broadcasts and address filtering", broadcastsAndAddressFiltering},
        {"raw datasets and status guards", rawDatasetsAndStatusGuards},
        {"WiFi requests and wrapped writes", wifiDatasetRequestAndWrappedWrite},
        {"WiFi raw IDs and BF/3F wrappers", wifiRawDatasetRangeAndBothWrapperCommands},
        {"profile migration and own-slot status", profilesMigratePendingCommandsAndStatus},
        {"all modes and independent flags", allModeCommandsAndIndependentFlags},
        {"party temperature atomic mode payload", partyTemperatureIsAtomicAndPartOfModeCommand},
        {"atomic updates and capacity", atomicUpdatesAndCapacity},
        {"partial and failed writes", partialAndFailedWrites},
        {"new master traffic aborts stale TX", newMasterTrafficAbortsPendingTransmission},
        {"bounded TX retry preserves commands", transportRetryTimeoutKeepsQueuedCommands},
        {"parser noise, CRC, timeout", parserNoiseCrcAndTimeouts},
        {"embedded frame protection", parserDoesNotProcessEmbeddedValidFrame},
        {"unrelated frames and echoes own payload", unrelatedFramesAndEchoesOwnTheirPayload},
        {"corrupt outer cannot fabricate grant", corruptOuterFrameCannotFabricateBusGrant},
        {"malformed CRC-valid payload owns frame", malformedValidCrcPayloadCannotFabricateBusGrant},
        {"scheduling and online wraparound", schedulingAndOnlineWraparound},
        {"temperature boundaries and slot three", boundaryTemperaturesAndSlotThree},
    };
    unsigned failed = 0;
    for (const Test& test : tests) {
        clockMillis = 0;
        try { test.run(); std::printf("PASS %s\n", test.name); }
        catch (const std::exception& error) {
            ++failed; std::fprintf(stderr, "FAIL %s: %s\n", test.name, error.what());
        }
    }
    std::printf("%zu tests, %u assertions, %u failures\n",
                sizeof(tests) / sizeof(tests[0]), assertions, failed);
    return failed ? 1 : 0;
}
