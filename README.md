# Viessmann Decoder - Home Assistant Add-on

[![Add repository to Home Assistant](https://img.shields.io/badge/Add%20repository%20to-Home%20Assistant-blue?logo=home-assistant&logoColor=white)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https://github.com/MrTir1995/Viessmann-HA-Addon)
[![GitHub Release](https://img.shields.io/github/v/release/MrTir1995/Viessmann-HA-Addon?logo=github)](https://github.com/MrTir1995/Viessmann-HA-Addon/releases)
![Version](https://img.shields.io/badge/version-2.1.3-blue.svg)
![Supports amd64 Architecture](https://img.shields.io/badge/amd64-yes-green.svg)
![Supports aarch64 Architecture](https://img.shields.io/badge/aarch64-yes-green.svg)
![Supports armhf Architecture](https://img.shields.io/badge/armhf-yes-green.svg)
![Supports armv7 Architecture](https://img.shields.io/badge/armv7-yes-green.svg)
![Supports i386 Architecture](https://img.shields.io/badge/i386-yes-green.svg)

Überwachen und steuern Sie Ihre Viessmann-Heizungsanlage direkt aus Home Assistant mit professioneller Protokollunterstützung!

Dieses Add-on bietet eine umfassende Web-Oberfläche zur Kommunikation mit Viessmann-Heizungssteuerungen unter Verwendung mehrerer Industriestandard-Protokolle (VBUS, KW-Bus, P300/Optolink, KM-Bus).

## 🚀 Schnellinstallation

1. Klicken Sie auf den Button oben, um dieses Repository zu Home Assistant hinzuzufügen
2. Gehen Sie zu **Einstellungen** → **Add-ons** → **Add-on Store**
3. Suchen Sie "Viessmann Decoder" und klicken Sie auf **INSTALLIEREN**
4. Konfigurieren Sie Ihren seriellen Port und das Protokoll
5. Klicken Sie auf **STARTEN**

Für detaillierte Installationsanweisungen siehe [INSTALL.md](viessmann-decoder/INSTALL.md)

## ✨ Features

- 🔄 **Multi-Protokoll-Unterstützung**: Funktioniert mit VBUS, KW-Bus, P300 und KM-Bus Protokollen
- 📊 **Echtzeit-Überwachung**: Live-Temperatursensoren, Pumpenzustände und Relaiszustände
- 🖥️ **Web-Interface**: Sauberes, responsives Dashboard zugänglich aus Home Assistant
- 🔍 **Automatische Erkennung**: Erkennt automatisch Geräte auf dem Bus
- ⚙️ **Einfache Konfiguration**: Intuitive Einrichtung über Home Assistant UI
- 🪶 **Leichtgewichtig**: Auf Alpine Linux basierend für minimalen Ressourcenverbrauch
- 🏠 **Home Assistant Integration**: Native Sensor- und Entitätserstellung
- 🔒 **Sicher**: Läuft mit angemessenen Berechtigungen und Sicherheitskontext

## 🎯 Unterstützte Geräte

### VBUS Protokoll Geräte

- ✅ Viessmann Vitosolic 200 Solarregler
- ✅ RESOL DeltaSol BX Plus/BX/MX Regler
- ✅ Generische RESOL Solar- und Heizungsregler
- ✅ VBUS-kompatible Geräte von Drittanbietern

### KW-Bus (VS1) Protokoll Geräte

- ✅ Viessmann Vitotronic 100/200/300 Serie
- ✅ Vitodens und Vitocrossal Legacy-Modelle
- ✅ Ältere Viessmann Steuereinheiten

### P300 (VS2/Optolink) Protokoll Geräte

- ✅ Moderne Viessmann Vitodens Brennwertkessel
- ✅ Vitocrossal 300 Serie
- ✅ Aktuelle Generation Vitotronic Regler
- ✅ Viessmann Vitocrossal kommerzielle Systeme

### KM-Bus Protokoll Geräte

- ✅📦 Installation

1. Fügen Sie dieses Repository zum Home Assistant Add-on Store hinzu
2. Installieren Sie das "Viessmann Decoder" Add-on
3. Konfigurieren Sie Ihren seriellen Port und die Protokolleinstellungen
4. Starten Sie das Add-on
5. Greifen Sie über Home Assistant auf die Web-Oberfläche zu

Für detaillierte Installationsanweisungen siehe [INSTALL.md](viessmann-decoder/INSTALL.md)

## ⚙️ Konfigure your serial port and protocol settings

4. Start the add-on
5. Access the web interface through Home Assistant

## ⚙️ Konfiguration

Das Add-on kann über die Home Assistant Benutzeroberfläche konfiguriert werden:

### serial_port (erforderlich)

Das serielle Gerät, das mit Ihrem Viessmann-System verbunden ist.

**Häufige Werte:**

- `/dev/ttyUSB0` - USB-zu-Serial-Adapter (am häufigsten)
- `/dev/ttyUSB1` - Zweiter USB-zu-Serial-Adapter
- `/dev/ttyACM0` - Einige USB-Geräte
- `/dev/ttyAMA0` - Raspberry Pi GPIO UART

**So finden Sie Ihren seriellen Port:**

1. Gehen Sie zu Home Assistant Einstellungen → System → Hardware
2. Suchen Sie im Abschnitt "Serial" nach angeschlossenen Geräten
3. Oder verwenden Sie SSH/Terminal: `ls -la /dev/tty*`

### baud_rate (erforderlich)

Die Kommunikationsgeschwindigkeit für Ihr Protokoll.

**Häufige Werte:**

- `9600` - VBUS-Protokoll (Vitosolic, DeltaSol)
- `4800` - KW-Bus- und P300-Protokolle (Vitotronic, Vitodens)

### protocol (erforderlich)

Das von Ihrem Heizsystem verwendete Protokoll.

**Optionen:**

- `vbus` - RESOL VBUS-Protokoll (Vitosolic 200, DeltaSol-Regler)
- `kw` - KW-Bus (VS1)-Protokoll (Vitotronic 100/200/300, ältere Systeme)
- `p300` - P300/VS2 (Optolink)-Protokoll (moderne Vitodens-Kessel)
- `km` - KM-Bus-Protokoll (Fernbedienungen, Erweiterungsmodule)

### serial_config (erforderlich)

Die serielle Port-Konfiguration.

**Optionen:**

- `8N1` - 8 Datenbits, keine Parität, 1 Stoppbit (für VBUS, KM-Bus)
- `8E2` - 8 Datenbits, gerade Parität, 2 Stoppbits (für KW-Bus, P300)

### USB/IP Konfiguration (optional)

Für Remote-USB-Zugriff über das Netzwerk.

**usbip_enable** (optional)

- `false` - Deaktiviert (Standard)
- `true` - Aktiviert USB/IP Unterstützung

**usbip_host** (optional)

- IP-Adresse oder Hostname des USB/IP-Servers
- Beispiel: `192.168.1.100`

**usbip_port** (optional)

- Port des USB/IP-Servers
- Standard: `3240`

**usbip_busid** (optional)

- USB Bus ID des Geräts auf dem Remote-Server
- Beispiel: `1-1.3`
- Wird mit `usbip list -l` ermittelt

## 📝 Konfigurationsbeispiele

### Beispiel 1: Vitosolic 200 (Solarregler)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 9600,
  "protocol": "vbus",
  "serial_config": "8N1"
}
```

### Beispiel 2: Vitotronic 200 (KW-Bus)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 4800,
  "protocol": "kw",
  "serial_config": "8E2"
}
```

### Beispiel 3: Moderner Vitodens (Optolink)

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 4800,
  "protocol": "p300",
  "serial_config": "8E2"
}
```

## 🔌 Hardware-Einrichtung

### USB-zu-Serial-Adapter

Die häufigste Einrichtung verwendet einen USB-zu-Serial-Adapter (FTDI, CH340, CP2102, etc.), der mit dem Datenbus Ihres Viessmann-Systems verbunden ist.

**Verkabelung:**

- Verbinden Sie Adapter RX mit Bus TX
- Verbinden Sie Adapter TX mit Bus RX
- Verbinden Sie GND mit Bus GND
- Erwägen Sie die Verwendung eines Optokopplers für elektrische Isolation

### Raspberry Pi GPIO

Sie können auch den eingebauten UART des Raspberry Pi verwenden:

- Aktivieren Sie UART in der Raspberry Pi Konfiguration
- Verbinden Sie GPIO 14 (TX) und GPIO 15 (RX)
- Setzen Sie `serial_port` auf `/dev/ttyAMA0`

### Remote USB/IP (Netzwerk-Serial-Adapter)

Das Add-on unterstützt auch USB-Geräte über das Netzwerk via USB/IP:

**Anwendungsfall:** Ihr USB-zu-Serial-Adapter ist an einem anderen Rechner im Netzwerk angeschlossen (z.B. direkt bei der Heizung).

**Konfiguration:**

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

**Einrichtung des USB/IP-Servers:**

Auf dem Rechner mit dem USB-Adapter:

```bash
# USB/IP installieren
sudo apt-get install usbip

# USB/IP Server starten
sudo modprobe usbip-host
sudo usbipd -D

# Verfügbare USB-Geräte auflisten
usbip list -l

# USB-Gerät freigeben (z.B. busid 1-1.3)
sudo usbip bind -b 1-1.3
```

**Vorteile:**

- Flexibler Standort des USB-Adapters
- Keine direkte USB-Verbindung zum Home Assistant Server nötig
- Ideal für verteilte Installationen

⚠️ **Wichtig:** Stellen Sie immer eine ordnungsgemäße elektrische Isolation sicher, wenn Sie sich mit Ihrem Heizsystem verbinden. Befolgen Sie lokale elektrische Vorschriften und Bestimmungen.

## 💻 Verwendung des Add-ons

### Web-Interface

Nach dem Start des Add-ons greifen Sie auf die Web-Oberfläche zu:

1. Klicken Sie auf "WEB UI ÖFFNEN" auf der Add-on-Infoseite
2. Oder navigieren Sie zu `http://homeassistant.local:8099`

Die Web-Oberfläche bietet:

- **Dashboard**: Echtzeitansicht aller Sensordaten
- **Status**: System- und Konfigurationsinformationen

### Datenaktualisierungen

Das Dashboard aktualisiert die Daten automatisch alle 2 Sekunden und zeigt:

- Temperatursensoren (°C)
- Pumpenleistungsstufen (%)
- Relaiszustände (EIN/AUS)
- Kommunikationsstatus

## 🔧 Fehlerbehebung

### Serieller Port nicht gefunden

**Symptom:** Add-on startet nicht mit Fehler "Serial port not found"

**Lösungen:**

1. Überprüfen Sie, ob das serielle Gerät verbunden ist: Einstellungen → System → Hardware
2. Prüfen Sie, ob die `serial_port` Konfiguration mit Ihrem tatsächlichen Gerät übereinstimmt
3. Stellen Sie sicher, dass das Gerät vom System erkannt wird
4. Versuchen Sie, den USB-Adapter ab- und wieder anzustecken

### Keine Daten empfangen

**Symptom:** Dashboard zeigt "Warten auf Daten..."

**Lösungen:**

1. Überprüfen Sie die physischen Verbindungen zu Ihrem Heizsystem
2. Prüfen Sie, ob `protocol` mit Ihrem Gerät übereinstimmt
3. Stellen Sie sicher, dass `baud_rate` und `serial_config` korrekt sind
4. Überprüfen Sie, ob das Heizsystem eingeschaltet ist und kommuniziert
5. Prüfen Sie auf vertauschte RX/TX-Verbindungen

### Kommunikationsstatus: Fehler

**Symptom:** Status zeigt "Fehler" anstelle von "OK"

**Lösungen:**

1. Überprüfen Sie die Protokolleinstellungen
2. Verifizieren Sie, dass die Baudrate für Ihr Gerät korrekt ist
3. Prüfen Sie die serielle Konfiguration (8N1 vs 8E2)
4. Stellen Sie sicher, dass keine andere Software den seriellen Port verwendet
5. Versuchen Sie, das Add-on neu zu starten

### Zugriff verweigert

**Symptom:** Zugriff auf den seriellen Port aufgrund von Berechtigungen nicht möglich

**Lösung:** Dies sollte automatisch durch den privilegierten Zugriff des Add-ons gehandhabt werden. Bei anhaltenden Problemen versuchen Sie, Home Assistant neu zu starten.

## 🏠 Integration mit Home Assistant

### Sensoren

Das Add-on stellt Daten über HTTP API am `/data` Endpunkt bereit. Sie können Home Assistant Sensoren mit der RESTful-Integration erstellen:

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
      - name: "Kesseltemperatur"
        unique_id: viessmann_temp_1
        unit_of_measurement: "°C"
        state: '{{ state_attr("sensor.viessmann_data", "temperatures")[0] }}'
      - name: "Zirkulationspumpe"
        unique_id: viessmann_pump_1
        unit_of_measurement: "%"
        state: '{{ state_attr("sensor.viessmann_data", "pumps")[0] }}'
```

### Automatisierungsbeispiele

**Beispiel: Warnung bei niedriger Temperatur**

```yaml
automation:
  - alias: "Warnung bei niedriger Kesseltemperatur"
    trigger:
      platform: numeric_state
      entity_id: sensor.viessmann_temp_1
      below: 30
    action:
      service: notify.notify
      data:
        message: "Warnung: Kesseltemperatur ist unter 30°C"
```

## 📚 Weitere Dokumentation

- [Installations-Anleitung](viessmann-decoder/INSTALL.md)
- [Entwickler-Dokumentation](viessmann-decoder/DEVELOPMENT.md)
- [MQTT-Setup](doc/MQTT_SETUP.md)
- [Hardware-Setup](doc/HARDWARE_SETUP.md)
- [Scheduler-Anleitung](doc/SCHEDULER_GUIDE.md)
- [Changelog](CHANGELOG.md)

## 💬 Support

Bei Problemen, Fragen oder Beiträgen:

- GitHub: <https://github.com/MrTir1995/Viessmann-HA-Addon>
- Issues: <https://github.com/MrTir1995/Viessmann-HA-Addon/issues>

## 📄 Lizenz

Siehe die Haupt-Repository LICENSE-Datei für Details.

## ⚠️ Haftungsausschluss

**WARNUNG**: Dieses Add-on kommuniziert mit Ihrem Heizsystem. Verwendung auf eigene Gefahr. Befolgen Sie immer ordnungsgemäße elektrische Sicherheitsverfahren und lokale Vorschriften beim Anschluss an Heizsysteme. Die Autoren übernehmen keine Haftung für Schäden.
