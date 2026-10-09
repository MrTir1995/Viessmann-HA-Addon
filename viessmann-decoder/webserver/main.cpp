/*
 * Viessmann Multi-Protocol Library - Linux Web Server
 *
 * This is a Linux-based web server for Home Assistant integration
 * that provides a web interface for monitoring and configuring
 * Viessmann heating systems.
 *
 * Based on the ESP32/ESP8266 webserver example but ported to Linux
 * using libmicrohttpd for HTTP server functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <microhttpd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glob.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <cmath>
#include <chrono>
#include <deque>
#include <mutex>
#include <ctime>
#include <memory>
#include <thread>
#include <atomic>
#include <map>
#include <limits.h>
#include "adapter_settings.h"
#include "KMBusVitotrol.h"
#include "LinuxSerial.h"
#include "vbusdecoder.h"

constexpr int COMPATIBILITY_ATTEMPTS = 200; // ~2 seconds with 10ms delay
constexpr useconds_t COMPATIBILITY_DELAY_US = 10000;
constexpr int RECONNECT_INTERVAL_TICKS = 500; // 5 seconds with 10ms loop delay
constexpr useconds_t LOOP_DELAY_US = 10000;
constexpr int KMBUS_POLL_INTERVAL_TICKS = 300; // 3 seconds with 10ms loop delay for KM-Bus polling
constexpr uint8_t PROTOCOL_KM_REMOTE = 4;

// Configuration structure
struct Config {
    uint8_t protocol;      // 0=VBUS, 1=KW, 2=P300, 3=KM, 4=KM-Bus remote
    unsigned long baudRate;
    uint8_t serialConfig;  // SERIAL_8N1, SERIAL_8E1, or SERIAL_8E2
    bool invertSerial;     // Invert RX/TX signals for M-Bus adapters
    const char* serialPort;
    uint16_t webPort;
    uint8_t remoteModelId;
    uint8_t remoteSlot;
};

static_assert(std::atomic<bool>::is_always_lock_free, "Signal stop flag must be lock-free");
std::atomic<bool> running{true};

struct BusLogEntry {
    std::chrono::system_clock::time_point timestamp;
    bool transmitted;
    std::vector<uint8_t> bytes;
};

constexpr size_t MAX_BUS_LOG_ENTRIES = 500;
constexpr size_t MAX_BUS_LOG_BYTES = 32;
struct AdapterContext {
    std::string id, name, port;
    Config options{};
    Config savedOptions{};
    std::string savedPort;
    bool connected = false;
    bool compatible = false;
    LinuxSerial serial;
    VBUSDecoder* decoder = nullptr;
    KMBusVitotrol* remote = nullptr;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    std::string activePort;
    std::string claimedPort;
    dev_t claimedDevice = 0;
    dev_t connectedDevice = 0;
    ino_t connectedInode = 0;
    std::deque<BusLogEntry> logs;
    std::mutex logsMutex;
    std::thread worker;
    explicit AdapterContext(std::string identifier) : id(std::move(identifier)), name(id) {}
    ~AdapterContext() {
        delete decoder;
        delete remote;
        pthread_mutex_destroy(&mutex);
    }
};
AdapterContext primaryAdapter("primary");
thread_local AdapterContext* selectedAdapter = &primaryAdapter;
AdapterContext& currentAdapter() { return *selectedAdapter; }
struct AdapterSelection {
    AdapterContext* previous;
    explicit AdapterSelection(AdapterContext& adapter) : previous(selectedAdapter) {
        selectedAdapter = &adapter;
    }
    ~AdapterSelection() { selectedAdapter = previous; }
};
constexpr size_t MAX_ADAPTERS = 8;
std::vector<std::unique_ptr<AdapterContext>> extraAdapters;
std::mutex adaptersMutex;
void adapterWorker(AdapterContext* adapter);

bool sameSerialPort(const std::string& left, const std::string& right) {
    if (left.empty() || right.empty()) return false;
    if (left == right) return true;
    struct stat a{}, b{};
    if (stat(left.c_str(), &a) == 0 && stat(right.c_str(), &b) == 0) {
        if (S_ISCHR(a.st_mode) && S_ISCHR(b.st_mode)) return a.st_rdev == b.st_rdev;
        if (a.st_dev == b.st_dev && a.st_ino == b.st_ino) return true;
    }
    char canonicalLeft[PATH_MAX], canonicalRight[PATH_MAX];
    return realpath(left.c_str(), canonicalLeft) && realpath(right.c_str(), canonicalRight) &&
           strcmp(canonicalLeft, canonicalRight) == 0;
}

// Caller holds adaptersMutex. Reserve configured and pending ports even while disconnected.
bool portReserved(const std::string& port, const AdapterContext* except = nullptr) {
    struct stat device{};
    const bool characterDevice = stat(port.c_str(), &device) == 0 && S_ISCHR(device.st_mode);
    auto reserved = [&](const AdapterContext& adapter) {
        if (&adapter == except) return false;
        if (sameSerialPort(port, adapter.claimedPort) ||
            (characterDevice && adapter.claimedDevice && device.st_rdev == adapter.claimedDevice))
            return true;
        // Primary options win over conflicting persisted configs, never over open devices.
        if (except == &primaryAdapter && sameSerialPort(port, primaryAdapter.port)) return false;
        return sameSerialPort(port, adapter.port) || sameSerialPort(port, adapter.savedPort);
    };
    if (reserved(primaryAdapter)) return true;
    for (const auto& adapter : extraAdapters) if (reserved(*adapter)) return true;
    return false;
}
std::mutex restartMutex;
bool restartPending = false;
std::chrono::steady_clock::time_point restartDeadline;
constexpr int RESTART_EXIT_CODE = 75;
const std::string processInstanceId = std::to_string(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

bool containerRestartSupported() {
    return (access("/.dockerenv", F_OK) == 0 ||
            access("/run/.containerenv", F_OK) == 0 || getpid() == 1) &&
           access("/run.sh", X_OK) == 0;
}

void recordBusTraffic(bool transmitted, const uint8_t* data, size_t size) {
    auto& busLogs = currentAdapter().logs;
    auto& busLogMutex = currentAdapter().logsMutex;
    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(busLogMutex);
    for (size_t i = 0; i < size; ++i) {
        // Group nearby bytes for readability, not as a protocol frame boundary.
        if (busLogs.empty() || busLogs.back().transmitted != transmitted ||
            busLogs.back().bytes.size() >= MAX_BUS_LOG_BYTES ||
            now - busLogs.back().timestamp >= std::chrono::milliseconds(10)) {
            if (busLogs.size() >= MAX_BUS_LOG_ENTRIES) busLogs.pop_front();
            busLogs.push_back({now, transmitted, {}});
        }
        busLogs.back().bytes.push_back(data[i]);
    }
}

std::string generateBusLogs() {
    auto& busLogs = currentAdapter().logs;
    auto& busLogMutex = currentAdapter().logsMutex;
    std::lock_guard<std::mutex> lock(busLogMutex);
    std::string logs;
    for (const auto& entry : busLogs) {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch()).count();
        const time_t seconds = milliseconds / 1000;
        struct tm timestamp;
        localtime_r(&seconds, &timestamp);
        char date[32];
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &timestamp);
        char prefix[48];
        snprintf(prefix, sizeof(prefix), "%s.%03d %s  ", date,
                 static_cast<int>(milliseconds % 1000), entry.transmitted ? "TX" : "RX");
        logs += prefix;
        for (uint8_t byte : entry.bytes) {
            char hex[4];
            snprintf(hex, sizeof(hex), "%02X ", byte);
            logs += hex;
        }
        logs += '\n';
    }
    return logs;
}

// Signal handler
void signalHandler(int) {
    running.store(false, std::memory_order_relaxed);
}

// Helper functions
const char* getProtocolName(uint8_t protocol) {
    switch(protocol) {
        case PROTOCOL_VBUS: return "VBUS (RESOL)";
        case PROTOCOL_KW: return "KW-Bus (VS1)";
        case PROTOCOL_P300: return "P300 (VS2/Optolink)";
        case PROTOCOL_KM: return "KM-Bus";
        case PROTOCOL_KM_REMOTE: return "KM-Bus Slave (Vitotrol)";
        default: return "Unknown";
    }
}

ProtocolType parseProtocol(const char* str) {
    if (strcasecmp(str, "vbus") == 0) return PROTOCOL_VBUS;
    if (strcasecmp(str, "kw") == 0) return PROTOCOL_KW;
    if (strcasecmp(str, "p300") == 0) return PROTOCOL_P300;
    if (strcasecmp(str, "km") == 0) return PROTOCOL_KM;
    if (strcasecmp(str, "km_remote") == 0) return static_cast<ProtocolType>(PROTOCOL_KM_REMOTE);
    return PROTOCOL_VBUS;
}

uint8_t parseSerialConfig(const char* str) {
    if (strcasecmp(str, "8N1") == 0) return SERIAL_8N1;
    if (strcasecmp(str, "8E1") == 0) return SERIAL_8E1;
    if (strcasecmp(str, "8E2") == 0) return SERIAL_8E2;
    return SERIAL_8N1;
}

bool portExists(const std::string& port) {
    struct stat st;
    return stat(port.c_str(), &st) == 0;
}

void addPortsFromGlob(const char* pattern, std::vector<std::string>& ports, std::unordered_set<std::string>& seen) {
    glob_t glob_result = {};
    int glob_status = glob(pattern, 0, nullptr, &glob_result);
    if (glob_status == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            std::string path(glob_result.gl_pathv[i]);
            if (seen.insert(path).second) {
                ports.push_back(path);
            }
        }
        globfree(&glob_result);
    }
}

std::vector<std::string> discoverSerialPorts() {
    const auto& config = currentAdapter().options;
    std::vector<std::string> ports;
    std::unordered_set<std::string> seen;
    const bool hasConfiguredPort = config.serialPort && strlen(config.serialPort) > 0;
    if (currentAdapter().id != "primary") {
        if (hasConfiguredPort) ports.push_back(config.serialPort);
        return ports;
    }
    if (hasConfiguredPort && portExists(config.serialPort) && seen.insert(config.serialPort).second) {
        ports.push_back(config.serialPort);
    }
    addPortsFromGlob("/dev/ttyUSB*", ports, seen);
    addPortsFromGlob("/dev/ttyACM*", ports, seen);
    addPortsFromGlob("/dev/ttyAMA*", ports, seen);
    if (ports.empty() && hasConfiguredPort) {
        ports.push_back(config.serialPort);
    }
    return ports;
}

bool waitForCompatibility(VBUSDecoder* decoder) {
    const auto& config = currentAdapter().options;
    if (!decoder) {
        return false;
    }
    for (int i = 0; i < COMPATIBILITY_ATTEMPTS && running; ++i) {
        if (config.protocol == PROTOCOL_KM && i % 100 == 0) {
            decoder->pollKMBusStatusRecord(KMBUS_ADDR_MASTER_STATUS);
        }
        decoder->loop();
        if (decoder->isReady() && decoder->getVbusStat()) {
            return true;
        }
        usleep(COMPATIBILITY_DELAY_US);
    }
    return false;
}

void releaseAdapterClaim(AdapterContext& adapter) {
    std::lock_guard<std::mutex> lock(adaptersMutex);
    adapter.claimedPort.clear();
    adapter.claimedDevice = 0;
}

bool attemptConnection(const std::string& port) {
    auto& adapter = currentAdapter();
    const auto& config = adapter.options;
    auto& vbusSerial = adapter.serial;
    auto& vbus = adapter.decoder;
    auto& vitotrol = adapter.remote;
    auto& serialConnected = adapter.connected;
    auto& deviceCompatible = adapter.compatible;
    auto& activeSerialPort = adapter.activePort;
    auto& data_mutex = adapter.mutex;
    {
        std::lock_guard<std::mutex> lock(adaptersMutex);
        if (portReserved(port, &adapter)) return false;
        adapter.claimedPort = port;
        struct stat device{};
        adapter.claimedDevice = stat(port.c_str(), &device) == 0 && S_ISCHR(device.st_mode) ?
                                device.st_rdev : 0;
    }
    auto releasePort = [&]() {
        releaseAdapterClaim(adapter);
    };
    if (vbusSerial.isOpen()) {
        vbusSerial.end();
    }
    if (!vbusSerial.begin(port.c_str(), config.baudRate, config.serialConfig)) {
        fprintf(stderr, "Failed to open serial port %s\n", port.c_str());
        releasePort();
        return false;
    }
    struct stat device{};
    if (stat(port.c_str(), &device) == 0) {
        adapter.connectedDevice = device.st_rdev;
        adapter.connectedInode = device.st_ino;
    }
    
    // Set signal inversion if configured (for M-Bus/KM-Bus adapters)
    vbusSerial.setInvertSignal(config.invertSerial);
    if (config.invertSerial) {
        printf("Serial signal inversion enabled (for M-Bus adapters)\n");
    }

    pthread_mutex_lock(&data_mutex);
    VBUSDecoder* oldDecoder = vbus;
    vbus = nullptr;
    pthread_mutex_unlock(&data_mutex);

    if (oldDecoder) {
        delete oldDecoder;
    }

    pthread_mutex_lock(&data_mutex);
    KMBusVitotrol* oldVitotrol = vitotrol;
    vitotrol = nullptr;
    pthread_mutex_unlock(&data_mutex);
    delete oldVitotrol;

    if (config.protocol == PROTOCOL_KM_REMOTE) {
        pthread_mutex_lock(&data_mutex);
        delete vitotrol;
        vitotrol = new KMBusVitotrol(&vbusSerial, config.remoteModelId, config.remoteSlot);
        serialConnected = true;
        deviceCompatible = true;
        activeSerialPort = port;
        pthread_mutex_unlock(&data_mutex);
        printf("Connected to %s as Vitotrol %s, KM-Bus slot %u\n",
               port.c_str(), config.remoteModelId == 0x38 ? "300" : "200",
               config.remoteSlot);
        return true;
    }

    VBUSDecoder* decoder = new VBUSDecoder(&vbusSerial);
    decoder->begin((ProtocolType)config.protocol);

    bool compatible = waitForCompatibility(decoder);
    pthread_mutex_lock(&data_mutex);
    if (compatible) {
        vbus = decoder;
        serialConnected = true;
        deviceCompatible = true;
        activeSerialPort = port;
    } else {
        delete decoder;
        vbus = nullptr;
        serialConnected = false;
        deviceCompatible = false;
        activeSerialPort = "";
    }
    pthread_mutex_unlock(&data_mutex);

    if (compatible) {
        printf("Connected to %s and detected compatible frames\n", port.c_str());
        return true;
    }

    fprintf(stderr, "No compatible frames detected on %s\n", port.c_str());
    vbusSerial.end();
    releasePort();
    return false;
}

// Caller holds data_mutex; keep /data and /api/remote on the same contract.
std::string generateRemoteJSON(bool includeDatasets) {
    const auto& config = currentAdapter().options;
    const auto serialConnected = currentAdapter().connected;
    auto* vitotrol = currentAdapter().remote;
    const bool connected = serialConnected && vitotrol;
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"model\":\"Vitotrol %s\",\"slot\":%u,\"online\":%s,"
             "\"room_temperature\":%.1f,\"desired_room_temperature\":%.1f,"
             "\"reduced_room_temperature\":%.1f,\"party_room_temperature\":%.1f,"
             "\"mode\":%u,\"last_master_dataset\":%u,\"profile\":\"%s\","
             "\"requested_party_mode\":%s,\"requested_economy_mode\":%s,"
             "\"pending_commands\":%u,\"crc_errors\":%u,"
             "\"malformed_frames\":%u,\"unknown_commands\":%u,"
             "\"measurements_verified\":false,\"outside_temperature\":null,"
             "\"heating_enabled\":null,\"controller_fault\":null",
             config.remoteModelId == 0x38 ? "300" : "200", config.remoteSlot,
             connected && vitotrol->isOnline() ? "true" : "false",
             connected ? vitotrol->getCurrentRoomTemperature() : 0.0,
             connected ? vitotrol->getDesiredRoomTemperature() : 0.0,
             connected ? vitotrol->getReducedRoomTemperature() : 0.0,
             connected ? vitotrol->getPartyRoomTemperature() : 0.0,
             connected ? vitotrol->getOperatingMode() : 0,
             connected ? vitotrol->getLastMasterDataset() : 0,
             connected ? vitotrol->getProtocolProfile() : "wifi",
             connected && vitotrol->getPartyEnabled() ? "true" : "false",
             connected && vitotrol->getEconomyEnabled() ? "true" : "false",
             connected ? vitotrol->getPendingCommandCount() : 0,
             connected ? vitotrol->getCrcErrorCount() : 0,
             connected ? vitotrol->getMalformedFrameCount() : 0,
             connected ? vitotrol->getUnknownCommandCount() : 0);
    std::string body(json);
    const uint8_t* bytes = nullptr;
    uint8_t length = 0;
    uint32_t receivedAt = 0;
    const uint8_t statusId = connected && strcmp(vitotrol->getProtocolProfile(), "openv") == 0
        ? 0x1C + config.remoteSlot : 0x1D;
    const bool hasStatus = connected && vitotrol->getDataset(statusId, bytes, length, receivedAt);
    body += ",\"status_dataset_age_ms\":";
    body += hasStatus ? std::to_string(static_cast<uint32_t>(millis()) - receivedAt) : "null";
    // The published interpretation gives 25 C on a controller reporting ~9 C.
    // Retain candidates for diagnosis, never present them as verified measurements.
    float outside = 0;
    bool heating = false;
    const bool fresh = hasStatus && static_cast<uint32_t>(millis() - receivedAt) < 180000;
    body += ",\"outside_temperature_candidate\":";
    body += fresh && vitotrol->getOutsideTemperature(outside) ? std::to_string(outside) : "null";
    body += ",\"heating_enabled_candidate\":";
    body += fresh && vitotrol->getHeatingEnabled(heating) ? (heating ? "true" : "false") : "null";
    if (includeDatasets) {
        body += ",\"datasets\":[";
        bool first = true;
        for (uint16_t id = 0; connected && id < 254; ++id) {
            if (!vitotrol->getDataset(static_cast<uint8_t>(id), bytes, length, receivedAt)) continue;
            if (!first) body += ',';
            first = false;
            body += "{\"id\":" + std::to_string(id) + ",\"age_ms\":" +
                    std::to_string(static_cast<uint32_t>(millis()) - receivedAt) + ",\"data\":[";
            for (uint8_t i = 0; i < length; ++i) {
                if (i) body += ',';
                body += std::to_string(bytes[i]);
            }
            body += "]}";
        }
        body += ']';
    }
    body += '}';
    return body;
}

// Generate JSON data response
std::string jsonQuote(const std::string& value);

std::string busParticipantsJSON(VBUSDecoder* decoder) {
    std::string participants = "[";
    if (decoder) {
        for (uint8_t i = 0; i < decoder->getParticipantCount(); ++i) {
            const BusParticipant* participant = decoder->getParticipant(i);
            if (!participant) continue;
            if (participants.size() > 1) participants += ',';
            participants += "{\"address\":" + std::to_string(participant->address) +
                            ",\"name\":" + jsonQuote(participant->name) +
                            ",\"active\":" + (participant->active ? "true" : "false") +
                            ",\"auto_detected\":" +
                            (participant->autoDetected ? "true" : "false") +
                            ",\"temperature_channels\":" +
                            std::to_string(participant->tempChannels) +
                            ",\"pump_channels\":" + std::to_string(participant->pumpChannels) +
                            ",\"relay_channels\":" + std::to_string(participant->relayChannels) + "}";
        }
    }
    return participants + "]";
}

char* generateDataJSON() {
    auto& adapter = currentAdapter();
    const auto& config = adapter.options;
    const auto& activeSerialPort = adapter.activePort;
    auto& serialConnected = adapter.connected;
    auto& deviceCompatible = adapter.compatible;
    auto*& vbus = adapter.decoder;
    auto*& vitotrol = adapter.remote;
    auto& data_mutex = adapter.mutex;
    static char json[4096];
    int offset = 0;
    int remaining = sizeof(json) - 1; // Reserve space for null terminator

    pthread_mutex_lock(&data_mutex);
    VBUSDecoder* decoder = vbus;

    #define JSON_APPEND(fmt, ...) do { \
        int written = snprintf(json + offset, remaining, fmt, ##__VA_ARGS__); \
        if (written < 0 || written >= remaining) { \
            /* Overflow detected - close JSON properly */ \
            if (offset > 0 && json[offset - 1] == ',') offset--; /* Remove trailing comma */ \
            int close_written = snprintf(json + offset, remaining, "}"); \
            if (close_written > 0 && close_written < remaining) { \
                offset += close_written; \
            } \
            pthread_mutex_unlock(&data_mutex); \
            json[sizeof(json) - 1] = '\0'; /* Ensure null termination */ \
            return json; \
        } \
        offset += written; \
        remaining -= written; \
    } while(0)

    JSON_APPEND("{");
    const char* status = "Disconnected";
    if (config.protocol == PROTOCOL_KM_REMOTE) {
        status = serialConnected ? "Vitotrol emulator active" : "Disconnected";
    } else if (serialConnected && deviceCompatible && decoder) {
        status = decoder->getVbusStat() ? "OK" : "Error";
    }

    JSON_APPEND("\"serialConnected\":%s,", serialConnected ? "true" : "false");
    JSON_APPEND("\"compatible\":%s,", deviceCompatible ? "true" : "false");
    JSON_APPEND("\"serialPort\":%s,", jsonQuote(activeSerialPort).c_str());
    const bool dataReady = config.protocol == PROTOCOL_KM_REMOTE
                               ? (serialConnected && vitotrol && vitotrol->isOnline())
                               : (decoder && serialConnected && deviceCompatible && decoder->isReady());
    JSON_APPEND("\"ready\":%s,", dataReady ? "true" : "false");
    JSON_APPEND("\"status\":\"%s\",", status);
    JSON_APPEND("\"protocol\":%d,", config.protocol);

    if (config.protocol == PROTOCOL_KM_REMOTE) {
        JSON_APPEND("\"temperatures\":[],\"pumps\":[],\"relays\":[],\"participants\":[],");
        const std::string remote = generateRemoteJSON(false);
        JSON_APPEND("\"remote\":%s", remote.c_str());
        pthread_mutex_unlock(&data_mutex);
        JSON_APPEND("}");
        return json;
    }

    if (!serialConnected || !decoder || !deviceCompatible) {
        JSON_APPEND("\"temperatures\":[],\"pumps\":[],\"relays\":[],\"participants\":[]");
        pthread_mutex_unlock(&data_mutex);
        JSON_APPEND("}");
        return json;
    }

    // Temperatures
    JSON_APPEND("\"temperatures\":[");
    if (decoder->isReady()) {
        uint8_t tempNum = decoder->getTempNum();
        for (uint8_t i = 0; i < tempNum && i < 32; i++) {
            if (i > 0) JSON_APPEND(",");
            JSON_APPEND("%.1f", decoder->getTemp(i));
        }
    }
    JSON_APPEND("],");

    // Pumps
    JSON_APPEND("\"pumps\":[");
    if (decoder->isReady()) {
        uint8_t pumpNum = decoder->getPumpNum();
        for (uint8_t i = 0; i < pumpNum && i < 32; i++) {
            if (i > 0) JSON_APPEND(",");
            JSON_APPEND("%d", decoder->getPump(i));
        }
    }
    JSON_APPEND("],");

    // Relays
    JSON_APPEND("\"relays\":[");
    if (decoder->isReady()) {
        uint8_t relayNum = decoder->getRelayNum();
        for (uint8_t i = 0; i < relayNum && i < 32; i++) {
            if (i > 0) JSON_APPEND(",");
            JSON_APPEND("%s", decoder->getRelay(i) ? "true" : "false");
        }
    }
    JSON_APPEND("]");
    const std::string participants = busParticipantsJSON(decoder);
    JSON_APPEND(",\"participants\":%s", participants.c_str());

    pthread_mutex_unlock(&data_mutex);

    JSON_APPEND("}");

    #undef JSON_APPEND

    return json;
}

