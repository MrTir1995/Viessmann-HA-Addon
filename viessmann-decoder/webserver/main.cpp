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
#include <sys/stat.h>
#include <glob.h>
#include <vector>
#include <string>
#include <unordered_set>
#include "LinuxSerial.h"
#include "vbusdecoder.h"

constexpr int COMPATIBILITY_ATTEMPTS = 200; // ~2 seconds with 10ms delay
constexpr useconds_t COMPATIBILITY_DELAY_US = 10000;
constexpr int RECONNECT_INTERVAL_TICKS = 500; // 5 seconds with 10ms loop delay
constexpr useconds_t LOOP_DELAY_US = 10000;

// Configuration structure
struct Config {
    uint8_t protocol;      // 0=VBUS, 1=KW, 2=P300, 3=KM
    unsigned long baudRate;
    uint8_t serialConfig;  // SERIAL_8N1 or SERIAL_8E2
    const char* serialPort;
    uint16_t webPort;
};

// Global variables
volatile bool running = true;
volatile bool serialConnected = false;
volatile bool deviceCompatible = false;
LinuxSerial vbusSerial;
VBUSDecoder* vbus = nullptr;
Config config;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
std::string activeSerialPort;

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
        default: return "Unknown";
    }
}

ProtocolType parseProtocol(const char* str) {
    if (strcasecmp(str, "vbus") == 0) return PROTOCOL_VBUS;
    if (strcasecmp(str, "kw") == 0) return PROTOCOL_KW;
    if (strcasecmp(str, "p300") == 0) return PROTOCOL_P300;
    if (strcasecmp(str, "km") == 0) return PROTOCOL_KM;
    return PROTOCOL_VBUS;
}

