#include <Arduino.h>
#include <PubSubClient.h>

#include <cstdio>
#include <limits>
#include <stdexcept>

// Seed decoder state without involving protocol parsing or a serial device.
#define private public
#include "VBUSDataLogger.h"
#include "VBUSScheduler.h"
#include "VBUSMqttClient.h"
#undef private

namespace {
uint32_t clockMillis = 0;
unsigned assertions = 0;

#define CHECK(condition) do { ++assertions; if (!(condition)) \
    throw std::runtime_error(std::string(__func__) + ":" + \
    std::to_string(__LINE__) + ": " #condition); } while (false)

void zeroCapacity(VBUSDecoder& decoder) {
    VBUSDataLogger logger(&decoder, 0);
    logger.begin();
    logger.logNow();
    logger.loop();
    CHECK(logger.getDataPointCount() == 0);
    CHECK(logger.getLatestDataPoint() == nullptr);
    CHECK(logger.getOldestDataPoint() == nullptr);
    CHECK(logger.getDataPoint(0) == nullptr);

    logger.setMaxDataPoints(2);
    logger.logNow();
    CHECK(logger.getDataPointCount() == 1);
    logger.setMaxDataPoints(0);
    logger.logNow();
    CHECK(logger.getDataPointCount() == 0);
    CHECK(logger.getLatestDataPoint() == nullptr);
    logger.setMaxDataPoints(1);
    logger.logNow();
    CHECK(logger.getDataPointCount() == 1);
    logger.clear();
    CHECK(logger.getOldestDataPoint() == nullptr);
}

void recentStatistics(VBUSDecoder& decoder) {
    VBUSDataLogger logger(&decoder, 1);
    clockMillis = 10000;
    logger.logNow();
    CHECK(logger.getStatisticsLastHours(1).tempAvg[0] == 10);
    CHECK(logger.getStatisticsLastHours(255).tempAvg[1] == 20);
}

void channelStatistics(VBUSDecoder& decoder) {
    VBUSDataLogger logger(&decoder, 3);
    clockMillis = 10000;
    decoder._temp[0] = 10; decoder._temp[1] = 20;
    logger.logNow();
    clockMillis = 20000;
    decoder._temp[0] = -999; decoder._temp[1] = 40;
    logger.logNow();
    clockMillis = 30000;
    decoder._temp[0] = 30;
    decoder._temp[1] = std::numeric_limits<float>::quiet_NaN();
    logger.logNow();

    DataStats stats = logger.getStatisticsAll();
    CHECK(stats.tempAvg[0] == 20);
    CHECK(stats.tempAvg[1] == 30);
    CHECK(stats.tempAvg[2] == 0);
    CHECK(stats.tempMin[0] == 10 && stats.tempMax[0] == 30);
    CHECK(stats.tempMin[1] == 20 && stats.tempMax[1] == 40);
    CHECK(logger.getStatisticsLastHours(255).tempAvg[0] == 20);
    CHECK(logger.getStatistics(11, 20).tempAvg[0] == 0);
    CHECK(logger.getStatistics(11, 20).tempAvg[1] == 40);
    CHECK(logger.getStatisticsLastHours(0).tempAvg[0] == 30);

    clockMillis = 40000;
    decoder._temp[0] = 50; decoder._temp[1] = 60;
    logger.logNow();
    CHECK(logger.getOldestDataPoint()->timestamp == 20);
    CHECK(logger.getLatestDataPoint()->timestamp == 40);
    CHECK(logger.getDataPoint(0)->timestamp == 20);
    CHECK(logger.getDataPoint(2)->timestamp == 40);
    CHECK(logger.getStatisticsAll().tempAvg[0] == 40);
    clockMillis = 4000000;
    CHECK(logger.getStatisticsLastHours(1).tempAvg[0] == 0);
}

void sensorBounds(VBUSDecoder& decoder) {
    VBUSScheduler scheduler(&decoder);
    CHECK(scheduler.addTemperatureRule(32, 20, false, ACTION_CALLBACK) == 0);
    CHECK(scheduler.addTemperatureRule(255, 20, false, ACTION_CALLBACK) == 0);
    CHECK(scheduler.getRuleCount() == 0);
    const uint8_t id = scheduler.addTemperatureRule(31, 20, false, ACTION_CALLBACK);
    CHECK(id == 1);
    CHECK(scheduler.getRuleCount() == 1);
    ScheduleRule* rule = scheduler.getRule(id);
    rule->tempCondition.sensorIndex = 32;
    CHECK(!scheduler._checkTemperatureRule(*rule));
}

MqttConfig mqttConfig() {
    return {"localhost", 1883, nullptr, nullptr, "test_device",
            "building/heating", 30, true, "homeassistant"};
}

void discoveryTopic(VBUSDecoder& decoder) {
    Client network;
    VBUSMqttClient mqtt(&decoder, &network);
    mqtt.begin(mqttConfig());
    decoder._tempNum = 1;
    decoder._pumpNum = decoder._relayNum = 0;
    CHECK(mqtt.connect());
    CHECK(mqtt._mqttClient->attempts.size() == 2);
    CHECK(mqtt._mqttClient->attempts[0].topic ==
          "homeassistant/sensor/building_heating_temperature_0/config");
    CHECK(mqtt._mqttClient->attempts[0].payload.find(
          "\"unique_id\":\"building/heating/temperature/0\"") != std::string::npos);
    CHECK(mqtt._mqttClient->attempts[0].payload.find(
          "\"state_topic\":\"building/heating/temperature/0\"") != std::string::npos);
}

void connectionPublishFailure(VBUSDecoder& decoder) {
    Client network;
    VBUSMqttClient mqtt(&decoder, &network);
    mqtt.begin(mqttConfig());
    mqtt._mqttClient->failures = 1;
    CHECK(mqtt.connect());
    CHECK(!mqtt._discoveryPublished);
    clockMillis = 0;
    mqtt.loop();
    CHECK(mqtt._discoveryPublished);
}

void discoveryLifecycle(VBUSDecoder& decoder) {
    Client network;
    VBUSMqttClient mqtt(&decoder, &network);
    const MqttConfig config = mqttConfig();
    mqtt.begin(config);
    decoder._readyFlag = false;
    mqtt.publishHomeAssistantDiscovery();
    CHECK(mqtt._mqttClient->attempts.empty());
    CHECK(mqtt.connect());
    CHECK(mqtt._mqttClient->attempts.empty());
    CHECK(!mqtt._discoveryPublished);
    clockMillis = 0;
    mqtt.loop();
    CHECK(mqtt._mqttClient->attempts.empty());

    decoder._readyFlag = true;
    decoder._tempNum = 2; decoder._pumpNum = 1; decoder._relayNum = 1;
    mqtt._mqttClient->failures = 1;
    mqtt.loop();
    CHECK(!mqtt._discoveryPublished);
    CHECK(mqtt._mqttClient->attempts.size() == 5);
    mqtt.loop();
    CHECK(mqtt._discoveryPublished);
    CHECK(mqtt._mqttClient->attempts.size() == 10);
    mqtt.loop();
    CHECK(mqtt._mqttClient->attempts.size() == 10);

    for (const auto& attempt : mqtt._mqttClient->attempts) {
        CHECK(attempt.retained);
        CHECK(std::count(attempt.topic.begin(), attempt.topic.end(), '/') == 3);
        const std::string field = "\"unique_id\":\"";
        const size_t position = attempt.payload.find(field);
        CHECK(position != std::string::npos);
        const size_t begin = position + field.size();
        const std::string unique = attempt.payload.substr(
            begin, attempt.payload.find('"', begin) - begin);
        CHECK(attempt.payload.find("\"state_topic\":\"" + unique + "\"") != std::string::npos);
        std::string objectId = unique;
        std::replace(objectId.begin(), objectId.end(), '/', '_');
        CHECK(attempt.topic.find("/" + objectId + "/config") != std::string::npos);
        CHECK(attempt.payload.find(
            "\"state_topic\":\"building/heating/") != std::string::npos);
    }

    mqtt.setConfig(config);
    CHECK(!mqtt._discoveryPublished);
    mqtt.disconnect();
    mqtt.publishHomeAssistantDiscovery();
    CHECK(mqtt._mqttClient->attempts.size() == 10);
    CHECK(mqtt.connect());
    CHECK(mqtt._discoveryPublished);
    CHECK(mqtt._mqttClient->attempts.size() == 15);
}

void discoveryBuffer(VBUSDecoder& decoder) {
    Client network;
    VBUSMqttClient mqtt(&decoder, &network);
    MqttConfig config = mqttConfig();
    config.useHomeAssistant = false;
    mqtt.begin(config);
    CHECK(mqtt._mqttClient->getBufferSize() == 256);
    CHECK(mqtt._mqttClient->resizeCalls == 0);
    config.useHomeAssistant = true;
    mqtt.setConfig(config);
    CHECK(mqtt._mqttClient->getBufferSize() >= MQTT_MAX_HEADER_SIZE + 2 + 128 + 512);
    CHECK(mqtt.connect());
    CHECK(mqtt._discoveryPublished);
    CHECK(!mqtt._mqttClient->attempts.empty());
    const auto attempt = mqtt._mqttClient->attempts[0];
    CHECK(MQTT_MAX_HEADER_SIZE + 2 + attempt.topic.size() + attempt.payload.size() > 256);
    CHECK(!mqtt._mqttClient->publish(attempt.topic.c_str(),
          std::string(1024, 'x').c_str(), true));
    mqtt._mqttClient->attempts.clear();
    mqtt._mqttClient->bufferSize = 1024;
    mqtt.setConfig(config);
    CHECK(mqtt._mqttClient->getBufferSize() == 1024);

    mqtt.disconnect();
    mqtt._mqttClient->bufferSize = 256;
    mqtt._mqttClient->failResize = true;
    mqtt.setConfig(config);
    CHECK(mqtt.connect());
    CHECK(!mqtt._discoveryPublished);
    CHECK(mqtt._mqttClient->attempts.empty());
    clockMillis = 0;
    mqtt.loop();
    CHECK(mqtt._mqttClient->attempts.empty());
    CHECK(!mqtt._discoveryPublished);
    mqtt._mqttClient->failResize = false;
    mqtt.loop();
    CHECK(mqtt._discoveryPublished);
}
}

unsigned long millis() { return clockMillis; }
void delay(unsigned long ms) { clockMillis += ms; }

int main() {
    try {
        VBUSDecoder decoder(nullptr);
        decoder._readyFlag = true;
        decoder._tempNum = 2;
        decoder._temp[0] = 10; decoder._temp[1] = 20;
        zeroCapacity(decoder);
        recentStatistics(decoder);
        channelStatistics(decoder);
        sensorBounds(decoder);
        discoveryTopic(decoder);
        connectionPublishFailure(decoder);
        discoveryLifecycle(decoder);
        discoveryBuffer(decoder);
        printf("%u helper assertions passed\n", assertions);
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
