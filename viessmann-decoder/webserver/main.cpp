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

// Global variables
volatile bool running = true;
volatile bool serialConnected = false;
volatile bool deviceCompatible = false;
LinuxSerial vbusSerial;
VBUSDecoder* vbus = nullptr;
KMBusVitotrol* vitotrol = nullptr;
Config config;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
std::string activeSerialPort;

struct BusLogEntry {
    std::chrono::system_clock::time_point timestamp;
    bool transmitted;
    std::vector<uint8_t> bytes;
};

constexpr size_t MAX_BUS_LOG_ENTRIES = 500;
constexpr size_t MAX_BUS_LOG_BYTES = 32;
std::deque<BusLogEntry> busLogs;
std::mutex busLogMutex;
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
void signalHandler(int signum) {
    printf("\nShutting down...\n");
    running = false;
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
    std::vector<std::string> ports;
    std::unordered_set<std::string> seen;
    const bool hasConfiguredPort = config.serialPort && strlen(config.serialPort) > 0;
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

bool attemptConnection(const std::string& port) {
    if (vbusSerial.isOpen()) {
        vbusSerial.end();
    }
    if (!vbusSerial.begin(port.c_str(), config.baudRate, config.serialConfig)) {
        fprintf(stderr, "Failed to open serial port %s\n", port.c_str());
        return false;
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
    return false;
}

// Caller holds data_mutex; keep /data and /api/remote on the same contract.
std::string generateRemoteJSON(bool includeDatasets) {
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
char* generateDataJSON() {
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
    JSON_APPEND("\"serialPort\":\"%s\",", activeSerialPort.empty() ? "" : activeSerialPort.c_str());
    const bool dataReady = config.protocol == PROTOCOL_KM_REMOTE
                               ? (serialConnected && vitotrol && vitotrol->isOnline())
                               : (decoder && serialConnected && deviceCompatible && decoder->isReady());
    JSON_APPEND("\"ready\":%s,", dataReady ? "true" : "false");
    JSON_APPEND("\"status\":\"%s\",", status);
    JSON_APPEND("\"protocol\":%d,", config.protocol);

    if (config.protocol == PROTOCOL_KM_REMOTE) {
        JSON_APPEND("\"temperatures\":[],\"pumps\":[],\"relays\":[],");
        const std::string remote = generateRemoteJSON(false);
        JSON_APPEND("\"remote\":%s", remote.c_str());
        pthread_mutex_unlock(&data_mutex);
        JSON_APPEND("}");
        return json;
    }

    if (!serialConnected || !decoder || !deviceCompatible) {
        JSON_APPEND("\"temperatures\":[],\"pumps\":[],\"relays\":[]");
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

bool saveSettings(const std::string& protocol, unsigned long baudRate,
                  const std::string& serialConfig, const std::string& remoteModel,
                  unsigned int remoteSlot, bool invertSerial) {
    char temporaryPath[] = "/data/ui_settings.json.XXXXXX";
    const int descriptor = mkstemp(temporaryPath);
    if (descriptor < 0) return false;

    FILE* file = fdopen(descriptor, "w");
    if (!file) {
        close(descriptor);
        unlink(temporaryPath);
        return false;
    }
    const bool written =
        fprintf(file,
                "{\"baud_rate\":%lu,\"protocol\":\"%s\",\"serial_config\":\"%s\","
                "\"remote_model\":\"%s\",\"remote_slot\":%u,\"invert_serial\":%s}\n",
                baudRate, protocol.c_str(), serialConfig.c_str(), remoteModel.c_str(),
                remoteSlot, invertSerial ? "true" : "false") >= 0;
    const bool flushed = written && fflush(file) == 0 && fsync(fileno(file)) == 0;
    const bool closed = fclose(file) == 0;
    if (!flushed || !closed || rename(temporaryPath, "/data/ui_settings.json") != 0) {
        unlink(temporaryPath);
        return false;
    }
    return true;
}

MHD_Result handleSettingsApi(MHD_Connection* connection, const char* method,
                             size_t* uploadDataSize, const char* uploadData,
                             void** connectionContext) {
    if (strcmp(method, "POST") != 0) {
        return queueJson(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                         "{\"error\":\"Use POST\"}");
    }

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

    std::string protocol;
    std::string serialConfig;
    std::string remoteModel;
    float baudRateValue = 0;
    float remoteSlotValue = 0;
    bool invertSerial = false;
    const int protocolField = parseJsonString(request->body, "protocol", protocol);
    const int baudRateField = parseJsonNumber(request->body, "baud_rate", baudRateValue);
    const int serialConfigField = parseJsonString(request->body, "serial_config", serialConfig);
    const int remoteModelField = parseJsonString(request->body, "remote_model", remoteModel);
    const int remoteSlotField = parseJsonNumber(request->body, "remote_slot", remoteSlotValue);
    const int invertSerialField = parseJsonBool(request->body, "invert_serial", invertSerial);

    const bool validProtocol =
        protocol == "vbus" || protocol == "kw" || protocol == "p300" ||
        protocol == "km" || protocol == "km_remote";
    const bool validBaudRate =
        baudRateValue == 1200 || baudRateValue == 2400 || baudRateValue == 4800 ||
        baudRateValue == 9600 || baudRateValue == 19200 || baudRateValue == 38400 ||
        baudRateValue == 115200;
    const bool validSerialConfig =
        serialConfig == "8N1" || serialConfig == "8E1" || serialConfig == "8E2";
    const bool validRemoteModel = remoteModel == "vitotrol200" || remoteModel == "vitotrol300";
    const bool validRemoteSlot = remoteSlotValue >= 1 && remoteSlotValue <= 3 &&
                                 floorf(remoteSlotValue) == remoteSlotValue;
    const bool valid =
        protocolField == 1 && baudRateField == 1 && serialConfigField == 1 &&
        remoteModelField == 1 && remoteSlotField == 1 && invertSerialField == 1 &&
        validProtocol && validBaudRate && validSerialConfig && validRemoteModel &&
        validRemoteSlot;

    bool saved = false;
    if (valid) {
        if (protocol == "km_remote") {
            baudRateValue = 1200;
            serialConfig = "8E1";
        }
        saved = saveSettings(protocol, static_cast<unsigned long>(baudRateValue),
                             serialConfig, remoteModel,
                             static_cast<unsigned int>(remoteSlotValue), invertSerial);
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
const char* getDashboardHTML() {
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
    ".status-indicator{width:8px;height:8px;border-radius:50%%;background:var(--disabled-text);}"
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
    ".nav-buttons{display:flex;gap:16px;margin-bottom:24px;flex-wrap:wrap;}"
    ".nav-button{background:var(--card-background);padding:16px 24px;border-radius:8px;box-shadow:var(--card-shadow);display:flex;align-items:center;gap:12px;text-decoration:none;color:var(--primary-text);transition:all 0.2s;font-weight:500;}"
    ".nav-button:hover{transform:translateY(-2px);box-shadow:0 4px 8px rgba(0,0,0,0.2);background:var(--primary-color);color:white;}"
    ".button-icon{width:24px;height:24px;}"
    "@media(max-width:768px){"
    ".view-container{padding:0 16px;margin:16px auto;}"
    ".header-toolbar{padding:12px 16px;}"
    ".sensor-grid{grid-template-columns:1fr;}"
    "}"
    "</style>"
    "<script>"
    "function updateData(){"
    "fetch('data').then(r=>r.json()).then(d=>{"
    "document.getElementById('remoteControls').style.display=d.protocol===4?'flex':'none';"
    "const statusDot=document.getElementById('statusDot');"
    "const statusText=document.getElementById('statusText');"
    "const protocolText=document.getElementById('protocol');"
    "const container=document.getElementById('sensorData');"
    "if(d.serialConnected===false){"
    "statusDot.className='status-indicator error';"
    "statusText.textContent='Serial port not connected';"
    "const protocols=['VBUS','KW-Bus','P300','KM-Bus','KM-Bus Slave'];"
    "protocolText.textContent=protocols[d.protocol]||'Unknown';"
    "container.innerHTML='<div class=\"empty-state\"><div class=\"empty-state-icon\">🔌</div><div style=\"font-size:18px;margin-bottom:8px;\">Serial port not connected</div><div style=\"color:var(--secondary-text);\">Please connect your Viessmann device and check the serial port configuration.</div></div>';"
    "return;"
    "}"
    "statusDot.className='status-indicator '+(d.status==='OK'?'ok':'error');"
    "statusText.textContent=d.status;"
    "const protocols=['VBUS','KW-Bus','P300','KM-Bus','KM-Bus Slave'];"
    "protocolText.textContent=protocols[d.protocol]||'Unknown';"
    "if(!d.ready||(!d.temperatures.length&&!d.pumps.length&&!d.relays.length)){"
    "container.innerHTML='<div class=\"empty-state\"><div class=\"empty-state-icon\">⏳</div><div>Waiting for data...</div></div>';"
    "return;"
    "}"
    "let html='';"
    "if(d.temperatures&&d.temperatures.length>0){"
    "d.temperatures.forEach((t,i)=>{"
    "html+='<div class=\"sensor-item\">';"
    "html+='<div class=\"sensor-label\">Temperature '+(i+1)+'</div>';"
    "html+='<div class=\"sensor-value\">'+t.toFixed(1)+'<span class=\"sensor-unit\">°C</span></div>';"
    "html+='</div>';"
    "});"
    "}"
    "if(d.pumps&&d.pumps.length>0){"
    "d.pumps.forEach((p,i)=>{"
    "html+='<div class=\"sensor-item\">';"
    "html+='<div class=\"sensor-label\">Pump '+(i+1)+' Power</div>';"
    "html+='<div class=\"sensor-value\">'+p+'<span class=\"sensor-unit\">%%</span></div>';"
    "html+='</div>';"
    "});"
    "}"
    "if(d.relays&&d.relays.length>0){"
    "d.relays.forEach((r,i)=>{"
    "html+='<div class=\"sensor-item\">';"
    "html+='<div class=\"sensor-label\">Relay '+(i+1)+'</div>';"
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
    "<span>Add Device</span>"
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
    "</div>"
    "</div>"
    "</div>"
    "</body></html>";

    return html;
}

const char* getStatusHTML() {
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
const char* getSettingsHTML() {
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
    ".form-control{width:100%%;padding:12px;border:1px solid var(--divider-color);border-radius:4px;font-size:14px;}"
    ".form-control:focus{outline:none;border-color:var(--primary-color);}"
    ".form-select{width:100%%;padding:12px;border:1px solid var(--divider-color);border-radius:4px;font-size:14px;background:white;}"
    ".button-group{padding:20px;display:flex;gap:12px;justify-content:flex-end;}"
    ".btn{padding:12px 24px;border:none;border-radius:4px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s;}"
    ".btn-primary{background:var(--primary-color);color:white;}"
    ".btn-primary:hover{background:#0288d1;}"
    ".btn-secondary{background:var(--divider-color);color:var(--primary-text);}"
    ".btn-secondary:hover{background:#ccc;}"
    "@media(max-width:768px){.view-container{padding:0 16px;margin:16px auto;}}"
    "</style>"
    "</head><body>"
    "<div class='app-header'>"
    "<div class='header-toolbar'>"
    "<a href='.' class='back-button'>"
    "<svg class='back-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M20,11V13H8L13.5,18.5L12.08,19.92L4.16,12L12.08,4.08L13.5,5.5L8,11H20Z'/></svg>"
    "</a>"
    "<div class='header-title'>Settings</div>"
    "</div>"
    "</div>"
    "<div class='view-container'>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>Connection Settings</div></div>"
    "<form id='settingsForm' onsubmit='saveSettings(event)'>"
    "<div class='form-group'>"
    "<label class='form-label'>Serial Port</label>"
    "<input type='text' class='form-control' value='%s' readonly>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Baud Rate</label>"
    "<select class='form-select' name='baud_rate'>"
    "<option value='1200'%s>1200</option>"
    "<option value='2400'%s>2400</option>"
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
    "<button class='btn btn-secondary' onclick='window.location.href=\"/\"'>Cancel</button>"
    "<button class='btn btn-primary' type='submit'>Save</button>"
    "</div>"
    "</form>"
    "</div>"
    "</div>"
    "<script>"
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
    "try{const response=await fetch('api/settings',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(values)});"
    "const result=await response.json();if(!response.ok)throw new Error(result.error);"
    "settingsMessage.textContent='Settings saved. Restart the container to apply them.';"
    "}catch(error){settingsMessage.textContent='Could not save settings: '+error.message;}}"
    "</script>"
    "</body></html>",
    config.serialPort,
    config.baudRate == 1200 ? " selected" : "",
    config.baudRate == 2400 ? " selected" : "",
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

    return html;
}

// Generate Device Configuration Page HTML
const char* getDevicesHTML() {
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
    "<button class='btn btn-secondary' onclick='window.location.href=\"/\"'>Cancel</button>"
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
*{box-sizing:border-box}
body{margin:0;font-family:Roboto,Noto,sans-serif;background:#fafafa;color:#212121}
header{background:#03a9f4;color:white;padding:16px 24px;box-shadow:0 2px 4px #0003}
header nav{max-width:1152px;margin:auto;display:flex;align-items:center;gap:24px}
header a{color:white;text-decoration:none}
h1{font-size:20px;font-weight:400;margin:0}
main{max-width:1200px;margin:24px auto;padding:0 24px}
.card{background:white;border-radius:8px;padding:20px;box-shadow:0 2px 5px #0003}
.controls{display:flex;gap:16px;align-items:center;flex-wrap:wrap}
button{background:#03a9f4;color:white;border:0;border-radius:4px;padding:10px 16px;cursor:pointer}
pre{background:#212121;color:#e0e0e0;padding:16px;border-radius:4px;overflow:auto;max-height:65vh;white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}
#status{color:#727272}
@media(max-width:768px){main{padding:0 16px}}
</style></head><body>
<header><nav><a href=".">← Dashboard</a><h1>Bus-Logs</h1></nav></header>
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
*{box-sizing:border-box}body{margin:0;background:#fafafa;color:#212121;font-family:Roboto,Noto,sans-serif}
header{background:#03a9f4;color:white;padding:16px 24px}header a{color:white}
main{max-width:1000px;margin:24px auto;padding:0 16px}
section{background:white;padding:20px;margin-bottom:20px;border-radius:8px;box-shadow:0 2px 5px #0003}
h1{font-size:20px}form{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin:16px 0}
input,select,button{padding:10px;border:1px solid #ccc;border-radius:4px;font:inherit}
button{background:#03a9f4;color:white;cursor:pointer}pre{white-space:pre-wrap;overflow-wrap:anywhere}
</style></head><body>
<header><a href=".">← Dashboard</a> · <a href="logs">Bus-Logs</a></header>
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
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    else if (strcmp(url, "/devices") == 0) {
        const char* html = getDevicesHTML();
        response = MHD_create_response_from_buffer(strlen(html),
                                                   (void*)html,
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
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

int main(int argc, char* argv[]) {
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

    // Try to initialize serial port (don't exit on failure)
    vbusSerial.setTrafficCallback(recordBusTraffic);
    bool connected = false;
    for (const auto& port : discoverSerialPorts()) {
        printf("Attempting to connect on %s...\n", port.c_str());
        if (attemptConnection(port)) {
            connected = true;
            break;
        }
    }
    if (!connected) {
        fprintf(stderr, "Warning: No compatible serial device found - starting in disconnected mode\n");
        fprintf(stderr, "The web interface will show 'Serial port not connected'\n");
    }

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
        if (vbus) delete vbus;
        return 1;
    }

    printf("Web server started on port %d\n", config.webPort);
    printf("Access the dashboard at: http://localhost:%d\n", config.webPort);
    if (!serialConnected) {
        printf("Note: Serial port not connected - will retry periodically\n");
    }
    printf("\nPress Ctrl+C to stop\n\n");

    // Main loop with serial port reconnection logic
    int reconnectCounter = 0;
    int kmbusPollCounter = 0;  // Counter for KM-Bus polling
    bool intentionalRestart = false;

    while (running) {
        {
            std::lock_guard<std::mutex> lock(restartMutex);
            if (restartPending && std::chrono::steady_clock::now() >= restartDeadline) {
                intentionalRestart = true;
                break;
            }
        }
        bool shouldReconnect;

        pthread_mutex_lock(&data_mutex);
        // Always call loop() if connected (KM-Bus needs it even when not compatible yet)
        if (config.protocol == PROTOCOL_KM_REMOTE && serialConnected && vitotrol) {
            vitotrol->loop();
        } else if (serialConnected && vbus) {
            vbus->loop();
            
            // KM-Bus active polling: Request status data periodically
            // IMPORTANT: Must run even when deviceCompatible=false to receive first frames!
            if (config.protocol == PROTOCOL_KM) {
                kmbusPollCounter++;
                if (kmbusPollCounter >= KMBUS_POLL_INTERVAL_TICKS) {
                    kmbusPollCounter = 0;
                    // Poll status record from master controller
                    printf("[DEBUG] KM-Bus: Sending status request to address 0x%02X\n", KMBUS_ADDR_MASTER_STATUS);
                    bool result = vbus->pollKMBusStatusRecord(KMBUS_ADDR_MASTER_STATUS);
                    printf("[DEBUG] KM-Bus: Poll result: %s\n", result ? "SUCCESS" : "FAILED");
                }
            }
        }
        shouldReconnect = config.protocol == PROTOCOL_KM_REMOTE
                              ? !serialConnected
                              : (!serialConnected || !vbus || !deviceCompatible);
        pthread_mutex_unlock(&data_mutex);

        if (shouldReconnect) {
            // Try to reconnect periodically
            reconnectCounter++;
            if (reconnectCounter >= RECONNECT_INTERVAL_TICKS) {
                reconnectCounter = 0;
                auto ports = discoverSerialPorts();
                if (ports.empty() && config.serialPort && strlen(config.serialPort) > 0) {
                    ports.push_back(config.serialPort);
                }
                for (const auto& port : ports) {
                    printf("Attempting to connect on %s...\n", port.c_str());
                    if (attemptConnection(port)) {
                        break;
                    }
                }
            }
        }
        if (config.protocol == PROTOCOL_KM_REMOTE && serialConnected) {
            // Wake on RX instead of imposing a fixed delay on every slave response.
            vbusSerial.waitForData(10);
        } else {
            usleep(LOOP_DELAY_US);
        }
    }

    // Cleanup
    printf("Stopping web server...\n");
    MHD_stop_daemon(daemon);
    if (vbus) delete vbus;
    if (vitotrol) delete vitotrol;
    vbusSerial.end();

    printf("Shutdown complete\n");
    // Exit the entrypoint, not just re-exec the server: the runtime then runs
    // /run.sh again. HA needs its watchdog; Docker needs a restart policy.
    return intentionalRestart ? RESTART_EXIT_CODE : 0;
}