uint8_t parseSerialConfig(const char* str) {
    if (strcasecmp(str, "8N1") == 0) return SERIAL_8N1;
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

    pthread_mutex_lock(&data_mutex);
    VBUSDecoder* oldDecoder = vbus;
    vbus = nullptr;
    pthread_mutex_unlock(&data_mutex);

    if (oldDecoder) {
        delete oldDecoder;
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
    if (serialConnected && deviceCompatible && decoder) {
        status = decoder->getVbusStat() ? "OK" : "Error";
    }

    JSON_APPEND("\"serialConnected\":%s,", serialConnected ? "true" : "false");
    JSON_APPEND("\"compatible\":%s,", deviceCompatible ? "true" : "false");
    JSON_APPEND("\"serialPort\":\"%s\",", activeSerialPort.empty() ? "" : activeSerialPort.c_str());
    JSON_APPEND("\"ready\":%s,", (decoder && serialConnected && deviceCompatible && decoder->isReady()) ? "true" : "false");
    JSON_APPEND("\"status\":\"%s\",", status);
    JSON_APPEND("\"protocol\":%d,", config.protocol);
    
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
    "fetch('/data').then(r=>r.json()).then(d=>{"
    "const statusDot=document.getElementById('statusDot');"
    "const statusText=document.getElementById('statusText');"
    "const protocolText=document.getElementById('protocol');"
    "const container=document.getElementById('sensorData');"
    "if(d.serialConnected===false){"
    "statusDot.className='status-indicator error';"
    "statusText.textContent='Serial port not connected';"
    "const protocols=['VBUS','KW-Bus','P300','KM-Bus'];"
    "protocolText.textContent=protocols[d.protocol]||'Unknown';"
    "container.innerHTML='<div class=\"empty-state\"><div class=\"empty-state-icon\">🔌</div><div style=\"font-size:18px;margin-bottom:8px;\">Serial port not connected</div><div style=\"color:var(--secondary-text);\">Please connect your Viessmann device and check the serial port configuration.</div></div>';"
    "return;"
    "}"
    "statusDot.className='status-indicator '+(d.status==='OK'?'ok':'error');"
    "statusText.textContent=d.status;"
    "const protocols=['VBUS','KW-Bus','P300','KM-Bus'];"
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
    "<a href='/settings' class='nav-button'>"
    "<svg class='button-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M12,15.5A3.5,3.5 0 0,1 8.5,12A3.5,3.5 0 0,1 12,8.5A3.5,3.5 0 0,1 15.5,12A3.5,3.5 0 0,1 12,15.5M19.43,12.97C19.47,12.65 19.5,12.33 19.5,12C19.5,11.67 19.47,11.34 19.43,11L21.54,9.37C21.73,9.22 21.78,8.95 21.66,8.73L19.66,5.27C19.54,5.05 19.27,4.96 19.05,5.05L16.56,6.05C16.04,5.66 15.5,5.32 14.87,5.07L14.5,2.42C14.46,2.18 14.25,2 14,2H10C9.75,2 9.54,2.18 9.5,2.42L9.13,5.07C8.5,5.32 7.96,5.66 7.44,6.05L4.95,5.05C4.73,4.96 4.46,5.05 4.34,5.27L2.34,8.73C2.21,8.95 2.27,9.22 2.46,9.37L4.57,11C4.53,11.34 4.5,11.67 4.5,12C4.5,12.33 4.53,12.65 4.57,12.97L2.46,14.63C2.27,14.78 2.21,15.05 2.34,15.27L4.34,18.73C4.46,18.95 4.73,19.03 4.95,18.95L7.44,17.94C7.96,18.34 8.5,18.68 9.13,18.93L9.5,21.58C9.54,21.82 9.75,22 10,22H14C14.25,22 14.46,21.82 14.5,21.58L14.87,18.93C15.5,18.67 16.04,18.34 16.56,17.94L19.05,18.95C19.27,19.03 19.54,18.95 19.66,18.73L21.66,15.27C21.78,15.05 21.73,14.78 21.54,14.63L19.43,12.97Z'/></svg>"
    "<span>Settings</span>"
    "</a>"
    "<a href='/devices' class='nav-button'>"
    "<svg class='button-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M17,13H13V17H11V13H7V11H11V7H13V11H17M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2Z'/></svg>"
    "<span>Add Device</span>"
    "</a>"
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
    
    // Check if vbus is valid
    if (!vbus) {
        snprintf(html, sizeof(html), 
                "<!DOCTYPE html><html><body><h1>Error: System not initialized</h1></body></html>");
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
    config.serialConfig == SERIAL_8N1 ? "8N1" : "8E2",
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
    
    return html;
}

// Generate Settings Page HTML
const char* getSettingsHTML() {
    static thread_local char html[16384];
    
    if (!vbus) {
        snprintf(html, sizeof(html), 
                "<!DOCTYPE html><html><body><h1>Error: System not initialized</h1></body></html>");
        return html;
    }
    
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
    "<a href='/' class='back-button'>"
    "<svg class='back-icon' viewBox='0 0 24 24' fill='currentColor'><path d='M20,11V13H8L13.5,18.5L12.08,19.92L4.16,12L12.08,4.08L13.5,5.5L8,11H20Z'/></svg>"
    "</a>"
    "<div class='header-title'>Settings</div>"
    "</div>"
    "</div>"
    "<div class='view-container'>"
    "<div class='card'>"
    "<div class='card-header'><div class='card-title'>Connection Settings</div></div>"
    "<form onsubmit='return false;'>"
    "<div class='form-group'>"
    "<label class='form-label'>Serial Port</label>"
    "<input type='text' class='form-control' value='%s' readonly>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Baud Rate</label>"
    "<select class='form-select'>"
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
    "<select class='form-select'>"
    "<option value='vbus'%s>VBUS (RESOL)</option>"
    "<option value='kw'%s>KW-Bus (VS1)</option>"
    "<option value='p300'%s>P300 (VS2/Optolink)</option>"
    "<option value='km'%s>KM-Bus</option>"
    "</select>"
    "</div>"
    "<div class='form-group'>"
    "<label class='form-label'>Serial Configuration</label>"
    "<select class='form-select'>"
    "<option value='8N1'%s>8N1</option>"
    "<option value='8E2'%s>8E2</option>"
    "</select>"
    "</div>"
    "<div class='button-group'>"
    "<button class='btn btn-secondary' onclick='window.location.href=\"/\"'>Cancel</button>"
    "<button class='btn btn-primary' onclick='alert(\"Settings are read-only in this version. Configure through Home Assistant addon settings.\")'>Save</button>"
    "</div>"
    "</form>"
    "</div>"
    "</div>"
    "</body></html>",
    config.serialPort,
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
    config.serialConfig == SERIAL_8N1 ? " selected" : "",
    config.serialConfig == SERIAL_8E2 ? " selected" : "");
    
    html[sizeof(html) - 1] = '\0';
    if (written < 0 || written >= (int)(sizeof(html) - 1)) {
        fprintf(stderr, "Warning: HTML buffer overflow detected\n");
    }
    
    return html;
}

// Generate Device Configuration Page HTML
const char* getDevicesHTML() {
    static thread_local char html[16384];
    
    if (!vbus) {
        snprintf(html, sizeof(html), 
                "<!DOCTYPE html><html><body><h1>Error: System not initialized</h1></body></html>");
        return html;
    }
    
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
    "<a href='/' class='back-button'>"
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
    vbus->isReady() ? 1 : 0);
    
    html[sizeof(html) - 1] = '\0';
    if (written < 0 || written >= (int)(sizeof(html) - 1)) {
        fprintf(stderr, "Warning: HTML buffer overflow detected\n");
    }
    
    return html;
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
    
    // Handle routes
    if (strcmp(url, "/") == 0) {
        const char* html = getDashboardHTML();
        response = MHD_create_response_from_buffer(strlen(html),
                                                   (void*)html,
                                                   MHD_RESPMEM_PERSISTENT);
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
        const char* html = getSettingsHTML();
        response = MHD_create_response_from_buffer(strlen(html),
                                                   (void*)html,
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
    printf("  -b <baud>      Baud rate (default: 9600)\n");
    printf("  -t <protocol>  Protocol type: vbus, kw, p300, km (default: vbus)\n");
    printf("  -c <config>    Serial config: 8N1, 8E2 (default: 8N1)\n");
    printf("  -w <port>      Web server port (default: 8099)\n");
    printf("  -h             Show this help\n");
}

int main(int argc, char* argv[]) {
    // Default configuration
    config.serialPort = "/dev/ttyUSB0";
    config.baudRate = 9600;
    config.protocol = PROTOCOL_VBUS;
    config.serialConfig = SERIAL_8N1;
    config.webPort = 8099;
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:b:t:c:w:h")) != -1) {
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
    
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    printf("Viessmann Decoder Web Server\n");
    printf("=============================\n");
    printf("Serial Port: %s\n", config.serialPort);
    printf("Baud Rate: %lu\n", config.baudRate);
    printf("Protocol: %s\n", getProtocolName(config.protocol));
    printf("Serial Config: %s\n", config.serialConfig == SERIAL_8N1 ? "8N1" : "8E2");
    printf("Web Port: %d\n", config.webPort);
    printf("\n");
    
    // Try to initialize serial port (don't exit on failure)
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
    
    while (running) {
        if (serialConnected && vbus && deviceCompatible) {
            vbus->loop();
        } else {
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
        usleep(LOOP_DELAY_US);  // 10ms delay
    }
    
    // Cleanup
    printf("Stopping web server...\n");
    MHD_stop_daemon(daemon);
    if (vbus) delete vbus;
    
    printf("Shutdown complete\n");
    return 0;
}
