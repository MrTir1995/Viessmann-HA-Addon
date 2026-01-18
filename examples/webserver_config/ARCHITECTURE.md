# Web Server Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────┐
│                    WiFi Network                          │
│                                                          │
│  ┌──────────┐        ┌──────────────┐                  │
│  │ Browser  │◄──────►│ ESP32/ESP8266 │◄───Serial───► Viessmann
│  │ (Client) │  HTTP  │  Web Server   │               Heating
│  └──────────┘        └──────────────┘               System
│                                                          │
└─────────────────────────────────────────────────────────┘
```

## Web Interface Structure

```
┌───────────────────────────────────────────────────────────┐
│                   Web Interface                           │
├───────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐   │
│  │  Dashboard  │  │ Configuration │  │   Status    │   │
│  │             │  │               │  │             │   │
│  │ - Live Data │  │ - Protocol    │  │ - Network   │   │
│  │ - Temps     │  │ - Baud Rate   │  │ - System    │   │
│  │ - Pumps     │  │ - Serial Cfg  │  │ - Memory    │   │
│  │ - Relays    │  │ - GPIO Pins   │  │ - Config    │   │
│  │             │  │               │  │             │   │
│  │ Auto-refresh│  │ Save & Restart│  │ View-only   │   │
│  └─────────────┘  └──────────────┘  └─────────────┘   │
│                                                           │
└───────────────────────────────────────────────────────────┘
```

## Data Flow

```
┌──────────────┐
│   Browser    │
└──────┬───────┘
       │ HTTP GET /
       ▼
┌──────────────────────┐
│   ESP Web Server     │
│                      │
│  getHTML()          │◄─── Generate HTML
│  - Dashboard        │
│  - Config Form      │
│  - Status Table     │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│   Client Browser     │
│  Display HTML/CSS    │
└──────┬───────────────┘
       │ JavaScript Timer
       │ HTTP GET /data
       ▼
┌──────────────────────┐
│   ESP Web Server     │
│                      │
│  handleData()       │
│  - Read VBUSDecoder │
│  - Format as JSON   │
└──────┬───────────────┘
       │ JSON Response
       ▼
┌──────────────────────┐
│   Client Browser     │
│  Update DOM elements │
│  (2 sec interval)    │
└──────────────────────┘
```

## Configuration Flow

```
User fills form
      │
      ▼
HTTP POST /saveconfig
  - protocol
  - baudRate
  - serialConfig
  - rxPin
  - txPin
      │
      ▼
┌─────────────────────┐
│  handleSaveConfig() │
│                     │
│ 1. Parse POST data │
│ 2. Update config   │
│ 3. saveConfig()    │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│  Flash Storage      │
│  (Preferences/      │
│   EEPROM)           │
└──────┬──────────────┘
       │
       ▼
   User restarts
       │
       ▼
┌─────────────────────┐
│  loadConfig()       │
│  applyConfig()      │
│                     │
│ 1. Read from flash │
│ 2. Configure Serial │
│ 3. Init VBUSDecoder │
└─────────────────────┘
```

## API Endpoints

```
GET  /          → Dashboard HTML
GET  /config    → Configuration HTML
POST /saveconfig → Save settings
GET  /status    → Status HTML
GET  /data      → JSON data
GET  /restart   → Restart device
```

## Storage Architecture

### ESP32 (Preferences)

```
┌──────────────────────────┐
│   Flash Memory           │
│  ┌────────────────────┐  │
│  │ Preferences        │  │
│  │ namespace:         │  │
│  │ "viessmann"        │  │
│  ├────────────────────┤  │
│  │ magic: 0xDEADBEEF │  │
│  │ protocol: 0-3     │  │
│  │ baudRate: uint32  │  │
│  │ serialCfg: 0-1    │  │
│  │ rxPin: 0-39       │  │
│  │ txPin: 0-39       │  │
│  └────────────────────┘  │
└──────────────────────────┘
```

### ESP8266 (EEPROM)

```
┌──────────────────────────┐
│   EEPROM (512 bytes)     │
│  ┌────────────────────┐  │
│  │ Address 0          │  │
│  │ Config struct      │  │
│  │ (20 bytes)         │  │
│  ├────────────────────┤  │
│  │ uint32_t magic    │  │
│  │ uint8_t protocol  │  │
│  │ uint32_t baudRate │  │
│  │ uint8_t serialCfg │  │
│  │ uint8_t rxPin     │  │
│  │ uint8_t txPin     │  │
│  │ uint32_t magic    │  │
│  └────────────────────┘  │
└──────────────────────────┘
```

## Security Model

```
┌──────────────────────────────────────────┐
│  ⚠️  NO AUTHENTICATION                   │
├──────────────────────────────────────────┤
│                                          │
│  Any device on network can:              │
│  - View all data                         │
│  - Change configuration                  │
│  - Restart device                        │
│                                          │
│  Recommended: Use on trusted LAN only    │
│                                          │
└──────────────────────────────────────────┘
```

## Memory Usage

### ESP32
```
Flash: ~500KB for code
RAM:   ~50KB runtime
       - Web server: ~15KB
       - HTML/CSS: ~20KB
       - Buffers: ~15KB
Free:  ~250KB available
```

### ESP8266
```
Flash: ~400KB for code
RAM:   ~25KB runtime
       - Web server: ~10KB
       - HTML/CSS: ~10KB
       - Buffers: ~5KB
Free:  ~50KB available (tight!)
```

## Performance Metrics

```
Operation               Time
─────────────────────  ─────────
WiFi Connection        2-5 sec
HTML Page Load         500 ms
JSON Data Fetch        50 ms
Config Save            100 ms
Device Restart         2-3 sec
```

## Protocol Configuration Examples

### VBUS (Vitosolic 200)
```
Protocol:  VBUS (0)
Baud:      9600
Serial:    8N1 (0)
RX Pin:    16
TX Pin:    17
```

### KW-Bus (Vitotronic 200)
```
Protocol:  KW-Bus (1)
Baud:      4800
Serial:    8E2 (1)
RX Pin:    16
TX Pin:    17
```

### P300 (Vitodens)
```
Protocol:  P300 (2)
Baud:      4800
Serial:    8E2 (1)
RX Pin:    16
TX Pin:    17
```

### KM-Bus
```
Protocol:  KM-Bus (3)
Baud:      9600
Serial:    8N1 (0)
RX Pin:    16
TX Pin:    17
```
