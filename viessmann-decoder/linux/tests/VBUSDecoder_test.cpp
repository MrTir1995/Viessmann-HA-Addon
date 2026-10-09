#include "vbusdecoder.h"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned long clockMillis = 0;
unsigned assertions = 0;
#define CHECK(condition) do { ++assertions; if (!(condition)) \
    throw std::runtime_error(std::string(__func__) + ":" + \
    std::to_string(__LINE__) + ": " #condition); } while (false)
using Bytes = std::vector<uint8_t>;

class FakeStream : public Stream {
public:
    std::deque<uint8_t> input;
    bool failRead = false;
    int available() override { return static_cast<int>(input.size()); }
    int read() override {
        if (failRead) { failRead = false; return -1; }
        if (input.empty()) return -1;
        const int value = input.front();
        input.pop_front();
        return value;
    }
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t*, size_t length) override { return length; }
    void flush() override {}
    void feed(const Bytes& bytes) { input.insert(input.end(), bytes.begin(), bytes.end()); }
};

uint8_t vbusCRC(const Bytes& bytes, size_t offset, size_t length) {
    uint8_t crc = 0x7F;
    for (size_t i = 0; i < length; ++i) crc = (crc - bytes[offset + i]) & 0x7F;
    return crc;
}

Bytes vbusFrame(uint16_t source = 0x1234, uint8_t count = 2,
                uint16_t command = 0x0100) {
    Bytes bytes = {0xAA, 0x10, 0, static_cast<uint8_t>(source),
                   static_cast<uint8_t>(source >> 8), 0x10,
                   static_cast<uint8_t>(command), static_cast<uint8_t>(command >> 8),
                   count};
    bytes.push_back(vbusCRC(bytes, 1, 8));
    for (uint8_t i = 0; i < count; ++i) {
        const size_t offset = bytes.size();
        bytes.insert(bytes.end(), {100, 0, 110, 0, 0});
        bytes.push_back(vbusCRC(bytes, offset, 5));
    }
    return bytes;
}

