# Viessmann Decoder - Docker-Container und Home Assistant Add-on

[![Add repository to Home Assistant](https://img.shields.io/badge/Add%20repository%20to-Home%20Assistant-blue?logo=home-assistant&logoColor=white)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https://github.com/MrTir1995/Viessmann-HA-Addon)
[![GitHub Release](https://img.shields.io/github/v/release/MrTir1995/Viessmann-HA-Addon?logo=github)](https://github.com/MrTir1995/Viessmann-HA-Addon/releases)
![Version](https://img.shields.io/badge/version-2.3.0-blue.svg)

Überwachen und steuern Sie Ihre Viessmann-Heizungsanlage direkt aus Home Assistant mit professioneller Protokollunterstützung!

Dieses Add-on bietet eine umfassende Web-Oberfläche zur Kommunikation mit Viessmann-Heizungssteuerungen unter Verwendung mehrerer Industriestandard-Protokolle (VBUS, KW-Bus, P300/Optolink, KM-Bus).

## Standalone-Dockerbetrieb (ohne Supervisor)

Der Decoder läuft auch neben Home Assistant Container oder auf einem anderen
Linux-Host im Netzwerk. Home Assistant und dessen Supervisor werden zum Betrieb
des Decoders nicht benötigt. Alle Build-Dateien liegen in diesem Verzeichnis.

```bash
cd /pfad/zum/Viessmann-HA-Addon/viessmann-decoder
docker compose up -d --build
```

Die mitgelieferte `compose.yaml` baut das vorhandene Dockerfile mit Alpine als
Basis, reicht ausschließlich den seriellen Adapter durch und veröffentlicht Port
8099. Weboberfläche: `http://<decoder-host>:8099`, API: `/data`,
Container-Healthcheck: `/health`. Kein `privileged`-Modus erforderlich.
Auf dem Decoder-Host muss der passende Busadapter angeschlossen sein; der
Netzwerkzugriff von Home Assistant erfolgt über HTTP, nicht über USB/IP.
Standardmäßig startet der Container als KM-Bus-Vitotrol-Slave
(`km_remote`, 1200 Baud, 8E1, Vitotrol 300, Steckplatz 1).
Die Einstellungen lassen sich unter `/settings` ändern. Gespeicherte Werte
werden nach einem Neustart des Containers angewendet und bleiben im persistenten
`/data`-Volume erhalten.

### Container über die Weboberfläche neu starten

Der Button **Container neu starten** im Dashboard und unter `/settings`
fordert eine Bestätigung an. Einstellungen zuerst speichern: Beim Neustart
wird `run.sh` erneut ausgeführt und liest die gespeicherte Konfiguration ein.
Die Verbindung wird dabei unterbrochen; die Oberfläche prüft anschließend,
ob eine neue Serverinstanz erreichbar ist. Bleibt sie aus, Containerstatus
prüfen und die Seite manuell neu laden.

Der Server beendet sich dafür geordnet; erst ein externer Neustartmechanismus
startet den **Container** wieder. Ein Docker-Socket, Supervisor-Token oder
Neustart des Hosts ist dafür nicht nötig:

- **Docker:** `compose.yaml` und das untenstehende `docker run` verwenden
  `restart: unless-stopped` bzw. `--restart unless-stopped`.
  Ein eigener Docker-Aufruf ohne passende Neustartregel bleibt dagegen gestoppt.
- **Home Assistant Add-on:** Den **Watchdog** in der Add-on-Ansicht aktivieren.
  Ohne aktivierten Watchdog bleibt der Container nach der Anfrage gestoppt;
  eine Docker-Neustartregel des Supervisors wird nicht vorausgesetzt.
- **Natives Hostprogramm:** Der Container-Neustart ist nicht verfügbar.

Die Oberfläche erkennt die Containerumgebung, kann aber die tatsächliche
Neustartregel bzw. Watchdog-Einstellung nicht prüfen. Ein angenommener
Neustartauftrag ist deshalb noch keine Bestätigung eines erfolgreichen Neustarts.
Das KM-Bus-Laufzeitprofil wird zurückgesetzt; Bus-Logs, empfangene Datensätze
und ausstehende Steuerbefehle im Arbeitsspeicher gehen verloren.

Adapter und Protokoll können beim Start gesetzt werden:

```bash
SERIAL_DEVICE=/dev/serial/by-id/usb-mein-adapter \
PROTOCOL=p300 BAUD_RATE=4800 SERIAL_CONFIG=8E2 \
docker compose up -d --build
docker compose logs -f
```

Alternativ kann im selben Verzeichnis eine lokale `.env`-Datei mit diesen
Werten verwendet werden. `SERIAL_DEVICE` ist der Gerätepfad auf dem Host; Compose
bildet ihn im Container auf `/dev/ttyUSB0` ab.

Ohne Compose:

```bash
docker build -t viessmann-decoder:local .
docker run -d --name viessmann-decoder --init --restart unless-stopped \
  --device /dev/ttyUSB0:/dev/ttyUSB0 -p 8099:8099 \
  -v viessmann-decoder-data:/data \
  -e SERIAL_PORT=/dev/ttyUSB0 -e PROTOCOL=km_remote \
  -e BAUD_RATE=1200 -e SERIAL_CONFIG=8E1 viessmann-decoder:local
```

Ohne `/data/options.json` liest `run.sh` die Umgebungsvariablen `SERIAL_PORT`,
`BAUD_RATE`, `PROTOCOL`, `SERIAL_CONFIG`, `REMOTE_MODEL`, `REMOTE_SLOT` und
`INVERT_SERIAL`. Die Standardwerte sind `km_remote`, 1200 Baud und 8E1.
Für KW/P300 Baudrate und Parität passend zur Anlage einstellen. `km_remote`
erzwingt im Programm 1200 Baud/8E1; `REMOTE_MODEL` (`vitotrol200`/`vitotrol300`),
`REMOTE_SLOT` (1–3) und `INVERT_SERIAL` (`true`/`false`) bleiben konfigurierbar.
Existiert `/data/options.json`, hat diese Datei Vorrang vor Umgebungsvariablen.
Über `/settings` gespeicherte Werte unter `/data/ui_settings.json` haben Vorrang
vor Add-on-Optionen und Umgebungsvariablen.
Die Integration legt keine Decoder-Konfiguration an.

**Netzwerksicherheit:** Die API und Weboberfläche haben keine Authentifizierung;
im Vitotrol-Modus sind auch Steuerbefehle möglich, `/settings` kann
Konfiguration speichern und die API kann einen Container-Neustart anfordern.
Der Neustart verlangt bestätigtes JSON und weist fremde Browser-Ursprünge ab;
das ersetzt keine Authentifizierung. Port 8099 nur für
vertrauenswürdige Geräte im LAN/VPN freigeben, niemals direkt ins Internet.
Für verschlüsselten Zugriff einen HTTPS-Reverse-Proxy verwenden.

**Home Assistant:** Die mitgelieferte Custom Integration unter
`custom_components/viessmann_decoder` erstellt Entitäten automatisch.
Im Vitotrol-Modus stehen auch lokale Temperaturvorgaben, Betriebsartauswahl
und Party-/Sparbetrieb-Schalter über die API bereit. Unbestätigte Messwert-
und Störungszuordnungen werden nicht als gesicherte Anlagendaten ausgegeben.
Installation und Einrichtung: [INTEGRATION.md](INTEGRATION.md#custom-integration-empfohlen).

## ✨ Features

- **🔄 Multi-Protocol Support**: Works with VBUS, KW-Bus, P300, and KM-Bus protocols
- **📊 Real-Time Monitoring**: Live temperature sensors, pump states, and relay status
- **🖥️ Web Interface**: Clean, responsive dashboard with ingress support
- **🔍 Auto-Discovery**: Automatically detects and identifies devices on the bus
- **⚙️ Easy Configuration**: Intuitive setup through Home Assistant UI
- **🪶 Lightweight**: Optimized Alpine Linux container with minimal resource usage
- **📈 Data Logging**: Historical data collection and export capabilities
- **🔧 Advanced Diagnostics**: Protocol analyzer and debugging tools
- **🏠 Home Assistant Integration**: Custom integration with automatic sensor entities via the REST API
- **🔒 Secure**: Runs with appropriate permissions and security context

## 🎯 Supported Devices

### Bus-Kommunikationslogs

Der Button **Bus-Logs** neben **Add Device** im Dashboard öffnet die neue
Log-Seite (`logs`). Sie zeigt empfangene (RX) und erfolgreich gesendete (TX)
Bytes aller unterstützten Busprotokolle als Hexadezimaldaten mit Zeitstempel.
Bei aktivierter Signalinvertierung werden die logischen, nicht die invertierten
Leitungsbytes angezeigt. Byte-Gruppen entsprechen nicht zwingend Protokollrahmen.

Die Anzeige aktualisiert sich alle zwei Sekunden. **Anzeige pausieren** hält
nur die Darstellung an; die Aufzeichnung läuft weiter. Automatisches Scrollen
kann deaktiviert werden. Ohne Busverkehr erscheint ein entsprechender Hinweis.
Die letzten 500 Einträge mit jeweils höchstens 32 Bytes werden ausschließlich
im Arbeitsspeicher gehalten; ältere Einträge werden verworfen und ein Neustart
löscht die Logs. Auch Verkehr während der Verbindungserkennung wird erfasst.
Die Text-API ist unter `api/bus-logs` erreichbar; Navigation und API funktionieren
auch über Home Assistant Ingress.

### VBUS Protocol Devices

- ✅ Viessmann Vitosolic 200 solar controllers
- ✅ RESOL DeltaSol BX Plus/BX/MX controllers
- ✅ Generic RESOL solar and heating controllers
- ✅ Third-party VBUS-compatible devices

### KW-Bus (VS1) Protocol Devices

- ✅ Viessmann Vitotronic 100/200/300 series
- ✅ Vitodens and Vitocrossal legacy models
- ✅ Older Viessmann control units

### P300 (VS2/Optolink) Protocol Devices

- ✅ Modern Viessmann Vitodens condensing boilers
- ✅ Vitocrossal 300 series
- ✅ Current generation Vitotronic controllers
- Viessmann Vitocrossal commercial systems

### KM-Bus Protocol Devices

- Vitotrol 200/300 remote controls
- Vitocom 100 internet gateway
- Expansion modules and switching modules

## Installation

1. Add this repository to your Home Assistant add-on store
2. Install the "Viessmann Decoder" add-on
3. Configure your serial port and protocol settings
4. Start the add-on
5. Access the web interface through Home Assistant

## Configuration

The add-on can be configured through the Home Assistant UI with the following options:

### serial_port (required)

The serial device connected to your Viessmann system.

**Common values:**

- `/dev/ttyUSB0` - USB-to-Serial adapter (most common)
- `/dev/ttyUSB1` - Second USB-to-Serial adapter
- `/dev/ttyACM0` - Some USB devices
- `/dev/ttyAMA0` - Raspberry Pi GPIO UART

**How to find your serial port:**

1. Go to Home Assistant Settings → System → Hardware
2. Look under "Serial" section for connected devices
3. Or use SSH/Terminal to run: `ls -la /dev/tty*`

### baud_rate (required)

The communication speed for your protocol.

**Common values:**

- `9600` - VBUS protocol (Vitosolic, DeltaSol)
- `4800` - KW-Bus and P300 protocols (Vitotronic, Vitodens)
- `1200` - KM-Bus Vitotrol slave emulation (selected automatically in that mode)

### protocol (required)

The protocol used by your heating system.

**Options:**

- `vbus` - RESOL VBUS protocol (Vitosolic 200, DeltaSol controllers)
- `kw` - KW-Bus (VS1) protocol (Vitotronic 100/200/300, older systems)
- `p300` - P300/VS2 (Optolink) protocol (modern Vitodens boilers)
- `km` - KM-Bus protocol (remote controls, expansion modules)
- `km_remote` - Experimental KM-Bus slave that emulates a Vitotrol remote (default)

### serial_config (required)

The serial port configuration.

**Options:**

- `8N1` - 8 data bits, no parity, 1 stop bit (for VBUS)
- `8E1` - 8 data bits, even parity, 1 stop bit (KM-Bus Vitotrol emulation)
- `8E2` - 8 data bits, even parity, 2 stop bits (for KW-Bus, P300)

### KM-Bus Vitotrol emulation (experimental)

Select protocol `km_remote`, model `vitotrol300`, and the heating-circuit slot (usually 1). The add-on uses 1200 baud, 8E1, and an FC722-based M-Bus slave USB interface.

Der Dashboard-Button **Vitotrol-Steuerung** öffnet `/remote`. Dort lassen sich
Raum-Isttemperatur, normale und reduzierte Raum-Solltemperatur, Betriebsart,
Party- und Sparbetrieb vorgeben. Im Profil **WiFiVitotrol** ist beim Einschalten
des Partybetriebs dessen Solltemperatur wählbar (`0xCF`, standardmäßig 20 °C).
**OpenV** verwendet dagegen `0xCB` zum Einschalten ohne Temperaturvorgabe;
eine explizite Partytemperatur wird in diesem Profil atomar abgelehnt.
Bei einem Profilwechsel werden noch wartende Partybefehle erst beim Senden
passend zum aktiven Profil kodiert. Im OpenV-Profil wird dabei keine
WiFiVitotrol-spezifische Temperatur-Nutzlast übertragen.
Steuerwerte sind lokale Vorgaben, keine Bestätigung durch die Regelung:
vor Verwendung an der konkreten Regelung prüfen.
Mehrere API-Vorgaben werden atomar übernommen;
ungültige Werte oder eine volle Warteschlange führen zu keiner Teiländerung.

Die Live-Anzeige zeigt empfangene, XOR-dekodierte Datensätze mit Alter,
belegte Außentemperatur-/Heizfreigabefelder und Diagnosezähler. Die Laufzeitprofile
**WiFiVitotrol** und **OpenV** machen widersprüchliche Quellenangaben zu
Schreibquittierungen und Heizkreis-Datensätzen wählbar, statt sie als universell
korrekt auszugeben. Das Profil wird bei Neustart/Neuverbinden zurückgesetzt.
API und genaue Grenzen: [KM-Bus-Protokollbeschreibung](../doc/KM_BUS_VITOTROL.md).
Für nicht belegte Speicher-/Zeitprogramm-/Datumskommandos werden keine
Telegramme erfunden; insbesondere sind KW-/Optolink-Speicheradressen nicht
direkt auf KM-Bus-Slaveregister übertragbar.

This is an experimental implementation based on OpenV KM-Bus observations and the public WiFiVitotrol project. Those protocol observations primarily target a Vitotronic 200 KW2, so compatibility with the Vitotronic 200 KM1 and all Vitotrol 300 functions is not guaranteed. The Linux add-on cannot guarantee the strict response timing a physical remote provides. Verify the adapter and operation on your exact controller, preferably with passive bus captures, before relying on control commands. KM-Bus wiring can damage the heating controller; use an isolated bus interface and consult a qualified installer.

## Configuration Examples

### Example 1: Vitosolic 200 (Solar Controller)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 9600,
  "protocol": "vbus",
  "serial_config": "8N1"
}
```

### Example 2: Vitotronic 200 (KW-Bus)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 4800,
  "protocol": "kw",
  "serial_config": "8E2"
}
```

### Example 3: Modern Vitodens (Optolink)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 4800,
  "protocol": "p300",
  "serial_config": "8E2"
}
```

### Example 4: Remote USB/IP Setup

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 9600,
  "protocol": "vbus",
  "serial_config": "8N1",
  "usbip_enable": true,
  "usbip_host": "192.168.1.100",
  "usbip_port": 3240,
  "usbip_busid": "1-1.3"
}
```

## Hardware Setup

### USB-to-Serial Adapter

The most common setup uses a USB-to-Serial adapter (FTDI, CH340, CP2102, etc.) connected to your Viessmann system's data bus.

**Wiring:**

- Connect adapter RX to bus TX
- Connect adapter TX to bus RX
- Connect GND to bus GND
- Consider using an optocoupler for electrical isolation

### Raspberry Pi GPIO

You can also use the Raspberry Pi's built-in UART:

- Enable UART in Raspberry Pi configuration
- Connect GPIO 14 (TX) and GPIO 15 (RX)
- Set `serial_port` to `/dev/ttyAMA0`

### Remote USB/IP (Network Serial Adapter)

The add-on supports USB devices over network via USB/IP.

**Use case:** Your USB-to-Serial adapter is connected to another computer on the network (e.g., directly at the heating system).

**USB/IP Server Setup:**

On the computer with the USB adapter:

```bash
# Install USB/IP
sudo apt-get install usbip

# Start USB/IP server
sudo modprobe usbip-host
sudo usbipd -D

# List available USB devices
usbip list -l

# Share USB device (e.g., busid 1-1.3)
sudo usbip bind -b 1-1.3
```

**Add-on Configuration:**

See Example 4 above for the configuration settings.

**Benefits:**

- Flexible location of USB adapter
- No direct USB connection to Home Assistant server needed
- Ideal for distributed installations

⚠️ **Important:** Always ensure proper electrical isolation when connecting to your heating system. Follow local electrical codes and regulations.

## Using the Add-on

### Web Interface

After starting the add-on, access the web interface:

1. Click "OPEN WEB UI" in the add-on info page
2. Or navigate to `http://homeassistant.local:8099`

The web interface provides:

- **Dashboard**: Real-time view of all sensor data
- **Status**: System and configuration information

### Data Updates

The dashboard automatically refreshes data every 2 seconds, showing:

- Temperature sensors (°C)
- Pump power levels (%)
- Relay states (ON/OFF)
- Communication status

## Troubleshooting

### Serial Port Not Found

**Symptom:** Add-on fails to start with "Serial port not found" error

**Solutions:**

1. Verify the serial device is connected: Settings → System → Hardware
2. Check the `serial_port` configuration matches your actual device
3. Ensure the device is properly recognized by the system
4. Try unplugging and replugging the USB adapter

### No Data Received

**Symptom:** Dashboard shows "Waiting for data..."

**Solutions:**

1. Verify physical connections to your heating system
2. Check that `protocol` matches your device
3. Ensure `baud_rate` and `serial_config` are correct
4. Verify the heating system is powered and communicating
5. Check for reversed RX/TX connections

### Communication Status: Error

**Symptom:** Status shows "Error" instead of "OK"

**Solutions:**

1. Double-check protocol settings
2. Verify baud rate is correct for your device
3. Check serial configuration (8N1 for VBUS, 8E2 for KW/P300, 8E1 for `km_remote`)
4. Ensure no other software is using the serial port
5. Try restarting the add-on

### Permission Denied

**Symptom:** Cannot access serial port due to permissions

**Solution:** This should be handled automatically by the add-on's privileged access. If issues persist, try restarting Home Assistant.

## Integration with Home Assistant

### Sensors

The add-on exposes data via HTTP API at `/data` endpoint. You can create Home Assistant sensors using the RESTful integration:

```yaml
sensor:
  - platform: rest
    resource: http://localhost:8099/data
    name: Viessmann Data
    json_attributes:
      - temperatures
      - pumps
      - relays
    value_template: "{{ value_json.status }}"
    scan_interval: 10

template:
  - sensor:
      - name: "Boiler Temperature"
        unique_id: viessmann_temp_1
        unit_of_measurement: "°C"
        state: '{{ state_attr("sensor.viessmann_data", "temperatures")[0] }}'
      - name: "Circulation Pump"
        unique_id: viessmann_pump_1
        unit_of_measurement: "%"
        state: '{{ state_attr("sensor.viessmann_data", "pumps")[0] }}'
```

### Automation Examples

**Example: Alert on low temperature**

```yaml
automation:
  - alias: "Low boiler temperature alert"
    trigger:
      platform: numeric_state
      entity_id: sensor.viessmann_temp_1
      below: 30
    action:
      service: notify.notify
      data:
        message: "Warning: Boiler temperature is below 30°C"
```

## Support

For issues, questions, or contributions:

- GitHub: https://github.com/MrTir1995/Viessmann-HA-Addon
- Issues: https://github.com/MrTir1995/Viessmann-HA-Addon/issues

## Troubleshooting

### Common Issues

#### Container startet nach dem Neustartauftrag nicht wieder

Das aktuelle Image verwendet kein s6-overlay: `run.sh` startet das
Webserverprogramm direkt mit `exec`, unter Docker/Supervisor mit tini.
Prüfen Sie bei einem gestoppten Container die Docker-Neustartregel oder den
aktivierten Home-Assistant-Watchdog. Ohne diese externe Überwachung startet
ein Prozessende den Container nicht erneut. Starten Sie ihn nötigenfalls
manuell über Docker oder die Add-on-Ansicht.

#### Serial Port Not Found

If the addon reports that the serial port is not found:

1. Verify that your USB-to-serial adapter is properly connected
2. Check that the device appears in `/dev` (e.g., `/dev/ttyUSB0`)
3. Ensure the addon has permission to access the serial device
4. Try a different USB port
5. Check the addon logs for specific error messages

#### Connection Issues

If the addon starts but cannot communicate with the heating system:

1. Verify the protocol selection matches your device
2. Check the baud rate setting (9600 for VBUS, 4800 for KW/P300, forced 1200 for `km_remote`)
3. Verify the serial configuration (8N1 for VBUS, 8E2 for KW/P300, forced 8E1 for `km_remote`)
4. Test the serial adapter with another tool to confirm it works
5. Check your hardware wiring and connections

## License

See the main repository LICENSE file for details.

## Disclaimer

⚠️ **WARNING**: This add-on interfaces with your heating system. Use at your own risk. Always follow proper electrical safety procedures and local regulations when interfacing with heating systems. The authors assume no liability for any damages.