struct RemotePostData {
    std::string body;
    bool tooLarge = false;
};

int parseJsonNumber(const std::string& body, const char* key, float& value) {
    const std::string field = std::string("\"") + key + "\"";
    const size_t keyPosition = body.find(field);
    if (keyPosition == std::string::npos) return 0;
    const size_t colon = body.find(':', keyPosition + field.length());
    if (colon == std::string::npos) return -1;

    const char* start = body.c_str() + colon + 1;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') ++start;
    char* end = nullptr;
    const float parsed = strtof(start, &end);
    if (end == start || !std::isfinite(parsed)) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (*end != ',' && *end != '}') return -1;
    value = parsed;
    return 1;
}

int parseJsonString(const std::string& body, const char* key, std::string& value) {
    const std::string field = std::string("\"") + key + "\"";
    const size_t keyPosition = body.find(field);
    if (keyPosition == std::string::npos) return 0;
    const size_t colon = body.find(':', keyPosition + field.length());
    if (colon == std::string::npos) return -1;
    const size_t quote = body.find('"', colon + 1);
    if (quote == std::string::npos) return -1;
    const size_t endQuote = body.find('"', quote + 1);
    if (endQuote == std::string::npos) return -1;
    value = body.substr(quote + 1, endQuote - quote - 1);
    return 1;
}