uint16_t kmCRC(const Bytes& bytes, size_t offset, size_t length) {
    uint16_t crc = 0;
    for (size_t i = offset; i < offset + length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return crc;
}

Bytes protocolFrame(ProtocolType protocol) {
    if (protocol == PROTOCOL_VBUS) return vbusFrame();
    if (protocol == PROTOCOL_KW) {
        Bytes frame = {1, 3, 0x10, 0, 100};
        uint8_t crc = 0;
        for (uint8_t byte : frame) crc ^= byte;
        frame.push_back(crc);
        return frame;
    }
    if (protocol == PROTOCOL_P300) {
        Bytes frame = {5, 5, 1, 0, 0x10, 0, 100};
        uint8_t crc = 0;
        for (uint8_t byte : frame) crc += byte;
        frame.push_back(crc);
        return frame;
    }
    Bytes frame = {0x68, 15, 15, 0x68, 0xBF, 1, 1, 0x1C};
    frame.insert(frame.end(), 11, 0xAA);
    const uint16_t crc = kmCRC(frame, 4, 15);
    frame.push_back(static_cast<uint8_t>(crc));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    frame.push_back(0x16);
    return frame;
}

void drain(VBUSDecoder& decoder, FakeStream& stream) {
    for (unsigned i = 0; i < 600 && stream.available(); ++i) decoder.loop();
    CHECK(stream.available() == 0);
    decoder.loop();
    decoder.loop();
}

void gettersAndBeginReset() {
    FakeStream stream;
    VBUSDecoder decoder(&stream);
    CHECK(decoder.getCurrentSourceAddress() == 0);
    for (unsigned i = 0; i < 256; ++i) {
        CHECK(decoder.getTemp(i) == 0);
        CHECK(decoder.getPump(i) == 0);
        CHECK(!decoder.getRelay(i));
        CHECK(decoder.getOperatingHours(i) == 0);
    }
    decoder.begin();
    stream.feed(vbusFrame());
    drain(decoder, stream);
    CHECK(decoder.isReady() && decoder.getTemp(0) == 10);
    CHECK(decoder.getCurrentSourceAddress() == 0x1234);
    decoder.begin(PROTOCOL_KW);
    CHECK(!decoder.isReady());
    CHECK(decoder.getCurrentSourceAddress() == 0);
    CHECK(decoder.getTempNum() == 0 && decoder.getPumpNum() == 0 &&
          decoder.getRelayNum() == 0);
    stream.feed(protocolFrame(PROTOCOL_KW));
    drain(decoder, stream);
    CHECK(decoder.isReady());
    CHECK(decoder.getParticipantCount() == 1);
    CHECK(decoder.getTemp(255) == 0 && decoder.getPump(255) == 0 &&
          !decoder.getRelay(255) && decoder.getOperatingHours(255) == 0);
}

void receiveTimeoutAndRecovery() {
    for (ProtocolType protocol : {PROTOCOL_VBUS, PROTOCOL_KW, PROTOCOL_P300, PROTOCOL_KM}) {
        clockMillis = 0;
        FakeStream stream;
        VBUSDecoder decoder(&stream);
        decoder.begin(protocol);
        stream.feed(protocolFrame(protocol));
        drain(decoder, stream);
        CHECK(decoder.isReady() && decoder.getVbusStat());
        CHECK(decoder.getCurrentSourceAddress() ==
              (protocol == PROTOCOL_VBUS ? 0x1234 : 0));
        CHECK(decoder.getParticipantCount() == (protocol == PROTOCOL_VBUS ? 1 : 0));
        const Bytes frame = protocolFrame(protocol);
        stream.feed(Bytes(frame.begin(), frame.begin() + 2));
        decoder.loop();
        decoder.loop();
        clockMillis += 499;
        decoder.loop();
        CHECK(decoder.isReady());
        clockMillis += 1;
        decoder.loop();
        CHECK(!decoder.isReady() && !decoder.getVbusStat());
        CHECK(decoder.getCurrentSourceAddress() == 0);
        stream.feed(frame);
        drain(decoder, stream);
        CHECK(decoder.isReady() && decoder.getVbusStat());

        stream.feed(Bytes(frame.begin(), frame.begin() + 2));
        decoder.loop();
        decoder.loop();
        clockMillis += 400;
        stream.feed({frame[2]});
        decoder.loop();
        clockMillis += 400;
        decoder.loop();
        CHECK(decoder.isReady());
        clockMillis += 100;
        decoder.loop();
        CHECK(!decoder.isReady());
    }
}

void clockWraparoundAndIdleTimeout() {
    for (ProtocolType protocol : {PROTOCOL_VBUS, PROTOCOL_KW, PROTOCOL_P300, PROTOCOL_KM}) {
        clockMillis = 0xFFFFFFF0UL;
        FakeStream stream;
        VBUSDecoder decoder(&stream);
        decoder.begin(protocol);
        stream.feed(protocolFrame(protocol));
        drain(decoder, stream);
        CHECK(decoder.isReady());
        clockMillis += 32;
        decoder.loop();
        decoder.loop();
        CHECK(decoder.isReady() && decoder.getVbusStat());
        stream.feed(protocolFrame(protocol));
        drain(decoder, stream);
        CHECK(decoder.isReady());
        decoder.loop();
        decoder.loop();
        CHECK(decoder.isReady() && decoder.getVbusStat());
        clockMillis += 20001;
        decoder.loop();
        decoder.loop();
        CHECK(!decoder.isReady() && !decoder.getVbusStat());

        clockMillis = 0xFFFFFFF0UL;
        decoder.begin(protocol);
        const Bytes frame = protocolFrame(protocol);
        stream.feed(Bytes(frame.begin(), frame.begin() + 2));
        decoder.loop();
        decoder.loop();
        clockMillis += 500;
        decoder.loop();
        CHECK(!decoder.getVbusStat());
        stream.feed(frame);
        drain(decoder, stream);
        CHECK(decoder.isReady() && decoder.getVbusStat());
    }
}

void deviceFrameMinimums() {
    struct Device { uint16_t source; uint8_t frames; uint8_t temperatures; };
    const Device devices[] = {{0x1234,2,4}, {0x1060,6,12},
                              {0x7E11,3,6}, {0x7E21,3,6}, {0x7E31,2,4}};
    for (const Device& device : devices) {
        FakeStream stream;
        VBUSDecoder decoder(&stream);
        decoder.begin();
        stream.feed(vbusFrame(device.source, device.frames));
        drain(decoder, stream);
        CHECK(decoder.isReady() && decoder.getTempNum() == device.temperatures);
        const float previous = decoder.getTemp(device.temperatures - 1);
        stream.feed(vbusFrame(device.source, device.frames - 1));
        drain(decoder, stream);
        CHECK(!decoder.isReady() && !decoder.getVbusStat());
        CHECK(decoder.getTemp(device.temperatures - 1) == previous);
        stream.feed(vbusFrame(device.source, device.frames));
        drain(decoder, stream);
        CHECK(decoder.isReady() && decoder.getVbusStat());
    }
}

void optionalDeviceFramesDoNotReuseOldData() {
    struct Device { uint16_t source; uint8_t minimum; uint8_t full; };
    const Device devices[] = {{0x1060,6,15}, {0x7E11,3,7},
                              {0x7E21,3,7}, {0x7E31,2,6}};
    for (const Device& device : devices) {
        FakeStream stream;
        VBUSDecoder decoder(&stream);
        decoder.begin();
        for (uint8_t frames = device.minimum; frames <= device.full; ++frames) {
            stream.feed(vbusFrame(device.source, device.full));
            drain(decoder, stream);
            CHECK(decoder.isReady() && decoder.getPumpNum() > 0);
            stream.feed(vbusFrame(device.source, frames));
            drain(decoder, stream);
            CHECK(decoder.isReady() && decoder.getVbusStat());
            if (device.source == 0x1060) {
                CHECK(decoder.getPumpNum() == (frames >= 13 ? 7 : (frames >= 12 ? 4 : 0)));
                CHECK(decoder.getErrorMask() == (frames >= 14 ? 100 : 0));
                CHECK(decoder.getSystemTime() == (frames >= 14 ? 110 : 0));
                CHECK(decoder.getSystemVariant() == (frames >= 15 ? 100 : 0));
            } else {
                const bool mx = device.source == 0x7E31;
                CHECK(decoder.getPumpNum() == (frames >= (mx ? 3 : 5) ? (mx ? 4 : 2) : 0));
                CHECK(decoder.getOperatingHours(0) == (frames >= (mx ? 4 : 6) ? 100 : 0));
                CHECK(decoder.getHeatQuantity() == (frames >= (mx ? 5 : 7) ? 100 : 0));
                if (mx) CHECK(decoder.getErrorMask() == (frames >= 6 ? 100 : 0));
            }
            CHECK(decoder.getRelayNum() == decoder.getPumpNum());
            for (uint8_t i = decoder.getPumpNum(); i < (device.source == 0x1060 ? 7 : 2); ++i) {
                CHECK(decoder.getPump(i) == 0);
                CHECK(!decoder.getRelay(i));
            }
        }
    }
}

void queuedFramesAndBounds() {
    FakeStream stream;
    VBUSDecoder decoder(&stream);
    decoder.begin();
    stream.feed(vbusFrame(0x1234, 2, 0x0200));
    stream.feed(vbusFrame());
    drain(decoder, stream);
    CHECK(decoder.isReady() && decoder.getTemp(0) == 10);
    stream.feed(vbusFrame(0x1234, 41));
    drain(decoder, stream);
    CHECK(decoder.isReady());
    stream.feed(vbusFrame(0x1234, 42));
    drain(decoder, stream);
    CHECK(!decoder.isReady());
    stream.feed(vbusFrame());
    drain(decoder, stream);
    CHECK(decoder.isReady());
}

void negativeReadsDoNotBecomeData() {
    for (ProtocolType protocol : {PROTOCOL_VBUS, PROTOCOL_KW, PROTOCOL_P300, PROTOCOL_KM}) {
        FakeStream stream;
        VBUSDecoder decoder(&stream);
        decoder.begin(protocol);
        stream.feed(protocolFrame(protocol));
        decoder.loop();
        stream.failRead = true;
        decoder.loop();
        drain(decoder, stream);
        CHECK(decoder.isReady() && decoder.getVbusStat());
    }
}
}

unsigned long millis() { return clockMillis; }
void delay(unsigned long) {}

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"getter bounds and begin reset", gettersAndBeginReset},
        {"receive timeout and recovery", receiveTimeoutAndRecovery},
        {"clock wraparound and idle timeout", clockWraparoundAndIdleTimeout},
        {"device frame minimums", deviceFrameMinimums},
        {"optional device frames never reuse old data", optionalDeviceFramesDoNotReuseOldData},
        {"queued frames and buffer bounds", queuedFramesAndBounds},
        {"negative reads do not become data", negativeReadsDoNotBecomeData},
    };
    unsigned failed = 0;
    for (const Test& test : tests) {
        clockMillis = 0;
        try { test.run(); std::printf("PASS %s\n", test.name); }
        catch (const std::exception& error) {
            ++failed;
            std::fprintf(stderr, "FAIL %s: %s\n", test.name, error.what());
        }
    }
    std::printf("%u assertions, %u failures\n", assertions, failed);
    return failed ? 1 : 0;
}
