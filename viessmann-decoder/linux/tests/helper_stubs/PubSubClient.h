#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define MQTT_MAX_PACKET_SIZE 256
#define MQTT_MAX_HEADER_SIZE 5

class Client {};

struct PublishAttempt {
    std::string topic;
    std::string payload;
    bool retained;
};

// A deterministic test double, not an implementation of the MQTT protocol.
class PubSubClient {
public:
    bool online = false;
    bool failResize = false;
    uint16_t bufferSize = MQTT_MAX_PACKET_SIZE;
    unsigned resizeCalls = 0;
    unsigned failures = 0;
    std::vector<PublishAttempt> attempts;

    explicit PubSubClient(Client&) {}
    void setServer(const char*, uint16_t) {}
    bool connected() const { return online; }
    bool connect(const char*) { return online = true; }
    bool connect(const char*, const char*, const char*) { return online = true; }
    void disconnect() { online = false; }
    void loop() {}

    bool setBufferSize(uint16_t size) {
        ++resizeCalls;
        if (failResize) return false;
        bufferSize = size;
        return true;
    }
    uint16_t getBufferSize() { return bufferSize; }

    bool publish(const char* topic, const char* payload, bool retained = false) {
        attempts.push_back({topic, payload, retained});
        if (bufferSize < MQTT_MAX_HEADER_SIZE + 2 +
            std::string(topic).size() + std::string(payload).size()) return false;
        if (failures) {
            --failures;
            return false;
        }
        return online;
    }

    void setCallback(void (*)(char*, uint8_t*, unsigned int)) {}
};