MHD_Result queueJson(MHD_Connection* connection, unsigned int status, const char* body) {
    MHD_Response* response = MHD_create_response_from_buffer(
        strlen(body), (void*)body, MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Cache-Control", "no-store");
    const MHD_Result result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

bool restartOriginAllowed(MHD_Connection* connection) {
    const char* site = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Sec-Fetch-Site");
    if (site && strcasecmp(site, "cross-site") == 0) return false;
    const char* origin = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Origin");
    if (!origin) return true;
    // Ingress rewrites Host; the browser's forbidden same-origin header retains
    // the original origin relationship. JSON-only POST still blocks HTML forms.
    if (site && strcasecmp(site, "same-origin") == 0) return true;
    const char* host = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Host");
    if (!host) return false;
    const std::string value(origin);
    size_t start;
    if (value.compare(0, 7, "http://") == 0) start = 7;
    else if (value.compare(0, 8, "https://") == 0) start = 8;
    else return false;
    return strcasecmp(value.substr(start).c_str(), host) == 0;
}

bool parseRestartConfirmation(const std::string& body) {
    size_t position = 0;
    for (const char* token : {"{", "\"confirm\"", ":", "true", "}"}) {
        while (position < body.size() &&
               (body[position] == ' ' || body[position] == '\t' ||
                body[position] == '\r' || body[position] == '\n')) ++position;
        const size_t length = strlen(token);
        if (body.compare(position, length, token) != 0) return false;
        position += length;
    }
    while (position < body.size() &&
           (body[position] == ' ' || body[position] == '\t' ||
            body[position] == '\r' || body[position] == '\n')) ++position;
    return position == body.size();
}

MHD_Result handleRestartApi(MHD_Connection* connection, const char* method,
                            size_t* uploadSize, const char* uploadData, void** context) {
    if (strcmp(method, "POST") != 0)
        return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "{\"error\":\"Use POST\"}");
    if (!restartOriginAllowed(connection))
        return queueJson(connection, MHD_HTTP_FORBIDDEN, "{\"error\":\"Cross-origin restart refused\"}");
    const char* contentType = MHD_lookup_connection_value(
        connection, MHD_HEADER_KIND, "Content-Type");
    std::string mediaType = contentType ? contentType : "";
    mediaType = mediaType.substr(0, mediaType.find(';'));
    while (!mediaType.empty() && (mediaType.back() == ' ' || mediaType.back() == '\t'))
        mediaType.pop_back();
    if (strcasecmp(mediaType.c_str(), "application/json") != 0)
        return queueJson(connection, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE,
                         "{\"error\":\"Use application/json\"}");
    auto* request = static_cast<RemotePostData*>(*context);
    if (!request) {
        *context = new RemotePostData();
        return MHD_YES;
    }
    if (*uploadSize > 0) {
        if (*uploadSize > 256 - request->body.size()) request->tooLarge = true;
        if (!request->tooLarge) request->body.append(uploadData, *uploadSize);
        *uploadSize = 0;
        return MHD_YES;
    }
    const bool tooLarge = request->tooLarge;
    const bool confirmed = !tooLarge && parseRestartConfirmation(request->body);
    delete request;
    *context = nullptr;
    if (tooLarge)
        return queueJson(connection, MHD_HTTP_PAYLOAD_TOO_LARGE, "{\"error\":\"Request body too large\"}");
    if (!confirmed)
        return queueJson(connection, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Expected {\\\"confirm\\\":true}\"}");
    if (!containerRestartSupported())
        return queueJson(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "{\"error\":\"Container restart unavailable on this host\"}");
    std::lock_guard<std::mutex> lock(restartMutex);
    if (restartPending)
        return queueJson(connection, MHD_HTTP_CONFLICT, "{\"error\":\"Restart already pending\"}");
    const MHD_Result result = queueJson(connection, MHD_HTTP_ACCEPTED,
                                       "{\"status\":\"restarting\"}");
    if (result == MHD_YES) {
        restartDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        restartPending = true;
    }
    return result;
}

const char* getRestartHTML() {
    return R"HTML(
<button type="button" id="restartButton" class="nav-button btn btn-secondary" disabled>Container neu starten</button>
<div id="restartMessage" role="status"></div>
<script>
const restartButton=document.getElementById('restartButton');
const restartMessage=document.getElementById('restartMessage');
async function restartFetch(path,options={}){
 const controller=new AbortController();
 const timer=setTimeout(()=>controller.abort(),2000);
 try{return await fetch(path,{...options,cache:'no-store',signal:controller.signal});}
 finally{clearTimeout(timer);}
}
async function loadRestartCapability(){
 try{const response=await restartFetch('api/system');if(!response.ok)throw new Error();
 const system=await response.json();restartButton.disabled=!system.restart_supported||system.restart_pending;
 restartMessage.textContent=system.restart_supported?
 'Neustart benötigt Docker-Neustartregel oder aktivierten Home-Assistant-Watchdog.':
 'Container-Neustart ist auf diesem Host nicht verfügbar.';}
 catch(error){restartMessage.textContent='Neustart-Verfügbarkeit konnte nicht geprüft werden.';}
}
async function waitForContainer(previousInstance){
 const deadline=Date.now()+90000;
 while(Date.now()<deadline){
  try{const response=await restartFetch('api/system');
   if(response.ok){const system=await response.json();
    if(typeof system.instance_id==='string'&&system.instance_id!==previousInstance){
     window.location.href=new URL('./',window.location.href).href;return;}}}
  catch(error){}
  await new Promise(resolve=>setTimeout(resolve,250));
 }
 restartMessage.textContent='Neustart nicht bestätigt. Bitte Container und Neustartregel bzw. Watchdog prüfen und die Seite manuell neu laden.';
}
restartButton.addEventListener('click',async()=>{
 if(!window.confirm('Container wirklich neu starten? Die Verbindung wird kurz unterbrochen. Docker-Neustartregel oder Home-Assistant-Watchdog muss aktiviert sein.'))return;
 restartButton.disabled=true;
 try{const before=await restartFetch('api/system');if(!before.ok)throw new Error('Statusprüfung fehlgeschlagen');
 const previousInstance=(await before.json()).instance_id;
 if(typeof previousInstance!=='string')throw new Error('Prozesskennung fehlt');
 const response=await restartFetch('api/restart',{method:'POST',
 headers:{'Content-Type':'application/json'},body:JSON.stringify({confirm:true})});
 const result=await response.json();if(response.status!==202)throw new Error(result.error||'Neustart abgelehnt');
 restartMessage.textContent='Container wird neu gestartet. Warte auf eine neue Prozesskennung…';
 await waitForContainer(previousInstance);}
 catch(error){await loadRestartCapability();
 restartMessage.textContent='Neustart nicht bestätigt: '+error.message+'. Bitte Container prüfen.';}
});
loadRestartCapability();
</script>
)HTML";
}

using SettingsPostData = RemotePostData;

int parseJsonBool(const std::string& body, const char* key, bool& value) {
    const std::string field = std::string("\"") + key + "\"";
    const size_t keyPosition = body.find(field);
    if (keyPosition == std::string::npos) return 0;
    const size_t colon = body.find(':', keyPosition + field.length());
    if (colon == std::string::npos) return -1;

    size_t position = colon + 1;
    while (position < body.size() &&
           (body[position] == ' ' || body[position] == '\t' ||
            body[position] == '\n' || body[position] == '\r')) {
        ++position;
    }
    if (body.compare(position, 4, "true") == 0) {
        position += 4;
        value = true;
    } else if (body.compare(position, 5, "false") == 0) {
        position += 5;
        value = false;
    } else {
        return -1;
    }
    while (position < body.size() &&
           (body[position] == ' ' || body[position] == '\t' ||
            body[position] == '\n' || body[position] == '\r')) {
        ++position;
    }
    return position == body.size() || body[position] == ',' || body[position] == '}' ? 1 : -1;
}

struct JsonField {
    char type;
    std::string value;
};
using JsonFields = std::map<std::string, JsonField>;

bool validUtf8(const std::string& value) {
    for (size_t i = 0; i < value.size();) {
        const unsigned char first = value[i++];
        if (first < 0x80) continue;
        unsigned code;
        size_t following;
        unsigned minimum;
        if (first >= 0xC2 && first <= 0xDF) {
            code = first & 0x1F; following = 1; minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            code = first & 0x0F; following = 2; minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            code = first & 0x07; following = 3; minimum = 0x10000;
        } else return false;
        if (following > value.size() - i) return false;
        while (following--) {
            const unsigned char next = value[i++];
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            return false;
    }
    return true;
}

bool parseConfigObject(const std::string& body, JsonFields& fields) {
    size_t position = 0;
    auto whitespace = [&]() {
        while (position < body.size() && body[position] &&
               strchr(" \t\r\n", body[position])) ++position;
    };
    auto quoted = [&](std::string& value) {
        if (position >= body.size() || body[position++] != '"') return false;
        while (position < body.size()) {
            unsigned char byte = body[position++];
            if (byte == '"') return validUtf8(value);
            if (byte < 32) return false;
            if (byte == '\\') {
                if (position >= body.size()) return false;
                byte = body[position++];
                if (byte == 'u') {
                    auto hexCode = [&](unsigned& code) {
                        code = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (position >= body.size()) return false;
                            const char digit = body[position++];
                            unsigned nibble;
                            if (digit >= '0' && digit <= '9') nibble = digit - '0';
                            else if (digit >= 'a' && digit <= 'f') nibble = digit - 'a' + 10;
                            else if (digit >= 'A' && digit <= 'F') nibble = digit - 'A' + 10;
                            else return false;
                            code = code * 16 + nibble;
                        }
                        return true;
                    };
                    unsigned code;
                    if (!hexCode(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (body.compare(position, 2, "\\u") != 0) return false;
                        position += 2;
                        unsigned low;
                        if (!hexCode(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                    } else if (code >= 0xDC00 && code <= 0xDFFF) return false;
                    if (code < 0x80) value += static_cast<char>(code);
                    else if (code < 0x800) {
                        value += static_cast<char>(0xC0 | (code >> 6));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        value += static_cast<char>(0xE0 | (code >> 12));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        value += static_cast<char>(0xF0 | (code >> 18));
                        value += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    continue;
                }
                if (byte == 'b') byte = '\b';
                else if (byte == 'f') byte = '\f';
                else if (byte == 'n') byte = '\n';
                else if (byte == 'r') byte = '\r';
                else if (byte == 't') byte = '\t';
                else if (byte != '"' && byte != '\\' && byte != '/') return false;
            }
            value += static_cast<char>(byte);
        }
        return false;
    };
    whitespace();
    if (position >= body.size() || body[position++] != '{') return false;
    while (true) {
        whitespace();
        std::string key;
        if (!quoted(key) || fields.count(key)) return false;
        whitespace();
        if (position >= body.size() || body[position++] != ':') return false;
        whitespace();
        if (position >= body.size()) return false;
        JsonField field{};
        if (body[position] == '"') {
            field.type = 's';
            if (!quoted(field.value)) return false;
        } else {
            size_t start = position;
            while (position < body.size() && body[position] != ',' && body[position] != '}' &&
                   !strchr(" \t\r\n", body[position])) ++position;
            field.value = body.substr(start, position - start);
            if (field.value == "true" || field.value == "false") field.type = 'b';
            else {
                field.type = 'n';
                if (field.value.empty() || field.value.size() > 6 ||
                    field.value.find_first_not_of("0123456789") != std::string::npos ||
                    (field.value.size() > 1 && field.value[0] == '0')) return false;
            }
        }
        fields.emplace(key, std::move(field));
        whitespace();
        if (position >= body.size()) return false;
        const char separator = body[position++];
        if (separator == '}') {
            whitespace();
            return position == body.size();
        }
        if (separator != ',') return false;
    }
}

const char* protocolToken(uint8_t protocol) {
    static const char* tokens[] = {"vbus", "kw", "p300", "km", "km_remote"};
    return protocol < 5 ? tokens[protocol] : "vbus";
}
const char* serialToken(uint8_t serial) {
    return serial == SERIAL_8E1 ? "8E1" : serial == SERIAL_8E2 ? "8E2" : "8N1";
}
std::string jsonQuote(const std::string& value) {
    std::string result = "\"";
    for (unsigned char byte : value) {
        if (byte == '"' || byte == '\\') result += '\\';
        if (byte < 32) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
            result += escaped;
        } else result += static_cast<char>(byte);
    }
    return result + '"';
}

bool validAdapterConfig(const JsonFields& fields, Config& options, std::string& port,
                        std::string& name, bool requirePort, bool persisted = false) {
    for (const auto& entry : fields) {
        if (entry.first != "protocol" && entry.first != "baud_rate" &&
            entry.first != "serial_config" && entry.first != "invert_serial" &&
            entry.first != "remote_model" && entry.first != "remote_slot" &&
            entry.first != "serial_port" && entry.first != "name" &&
            !(persisted && entry.first == "id")) return false;
    }
    auto field = [&](const char* key, char type) {
        auto item = fields.find(key);
        return item != fields.end() && item->second.type == type;
    };
    if (!field("protocol", 's') || !field("baud_rate", 'n') ||
        !field("serial_config", 's') || !field("invert_serial", 'b') ||
        !field("remote_model", 's') || !field("remote_slot", 'n')) return false;
    const auto& protocol = fields.at("protocol").value;
    const auto& serial = fields.at("serial_config").value;
    const auto& model = fields.at("remote_model").value;
    const unsigned long baud = strtoul(fields.at("baud_rate").value.c_str(), nullptr, 10);
    const unsigned long slot = strtoul(fields.at("remote_slot").value.c_str(), nullptr, 10);
    if (protocol != "vbus" && protocol != "kw" && protocol != "p300" &&
        protocol != "km" && protocol != "km_remote") return false;
    // Match the baud rates actually supported by LinuxSerial.
    if (baud != 1200 && baud != 2400 && baud != 4800 && baud != 9600 && baud != 19200 &&
        baud != 38400 && baud != 57600 && baud != 115200) return false;
    if (serial != "8N1" && serial != "8E1" && serial != "8E2") return false;
    if ((model != "vitotrol200" && model != "vitotrol300") || slot < 1 || slot > 3)
        return false;
    if (requirePort && !field("serial_port", 's')) return false;
    if (fields.count("serial_port")) {
        if (!field("serial_port", 's')) return false;
        port = fields.at("serial_port").value;
    }
    if (port.empty() || port.size() > 256 || port[0] != '/' ||
        port.find_first_of("\r\n\t") != std::string::npos ||
        port.find('\0') != std::string::npos ||
        port.find_first_of("<>&'\"") != std::string::npos) return false;
    for (unsigned char byte : port) if (byte < 32 || byte == 127) return false;
    struct stat device{};
    if (!persisted && stat(port.c_str(), &device) == 0 && !S_ISCHR(device.st_mode)) return false;
    if (fields.count("name")) {
        if (!field("name", 's')) return false;
        name = fields.at("name").value;
        if (name.empty() || name.size() > 80) return false;
        for (unsigned char byte : name) if (byte < 32 || byte == 127) return false;
    }
    options.protocol = parseProtocol(protocol.c_str());
    options.baudRate = options.protocol == PROTOCOL_KM_REMOTE ? 1200 : baud;
    options.serialConfig = options.protocol == PROTOCOL_KM_REMOTE ? SERIAL_8E1 :
                           parseSerialConfig(serial.c_str());
    options.invertSerial = fields.at("invert_serial").value == "true";
    options.remoteModelId = model == "vitotrol300" ? 0x38 : 0x34;
    options.remoteSlot = slot;
    return true;
}

std::string configJSON(const AdapterContext& adapter, bool saved) {
    const auto& options = saved ? adapter.savedOptions : adapter.options;
    const auto& port = saved ? adapter.savedPort : adapter.port;
    return "{\"id\":" + jsonQuote(adapter.id) + ",\"name\":" + jsonQuote(adapter.name) +
           ",\"serial_port\":" + jsonQuote(port) +
           ",\"protocol\":" + jsonQuote(protocolToken(options.protocol)) +
           ",\"baud_rate\":" + std::to_string(options.baudRate) +
           ",\"serial_config\":" + jsonQuote(serialToken(options.serialConfig)) +
           ",\"invert_serial\":" + (options.invertSerial ? "true" : "false") +
           ",\"remote_model\":" + jsonQuote(options.remoteModelId == 0x38 ? "vitotrol300" : "vitotrol200") +
           ",\"remote_slot\":" + std::to_string(options.remoteSlot) + "}";
}

std::string dataDirectory() {
    const char* directory = getenv("VIESSMANN_DATA_DIR");
    return directory && *directory ? directory : "/data";
}

bool atomicWrite(const std::string& filename, const std::string& body) {
    const std::string target = dataDirectory() + "/" + filename;
    std::string pattern = target + ".XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) return false;
    size_t offset = 0;
    while (offset < body.size()) {
        const ssize_t written = write(descriptor, body.data() + offset, body.size() - offset);
        if (written <= 0) break;
        offset += written;
    }
    bool success = offset == body.size() && fsync(descriptor) == 0;
    if (close(descriptor) != 0) success = false;
    if (success) success = rename(temporary.data(), target.c_str()) == 0;
    if (!success) unlink(temporary.data());
    else {
        const int directory = open(dataDirectory().c_str(), O_RDONLY | O_DIRECTORY);
        if (directory >= 0) { fsync(directory); close(directory); }
    }
    return success;
}

// adaptersMutex serializes updates, persistence, reservations and worker registration.
bool saveAdapters() {
    std::string body;
    for (const auto& adapter : extraAdapters) body += configJSON(*adapter, true) + "\n";
    return atomicWrite("adapters.jsonl", body);
}

void loadAdapters() {
    const std::string path = dataDirectory() + "/adapters.jsonl";
    FILE* file = fopen(path.c_str(), "r");
    if (!file) return;
    char body[16385];
    const size_t length = fread(body, 1, sizeof(body), file);
    const bool failed = ferror(file);
    fclose(file);
    if (failed || length > 16384) {
        fprintf(stderr, "Ignoring oversized or unreadable adapter configuration\n");
        return;
    }
    std::vector<std::unique_ptr<AdapterContext>> loaded;
    const std::string content(body, length);
    size_t position = 0;
    size_t records = 0;
    while (position < content.size()) {
        const size_t end = content.find('\n', position);
        const std::string line = content.substr(position, end - position);
        position = end == std::string::npos ? content.size() : end + 1;
        if (++records > MAX_ADAPTERS - 1 || line.size() > 2048) return;
        JsonFields fields;
        if (!parseConfigObject(line, fields) || !fields.count("id") ||
            fields.at("id").type != 's') return;
        const std::string id = fields.at("id").value;
        bool safeId = false;
        for (size_t i = 1; i < MAX_ADAPTERS; ++i)
            if (id == "adapter_" + std::to_string(i)) safeId = true;
        if (!safeId) return;
        auto adapter = std::make_unique<AdapterContext>(id);
        if (!validAdapterConfig(fields, adapter->options, adapter->port, adapter->name, true, true))
            return;
        for (const auto& other : loaded)
            if (other->id == id || sameSerialPort(other->port, adapter->port)) return;
        // Keep persisted adapters visible if new primary options reserve their port.
        adapter->savedOptions = adapter->options;
        adapter->savedPort = adapter->port;
        adapter->options.serialPort = adapter->port.c_str();
        adapter->savedOptions.serialPort = adapter->savedPort.c_str();
        loaded.push_back(std::move(adapter));
    }
    extraAdapters = std::move(loaded);
}

MHD_Result handleAdaptersApi(MHD_Connection* connection, const char* method,
                            size_t* uploadSize, const char* uploadData, void** context) {
    if (strcmp(method, "GET") == 0) {
        std::lock_guard<std::mutex> registryLock(adaptersMutex);
        std::string body = "{\"adapters\":[";
        auto append = [&](AdapterContext& adapter) {
            pthread_mutex_lock(&adapter.mutex);
            std::string entry = configJSON(adapter, false);
            entry.pop_back();
            const bool ready = adapter.connected &&
                (adapter.remote ? adapter.remote->isOnline() :
                 adapter.decoder && adapter.compatible && adapter.decoder->isReady());
            entry += ",\"serialConnected\":" + std::string(adapter.connected ? "true" : "false") +
                     ",\"ready\":" + (ready ? "true" : "false") +
                     ",\"api_url\":" + jsonQuote("/adapters/" + adapter.id) + "}";
            pthread_mutex_unlock(&adapter.mutex);
            if (body.back() != '[') body += ',';
            body += entry;
        };
        append(primaryAdapter);
        for (const auto& adapter : extraAdapters) append(*adapter);
        body += "]}";
        return queueJson(connection, MHD_HTTP_OK, body.c_str());
    }
    if (strcmp(method, "POST") != 0)
        return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "{\"error\":\"Use GET or POST\"}");
    if (!restartOriginAllowed(connection))
        return queueJson(connection, MHD_HTTP_FORBIDDEN, "{\"error\":\"Cross-origin request refused\"}");
    auto* request = static_cast<RemotePostData*>(*context);
    if (!request) { *context = new RemotePostData(); return MHD_YES; }
    if (*uploadSize > 0) {
        if (*uploadSize > 1024 - request->body.size()) request->tooLarge = true;
        if (!request->tooLarge) request->body.append(uploadData, *uploadSize);
        *uploadSize = 0;
        return MHD_YES;
    }
    std::unique_ptr<RemotePostData> completed(request);
    *context = nullptr;
    if (request->tooLarge)
        return queueJson(connection, MHD_HTTP_PAYLOAD_TOO_LARGE, "{\"error\":\"Request body too large\"}");
    JsonFields fields;
    auto adapter = std::make_unique<AdapterContext>("");
    if (!parseConfigObject(request->body, fields) ||
        !validAdapterConfig(fields, adapter->options, adapter->port, adapter->name, true))
        return queueJson(connection, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Invalid adapter configuration\"}");
    std::lock_guard<std::mutex> lock(adaptersMutex);
    if (portReserved(adapter->port))
        return queueJson(connection, MHD_HTTP_CONFLICT, "{\"error\":\"Serial port already reserved\"}");
    if (extraAdapters.size() >= MAX_ADAPTERS - 1)
        return queueJson(connection, MHD_HTTP_SERVICE_UNAVAILABLE, "{\"error\":\"Adapter capacity reached\"}");
    for (size_t i = 1; i < MAX_ADAPTERS; ++i) {
        const std::string id = "adapter_" + std::to_string(i);
        bool used = false;
        for (const auto& other : extraAdapters) if (other->id == id) used = true;
        if (!used) { adapter->id = id; break; }
    }
    if (adapter->name.empty()) adapter->name = adapter->id;
    adapter->savedOptions = adapter->options;
    adapter->savedPort = adapter->port;
    adapter->options.serialPort = adapter->port.c_str();
    adapter->savedOptions.serialPort = adapter->savedPort.c_str();
    extraAdapters.push_back(std::move(adapter));
    if (!saveAdapters()) {
        extraAdapters.pop_back();
        return queueJson(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"error\":\"Could not save adapter\"}");
    }
    auto& created = *extraAdapters.back();
    try {
        created.worker = std::thread(adapterWorker, &created);
    } catch (...) {
        extraAdapters.pop_back();
        saveAdapters();
        return queueJson(connection, MHD_HTTP_SERVICE_UNAVAILABLE, "{\"error\":\"Could not start adapter\"}");
    }
    const std::string body = "{\"id\":" + jsonQuote(created.id) +
                             ",\"api_url\":" + jsonQuote("/adapters/" + created.id) + "}";
    return queueJson(connection, MHD_HTTP_CREATED, body.c_str());
}

bool saveSettings(const std::string& protocol, unsigned long baudRate,
                  const std::string& serialConfig, const std::string& remoteModel,
                  unsigned int remoteSlot, bool invertSerial) {
    char body[512];
    snprintf(body, sizeof(body),
             "{\"baud_rate\":%lu,\"protocol\":\"%s\",\"serial_config\":\"%s\","
             "\"remote_model\":\"%s\",\"remote_slot\":%u,\"invert_serial\":%s}\n",
             baudRate, protocol.c_str(), serialConfig.c_str(), remoteModel.c_str(),
             remoteSlot, invertSerial ? "true" : "false");
    return atomicWrite("ui_settings.json", body);
}

void loadPrimarySettings(Config& options) {
    const std::string path = dataDirectory() + "/ui_settings.json";
    FILE* file = fopen(path.c_str(), "r");
    if (!file) return;
    char body[1025];
    const size_t length = fread(body, 1, sizeof(body), file);
    const bool failed = ferror(file);
    fclose(file);
    JsonFields fields;
    Config saved = options;
    std::string port = options.serialPort, name = "primary";
    if (failed || length > 1024 ||
        !parseConfigObject(std::string(body, length), fields) ||
        fields.count("serial_port") || fields.count("name") ||
        !validAdapterConfig(fields, saved, port, name, false)) {
        fprintf(stderr, "Ignoring invalid primary UI settings\n");
        return;
    }
    options = saved;
}

MHD_Result handleSettingsApi(MHD_Connection* connection, const char* method,
                             size_t* uploadDataSize, const char* uploadData,
                             void** connectionContext) {
    if (strcmp(method, "GET") == 0) {
        std::lock_guard<std::mutex> lock(adaptersMutex);
        const auto& adapter = currentAdapter();
        const std::string body = configJSON(adapter, false);
        return queueJson(connection, MHD_HTTP_OK, body.c_str());
    }
    if (strcmp(method, "POST") != 0) {
        return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                         "{\"error\":\"Use POST\"}");
    }
    if (!restartOriginAllowed(connection))
        return queueJson(connection, MHD_HTTP_FORBIDDEN,
                         "{\"error\":\"Cross-origin request refused\"}");

    SettingsPostData* request = static_cast<SettingsPostData*>(*connectionContext);
    if (!request) {
        request = new SettingsPostData();
        *connectionContext = request;
        return MHD_YES;
    }
    if (*uploadDataSize > 0) {
        if (*uploadDataSize > 1024 - request->body.size()) {
            request->tooLarge = true;
            request->body.clear();
        }
        if (!request->tooLarge) {
            request->body.append(uploadData, *uploadDataSize);
        }
        *uploadDataSize = 0;
        return MHD_YES;
    }
    if (request->tooLarge) {
        delete request;
        *connectionContext = nullptr;
        return queueJson(connection, MHD_HTTP_PAYLOAD_TOO_LARGE,
                         "{\"error\":\"Request body too large\"}");
    }

    if (currentAdapter().id != "primary") {
        std::unique_ptr<SettingsPostData> completed(request);
        *connectionContext = nullptr;
        JsonFields fields;
        std::lock_guard<std::mutex> lock(adaptersMutex);
        auto& adapter = currentAdapter();
        Config options = adapter.savedOptions;
        std::string port = adapter.savedPort, name = adapter.name;
        if (!parseConfigObject(request->body, fields) ||
            !validAdapterConfig(fields, options, port, name, false))
            return queueJson(connection, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Invalid settings\"}");
        if (portReserved(port, &adapter))
            return queueJson(connection, MHD_HTTP_CONFLICT, "{\"error\":\"Serial port already reserved\"}");
        const Config previous = adapter.savedOptions;
        const std::string previousPort = adapter.savedPort, previousName = adapter.name;
        adapter.savedOptions = options;
        adapter.savedPort = port;
        adapter.savedOptions.serialPort = adapter.savedPort.c_str();
        adapter.name = name;
        if (!saveAdapters()) {
            adapter.savedOptions = previous;
            adapter.savedPort = previousPort;
            adapter.savedOptions.serialPort = adapter.savedPort.c_str();
            adapter.name = previousName;
            return queueJson(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"error\":\"Could not save settings\"}");
        }
        return queueJson(connection, MHD_HTTP_OK, "{\"status\":\"saved\",\"restart_required\":true}");
    }

    JsonFields fields;
    Config options = currentAdapter().options;
    std::string port = currentAdapter().port, name = currentAdapter().name;
    const bool valid = parseConfigObject(request->body, fields) &&
                       !fields.count("serial_port") && !fields.count("name") &&
                       validAdapterConfig(fields, options, port, name, false);

    bool saved = false;
    if (valid) {
        saved = saveSettings(protocolToken(options.protocol), options.baudRate,
                             serialToken(options.serialConfig),
                             options.remoteModelId == 0x38 ? "vitotrol300" : "vitotrol200",
                             options.remoteSlot, options.invertSerial);
    }

    delete request;
    *connectionContext = nullptr;
    if (!valid) {
        return queueJson(connection, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Invalid settings\"}");
    }
    if (!saved) {
        return queueJson(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "{\"error\":\"Could not save settings\"}");
    }
    return queueJson(connection, MHD_HTTP_OK,
                     "{\"status\":\"saved\",\"restart_required\":true}");
}

bool parseRemoteUpdate(const std::string& body, KMBusVitotrol::ControlUpdate& update,
                       std::string& mode, std::string& profile) {
    size_t position = 0;
    auto skipSpace = [&]() {
        while (position < body.size() &&
               (body[position] == ' ' || body[position] == '\t' ||
                body[position] == '\r' || body[position] == '\n')) ++position;
    };
    auto quoted = [&](std::string& value) {
        if (position >= body.size() || body[position++] != '"') return false;
        const size_t start = position;
        while (position < body.size() && body[position] != '"') {
            if (static_cast<unsigned char>(body[position]) < 0x20 || body[position] == '\\')
                return false;
            ++position;
        }
        if (position >= body.size()) return false;
        value = body.substr(start, position++ - start);
        return true;
    };
    skipSpace();
    if (position >= body.size() || body[position++] != '{') return false;
    std::unordered_set<std::string> fields;
    while (true) {
        skipSpace();
        std::string key;
        if (!quoted(key) || !fields.insert(key).second) return false;
        skipSpace();
        if (position >= body.size() || body[position++] != ':') return false;
        skipSpace();
        if (key == "mode" || key == "profile") {
            std::string& value = key == "mode" ? mode : profile;
            if (!quoted(value)) return false;
            if (key == "mode") {
                if (value != "off" && value != "water" && value != "heat_water" &&
                    value != "party_on" && value != "party_off" &&
                    value != "economy_on" && value != "economy_off") return false;
                update.hasMode = true;
                update.mode = mode.c_str();
            } else {
                if (value != "wifi" && value != "openv") return false;
                update.hasProfile = true;
                update.profile = profile.c_str();
            }
        } else {
            if (key != "room_temperature" && key != "desired_room_temperature" &&
                key != "reduced_room_temperature" && key != "party_room_temperature") return false;
            const size_t start = position;
            if (position < body.size() && body[position] == '-') ++position;
            if (position >= body.size() || body[position] < '0' || body[position] > '9')
                return false;
            if (body[position] == '0') ++position;
            else while (position < body.size() && body[position] >= '0' && body[position] <= '9')
                ++position;
            if (position < body.size() && body[position] == '.') {
                ++position;
                const size_t fraction = position;
                while (position < body.size() && body[position] >= '0' && body[position] <= '9')
                    ++position;
                if (fraction == position) return false;
            }
            if (position < body.size() && (body[position] == 'e' || body[position] == 'E')) {
                ++position;
                if (position < body.size() && (body[position] == '+' || body[position] == '-'))
                    ++position;
                const size_t exponent = position;
                while (position < body.size() && body[position] >= '0' && body[position] <= '9')
                    ++position;
                if (exponent == position) return false;
            }
            const std::string token = body.substr(start, position - start);
            char* end = nullptr;
            const float value = strtof(token.c_str(), &end);
            if (*end || !std::isfinite(value)) return false;
            if (key == "room_temperature") {
                if (value < -20.0f || value > 50.0f) return false;
                update.hasRoomTemperature = true;
                update.roomTemperature = value;
            } else {
                if (value < 5.0f || value > 35.0f || value != std::round(value)) return false;
                if (key == "desired_room_temperature") {
                    update.hasDesiredRoomTemperature = true;
                    update.desiredRoomTemperature = value;
                } else if (key == "reduced_room_temperature") {
                    update.hasReducedRoomTemperature = true;
                    update.reducedRoomTemperature = value;
                } else {
                    update.hasPartyRoomTemperature = true;
                    update.partyRoomTemperature = value;
                }
            }
        }
        skipSpace();
        if (position >= body.size()) return false;
        const char separator = body[position++];
        if (separator == '}') {
            skipSpace();
            return position == body.size() && (!update.hasPartyRoomTemperature ||
                   (update.hasMode && mode == "party_on"));
        }
        if (separator != ',') return false;
    }
}

MHD_Result handleRemoteApi(MHD_Connection* connection, const char* method,
                           size_t* uploadDataSize, const char* uploadData,
                           void** connectionContext) {
    auto& data_mutex = currentAdapter().mutex;
    auto& serialConnected = currentAdapter().connected;
    auto*& vitotrol = currentAdapter().remote;
    if (strcmp(method, "GET") == 0) {
        pthread_mutex_lock(&data_mutex);
        const std::string body = generateRemoteJSON(true);
        pthread_mutex_unlock(&data_mutex);
        return queueJson(connection, MHD_HTTP_OK, body.c_str());
    }

    if (strcmp(method, "POST") != 0) {
        return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                         "{\"error\":\"Use GET or POST\"}");
    }

    RemotePostData* request = static_cast<RemotePostData*>(*connectionContext);
    if (!request) {
        request = new RemotePostData();
        *connectionContext = request;
        return MHD_YES;
    }
    if (*uploadDataSize > 0) {
        if (*uploadDataSize > 1024 - request->body.size()) {
            request->tooLarge = true;
            request->body.clear();
        }
        if (!request->tooLarge) {
            request->body.append(uploadData, *uploadDataSize);
        }
        *uploadDataSize = 0;
        return MHD_YES;
    }
    if (request->tooLarge) {
        delete request;
        *connectionContext = nullptr;
        return queueJson(connection, MHD_HTTP_PAYLOAD_TOO_LARGE,
                         "{\"error\":\"Request body too large\"}");
    }

    KMBusVitotrol::ControlUpdate update;
    std::string mode;
    std::string profile;
    const bool valid = parseRemoteUpdate(request->body, update, mode, profile);

    bool applied = false;
    if (valid) {
        pthread_mutex_lock(&data_mutex);
        if (serialConnected && vitotrol) {
            applied = vitotrol->applyControlUpdate(update);
        }
        pthread_mutex_unlock(&data_mutex);
    }

    delete request;
    *connectionContext = nullptr;
    if (!valid) {
        return queueJson(connection, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Invalid JSON, field, value, mode or profile\"}");
    }
    if (!applied) {
        return queueJson(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "{\"error\":\"Remote is disconnected or its command queue is full\"}");
    }
    return queueJson(connection, MHD_HTTP_OK, "{\"status\":\"queued\"}");
}

// Generate HTML pages
const char* getLegacyDashboardHTML() {
    static const char* html =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<title>Viessmann Decoder</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    ":root{"
    "--primary-color:#03a9f4;"
    "--primary-dark:#0288d1;"
    "--accent-color:#ff9800;"
    "--card-background:#fff;"
    "--primary-background:#fafafa;"
    "--secondary-background:#e5e5e5;"
    "--primary-text:#212121;"
    "--secondary-text:#727272;"
    "--divider-color:#e0e0e0;"
    "--error-color:#f44336;"
    "--success-color:#4caf50;"
    "--warning-color:#ff9800;"
    "--disabled-text:#9e9e9e;"
    "--card-shadow:0 2px 2px 0 rgba(0,0,0,.14),0 1px 5px 0 rgba(0,0,0,.12),0 3px 1px -2px rgba(0,0,0,.2);"
    "}"
    ":root[data-theme='dark']{color-scheme:dark;--primary-color:#1565c0;--primary-dark:#0d47a1;--accent-color:#ffb74d;--card-background:#1e1e1e;--primary-background:#121212;--secondary-background:#292929;--primary-text:#f5f5f5;--secondary-text:#bdbdbd;--divider-color:#424242;--error-color:#ef5350;--success-color:#81c784;--warning-color:#ffb74d;--disabled-text:#757575;--card-shadow:0 2px 8px rgba(0,0,0,.45);}"
    "*{margin:0;padding:0;box-sizing:border-box;}"
    "body{font-family:'Roboto','Noto',sans-serif;background:var(--primary-background);color:var(--primary-text);-webkit-font-smoothing:antialiased;}"
    ".app-header{background:var(--primary-color);color:white;padding:0;box-shadow:0 2px 4px rgba(0,0,0,0.2);position:sticky;top:0;z-index:100;}"
    ".header-toolbar{display:flex;align-items:center;justify-content:space-between;padding:16px 24px;max-width:1200px;margin:0 auto;}"
    ".header-title{font-size:20px;font-weight:400;letter-spacing:0.02em;}"
    ".header-icon{display:inline-block;width:24px;height:24px;margin-right:12px;vertical-align:middle;}"
    ".view-container{max-width:1200px;margin:24px auto;padding:0 24px;}"
    ".status-bar{display:flex;gap:16px;margin-bottom:24px;flex-wrap:wrap;}"
    ".status-chip{background:var(--card-background);padding:12px 20px;border-radius:16px;box-shadow:var(--card-shadow);display:flex;align-items:center;gap:8px;font-size:14px;}"
    ".status-chip .label{color:var(--secondary-text);font-weight:500;}"
    ".status-chip .value{color:var(--primary-text);font-weight:500;}"
    ".status-indicator{width:8px;height:8px;border-radius:50%;background:var(--disabled-text);}"
    ".status-indicator.ok{background:var(--success-color);}"
    ".status-indicator.error{background:var(--error-color);}"
    ".card{background:var(--card-background);border-radius:8px;box-shadow:var(--card-shadow);margin-bottom:24px;overflow:hidden;}"
    ".card-header{padding:16px 20px;border-bottom:1px solid var(--divider-color);}"
    ".card-title{font-size:16px;font-weight:500;color:var(--primary-text);}"
    ".card-content{padding:0;}"
    ".sensor-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:1px;background:var(--divider-color);}"
    ".sensor-item{background:var(--card-background);padding:20px;display:flex;flex-direction:column;gap:8px;}"
    ".sensor-label{font-size:14px;color:var(--secondary-text);font-weight:400;}"
    ".sensor-value{font-size:28px;font-weight:300;color:var(--primary-text);display:flex;align-items:baseline;gap:4px;}"
    ".sensor-unit{font-size:16px;color:var(--secondary-text);font-weight:400;}"
    ".sensor-icon{width:40px;height:40px;margin-bottom:8px;opacity:0.7;}"
    ".empty-state{padding:48px 20px;text-align:center;color:var(--secondary-text);}"
    ".empty-state-icon{font-size:64px;margin-bottom:16px;opacity:0.3;}"
    ".participant-section{padding:20px;border-top:1px solid var(--divider-color);}"
    ".participant-section h2{font-size:16px;font-weight:500;margin-bottom:12px;}"
    ".participant-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:12px;}"
    ".participant-card{background:var(--primary-background);border:1px solid var(--divider-color);border-radius:8px;padding:16px;overflow-wrap:anywhere;}"
    ".participant-card h3{font-size:16px;margin-bottom:8px;}"
    ".participant-card p{color:var(--secondary-text);font-size:14px;line-height:1.6;}"
    ".nav-buttons{display:flex;gap:16px;margin-bottom:24px;flex-wrap:wrap;}"
    ".nav-button{background:var(--card-background);padding:16px 24px;border-radius:8px;box-shadow:var(--card-shadow);display:flex;align-items:center;gap:12px;text-decoration:none;color:var(--primary-text);transition:all 0.2s;font-weight:500;}"
    ".nav-button:hover{transform:translateY(-2px);box-shadow:0 4px 8px rgba(0,0,0,0.2);background:var(--primary-color);color:white;}"
    ".button-icon{width:24px;height:24px;}"
    ".theme-toggle{border:1px solid rgba(255,255,255,.65);border-radius:20px;padding:8px 12px;background:transparent;color:white;font-size:14px;cursor:pointer;}"
    ".theme-toggle:hover{background:rgba(255,255,255,.15);}"
    "@media(max-width:768px){"
    ".view-container{padding:0 16px;margin:16px auto;}"
    ".header-toolbar{padding:12px 16px;gap:12px;}"
    ".header-title{font-size:18px;}"
    ".sensor-grid{grid-template-columns:1fr;}"
    ".theme-toggle{font-size:12px;padding:7px 9px;}"
    "}"
    "</style>"
    "<script>"
    "let themeButton=null;"
    "function setTheme(theme){document.documentElement.dataset.theme=theme;if(themeButton){themeButton.textContent=theme==='dark'?'Light theme':'Dark theme';themeButton.setAttribute('aria-pressed',theme==='dark'?'true':'false');}}"
    "try{setTheme(localStorage.getItem('viessmann-decoder-theme')==='dark'?'dark':'light');}catch(error){setTheme('light');}"
    "window.addEventListener('DOMContentLoaded',()=>{themeButton=document.getElementById('themeToggle');setTheme(document.documentElement.dataset.theme);themeButton.addEventListener('click',()=>{const theme=document.documentElement.dataset.theme==='dark'?'light':'dark';setTheme(theme);try{localStorage.setItem('viessmann-decoder-theme',theme);}catch(error){}});});"
    "function renderParticipants(participants){"
    "const container=document.getElementById('busParticipants');container.replaceChildren();"
    "if(!Array.isArray(participants)||participants.length===0){const empty=document.createElement('p');empty.className='empty-state';empty.textContent='No bus participants discovered yet.';container.appendChild(empty);return;}"
    "const grid=document.createElement('div');grid.className='participant-grid';"
    "participants.forEach(participant=>{if(!participant||typeof participant!=='object')return;"
    "const card=document.createElement('article');card.className='participant-card';"
    "const address=Number(participant.address);"
    "const title=document.createElement('h3');title.textContent=String(participant.name||'Bus participant')+' · '+(Number.isInteger(address)?'0x'+address.toString(16).toUpperCase().padStart(4,'0'):'');card.appendChild(title);"
    "const details=document.createElement('p');"
    "details.textContent=(participant.active?'Active':'Inactive')+' · '+(participant.auto_detected?'Auto-discovered':'Configured')+' · Temperatures: '+Number(participant.temperature_channels||0)+' · Pumps: '+Number(participant.pump_channels||0)+' · Relays: '+Number(participant.relay_channels||0);"
    "card.appendChild(details);grid.appendChild(card);});"
    "container.appendChild(grid);"
    "}"
    "function updateData(){"
    "fetch('data').then(r=>r.json()).then(d=>{"
    "document.getElementById('remoteControls').style.display=d.protocol===4?'flex':'none';"
    "const statusDot=document.getElementById('statusDot');"
    "const statusText=document.getElementById('statusText');"
    "const protocolText=document.getElementById('protocol');"
    "const container=document.getElementById('sensorData');"
    "renderParticipants(d.participants);"
    "if(d.serialConnected===false){"
    "statusDot.className='status-indicator error';"
    "statusText.textContent='Serial port not connected';"
    "const protocols=['VBUS','KW-Bus','P300','KM-Bus','KM-Bus Slave'];"
    "protocolText.textContent=protocols[d.protocol]||'Unknown';"
    "container.innerHTML='<div class=\"empty-state\"><div class=\"empty-state-icon\">🔌</div><div style=\"font-size:18px;margin-bottom:8px;\">Serial port not connected</div><div style=\"color:var(--secondary-text);\">Please connect your Viessmann device and check the serial port configuration.</div></div>';"
    "return;"
    "}"
    "statusDot.className='status-indicator '+((d.protocol===4?d.ready:d.status==='OK')?'ok':'error');"
    "statusText.textContent=d.status;"
    "const protocols=['VBUS','KW-Bus','P300','KM-Bus','KM-Bus Slave'];"
    "protocolText.textContent=protocols[d.protocol]||'Unknown';"
    "if(d.protocol===4){"
    "container.replaceChildren();const state=document.createElement('div');state.className='empty-state';"
    "state.textContent=d.ready?'Vitotrol emulator online. Open Vitotrol-Steuerung for controls and bus data.':'Waiting for KM-Bus master...';"
    "container.appendChild(state);return;"
    "}"
    "if(!d.ready||(!d.temperatures.length&&!d.pumps.length&&!d.relays.length)){"
    "container.innerHTML='<div class=\"empty-state\"><div class=\"empty-state-icon\">⏳</div><div>Waiting for data...</div></div>';"
    "return;"
    "}"
    "let html='';"
    "if(d.temperatures&&d.temperatures.length>0){"
    "const temperatureNames=d.protocol===3?['Boiler','Hot water','Outdoor','Setpoint','Flow']:[];"
    "d.temperatures.forEach((t,i)=>{"
    "html+='<div class=\"sensor-item\">';"
    "html+='<div class=\"sensor-label\">'+(temperatureNames[i]||'Temperature '+(i+1))+'</div>';"
    "html+='<div class=\"sensor-value\">'+t.toFixed(1)+'<span class=\"sensor-unit\">°C</span></div>';"
    "html+='</div>';"
    "});"
    "}"
    "if(d.pumps&&d.pumps.length>0){"
    "const pumpNames=d.protocol===3?['Main pump','Hot water pump']:[];"
    "d.pumps.forEach((p,i)=>{"
    "html+='<div class=\"sensor-item\">';"
    "html+='<div class=\"sensor-label\">'+(pumpNames[i]||'Pump '+(i+1)+' Power')+'</div>';"
    "html+='<div class=\"sensor-value\">'+p+'<span class=\"sensor-unit\">%</span></div>';"
    "html+='</div>';"
    "});"
    "}"
    "if(d.relays&&d.relays.length>0){"
    "d.relays.forEach((r,i)=>{"
    "html+='<div class=\"sensor-item\">';"
    "html+='<div class=\"sensor-label\">'+(d.protocol===3&&i===0?'Burner':'Relay '+(i+1))+'</div>';"
    "html+='<div class=\"sensor-value\" style=\"color:'+(r?'var(--success-color)':'var(--disabled-text)')+'\">'+(r?'ON':'OFF')+'</div>';"
    "html+='</div>';"
    "});"
    "}"
    "container.innerHTML=html;"
    "}).catch(err=>{"
    "console.error('Error fetching data:',err);"
    "document.getElementById('sensorData').innerHTML='<div class=\"empty-state\"><div class=\"empty-state-icon\">⚠️</div><div>Error loading data</div></div>';"
    "});"
    "}"
    "setInterval(updateData,2000);"
    "window.onload=updateData;"
    "</script>"
    "</head><body>"
    "<div class='app-header'>"
    "<div class='header-toolbar'>"
    "<div class='header-title'>"
    "<svg class='header-icon' viewBox='0 0 24 24' fill='currentColor'>"
    "<path d='M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12C20,14.4 19,16.5 17.3,18C15.9,16.7 14,16 12,16C10,16 8.2,16.7 6.7,18C5,16.5 4,14.4 4,12A8,8 0 0,1 12,4M14,5.89C13.62,5.9 13.26,6.15 13.1,6.54L11.81,9.77L11.71,10C11,10.13 10.41,10.6 10.14,11.26C9.73,12.29 10.23,13.45 11.26,13.86C12.29,14.27 13.45,13.77 13.86,12.74C14.12,12.08 14,11.32 13.57,10.76L13.67,10.5L14.96,7.29L14.97,7.26C15.17,6.75 14.92,6.17 14.41,5.96C14.28,5.91 14.15,5.89 14,5.89M10,6A1,1 0 0,0 9,7A1,1 0 0,0 10,8A1,1 0 0,0 11,7A1,1 0 0,0 10,6M7,9A1,1 0 0,0 6,10A1,1 0 0,0 7,11A1,1 0 0,0 8,10A1,1 0 0,0 7,9M17,9A1,1 0 0,0 16,10A1,1 0 0,0 17,11A1,1 0 0,0 18,10A1,1 0 0,0 17,9Z'/>"
    "</svg>"
    "Viessmann Decoder"
    "</div>"
    "<button type='button' class='theme-toggle' id='themeToggle' aria-label='Toggle dark theme' aria-pressed='false'>Dark theme</button>"
    "</div>"
    "</div>"
    "<div class='view-container'>"
    "<div class='status-bar'>"
    "<div class='status-chip'>"
    "<div id='statusDot' class='status-indicator'></div>"
    "<span class='label'>Status:</span>"
    "<span id='statusText' class='value'>Checking...</span>"
    "</div>"
    "<div class='status-chip'>"
    "<span class='label'>Protocol:</span>"
    "<span id='protocol' class='value'>-</span>"
    "</div>"
    "</div>"
    "<div class='nav-buttons'>"
    "<a href='settings' class='nav-button'>"
    "<svg class='button-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M12,15.5A3.5,3.5 0 0,1 8.5,12A3.5,3.5 0 0,1 12,8.5A3.5,3.5 0 0,1 15.5,12A3.5,3.5 0 0,1 12,15.5M19.43,12.97C19.47,12.65 19.5,12.33 19.5,12C19.5,11.67 19.47,11.34 19.43,11L21.54,9.37C21.73,9.22 21.78,8.95 21.66,8.73L19.66,5.27C19.54,5.05 19.27,4.96 19.05,5.05L16.56,6.05C16.04,5.66 15.5,5.32 14.87,5.07L14.5,2.42C14.46,2.18 14.25,2 14,2H10C9.75,2 9.54,2.18 9.5,2.42L9.13,5.07C8.5,5.32 7.96,5.66 7.44,6.05L4.95,5.05C4.73,4.96 4.46,5.05 4.34,5.27L2.34,8.73C2.21,8.95 2.27,9.22 2.46,9.37L4.57,11C4.53,11.34 4.5,11.67 4.5,12C4.5,12.33 4.53,12.65 4.57,12.97L2.46,14.63C2.27,14.78 2.21,15.05 2.34,15.27L4.34,18.73C4.46,18.95 4.73,19.03 4.95,18.95L7.44,17.94C7.96,18.34 8.5,18.68 9.13,18.93L9.5,21.58C9.54,21.82 9.75,22 10,22H14C14.25,22 14.46,21.82 14.5,21.58L14.87,18.93C15.5,18.67 16.04,18.34 16.56,17.94L19.05,18.95C19.27,19.03 19.54,18.95 19.66,18.73L21.66,15.27C21.78,15.05 21.73,14.78 21.54,14.63L19.43,12.97Z'/></svg>"
    "<span>Settings</span>"
    "</a>"
    "<a href='devices' class='nav-button'>"
    "<svg class='button-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M17,13H13V17H11V13H7V11H11V7H13V11H17M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2Z'/></svg>"
    "<span>Add Serial Adapter</span>"
    "</a>"
    "<a href='logs' class='nav-button'>"
    "<svg class='button-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M4 3h16v18H4V3m3 4v2h10V7H7m0 4v2h10v-2H7m0 4v2h7v-2H7Z'/></svg>"
    "<span>Bus-Logs</span>"
    "</a>"
    "<a href='remote' id='remoteControls' class='nav-button' style='display:none'>"
    "<span>Vitotrol-Steuerung</span></a>"
    "</div>"
    "<div class='card'>"
    "<div class='card-header'>"
    "<div class='card-title'>Sensor Data</div>"
    "</div>"
    "<div class='card-content'>"
    "<div id='sensorData' class='sensor-grid'>"
    "<div class='empty-state'><div class='empty-state-icon'>⏳</div><div>Loading...</div></div>"
    "</div>"
    "<div class='participant-section'><h2>Bus participants</h2><div id='busParticipants'><div class='empty-state'>Loading participant data...</div></div></div>"
    "</div>"
    "</div>"
    "</div>"
    "</body></html>";

    return html;
}

const char* getDashboardHTML() {
    static const char* html = R"DASHBOARD_HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark light">
<title>Viessmann Decoder</title>
<style>
:root{color-scheme:dark;--page:#111213;--surface:#1d1e20;--raised:#252628;--text:#e7e7e8;--muted:#a3a4a6;--line:#37383a;--accent:#03a9f4;--good:#00c781;--bad:#ff6370;--shadow:0 2px 12px #0005}
:root[data-theme=light]{color-scheme:light;--page:#f4f5f6;--surface:#fff;--raised:#f7f8f9;--text:#252629;--muted:#696b70;--line:#e0e1e3;--accent:#0288d1;--good:#008f60;--bad:#cf3545;--shadow:0 2px 12px #0001}
*{box-sizing:border-box}body{margin:0;background:var(--page);color:var(--text);font:15px/1.5 Roboto,"Noto Sans",Arial,sans-serif;-webkit-font-smoothing:antialiased}a{color:inherit;text-decoration:none}button,select,input{font:inherit}
.topbar{position:sticky;top:0;z-index:5;background:var(--surface);border-bottom:1px solid var(--line)}.toolbar{max-width:1440px;margin:auto;min-height:64px;padding:0 28px;display:flex;align-items:center;gap:28px}.brand{font-size:18px;font-weight:600;white-space:nowrap}.brand-mark{display:inline-grid;place-items:center;width:30px;height:30px;margin-right:10px;border-radius:9px;background:#03a9f422;color:var(--accent);font-size:18px}
.tabs{display:flex;align-self:stretch;align-items:stretch;gap:4px;flex:1}.tabs a{display:flex;align-items:center;padding:0 16px;color:var(--muted);border-bottom:2px solid transparent}.tabs a.active,.tabs a:hover{color:var(--text);border-color:var(--accent)}.icon-button,.button{cursor:pointer;color:var(--text);background:var(--raised);border:1px solid var(--line);border-radius:10px;padding:9px 14px}.button.primary{background:var(--accent);border-color:var(--accent);color:#fff;font-weight:600}.button:disabled{opacity:.6;cursor:wait}
.page{max-width:1440px;margin:auto;padding:28px}.welcome{display:flex;align-items:flex-end;justify-content:space-between;gap:20px;margin:0 0 22px}.welcome h1{font-size:26px;margin:0 0 3px;font-weight:500}.welcome p{margin:0;color:var(--muted)}.status{display:inline-flex;align-items:center;gap:9px;padding:8px 13px;border:1px solid var(--line);border-radius:22px;background:var(--surface);font-size:13px}.dot{width:9px;height:9px;border-radius:50%;background:var(--muted)}.dot.ok{background:var(--good);box-shadow:0 0 10px #00c78166}.dot.error{background:var(--bad)}
.layout{display:grid;grid-template-columns:minmax(0,1.45fr) minmax(320px,.9fr);gap:20px;align-items:start}.column{display:grid;gap:20px;min-width:0}.card{background:var(--surface);border:1px solid var(--line);border-radius:14px;box-shadow:var(--shadow);overflow:hidden}.card-head{padding:18px 20px 14px;display:flex;align-items:center;justify-content:space-between;gap:12px}.card-head h2{font-size:18px;font-weight:500;margin:0}.card-head p{color:var(--muted);font-size:13px;margin:3px 0 0}.card-body{padding:0 20px 20px}
.metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(175px,1fr));gap:12px}.metric{min-height:116px;padding:17px;border:1px solid var(--line);border-radius:12px;background:var(--raised)}.metric-label{color:var(--muted);font-size:13px}.metric-value{display:flex;align-items:baseline;gap:5px;margin-top:10px;font-size:30px;line-height:1.2;font-weight:400}.metric-unit{font-size:15px;color:var(--muted)}.metric-state{font-size:21px;font-weight:500}.empty{grid-column:1/-1;padding:28px 12px;text-align:center;color:var(--muted)}
.details{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:14px}.detail{padding:10px 12px;background:var(--raised);border-radius:9px}.detail-label{display:block;color:var(--muted);font-size:12px}.detail-value{display:block;margin-top:3px;font-size:14px;overflow-wrap:anywhere}
.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.field label{display:block;color:var(--muted);font-size:13px;margin-bottom:6px}.field input,.field select{width:100%;padding:10px 11px;color:var(--text);background:var(--raised);border:1px solid var(--line);border-radius:9px}.field input:focus,.field select:focus{outline:2px solid var(--accent);outline-offset:1px}.field input[readonly]{opacity:.75}.check-field{display:flex;align-items:center;gap:9px;margin-top:14px}.check-field input{accent-color:var(--accent);width:17px;height:17px}
.hint{margin:13px 0 0;color:var(--muted);font-size:13px}.actions{display:flex;justify-content:flex-end;margin-top:16px}.notice{margin:14px 0 0;padding:10px 12px;border-radius:9px;background:var(--raised);color:var(--muted);font-size:13px}.notice[data-error=true]{color:var(--bad)}.notice:empty{display:none}.links{display:flex;flex-wrap:wrap;gap:10px}.link-card{display:inline-flex;align-items:center;gap:9px;padding:10px 13px;background:var(--raised);border:1px solid var(--line);border-radius:10px}.link-card:hover{border-color:var(--accent)}.participants{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}.participant{padding:13px;border:1px solid var(--line);border-radius:10px;background:var(--raised);overflow-wrap:anywhere}.participant h3{margin:0 0 5px;font-size:14px;font-weight:500}.participant p{margin:0;color:var(--muted);font-size:13px}[hidden]{display:none!important}
@media(max-width:900px){.layout{grid-template-columns:1fr}.toolbar{gap:12px;padding:0 16px}.tabs a{padding:0 10px}.page{padding:20px}}@media(max-width:580px){.toolbar{flex-wrap:wrap;min-height:auto;padding:10px 14px;gap:8px}.brand{flex:1}.tabs{order:3;flex-basis:100%;overflow-x:auto;min-height:42px}.tabs a{white-space:nowrap}.page{padding:16px 12px}.welcome{align-items:flex-start;flex-direction:column}.welcome h1{font-size:23px}.form-grid{grid-template-columns:1fr}.details{grid-template-columns:1fr 1fr}.card-head{padding:16px 16px 12px}.card-body{padding:0 16px 16px}}
</style></head><body>
<header class="topbar"><div class="toolbar"><div class="brand"><span class="brand-mark" aria-hidden="true">⌂</span>Viessmann Decoder</div>
<nav class="tabs" aria-label="Hauptnavigation"><a class="active" href="./" aria-current="page">Übersicht</a><a href='settings'>Einstellungen</a><a href='devices'>Adapter</a><a href='logs'>Bus-Protokoll</a><a href='remote' id='remoteControls' hidden>Vitotrol</a></nav>
<button type="button" class="icon-button" id='themeToggle' aria-label="Farbschema wechseln">☼</button></div></header>
<main class="page"><div class="welcome"><div><h1>Systemübersicht</h1><p>Live-Daten und Konfiguration Ihrer Heizungsanlage</p></div><div class="status"><span id="statusDot" class="dot"></span><span id="statusText" role="status" aria-live="polite">Verbindung wird geprüft …</span></div></div>
<div class="layout"><div class="column">
<section class="card" aria-labelledby="sensor-heading"><div class="card-head"><div><h2 id="sensor-heading">Sensordaten</h2><p>Aktuelle Messwerte vom Heizsystem</p></div></div><div class="card-body"><div id="sensorData" class="metrics"><div class="empty">Messwerte werden geladen …</div></div>
<div class="details"><div class="detail"><span class="detail-label">Protokoll</span><span class="detail-value" id="protocol">–</span></div><div class="detail"><span class="detail-label">Serielle Verbindung</span><span class="detail-value" id="serialPort">–</span></div><div class="detail"><span class="detail-label">Datenstatus</span><span class="detail-value" id="dataStatus">–</span></div></div></div></section>
<section class="card"><div class="card-head"><div><h2>Bus-Teilnehmer</h2><p>Automatisch erkannte Geräte</p></div></div><div class="card-body"><div id='busParticipants' class="participants"><div class="empty">Teilnehmer werden geladen …</div></div></div></section>
<section class="card"><div class="card-head"><div><h2>Schnellzugriff</h2><p>Weitere Werkzeuge und Adapter</p></div></div><div class="card-body"><div class="links"><a class="link-card" href='devices' aria-label="Add Serial Adapter">＋ Adapter verwalten</a><a class="link-card" href='logs'>▤ Bus-Protokoll</a><a class="link-card" href='status'>ⓘ Systemstatus</a></div></div></section>
</div><div class="column">
<section class="card" aria-labelledby="settings-heading"><div class="card-head"><div><h2 id="settings-heading">Verbindung einstellen</h2><p>Änderungen werden nach einem Neustart aktiv</p></div></div><div class="card-body"><form id="settingsForm">
<div class="form-grid"><div class="field"><label for="serialPortInput">Serieller Gerätepfad</label><input id="serialPortInput" name="serial_port" type="text" autocomplete="off"></div>
<div class="field"><label for="protocolInput">Protokoll</label><select id="protocolInput" name="protocol"><option value="vbus">VBUS (RESOL)</option><option value="kw">KW-Bus (VS1)</option><option value="p300">P300 (VS2/Optolink)</option><option value="km">KM-Bus</option><option value="km_remote">KM-Bus Fernbedienung</option></select></div>
<div class="field"><label for="baudInput">Baudrate</label><select id="baudInput" name="baud_rate"><option>1200</option><option>2400</option><option>4800</option><option>9600</option><option>19200</option><option>38400</option><option>57600</option><option>115200</option></select></div>
<div class="field"><label for="serialConfigInput">Serielle Konfiguration</label><select id="serialConfigInput" name="serial_config"><option>8N1</option><option>8E1</option><option>8E2</option></select></div>
<div class="field remote-setting"><label for="remoteModelInput">Fernbedienungsmodell</label><select id="remoteModelInput" name="remote_model"><option value="vitotrol200">Vitotrol 200</option><option value="vitotrol300">Vitotrol 300</option></select></div>
<div class="field remote-setting"><label for="remoteSlotInput">Heizkreis / Slot</label><select id="remoteSlotInput" name="remote_slot"><option>1</option><option>2</option><option>3</option></select></div></div>
<label class="check-field"><input type="checkbox" name="invert_serial"><span>Serielles Signal invertieren</span></label><p class="hint">Baudrate und serielle Konfiguration richten sich nach dem verwendeten Bus.</p>
<div class="actions"><button class="button primary" id="saveSettings" type="submit">Einstellungen speichern</button></div><p class="notice" id="settingsMessage" role="status" aria-live="polite"></p>
</form></div></section></div></div></main>
<script>
'use strict';
const el=id=>document.getElementById(id);
const protocolNames=['VBUS','KW-Bus','P300','KM-Bus','KM-Bus Fernbedienung'];
function setTheme(theme){document.documentElement.dataset.theme=theme;el('themeToggle').setAttribute('aria-pressed',theme==='light'?'true':'false');}
try{setTheme(localStorage.getItem('viessmann-decoder-theme')==='light'?'light':'dark');}catch(error){setTheme('dark');}
el('themeToggle').addEventListener('click',()=>{const next=document.documentElement.dataset.theme==='dark'?'light':'dark';setTheme(next);try{localStorage.setItem('viessmann-decoder-theme',next);}catch(error){}});
function updateProtocolFields(){const remote=el('protocolInput').value==='km_remote';document.querySelectorAll('.remote-setting').forEach(field=>field.hidden=!remote);el('baudInput').disabled=remote;el('serialConfigInput').disabled=remote;if(remote){el('baudInput').value='1200';el('serialConfigInput').value='8E1';}}
function renderSettings(settings){const form=el('settingsForm');form.elements.serial_port.value=settings.serial_port||'';form.elements.protocol.value=settings.protocol;form.elements.baud_rate.value=String(settings.baud_rate);form.elements.serial_config.value=settings.serial_config;form.elements.remote_model.value=settings.remote_model;form.elements.remote_slot.value=String(settings.remote_slot);form.elements.invert_serial.checked=Boolean(settings.invert_serial);form.elements.serial_port.readOnly=!/\/adapters\/[^/]+\/?$/.test(location.pathname);updateProtocolFields();}
function message(text,error=false){el('settingsMessage').textContent=text;el('settingsMessage').dataset.error=String(error);}
fetch('api/settings',{headers:{Accept:'application/json'}}).then(response=>{if(!response.ok)throw new Error();return response.json();}).then(renderSettings).catch(()=>message('Einstellungen konnten nicht geladen werden. Bitte Seite neu laden.',true));
el('protocolInput').addEventListener('change',updateProtocolFields);
el('settingsForm').addEventListener('submit',async event=>{event.preventDefault();const form=event.currentTarget,button=el('saveSettings');const values={protocol:form.elements.protocol.value,baud_rate:Number(form.elements.baud_rate.value),serial_config:form.elements.serial_config.value,remote_model:form.elements.remote_model.value,remote_slot:Number(form.elements.remote_slot.value),invert_serial:form.elements.invert_serial.checked};if(!form.elements.serial_port.readOnly)values.serial_port=form.elements.serial_port.value.trim();button.disabled=true;message('Einstellungen werden gespeichert …');try{const response=await fetch('api/settings',{method:'POST',headers:{'Content-Type':'application/json',Accept:'application/json'},body:JSON.stringify(values)});const result=await response.json();if(!response.ok)throw new Error(result.error||'Unbekannter Fehler');message('Gespeichert. Bitte starten Sie den Container neu, damit die Änderungen aktiv werden.');}catch(error){message('Speichern fehlgeschlagen: '+error.message,true);}finally{button.disabled=false;}});
function node(tag,className,text){const item=document.createElement(tag);if(className)item.className=className;if(text!==undefined)item.textContent=text;return item;}
function renderParticipants(participants){const container=el('busParticipants');container.replaceChildren();if(!Array.isArray(participants)||!participants.length){container.appendChild(node('p','empty','Noch keine Bus-Teilnehmer erkannt.'));return;}participants.forEach(participant=>{if(!participant||typeof participant!=='object')return;const card=node('article','participant'),address=Number(participant.address);card.appendChild(node('h3','',String(participant.name||'Bus-Teilnehmer')+' · '+(Number.isInteger(address)?'0x'+address.toString(16).toUpperCase().padStart(4,'0'):'')));card.appendChild(node('p','',(participant.active?'Aktiv':'Inaktiv')+' · '+(participant.auto_detected?'Automatisch erkannt':'Konfiguriert')+' · '+Number(participant.temperature_channels||0)+' Temperaturen · '+Number(participant.pump_channels||0)+' Pumpen · '+Number(participant.relay_channels||0)+' Relais'));container.appendChild(card);});}
function addMetric(container,label,value,unit,state){const card=node('article','metric');card.appendChild(node('div','metric-label',label));const output=node('div',state?'metric-value metric-state':'metric-value',value);if(unit)output.appendChild(node('span','metric-unit',unit));card.appendChild(output);container.appendChild(card);}
async function updateData(){try{const response=await fetch('data');if(!response.ok)throw new Error();const data=await response.json(),container=el('sensorData'),protocol=Number(data.protocol);container.replaceChildren();el('protocol').textContent=protocolNames[protocol]||'Unbekannt';el('serialPort').textContent=data.serialPort||'–';el('dataStatus').textContent=data.ready?'Daten verfügbar':'Warte auf Daten';el('remoteControls').hidden=protocol!==4;el('statusText').textContent=data.serialConnected===false?'Serieller Port nicht verbunden':(data.status||'Verbunden');el('statusDot').className='dot '+(data.serialConnected===false?'error':((protocol===4?data.ready:data.status==='OK')?'ok':''));if(protocol===4){addMetric(container,'Vitotrol-Status',data.ready?'Online':'Warte auf KM-Bus',null,true);}else if(data.serialConnected===false){container.appendChild(node('div','empty','Bitte Gerät und seriellen Anschluss prüfen.'));}else if(!data.ready){container.appendChild(node('div','empty','Warte auf Messwerte vom Heizsystem …'));}else{const temperatures=Array.isArray(data.temperatures)?data.temperatures:[],pumps=Array.isArray(data.pumps)?data.pumps:[],relays=Array.isArray(data.relays)?data.relays:[],temperatureNames=protocol===3?['Kessel','Warmwasser','Außen','Sollwert','Vorlauf']:[],pumpNames=protocol===3?['Hauptpumpe','Warmwasserpumpe']:[];temperatures.forEach((value,index)=>addMetric(container,temperatureNames[index]||'Temperatur '+(index+1),Number(value).toFixed(1),'°C'));pumps.forEach((value,index)=>addMetric(container,pumpNames[index]||'Pumpe '+(index+1),String(value),'%'));relays.forEach((value,index)=>addMetric(container,protocol===3&&index===0?'Brenner':'Relais '+(index+1),value?'EIN':'AUS',null,true));if(!container.children.length)container.appendChild(node('div','empty','Keine Messwerte verfügbar.'));}renderParticipants(data.participants);}catch(error){el('statusDot').className='dot error';el('statusText').textContent='Verbindung nicht verfügbar';el('sensorData').replaceChildren(node('div','empty','Sensordaten konnten nicht geladen werden.'));}}
updateData();setInterval(updateData,2000);
</script></body></html>)DASHBOARD_HTML";
    return html;
}

const char* getStatusHTML() {
    const auto& config = currentAdapter().options;
    auto*& vbus = currentAdapter().decoder;
    auto*& vitotrol = currentAdapter().remote;
    auto& data_mutex = currentAdapter().mutex;
    static thread_local char html[16384]; // Thread-local buffer for thread-safe access

    pthread_mutex_lock(&data_mutex);
    if (config.protocol == PROTOCOL_KM_REMOTE && vitotrol) {
        snprintf(html, sizeof(html),
                 "<!doctype html><html><body><h1>Vitotrol Slave</h1>"
                 "<p>Model: Vitotrol %s; slot %u; bus %s.</p>"
                 "<p><a href='remote'>Open remote controls</a></p></body></html>",
                 config.remoteModelId == 0x38 ? "300" : "200",
                 config.remoteSlot,
                 vitotrol->isOnline() ? "online" : "waiting for KM1");
        pthread_mutex_unlock(&data_mutex);
        return html;
    }
    // Check if vbus is valid
    if (!vbus) {
        snprintf(html, sizeof(html),
                "<!DOCTYPE html><html><body><h1>Error: System not initialized</h1></body></html>");
        pthread_mutex_unlock(&data_mutex);
        return html;
    }

    int written = snprintf(html, sizeof(html) - 1, // Reserve space for null terminator
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<title>Viessmann Decoder - Status</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    ":root{"
    "--primary-color:#03a9f4;"
    "--card-background:#fff;"
    "--primary-background:#fafafa;"
    "--primary-text:#212121;"
    "--secondary-text:#727272;"
    "--divider-color:#e0e0e0;"
    "--card-shadow:0 2px 2px 0 rgba(0,0,0,.14),0 1px 5px 0 rgba(0,0,0,.12),0 3px 1px -2px rgba(0,0,0,.2);"
    "}"
    "*{margin:0;padding:0;box-sizing:border-box;}"
    "body{font-family:'Roboto','Noto',sans-serif;background:var(--primary-background);color:var(--primary-text);}"
    ".app-header{background:var(--primary-color);color:white;padding:0;box-shadow:0 2px 4px rgba(0,0,0,0.2);}"
    ".header-toolbar{display:flex;align-items:center;padding:16px 24px;max-width:1200px;margin:0 auto;}"
    ".header-title{font-size:20px;font-weight:400;}"
    ".header-icon{width:24px;height:24px;margin-right:12px;vertical-align:middle;}"
    ".view-container{max-width:1200px;margin:24px auto;padding:0 24px;}"
    ".card{background:var(--card-background);border-radius:8px;box-shadow:var(--card-shadow);margin-bottom:24px;overflow:hidden;}"
    ".card-header{padding:16px 20px;border-bottom:1px solid var(--divider-color);}"
    ".card-title{font-size:16px;font-weight:500;}"
    ".info-table{width:100%%;}"
    ".info-row{display:flex;padding:16px 20px;border-bottom:1px solid var(--divider-color);}"
    ".info-row:last-child{border-bottom:none;}"
    ".info-label{flex:1;color:var(--secondary-text);font-size:14px;}"
    ".info-value{flex:1;color:var(--primary-text);font-size:14px;font-weight:500;text-align:right;}"
    "@media(max-width:768px){.view-container{padding:0 16px;margin:16px auto;}}"
    "</style>"
    "</head><body>"
    "<div class='app-header'>"
    "<div class='header-toolbar'>"
    "<div class='header-title'>"
    "<svg class='header-icon' viewBox='0 0 24 24' fill='currentColor'>"
    "<path d='M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12C20,14.4 19,16.5 17.3,18C15.9,16.7 14,16 12,16C10,16 8.2,16.7 6.7,18C5,16.5 4,14.4 4,12A8,8 0 0,1 12,4M14,5.89C13.62,5.9 13.26,6.15 13.1,6.54L11.81,9.77L11.71,10C11,10.13 10.41,10.6 10.14,11.26C9.73,12.29 10.23,13.45 11.26,13.86C12.29,14.27 13.45,13.77 13.86,12.74C14.12,12.08 14,11.32 13.57,10.76L13.67,10.5L14.96,7.29L14.97,7.26C15.17,6.75 14.92,6.17 14.41,5.96C14.28,5.91 14.15,5.89 14,5.89M10,6A1,1 0 0,0 9,7A1,1 0 0,0 10,8A1,1 0 0,0 11,7A1,1 0 0,0 10,6M7,9A1,1 0 0,0 6,10A1,1 0 0,0 7,11A1,1 0 0,0 8,10A1,1 0 0,0 7,9M17,9A1,1 0 0,0 16,10A1,1 0 0,0 17,11A1,1 0 0,0 18,10A1,1 0 0,0 17,9Z'/>"
    "</svg>"
    "System Status"
    "</div>"
    "</div>"
    "</div>"
    "<div class='view-container'>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>Current Configuration</div></div>"
    "<div class='info-table'>"
    "<div class='info-row'><div class='info-label'>Protocol</div><div class='info-value'>%s</div></div>"
    "<div class='info-row'><div class='info-label'>Baud Rate</div><div class='info-value'>%lu</div></div>"
    "<div class='info-row'><div class='info-label'>Serial Config</div><div class='info-value'>%s</div></div>"
    "<div class='info-row'><div class='info-label'>Serial Port</div><div class='info-value'>%s</div></div>"
    "<div class='info-row'><div class='info-label'>Web Port</div><div class='info-value'>%d</div></div>"
    "</div>"
    "</div>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>System Information</div></div>"
    "<div class='info-table'>"
    "<div class='info-row'><div class='info-label'>Platform</div><div class='info-value'>Linux</div></div>"
    "<div class='info-row'><div class='info-label'>Communication Status</div><div class='info-value'>%s</div></div>"
    "<div class='info-row'><div class='info-label'>Data Ready</div><div class='info-value'>%s</div></div>"
    "</div>"
    "</div>"
    "</div></body></html>",
    getProtocolName(config.protocol),
    config.baudRate,
    config.serialConfig == SERIAL_8N1 ? "8N1" :
    config.serialConfig == SERIAL_8E1 ? "8E1" : "8E2",
    config.serialPort,
    config.webPort,
    vbus->getVbusStat() ? "OK" : "Error",
    vbus->isReady() ? "Yes" : "No");

    // Ensure null termination and check for overflow
    html[sizeof(html) - 1] = '\0';
    if (written < 0 || written >= (int)(sizeof(html) - 1)) {
        fprintf(stderr, "Warning: HTML buffer overflow detected and handled\n");
        // Buffer is already null-terminated and truncated
    }

    pthread_mutex_unlock(&data_mutex);
    return html;
}

// Generate Settings Page HTML
std::string getSettingsHTML() {
    const auto& config = currentAdapter().options;
    const bool primary = currentAdapter().id == "primary";
    const std::string additionalBaudOption = std::string(
        (config.baudRate == 2400 ? "<option value='2400' selected>2400</option>" :
                                  "<option value='2400'>2400</option>")) +
        (config.baudRate == 57600 ? "<option value='57600' selected>57600</option>" :
                                   "<option value='57600'>57600</option>");
    static thread_local char html[16384];

    int written = snprintf(html, sizeof(html) - 1,
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<title>Settings - Viessmann Decoder</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    ":root{"
    "--primary-color:#03a9f4;"
    "--card-background:#fff;"
    "--primary-background:#fafafa;"
    "--primary-text:#212121;"
    "--secondary-text:#727272;"
    "--divider-color:#e0e0e0;"
    "--success-color:#4caf50;"
    "--card-shadow:0 2px 2px 0 rgba(0,0,0,.14),0 1px 5px 0 rgba(0,0,0,.12),0 3px 1px -2px rgba(0,0,0,.2);"
    "}"
    ":root[data-theme='dark']{color-scheme:dark;--primary-color:#1565c0;--card-background:#1e1e1e;--primary-background:#121212;--primary-text:#f5f5f5;--secondary-text:#bdbdbd;--divider-color:#424242;--success-color:#81c784;--card-shadow:0 2px 8px rgba(0,0,0,.45);}"
    "*{margin:0;padding:0;box-sizing:border-box;}"
    "body{font-family:'Roboto','Noto',sans-serif;background:var(--primary-background);color:var(--primary-text);}"
    ".app-header{background:var(--primary-color);color:white;box-shadow:0 2px 4px rgba(0,0,0,0.2);}"
    ".header-toolbar{display:flex;align-items:center;padding:16px 24px;max-width:1200px;margin:0 auto;}"
    ".header-title{font-size:20px;font-weight:400;}"
    ".back-button{background:none;border:none;color:white;cursor:pointer;padding:8px;margin-right:16px;}"
    ".back-icon{width:24px;height:24px;}"
    ".view-container{max-width:1200px;margin:24px auto;padding:0 24px;}"
    ".card{background:var(--card-background);border-radius:8px;box-shadow:var(--card-shadow);margin-bottom:24px;overflow:hidden;}"
    ".card-header{padding:16px 20px;border-bottom:1px solid var(--divider-color);}"
    ".card-title{font-size:16px;font-weight:500;}"
    ".form-group{padding:20px;border-bottom:1px solid var(--divider-color);}"
    ".form-group:last-child{border-bottom:none;}"
    ".form-label{font-size:14px;color:var(--secondary-text);margin-bottom:8px;display:block;}"
    ".form-control{width:100%%;padding:12px;border:1px solid var(--divider-color);border-radius:4px;font-size:14px;background:var(--card-background);color:var(--primary-text);}"
    ".form-control:focus{outline:none;border-color:var(--primary-color);}"
    ".form-select{width:100%%;padding:12px;border:1px solid var(--divider-color);border-radius:4px;font-size:14px;background:var(--card-background);color:var(--primary-text);}"
    ".button-group{padding:20px;display:flex;gap:12px;justify-content:flex-end;}"
    ".btn{padding:12px 24px;border:none;border-radius:4px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s;}"
    ".btn-primary{background:var(--primary-color);color:white;}"
    ".btn-primary:hover{background:#0288d1;}"
    ".btn-secondary{background:var(--divider-color);color:var(--primary-text);}"
    ".btn-secondary:hover{background:#ccc;}"
    ".theme-toggle{margin-left:auto;border:1px solid rgba(255,255,255,.65);border-radius:20px;padding:8px 12px;background:transparent;color:white;cursor:pointer;}"
    "@media(max-width:768px){.view-container{padding:0 16px;margin:16px auto;}}"
    "</style>"
    "</head><body>"
    "<div class='app-header'>"
    "<div class='header-toolbar'>"
    "<a href='.' class='back-button'>"
    "<svg class='back-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M20,11V13H8L13.5,18.5L12.08,19.92L4.16,12L12.08,4.08L13.5,5.5L8,11H20Z'/></svg>"
    "</a>"
    "<div class='header-title'>Settings</div>"
    "<button type='button' class='theme-toggle' id='themeToggle' aria-label='Toggle dark theme' aria-pressed='false'>Dark theme</button>"
    "</div>"
    "</div>"
    "<div class='view-container'>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>Connection Settings</div></div>"
    "<form id='settingsForm' onsubmit='saveSettings(event)'>"
    "<div class='form-group'>"
    "<label class='form-label'>Serial Port</label>"
    "<input type='text' class='form-control' name='serial_port' value='%s'%s>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Baud Rate</label>"
    "<select class='form-select' name='baud_rate'>"
    "<option value='1200'%s>1200</option>"
    "%s"
    "<option value='4800'%s>4800</option>"
    "<option value='9600'%s>9600</option>"
    "<option value='19200'%s>19200</option>"
    "<option value='38400'%s>38400</option>"
    "<option value='115200'%s>115200</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Protocol</label>"
    "<select class='form-select' id='protocol' name='protocol'>"
    "<option value='vbus'%s>VBUS (RESOL)</option>"
    "<option value='kw'%s>KW-Bus (VS1)</option>"
    "<option value='p300'%s>P300 (VS2/Optolink)</option>"
    "<option value='km'%s>KM-Bus</option>"
    "<option value='km_remote'%s>KM-Bus Slave (Vitotrol emulation)</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Serial Configuration</label>"
    "<select class='form-select' name='serial_config'>"
    "<option value='8N1'%s>8N1</option>"
    "<option value='8E1'%s>8E1</option>"
    "<option value='8E2'%s>8E2</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Vitotrol Model</label>"
    "<select class='form-select' name='remote_model'>"
    "<option value='vitotrol200'%s>Vitotrol 200</option>"
    "<option value='vitotrol300'%s>Vitotrol 300</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Heating Circuit Slot</label>"
    "<select class='form-select' name='remote_slot'>"
    "<option value='1'%s>1</option>"
    "<option value='2'%s>2</option>"
    "<option value='3'%s>3</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'><input type='checkbox' name='invert_serial'%s> Invert serial signals</label>"
    "</div>"
    "<div id='settingsMessage' class='form-group' role='status'>Changes require a container restart.</div>"
    "<div class='button-group'>"
    "<button type='button' class='btn btn-secondary' onclick='window.location.href=\".\"'>Cancel</button>"
    "<button class='btn btn-primary' type='submit'>Save</button>"
    "</div>"
    "</form>"
    "</div>"
    "</div>"
    "<script>"
    "let themeButton=null;"
    "function setTheme(theme){document.documentElement.dataset.theme=theme;if(themeButton){themeButton.textContent=theme==='dark'?'Light theme':'Dark theme';themeButton.setAttribute('aria-pressed',theme==='dark'?'true':'false');}}"
    "try{setTheme(localStorage.getItem('viessmann-decoder-theme')==='dark'?'dark':'light');}catch(error){setTheme('light');}"
    "window.addEventListener('DOMContentLoaded',()=>{themeButton=document.getElementById('themeToggle');setTheme(document.documentElement.dataset.theme);themeButton.addEventListener('click',()=>{const theme=document.documentElement.dataset.theme==='dark'?'light':'dark';setTheme(theme);try{localStorage.setItem('viessmann-decoder-theme',theme);}catch(error){}});});"
    "const settingsForm=document.getElementById('settingsForm');"
    "const settingsMessage=document.getElementById('settingsMessage');"
    "document.getElementById('protocol').addEventListener('change',event=>{"
    "if(event.target.value==='km_remote'){settingsForm.elements.baud_rate.value='1200';"
    "settingsForm.elements.serial_config.value='8E1';}});"
    "async function saveSettings(event){event.preventDefault();"
    "const values={baud_rate:Number(settingsForm.elements.baud_rate.value),"
    "protocol:settingsForm.elements.protocol.value,"
    "serial_config:settingsForm.elements.serial_config.value,"
    "remote_model:settingsForm.elements.remote_model.value,"
    "remote_slot:Number(settingsForm.elements.remote_slot.value),"
    "invert_serial:settingsForm.elements.invert_serial.checked};"
    "if(!settingsForm.elements.serial_port.readOnly)"
    "values.serial_port=settingsForm.elements.serial_port.value;"
    "try{const response=await fetch('api/settings',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(values)});"
    "const result=await response.json();if(!response.ok)throw new Error(result.error);"
    "settingsMessage.textContent='Settings saved. Restart the container to apply them.';"
    "}catch(error){settingsMessage.textContent='Could not save settings: '+error.message;}}"
    "</script>"
    "</body></html>",
    config.serialPort,
    primary ? " readonly" : "",
    config.baudRate == 1200 ? " selected" : "",
    additionalBaudOption.c_str(),
    config.baudRate == 4800 ? " selected" : "",
    config.baudRate == 9600 ? " selected" : "",
    config.baudRate == 19200 ? " selected" : "",
    config.baudRate == 38400 ? " selected" : "",
    config.baudRate == 115200 ? " selected" : "",
    config.protocol == PROTOCOL_VBUS ? " selected" : "",
    config.protocol == PROTOCOL_KW ? " selected" : "",
    config.protocol == PROTOCOL_P300 ? " selected" : "",
    config.protocol == PROTOCOL_KM ? " selected" : "",
    config.protocol == PROTOCOL_KM_REMOTE ? " selected" : "",
    config.serialConfig == SERIAL_8N1 ? " selected" : "",
    config.serialConfig == SERIAL_8E1 ? " selected" : "",
    config.serialConfig == SERIAL_8E2 ? " selected" : "",
    config.remoteModelId == 0x34 ? " selected" : "",
    config.remoteModelId == 0x38 ? " selected" : "",
    config.remoteSlot == 1 ? " selected" : "",
    config.remoteSlot == 2 ? " selected" : "",
    config.remoteSlot == 3 ? " selected" : "",
    config.invertSerial ? " checked" : "");

    html[sizeof(html) - 1] = '\0';
    if (written < 0 || written >= (int)(sizeof(html) - 1)) {
        fprintf(stderr, "Warning: HTML buffer overflow detected\n");
    }

    std::string result(html);
    result.insert(result.find("<div class='card'>"), adapterSettingsPanel());
    return result;
}

// Generate Device Configuration Page HTML
const char* getDevicesHTML() {
    auto*& vbus = currentAdapter().decoder;
    auto& data_mutex = currentAdapter().mutex;
    static thread_local char html[16384];

    pthread_mutex_lock(&data_mutex);
    const uint8_t detectedDevices = vbus ? vbus->getParticipantCount() : 0;
    pthread_mutex_unlock(&data_mutex);

    int written = snprintf(html, sizeof(html) - 1,
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<title>Device Configuration - Viessmann Decoder</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    ":root{"
    "--primary-color:#03a9f4;"
    "--card-background:#fff;"
    "--primary-background:#fafafa;"
    "--primary-text:#212121;"
    "--secondary-text:#727272;"
    "--divider-color:#e0e0e0;"
    "--success-color:#4caf50;"
    "--card-shadow:0 2px 2px 0 rgba(0,0,0,.14),0 1px 5px 0 rgba(0,0,0,.12),0 3px 1px -2px rgba(0,0,0,.2);"
    "}"
    "*{margin:0;padding:0;box-sizing:border-box;}"
    "body{font-family:'Roboto','Noto',sans-serif;background:var(--primary-background);color:var(--primary-text);}"
    ".app-header{background:var(--primary-color);color:white;box-shadow:0 2px 4px rgba(0,0,0,0.2);}"
    ".header-toolbar{display:flex;align-items:center;padding:16px 24px;max-width:1200px;margin:0 auto;}"
    ".header-title{font-size:20px;font-weight:400;}"
    ".back-button{background:none;border:none;color:white;cursor:pointer;padding:8px;margin-right:16px;text-decoration:none;display:flex;align-items:center;}"
    ".back-icon{width:24px;height:24px;}"
    ".view-container{max-width:1200px;margin:24px auto;padding:0 24px;}"
    ".card{background:var(--card-background);border-radius:8px;box-shadow:var(--card-shadow);margin-bottom:24px;overflow:hidden;}"
    ".card-header{padding:16px 20px;border-bottom:1px solid var(--divider-color);}"
    ".card-title{font-size:16px;font-weight:500;}"
    ".form-group{padding:20px;border-bottom:1px solid var(--divider-color);}"
    ".form-group:last-child{border-bottom:none;}"
    ".form-label{font-size:14px;color:var(--secondary-text);margin-bottom:8px;display:block;}"
    ".form-control{width:100%%;padding:12px;border:1px solid var(--divider-color);border-radius:4px;font-size:14px;}"
    ".form-control:focus{outline:none;border-color:var(--primary-color);}"
    ".form-select{width:100%%;padding:12px;border:1px solid var(--divider-color);border-radius:4px;font-size:14px;background:white;}"
    ".form-hint{font-size:12px;color:var(--secondary-text);margin-top:4px;}"
    ".button-group{padding:20px;display:flex;gap:12px;justify-content:flex-end;}"
    ".btn{padding:12px 24px;border:none;border-radius:4px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s;}"
    ".btn-primary{background:var(--primary-color);color:white;}"
    ".btn-primary:hover{background:#0288d1;}"
    ".btn-secondary{background:var(--divider-color);color:var(--primary-text);}"
    ".btn-secondary:hover{background:#ccc;}"
    ".info-box{background:#e3f2fd;border-left:4px solid var(--primary-color);padding:16px;margin:20px;border-radius:4px;}"
    ".info-box-title{font-weight:500;margin-bottom:8px;}"
    ".info-box-text{font-size:14px;color:var(--secondary-text);}"
    "@media(max-width:768px){.view-container{padding:0 16px;margin:16px auto;}}"
    "</style>"
    "</head><body>"
    "<div class='app-header'>"
    "<div class='header-toolbar'>"
    "<a href='.' class='back-button'>"
    "<svg class='back-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M20,11V13H8L13.5,18.5L12.08,19.92L4.16,12L12.08,4.08L13.5,5.5L8,11H20Z'/></svg>"
    "</a>"
    "<div class='header-title'>Add Device</div>"
    "</div>"
    "</div>"
    "<div class='view-container'>"
    "<div class='info-box'>"
    "<div class='info-box-title'>Auto-Discovery Active</div>"
    "<div class='info-box-text'>Devices are automatically discovered on the bus. Manual configuration is available for advanced users.</div>"
    "</div>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>Manual Device Configuration</div></div>"
    "<form onsubmit='return false;'>"
    "<div class='form-group'>"
    "<label class='form-label'>Device Address</label>"
    "<input type='text' class='form-control' placeholder='e.g., 0x10 or 0x7E11'>"
    "<div class='form-hint'>Hexadecimal address of the device on the bus</div>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Device Type</label>"
    "<select class='form-select'>"
    "<option value=''>Select device type...</option>"
    "<option value='vitosolic200'>Viessmann Vitosolic 200 (0x1060)</option>"
    "<option value='deltasol_bx_plus'>DeltaSol BX Plus (0x7E11)</option>"
    "<option value='deltasol_bx'>DeltaSol BX (0x7E21)</option>"
    "<option value='deltasol_mx'>DeltaSol MX (0x7E31)</option>"
    "<option value='vitotronic100'>Vitotronic 100 Series</option>"
    "<option value='vitotronic200'>Vitotronic 200 Series</option>"
    "<option value='generic'>Generic Device</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Device Name</label>"
    "<input type='text' class='form-control' placeholder='e.g., Solar Controller'>"
    "<div class='form-hint'>Friendly name for this device</div>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Enable Discovery</label>"
    "<select class='form-select'>"
    "<option value='auto' selected>Automatic Discovery</option>"
    "<option value='manual'>Manual Configuration Only</option>"
    "</select>"
    "</div>"
    "<div class='button-group'>"
    "<button type='button' class='btn btn-secondary' onclick='window.location.href=\".\"'>Cancel</button>"
    "<button class='btn btn-primary' onclick='alert(\"Device management is handled automatically. For manual configuration, devices can be added through the library API.\")'>Add Device</button>"
    "</div>"
    "</form>"
    "</div>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>Discovered Devices</div></div>"
    "<div class='info-box'>"
    "<div class='info-box-text'>Currently detected: %d device(s) on the bus. Check the main dashboard for real-time sensor data.</div>"
    "</div>"
    "</div>"
    "</div>"
    "</body></html>",
    detectedDevices);

    html[sizeof(html) - 1] = '\0';
    if (written < 0 || written >= (int)(sizeof(html) - 1)) {
        fprintf(stderr, "Warning: HTML buffer overflow detected\n");
    }

    return html;
}

const char* getBusLogsHTML() {
    return R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Viessmann Decoder - Bus-Logs</title>
<style>
:root{color-scheme:light;--page:#fafafa;--card:#fff;--text:#212121;--muted:#727272;--line:#e0e0e0;--primary:#03a9f4;--log:#212121;--log-text:#e0e0e0}
:root[data-theme="dark"]{color-scheme:dark;--page:#121212;--card:#1e1e1e;--text:#f5f5f5;--muted:#bdbdbd;--line:#424242;--primary:#1565c0;--log:#090909;--log-text:#eee}
*{box-sizing:border-box}
body{margin:0;font-family:Roboto,Noto,sans-serif;background:var(--page);color:var(--text)}
header{background:var(--primary);color:white;padding:16px 24px;box-shadow:0 2px 4px #0003}
header nav{max-width:1152px;margin:auto;display:flex;align-items:center;gap:24px}
header a{color:white;text-decoration:none}
h1{font-size:20px;font-weight:400;margin:0}
main{max-width:1200px;margin:24px auto;padding:0 24px}
.card{background:var(--card);border-radius:8px;padding:20px;box-shadow:0 2px 5px #0003}
.controls{display:flex;gap:16px;align-items:center;flex-wrap:wrap}
button{background:var(--primary);color:white;border:0;border-radius:4px;padding:10px 16px;cursor:pointer}
pre{background:var(--log);color:var(--log-text);padding:16px;border-radius:4px;overflow:auto;max-height:65vh;white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}
#status{color:var(--muted)}
.theme-toggle{margin-left:auto;border:1px solid rgba(255,255,255,.65);border-radius:20px;padding:8px 12px;background:transparent;color:white;cursor:pointer}
@media(max-width:768px){main{padding:0 16px}}
</style></head><body>
<header><nav><a href=".">← Dashboard</a><h1>Bus-Logs</h1><button class="theme-toggle" id="themeToggle" type="button" aria-pressed="false">Dark theme</button></nav></header>
<main><div class="card">
<p>Bus-Kommunikation: RX = empfangen, TX = gesendet. Anzeige als Hexadezimaldaten mit Zeitstempel.</p>
<p>Die letzten 500 Einträge bleiben bis zum Neustart im Arbeitsspeicher.
Byte-Gruppen sind keine Protokollrahmen; bei Signalinvertierung werden die logischen Bytes angezeigt.</p>
<div class="controls"><button id="pause" type="button">Anzeige pausieren</button>
<label><input id="follow" type="checkbox" checked> Automatisch scrollen</label></div>
<p id="status" role="status">Logs werden geladen…</p>
<pre id="logs">Noch keine Bus-Kommunikation aufgezeichnet.</pre>
</div></main>
<script>
const themeButton=document.getElementById('themeToggle');
function setTheme(theme){document.documentElement.dataset.theme=theme;themeButton.textContent=theme==='dark'?'Light theme':'Dark theme';themeButton.setAttribute('aria-pressed',theme==='dark'?'true':'false');}
try{setTheme(localStorage.getItem('viessmann-decoder-theme')==='dark'?'dark':'light');}catch(error){setTheme('light');}
themeButton.addEventListener('click',()=>{const theme=document.documentElement.dataset.theme==='dark'?'light':'dark';setTheme(theme);try{localStorage.setItem('viessmann-decoder-theme',theme);}catch(error){}});
const logs=document.getElementById('logs'),status=document.getElementById('status');
const pause=document.getElementById('pause'),follow=document.getElementById('follow');
let paused=false;
pause.onclick=()=>{
    paused=!paused;
    pause.textContent=paused?'Anzeige fortsetzen':'Anzeige pausieren';
    status.textContent=paused?'Anzeige pausiert; Aufzeichnung läuft weiter.':'Live-Anzeige aktiv.';
};
async function refresh(){
    try{
        if(!paused){
            const response=await fetch('api/bus-logs',{cache:'no-store'});
            if(!response.ok)throw new Error('HTTP '+response.status);
            const text=await response.text();
            if(!paused){
                logs.textContent=text||'Noch keine Bus-Kommunikation aufgezeichnet.';
                status.textContent='Live-Anzeige aktiv · Aktualisierung alle 2 Sekunden';
                if(follow.checked)logs.scrollTop=logs.scrollHeight;
            }
        }
    }catch(error){
        if(!paused)status.textContent='Logs konnten nicht geladen werden. Erneuter Versuch folgt.';
    }finally{setTimeout(refresh,2000);}
}
refresh();
</script></body></html>)HTML";
}

const char* getRemoteHTML() {
    return R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Viessmann Decoder - Vitotrol</title>
<style>
:root{color-scheme:light;--page:#fafafa;--card:#fff;--text:#212121;--muted:#727272;--line:#ccc;--primary:#03a9f4}
:root[data-theme="dark"]{color-scheme:dark;--page:#121212;--card:#1e1e1e;--text:#f5f5f5;--muted:#bdbdbd;--line:#555;--primary:#1565c0}
*{box-sizing:border-box}body{margin:0;background:var(--page);color:var(--text);font-family:Roboto,Noto,sans-serif}
header{background:var(--primary);color:white;padding:16px 24px}header a{color:white}
main{max-width:1000px;margin:24px auto;padding:0 16px}
section{background:var(--card);padding:20px;margin-bottom:20px;border-radius:8px;box-shadow:0 2px 5px #0003}
h1{font-size:20px}form{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin:16px 0}
input,select,button{padding:10px;border:1px solid var(--line);border-radius:4px;font:inherit;background:var(--card);color:var(--text)}
button{background:var(--primary);color:white;cursor:pointer}pre{white-space:pre-wrap;overflow-wrap:anywhere}
.theme-toggle{float:right;border-color:rgba(255,255,255,.65);background:transparent;color:white;cursor:pointer}
</style></head><body>
<header><button class="theme-toggle" id="themeToggle" type="button" aria-pressed="false">Dark theme</button><a href=".">← Dashboard</a> · <a href="logs">Bus-Logs</a></header>
<main><h1>Vitotrol-Steuerung (experimentell)</h1>
<section><p id="state" role="status">Status wird geladen…</p>
<p>1200 Baud, 8E1. Die angezeigten Steuerwerte sind lokale Vorgaben, keine Bestätigung der Regelung.
Gesendet wird nur nach Freigabe durch den Master. Vor Verwendung am Zielgerät prüfen.</p>
<p id="feedback" role="status"></p>
<form id="roomForm"><label>Raum-Ist °C <input id="room" type="number" min="-20" max="50" step="0.1" required></label>
<button type="submit">Übermitteln</button></form>
<form id="desiredForm"><label>Raum-Soll normal °C <input id="desired" type="number" min="5" max="35" step="1" required></label>
<button type="submit">Übermitteln</button></form>
<form id="reducedForm"><label>Raum-Soll reduziert °C <input id="reduced" type="number" min="5" max="35" step="1" required></label>
<button type="submit">Übermitteln</button></form>
<form id="modeForm"><label>Befehl <select id="mode">
<option value="heat_water">Heizen + Warmwasser</option><option value="water">Nur Warmwasser</option>
<option value="off">Abschaltbetrieb</option><option value="party_on">Partybetrieb an</option>
<option value="party_off">Partybetrieb aus</option><option value="economy_on">Sparbetrieb an</option>
<option value="economy_off">Sparbetrieb aus</option></select></label>
<label>Party-Soll °C (bei Party an) <input id="party" type="number" min="5" max="35" step="1" required disabled></label>
<button type="submit">Übermitteln</button></form>
<p>Partytemperatur ist nur im angewendeten WiFi-Profil verfügbar.</p>
<p id="requested"></p></section>
<section><h2>Protokollvariante</h2>
<p>Die Quellen widersprechen sich bei Schreibquittierungen und Datensatzzuordnung.
WiFi nutzt PONG bei gezielten Schreibtelegrammen und die Datensätze 0x20/0x15.
OpenV nutzt keine Schreibquittierungen und eine slotabhängige Zuordnung.
Broadcasts werden ohne Antwort verarbeitet. Das Profil gilt bis Neustart oder Neuverbindung.</p>
<form id="profileForm"><label>Profil <select id="profile"><option value="wifi">WiFiVitotrol</option>
<option value="openv">OpenV</option></select></label><button type="submit">Profil übernehmen</button></form>
<p id="diagnostics"></p></section>
<section><h2>Empfangene Daten</h2><p id="sensors">Noch keine Messdaten.</p>
<p>Nur belegte Felder werden interpretiert. Rohdatensätze sind bereits XOR-dekodiert.
Alter beachten: alte Daten sind keine aktuellen Messwerte.</p><pre id="datasets">Noch keine Datensätze.</pre></section>
</main><script>
const themeButton=document.getElementById('themeToggle');
function setTheme(theme){document.documentElement.dataset.theme=theme;themeButton.textContent=theme==='dark'?'Light theme':'Dark theme';themeButton.setAttribute('aria-pressed',theme==='dark'?'true':'false');}
try{setTheme(localStorage.getItem('viessmann-decoder-theme')==='dark'?'dark':'light');}catch(error){setTheme('light');}
themeButton.addEventListener('click',()=>{const theme=document.documentElement.dataset.theme==='dark'?'light':'dark';setTheme(theme);try{localStorage.setItem('viessmann-decoder-theme',theme);}catch(error){}});
const el=id=>document.getElementById(id);
const dirty=new Set();
const revisions=new Map();
let appliedProfile=null;
['room','desired','reduced','party','mode','profile'].forEach(id=>el(id).addEventListener('input',()=>{
    dirty.add(id);revisions.set(id,(revisions.get(id)||0)+1);
}));
function sync(id,value){
    if(!dirty.has(id)&&document.activeElement!==el(id))el(id).value=value;
}
async function send(value,id){
    const feedback=el('feedback');
    const revision=revisions.get(id)||0;
    const partyRevision=revisions.get('party')||0;
    try{
        const response=await fetch('api/remote',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});
        const data=await response.json();
        if(!response.ok)throw new Error(data.error||'HTTP '+response.status);
        if((revisions.get(id)||0)===revision)dirty.delete(id);
        if(value.party_room_temperature!==undefined &&
           (revisions.get('party')||0)===partyRevision)dirty.delete('party');
        feedback.textContent='Vorgabe übernommen; Übertragung erfolgt bei Master-Freigabe.';
        await refresh();
    }catch(error){feedback.textContent='Nicht übernommen: '+error.message;}
}
[['roomForm','room','room_temperature'],['desiredForm','desired','desired_room_temperature'],
 ['reducedForm','reduced','reduced_room_temperature'],['modeForm','mode','mode'],['profileForm','profile','profile']]
.forEach(([form,id,key])=>el(form).onsubmit=event=>{
    event.preventDefault();
    const value=(id==='mode'||id==='profile')?el(id).value:Number(el(id).value);
    const command={[key]:value};
    if(id==='mode'&&value==='party_on'&&appliedProfile==='wifi')
        command.party_room_temperature=Number(el('party').value);
    send(command,id);
});
async function refresh(){
    try{
        const response=await fetch('api/remote',{cache:'no-store'});
        if(!response.ok)throw new Error('HTTP '+response.status);
        const data=await response.json();
        appliedProfile=data.profile;
        el('party').disabled=appliedProfile!=='wifi';
        el('state').textContent=data.model+' · Slot '+data.slot+' · '+(data.online?'Master erreichbar':'Keine aktuellen Master-Telegramme');
        sync('room',data.room_temperature);sync('desired',data.desired_room_temperature);
        sync('reduced',data.reduced_room_temperature);sync('party',data.party_room_temperature);
        sync('profile',data.profile);
        const baseMode={200:'off',201:'water',202:'heat_water'};
        if(baseMode[data.mode])sync('mode',baseMode[data.mode]);
        el('requested').textContent='Lokale Vorgaben: Betriebsart 0x'+data.mode.toString(16).toUpperCase()+
            ', Party '+(data.requested_party_mode?'an':'aus')+', Sparbetrieb '+(data.requested_economy_mode?'an':'aus');
        el('diagnostics').textContent='Wartende Befehle: '+data.pending_commands+' · CRC-Fehler: '+data.crc_errors+
            ' · Ungültige Telegramme: '+data.malformed_frames+' · Unbekannte Befehle: '+data.unknown_commands;
        el('sensors').textContent=data.measurements_verified
            ? 'Außentemperatur: '+(data.outside_temperature===null?'nicht verfügbar':data.outside_temperature+' °C')+
              ' · Heizfreigabe: '+(data.heating_enabled===null?'nicht verfügbar':(data.heating_enabled?'an':'aus'))
            : 'Außentemperatur, Heizfreigabe und Anlagenstörung: Zuordnung nicht verifiziert; keine gesicherten Messwerte. '+
              'Experimenteller Temperaturkandidat: '+(data.outside_temperature_candidate==null?'nicht verfügbar':data.outside_temperature_candidate+' °C (kein Messwert)');
        el('datasets').textContent=data.datasets.map(dataset=>'0x'+dataset.id.toString(16).toUpperCase()+
            ' · Alter '+Math.floor(dataset.age_ms/1000)+' s · '+dataset.data.map(byte=>byte.toString(16).toUpperCase().padStart(2,'0')).join(' '))
            .join('\n')||'Noch keine Datensätze.';
    }catch(error){el('state').textContent='Status nicht verfügbar: '+error.message;}
}
async function poll(){await refresh();setTimeout(poll,2000);}
poll();
</script></body></html>)HTML";
}

// HTTP request handler
static MHD_Result handle_request(void *cls,
                                 struct MHD_Connection *connection,
                                 const char *url,
                                 const char *method,
                                 const char *version,
                                 const char *upload_data,
                                 size_t *upload_data_size,
                                 void **con_cls) {
    if (strcmp(url, "/api/adapters") == 0)
        return handleAdaptersApi(connection, method, upload_data_size, upload_data, con_cls);
    AdapterContext* adapter = &primaryAdapter;
    std::string scopedUrl;
    if (strncmp(url, "/adapters/", 10) == 0) {
        const std::string path(url);
        const size_t slash = path.find('/', 10);
        const std::string id = path.substr(10, slash == std::string::npos ?
                                         std::string::npos : slash - 10);
        std::lock_guard<std::mutex> lock(adaptersMutex);
        adapter = id == "primary" ? &primaryAdapter : nullptr;
        for (const auto& candidate : extraAdapters)
            if (candidate->id == id) adapter = candidate.get();
        if (!adapter)
            return queueJson(connection, MHD_HTTP_NOT_FOUND, "{\"error\":\"Unknown adapter\"}");
        if (slash == std::string::npos) {
            MHD_Response* redirect = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
            if (!redirect) return MHD_NO;
            MHD_add_response_header(redirect, "Location", (id + "/").c_str());
            const MHD_Result result = MHD_queue_response(connection, MHD_HTTP_MOVED_PERMANENTLY, redirect);
            MHD_destroy_response(redirect);
            return result;
        }
        scopedUrl = path.substr(slash);
        url = scopedUrl.c_str();
    }
    AdapterSelection selection(*adapter);
    const auto& config = adapter->options;
    struct MHD_Response *response;
    MHD_Result ret;

    if (strcmp(url, "/api/system") == 0) {
        if (strcmp(method, "GET") != 0)
            return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "{\"error\":\"Use GET\"}");
        std::lock_guard<std::mutex> lock(restartMutex);
        char system[320];
        snprintf(system, sizeof(system),
                 "{\"restart_supported\":%s,\"restart_pending\":%s,"
                 "\"restart_mechanism\":\"process_exit\","
                 "\"restart_manager_required\":true,\"restart_exit_code\":75,"
                 "\"instance_id\":\"%s\"}",
                 containerRestartSupported() ? "true" : "false",
                 restartPending ? "true" : "false", processInstanceId.c_str());
        return queueJson(connection, MHD_HTTP_OK, system);
    }
    if (strcmp(url, "/api/restart") == 0)
        return handleRestartApi(connection, method, upload_data_size, upload_data, con_cls);

    if (config.protocol == PROTOCOL_KM_REMOTE && strcmp(url, "/api/remote") == 0) {
        return handleRemoteApi(connection, method, upload_data_size, upload_data, con_cls);
    }
    if (strcmp(url, "/api/settings") == 0) {
        return handleSettingsApi(connection, method, upload_data_size, upload_data, con_cls);
    }
    if (strcmp(url, "/logs") == 0 || strcmp(url, "/api/bus-logs") == 0) {
        if (strcmp(method, "GET") != 0) {
            return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                             "{\"error\":\"Use GET\"}");
        }
        const bool isPage = strcmp(url, "/logs") == 0;
        const std::string body = isPage ? getBusLogsHTML() : generateBusLogs();
        response = MHD_create_response_from_buffer(body.size(), (void*)body.data(),
                                                   MHD_RESPMEM_MUST_COPY);
        if (!response) return MHD_NO;
        MHD_add_response_header(response, "Content-Type",
                                isPage ? "text/html; charset=utf-8" : "text/plain; charset=utf-8");
        MHD_add_response_header(response, "Cache-Control", "no-store");
        MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    if (config.protocol == PROTOCOL_KM_REMOTE && strcmp(url, "/remote") == 0) {
        const char* html = getRemoteHTML();
        response = MHD_create_response_from_buffer(strlen(html), (void*)html, MHD_RESPMEM_PERSISTENT);
        if (!response) return MHD_NO;
        MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
        MHD_add_response_header(response, "Cache-Control", "no-store");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Handle routes
    if (strcmp(url, "/") == 0) {
        std::string html = getDashboardHTML();
        html.insert(html.find("</body>"), getRestartHTML());
        response = MHD_create_response_from_buffer(html.size(),
                                                   (void*)html.data(),
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        MHD_add_response_header(response, "Cache-Control", "no-store");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    else if (strcmp(url, "/data") == 0) {
        char* json = generateDataJSON();
        response = MHD_create_response_from_buffer(strlen(json),
                                                   (void*)json,
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    else if (strcmp(url, "/status") == 0) {
        const char* html = getStatusHTML();
        response = MHD_create_response_from_buffer(strlen(html),
                                                   (void*)html,
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        MHD_add_response_header(response, "Cache-Control", "no-store");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    else if (strcmp(url, "/settings") == 0) {
        std::string html = getSettingsHTML();
        html.insert(html.find("</body>"), getRestartHTML());
        response = MHD_create_response_from_buffer(html.size(),
                                                   (void*)html.data(),
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        MHD_add_response_header(response, "Cache-Control", "no-store");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    else if (strcmp(url, "/devices") == 0) {
        std::string html = getSettingsHTML();
        html.insert(html.find("</body>"), getRestartHTML());
        response = MHD_create_response_from_buffer(html.size(),
                                                   (void*)html.data(),
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        MHD_add_response_header(response, "Cache-Control", "no-store");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    else if (strcmp(url, "/health") == 0) {
        // Simple health check endpoint for watchdog and healthcheck
        // Returns a minimal response to indicate the server is running
        const char* health_response = "{\"status\":\"ok\"}";
        response = MHD_create_response_from_buffer(strlen(health_response),
                                                   (void*)health_response,
                                                   MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // 404 Not Found
    const char* not_found = "<html><body><h1>404 Not Found</h1></body></html>";
    response = MHD_create_response_from_buffer(strlen(not_found),
                                               (void*)not_found,
                                               MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

void printHelp(const char* progname) {
    printf("Viessmann Multi-Protocol Library - Web Server\n");
    printf("\nUsage: %s [options]\n", progname);
    printf("  -p <port>      Serial port (default: /dev/ttyUSB0)\n");
    printf("  -b <baud>      Baud rate (KM-Bus remote mode uses fixed 1200)\n");
    printf("  -t <protocol>  Protocol type: vbus, kw, p300, km, km_remote (default: km_remote)\n");
    printf("  -c <config>    Serial config: 8N1, 8E1, 8E2 (KM-Bus remote: 8E1)\n");
    printf("  -m <model>     Emulated remote: vitotrol200 or vitotrol300\n");
    printf("  -s <slot>      KM-Bus heating circuit slot: 1, 2, or 3\n");
    printf("  -i <invert>    Invert serial signals: true, false (default: false)\n");
    printf("  -w <port>      Web server port (default: 8099)\n");
    printf("  -h             Show this help\n");
}

static void requestCompleted(void*, MHD_Connection*, void** context,
                             enum MHD_RequestTerminationCode) {
    delete static_cast<RemotePostData*>(*context);
    *context = nullptr;
}

void adapterWorker(AdapterContext* adapter) {
    AdapterSelection selection(*adapter);
    const auto& config = adapter->options;
    adapter->serial.setTrafficCallback(recordBusTraffic);
    int reconnectCounter = RECONNECT_INTERVAL_TICKS;
    int pollCounter = 0;
    while (running) {
        bool reconnect;
        bool disconnected = false;
        pthread_mutex_lock(&adapter->mutex);
        if (adapter->connected) {
            struct stat device{};
            if (stat(adapter->activePort.c_str(), &device) != 0 ||
                device.st_rdev != adapter->connectedDevice ||
                device.st_ino != adapter->connectedInode) {
                adapter->connected = false;
                adapter->compatible = false;
                adapter->activePort.clear();
                delete adapter->decoder;
                adapter->decoder = nullptr;
                delete adapter->remote;
                adapter->remote = nullptr;
                adapter->serial.end();
                disconnected = true;
            }
        }
        if (adapter->connected && adapter->remote) adapter->remote->loop();
        else if (adapter->connected && adapter->decoder) {
            adapter->decoder->loop();
            if (config.protocol == PROTOCOL_KM && ++pollCounter >= KMBUS_POLL_INTERVAL_TICKS) {
                pollCounter = 0;
                adapter->decoder->pollKMBusStatusRecord(KMBUS_ADDR_MASTER_STATUS);
            }
        }
        reconnect = !adapter->connected;
        pthread_mutex_unlock(&adapter->mutex);
        if (disconnected) releaseAdapterClaim(*adapter);
        if (reconnect && ++reconnectCounter >= RECONNECT_INTERVAL_TICKS) {
            reconnectCounter = 0;
            for (const auto& port : discoverSerialPorts())
                if (running && attemptConnection(port)) break;
        }
        if (config.protocol == PROTOCOL_KM_REMOTE && adapter->connected)
            adapter->serial.waitForData(10);
        else usleep(LOOP_DELAY_US);
    }
    pthread_mutex_lock(&adapter->mutex);
    adapter->connected = false;
    adapter->compatible = false;
    delete adapter->decoder;
    adapter->decoder = nullptr;
    delete adapter->remote;
    adapter->remote = nullptr;
    adapter->serial.end();
    pthread_mutex_unlock(&adapter->mutex);
    releaseAdapterClaim(*adapter);
}

int main(int argc, char* argv[]) {
    auto& config = primaryAdapter.options;
    // Default configuration
    config.serialPort = "/dev/ttyUSB0";
    config.baudRate = 1200;
    config.protocol = PROTOCOL_KM_REMOTE;
    config.serialConfig = SERIAL_8E1;
    config.invertSerial = false;
    config.webPort = 8099;
    config.remoteModelId = 0x38;
    config.remoteSlot = 1;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:b:t:c:i:m:s:w:h")) != -1) {
        switch (opt) {
            case 'p':
                config.serialPort = optarg;
                break;
            case 'b':
                config.baudRate = atol(optarg);
                break;
            case 't':
                config.protocol = parseProtocol(optarg);
                break;
            case 'c':
                config.serialConfig = parseSerialConfig(optarg);
                break;
            case 'i':
                config.invertSerial = (strcasecmp(optarg, "true") == 0 || strcmp(optarg, "1") == 0);
                break;
            case 'm':
                if (strcasecmp(optarg, "vitotrol200") == 0 || strcmp(optarg, "200") == 0) {
                    config.remoteModelId = 0x34;
                } else if (strcasecmp(optarg, "vitotrol300") == 0 || strcmp(optarg, "300") == 0) {
                    config.remoteModelId = 0x38;
                } else {
                    fprintf(stderr, "Invalid remote model: %s\n", optarg);
                    return 1;
                }
                break;
            case 's':
                config.remoteSlot = static_cast<uint8_t>(atoi(optarg));
                if (config.remoteSlot < 1 || config.remoteSlot > 3) {
                    fprintf(stderr, "Remote slot must be 1, 2, or 3\n");
                    return 1;
                }
                break;
            case 'w':
                config.webPort = atoi(optarg);
                break;
            case 'h':
                printHelp(argv[0]);
                return 0;
            default:
                printHelp(argv[0]);
                return 1;
        }
    }

    loadPrimarySettings(config);
    if (config.protocol == PROTOCOL_KM_REMOTE) {
        config.baudRate = 1200;
        config.serialConfig = SERIAL_8E1;
    }

    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    printf("Viessmann Decoder Web Server\n");
    printf("=============================\n");
    printf("Serial Port: %s\n", config.serialPort);
    printf("Baud Rate: %lu\n", config.baudRate);
    printf("Protocol: %s\n", getProtocolName(config.protocol));
    printf("Serial Config: %s\n",
           config.serialConfig == SERIAL_8N1 ? "8N1" :
           config.serialConfig == SERIAL_8E1 ? "8E1" : "8E2");
    printf("Web Port: %d\n", config.webPort);
    printf("\n");

    primaryAdapter.port = config.serialPort;
    config.serialPort = primaryAdapter.port.c_str();
    primaryAdapter.savedPort = primaryAdapter.port;
    primaryAdapter.savedOptions = config;
    loadAdapters();

    // Start HTTP server (always start, even without serial connection)
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY,
                             config.webPort,
                             NULL, NULL,
                             &handle_request, NULL,
                             MHD_OPTION_NOTIFY_COMPLETED, &requestCompleted, NULL,
                             MHD_OPTION_END);

    if (daemon == NULL) {
        fprintf(stderr, "Error: Failed to start HTTP server on port %d\n", config.webPort);
        return 1;
    }
    try {
        std::lock_guard<std::mutex> lock(adaptersMutex);
        primaryAdapter.worker = std::thread(adapterWorker, &primaryAdapter);
        for (auto& adapter : extraAdapters)
            if (!adapter->worker.joinable())
                adapter->worker = std::thread(adapterWorker, adapter.get());
    } catch (...) {
        running = false;
        MHD_stop_daemon(daemon);
        if (primaryAdapter.worker.joinable()) primaryAdapter.worker.join();
        for (auto& adapter : extraAdapters)
            if (adapter->worker.joinable()) adapter->worker.join();
        fprintf(stderr, "Error: Could not start serial workers\n");
        return 1;
    }

    printf("Web server started on port %d\n", config.webPort);
    printf("Access the dashboard at: http://localhost:%d\n", config.webPort);
    printf("\nPress Ctrl+C to stop\n\n");

    // Main loop with serial port reconnection logic
    bool intentionalRestart = false;

    while (running) {
        {
            std::lock_guard<std::mutex> lock(restartMutex);
            if (restartPending && std::chrono::steady_clock::now() >= restartDeadline) {
                intentionalRestart = true;
                break;
            }
        }
        usleep(LOOP_DELAY_US);
    }

    // Cleanup
    printf("Stopping web server...\n");
    running = false;
    MHD_stop_daemon(daemon);
    primaryAdapter.worker.join();
    for (auto& adapter : extraAdapters)
        if (adapter->worker.joinable()) adapter->worker.join();

    printf("Shutdown complete\n");
    // Exit the entrypoint, not just re-exec the server: the runtime then runs
    // /run.sh again. HA needs its watchdog; Docker needs a restart policy.
    return intentionalRestart ? RESTART_EXIT_CODE : 0;
}
