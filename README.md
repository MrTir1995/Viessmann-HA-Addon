# Viessmann Decoder - Home Assistant Add-on

[![Add repository to Home Assistant](https://img.shields.io/badge/Add%20repository%20to-Home%20Assistant-blue?logo=home-assistant&logoColor=white)](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https://github.com/MrTir1995/Viessmann-HA-Addon)
[![GitHub Release](https://img.shields.io/github/v/release/MrTir1995/Viessmann-HA-Addon?logo=github)](https://github.com/MrTir1995/Viessmann-HA-Addon/releases)
![Version](https://img.shields.io/badge/version-2.3.1-blue.svg)
![Supports amd64 Architecture](https://img.shields.io/badge/amd64-yes-green.svg)
![Supports aarch64 Architecture](https://img.shields.io/badge/aarch64-yes-green.svg)
![Supports armhf Architecture](https://img.shields.io/badge/armhf-yes-green.svg)
![Supports armv7 Architecture](https://img.shields.io/badge/armv7-yes-green.svg)
![Supports i386 Architecture](https://img.shields.io/badge/i386-yes-green.svg)

Überwachen Sie Ihre Viessmann-Heizungsanlage über eine Web-Oberfläche in Home Assistant.

Das Add-on unterstützt VBUS, KW-Bus, P300/Optolink und KM-Bus. Ab Version **2.3.0** steht zusätzlich eine **experimentelle Vitotrol-200/300-Emulation** als KM-Bus-Slave zur Verfügung. Funktionsumfang und Kompatibilität hängen von der Regelung und dem verwendeten Busadapter ab.

> **Wichtig:** Das Add-on selbst erstellt keine Entitäten. Die mitgelieferte [Custom Integration](viessmann-decoder/INTEGRATION.md#custom-integration-empfohlen) legt Sensoren und Diagnoseentitäten über die REST-API automatisch an; im Vitotrol-Modus kommen Zahlenwerte, Betriebsartauswahl sowie Party-/Sparbetrieb-Schalter hinzu. Steuerwerte sind lokale Vorgaben, keine Bestätigung der Regelung. Unverifizierte Außentemperaturen und Anlagenstörungen bleiben nicht verfügbar. Die Vitotrol-Emulation ist kein verifizierter Ersatz für eine physische Fernbedienung.

## Dockerbetrieb ohne Home Assistant Supervisor

Der Decoder kann als eigenständiger Container neben Home Assistant Container
oder auf einem separaten Linux-Host betrieben werden. Unter
[`viessmann-decoder`](viessmann-decoder/README.md#standalone-dockerbetrieb-ohne-supervisor)
liegen Compose-Konfiguration, Dockerfile und die Custom Integration.
Der vorhandene Add-on-Betrieb bleibt erhalten.

## 🚀 Schnellinstallation

1. Klicken Sie auf den Button oben, um dieses Repository zu Home Assistant hinzuzufügen
2. Gehen Sie zu **Einstellungen** → **Add-ons** → **Add-on Store**
3. Suchen Sie "Viessmann Decoder" und klicken Sie auf **INSTALLIEREN**
4. Konfigurieren Sie Ihren seriellen Port und das Protokoll
5. Klicken Sie auf **STARTEN**
6. Öffnen Sie die Oberfläche über **WEB UI ÖFFNEN** (Home Assistant Ingress)

Für detaillierte Installationsanweisungen siehe [INSTALL.md](viessmann-decoder/INSTALL.md)

## ✨ Features

- 🔄 **Multi-Protokoll-Unterstützung**: Funktioniert mit VBUS, KW-Bus, P300 und KM-Bus Protokollen
- 📊 **Echtzeit-Überwachung**: Live-Temperatursensoren, Pumpenzustände und Relaiszustände
- 🖥️ **Web-Interface**: Sauberes, responsives Dashboard zugänglich aus Home Assistant
- 🔍 **Automatische Erkennung**: Erkennt automatisch Geräte auf dem Bus
- ⚙️ **Einfache Konfiguration**: Intuitive Einrichtung über Home Assistant UI
- 🔌 **Mehrere Adapter**: Unter `/settings` über **+** hinzufügen, unabhängig konfigurieren und gleichzeitig betreiben; je Adapter eine eigene API-Basis für Home Assistant
- 🪶 **Leichtgewichtig**: Auf Alpine Linux basierend für minimalen Ressourcenverbrauch
- 🏠 **Home Assistant Integration**: Ingress-Weboberfläche, REST-API und Custom Integration mit automatischen Sensoren
- 🎛️ **Experimentelle Fernbedienung**: Raumtemperatur, Solltemperatur und grundlegende Betriebsarten im Modus `km_remote`
- 📦 **Docker-Images**: Veröffentlichung auf Docker Hub für alle fünf aufgeführten Architekturen

Das Add-on benötigt erweiterten Zugriff auf serielle Geräte (`full_access` und privilegierte Berechtigungen). Nutzen Sie es nur in einer vertrauenswürdigen Umgebung und stellen Sie Port 8099 nicht ungeschützt ins Internet.

## 🎯 Protokolle und Gerätefamilien

Die folgenden Gerätefamilien dienen als Orientierung, nicht als Garantie für jedes Modell oder jeden Datenpunkt. Prüfen Sie das tatsächliche Protokoll Ihrer Regelung.

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

- `km`: vorhandener Decoder-/Polling-Modus für KM-Bus
- **Grenze des Legacy-Modus `km`:** Sein Sendeformat ist nicht mit den verifizierten `km_remote`-Telegrammen vereinheitlicht. Polling und Schreibbefehle sind daher nicht als funktionsfähig bestätigt; eine Korrektur benötigt passende reale Busmitschnitte. Der Modus `km_remote` ist eine separate Implementierung.
- `km_remote`: experimenteller Slave-Modus mit Vitotrol-200/300-Profilen und Heizkreis-Slot 1–3

**Vitotronic 200 KM1:** Die vollständige Kompatibilität der Emulation ist nicht bestätigt. Linux kann erforderliche Antwortzeiten verfehlen. Prüfen Sie Busmitschnitte und die Reaktion der Regelung vor dem regulären Einsatz. Weitere Grenzen und Protokollreferenzen: [KM-Bus/Vitotrol](doc/KM_BUS_VITOTROL.md).

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
- `1200` - KM-Bus-Slave (`km_remote`, wird automatisch erzwungen)

### protocol (erforderlich)

Das von Ihrem Heizsystem verwendete Protokoll.

**Optionen:**

- `vbus` - RESOL VBUS-Protokoll (Vitosolic 200, DeltaSol-Regler)
- `kw` - KW-Bus (VS1)-Protokoll (Vitotronic 100/200/300, ältere Systeme)
- `p300` - P300/VS2 (Optolink)-Protokoll (moderne Vitodens-Kessel)
- `km` - KM-Bus-Protokoll (Fernbedienungen, Erweiterungsmodule)
- `km_remote` - Experimentelle Vitotrol-Emulation als KM-Bus-Slave (nicht mit `km` gleichzusetzen)

### serial_config (erforderlich)

Die serielle Port-Konfiguration.

**Optionen:**

- `8N1` - 8 Datenbits, keine Parität, 1 Stoppbit (für VBUS, KM-Bus)
- `8E1` - 8 Datenbits, gerade Parität, 1 Stoppbit (für `km_remote`, wird automatisch erzwungen)
- `8E2` - 8 Datenbits, gerade Parität, 2 Stoppbits (für KW-Bus, P300)

### Vitotrol-Emulation (nur `km_remote`)

- `remote_model`: `vitotrol300` (Standard) oder `vitotrol200`
- `remote_slot`: Heizkreis-Slot `1`, `2` oder `3`
- `invert_serial`: optionale Signal-Invertierung; nur verwenden, wenn der Adapter sie benötigt und unterstützt

Die Emulation beantwortet Master-Anfragen und versendet vorgemerkte Steuerdaten erst nach einem Master-Ping. Unterstützt werden Raumtemperatur, Raumsolltemperatur und grundlegende Betriebsarten, aber keine vollständige Vitotrol-Funktionalität oder Zeitprogramme.

### USB/IP Konfiguration (experimentell)

Die Optionen für Remote-USB-Zugriff sind vorhanden. Die enthaltene `socat`-Weiterleitung ersetzt jedoch keinen vollständigen USB/IP-Kernelclient; eine funktionsfähige serielle Geräteanbindung muss separat sichergestellt werden.

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

### Beispiel 4: Experimentelle Vitotrol-300-Emulation

```json
{
  "serial_port": "/dev/ttyUSB0",
  "baud_rate": 1200,
  "protocol": "km_remote",
  "serial_config": "8E1",
  "remote_model": "vitotrol300",
  "remote_slot": 1,
  "invert_serial": false
}
```

Verwenden Sie einen geeigneten, galvanisch getrennten KM-Bus-Slave-Adapter, beispielsweise ein FC722-basiertes M-Bus-Slave-USB-Interface ohne Echo. Eine passende elektrische Schnittstelle allein implementiert noch nicht das KM-Bus-Protokoll.

## 🔌 Hardware-Einrichtung

### USB-zu-Serial-Adapter

Ein USB-zu-Serial-Adapter (FTDI, CH340, CP2102 etc.) benötigt zusätzlich eine zum jeweiligen Bus passende elektrische Schnittstelle. Ein gewöhnlicher TTL-UART-Adapter darf nicht direkt an VBUS oder KM-Bus angeschlossen werden.

**Bei einer bereits angepassten UART-Schnittstelle:**

- Verbinden Sie Adapter RX mit Bus TX
- Verbinden Sie Adapter TX mit Bus RX
- Verbinden Sie GND mit Bus GND
- Verwenden Sie eine geeignete galvanische Trennung

### Raspberry Pi GPIO

Sie können auch den eingebauten UART des Raspberry Pi verwenden:

- Aktivieren Sie UART in der Raspberry Pi Konfiguration
- Nutzen Sie GPIO 14 (TX) und GPIO 15 (RX) ausschließlich mit einer geeigneten, galvanisch getrennten Busschnittstelle
- Setzen Sie `serial_port` auf `/dev/ttyAMA0`

### Remote USB/IP (Netzwerk-Serial-Adapter)

Die experimentellen USB/IP-Optionen sind für Netzwerk-Anbindungen vorgesehen; prüfen Sie zuerst, ob tatsächlich ein serielles Gerät in Home Assistant verfügbar wird:

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

Der direkte Zugriff setzt voraus, dass Port 8099 in der Add-on-Netzwerkkonfiguration freigegeben ist. Ingress benötigt diese Portfreigabe nicht.

Die Web-Oberfläche bietet:

- **Dashboard**: Echtzeitansicht aller Sensordaten
- **Status**: System- und Konfigurationsinformationen
- **Fernbedienung**: Im Modus `km_remote` über `/remote`; API über `/api/remote`

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

Das Add-on erstellt **keine Sensoren oder Entitäten automatisch**. Die folgenden Beispiele verwenden die RESTful-Integration und den freigegebenen Port 8099; passen Sie Hostnamen und Sensorindizes an Ihre Anlage an.

### Sensoren

Das Add-on stellt Daten über HTTP API am `/data` Endpunkt bereit. Sie können Home Assistant Sensoren mit der RESTful-Integration erstellen:

```yaml
sensor:
  - platform: rest
    resource: http://homeassistant.local:8099/data
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
        availability: '{{ (state_attr("sensor.viessmann_data", "temperatures") or []) | count > 0 }}'
        state: '{{ (state_attr("sensor.viessmann_data", "temperatures") or [none])[0] }}'
      - name: "Zirkulationspumpe"
        unique_id: viessmann_pump_1
        unit_of_measurement: "%"
        availability: '{{ (state_attr("sensor.viessmann_data", "pumps") or []) | count > 0 }}'
        state: '{{ (state_attr("sensor.viessmann_data", "pumps") or [none])[0] }}'
```

### Automatisierungsbeispiele

**Beispiel: Warnung bei niedriger Temperatur**

```yaml
automation:
  - alias: "Warnung bei niedriger Kesseltemperatur"
    trigger:
      platform: numeric_state
      entity_id: sensor.kesseltemperatur
      below: 30
    action:
      service: notify.notify
      data:
        message: "Warnung: Kesseltemperatur ist unter 30°C"
```

## 📦 Update auf Version 2.3.1

1. Sichern Sie Ihre Add-on-Konfiguration.
2. Nach Übernahme der Release-Änderungen in den Standardbranch und erfolgreicher Docker-Veröffentlichung: Öffnen Sie den **Add-on Store** und wählen Sie im Menü **Nach Updates suchen**.
3. Öffnen Sie **Viessmann Decoder** und installieren Sie das angebotene Update.
4. Prüfen Sie Konfiguration, Add-on-Protokoll und Weboberfläche.

Bestehende Protokollmodi bleiben verfügbar. `km_remote` muss ausdrücklich ausgewählt und mit einem geeigneten Adapter getestet werden.

Home Assistant bezieht die Images aus `docker.io/mrtir071/viessmann-decoder-{arch}:2.3.1`. Ein GitHub-Release allein reicht nicht: Die Version in `config.yaml` im Standardbranch und die Docker-Tags müssen übereinstimmen.

Das veröffentlichte Image 2.3.0 enthält noch nicht die später ergänzte Adapterverwaltung und den Hell-/Dunkel-Schalter. Ab 2.3.1 führt **Add Serial Adapter** zur Adapterverwaltung mit **+**; der Theme-Schalter befindet sich oben rechts. Beide Funktionen stehen im Add-on und im Standalone-Container zur Verfügung. Weitere serielle Geräte müssen dem Container zugänglich gemacht werden (bei Compose über `devices`).

Standalone mit Registry-Image: Neues Image herunterladen und den Container neu erstellen; ein einfacher Neustart lädt kein neues Image. Bei Verwendung der mitgelieferten `compose.yaml` aus dem aktualisierten Quellcode mit `docker compose up -d --build` neu bauen. Falls nach dem Update noch **Add Device** statt **Add Serial Adapter** angezeigt wird, die Seite einmal mit **Strg+F5** neu laden und prüfen, ob tatsächlich das neue Image läuft.

Für Maintainer: Der Workflow **Publish to Docker Hub** baut alle fünf Architekturen und die Multi-Arch-Manifeste. Mit `version=2.3.1` und `create_release=true` veröffentlicht er nach erfolgreichem Build zusätzlich die Release-Notizen aus dem Add-on-Changelog. Für einen regulär verfügbaren Store-Release sollte der Workflow vom aktualisierten Standardbranch gestartet werden.

## 📚 Weitere Dokumentation

- [Installations-Anleitung](viessmann-decoder/INSTALL.md)
- [Entwickler-Dokumentation](viessmann-decoder/DEVELOPMENT.md)
- [MQTT-Setup](doc/MQTT_SETUP.md)
- [Hardware-Setup](doc/HARDWARE_SETUP.md)
- [Scheduler-Anleitung](doc/SCHEDULER_GUIDE.md)
- [Experimentelle KM-Bus/Vitotrol-Emulation](doc/KM_BUS_VITOTROL.md)
- [Changelog](CHANGELOG.md)

## 💬 Support

Bei Problemen, Fragen oder Beiträgen:

- GitHub: <https://github.com/MrTir1995/Viessmann-HA-Addon>
- Issues: <https://github.com/MrTir1995/Viessmann-HA-Addon/issues>

## 📄 Lizenz

Siehe die Haupt-Repository LICENSE-Datei für Details.

## ⚠️ Haftungsausschluss

**WARNUNG**: Dieses Add-on kommuniziert mit Ihrem Heizsystem. Verwendung auf eigene Gefahr. Befolgen Sie immer ordnungsgemäße elektrische Sicherheitsverfahren und lokale Vorschriften beim Anschluss an Heizsysteme. Die Autoren übernehmen keine Haftung für Schäden.
