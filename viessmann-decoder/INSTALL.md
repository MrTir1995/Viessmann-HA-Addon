# Viessmann Decoder Home Assistant Add-on

![Version](https://img.shields.io/badge/version-2.1.3-blue.svg)
![Supports amd64 Architecture](https://img.shields.io/badge/amd64-yes-green.svg)
![Supports aarch64 Architecture](https://img.shields.io/badge/aarch64-yes-green.svg)
![Supports armhf Architecture](https://img.shields.io/badge/armhf-yes-green.svg)
![Supports armv7 Architecture](https://img.shields.io/badge/armv7-yes-green.svg)
![Supports i386 Architecture](https://img.shields.io/badge/i386-yes-green.svg)

Monitor and configure Viessmann heating systems via VBUS, KW-Bus, P300, and KM-Bus protocols.

## Installation vom GitHub Add-on Store

### Schritt 1: Repository hinzufügen

1. Öffnen Sie Home Assistant
2. Navigieren Sie zu **Einstellungen** → **Add-ons** → **Add-on Store**
3. Klicken Sie auf die drei Punkte (⋮) oben rechts
4. Wählen Sie **Repositories**
5. Fügen Sie diese URL hinzu:
   ```
   https://github.com/MrTir1995/Viessmann-HA-Addon
   ```
6. Klicken Sie auf **Hinzufügen**

### Schritt 2: Add-on installieren

1. Aktualisieren Sie die Add-on Store Seite
2. Suchen Sie nach "Viessmann Decoder"
3. Klicken Sie auf das Add-on
4. Klicken Sie auf **INSTALLIEREN**
5. Warten Sie, bis die Installation abgeschlossen ist

### Schritt 3: Konfiguration

1. Navigieren Sie zum **Konfiguration**-Tab
2. Konfigurieren Sie Ihre Einstellungen:
   ```yaml
   serial_port: /dev/ttyUSB0
   baud_rate: 9600
   protocol: vbus
   serial_config: 8N1
   log_level: info
   ```

### Schritt 4: Starten

1. Klicken Sie auf **START**
2. (Optional) Aktivieren Sie **Start beim Booten**
3. (Optional) Aktivieren Sie **Watchdog**

## Unterstützte Protokolle

- **VBUS** (RESOL) - Standard für Viessmann Solar
- **KW** (KW-Bus) - Viessmann Heizungssteuerung
- **P300** - Viessmann P300 Protocol
- **KM** (KM-Bus) - Viessmann Kommunikationsbus

## Unterstützte Architekturen

| Architektur | Unterstützt | Beispiel Hardware            |
| ----------- | ----------- | ---------------------------- |
| amd64       | ✅          | Generic x86-64 PC, Intel NUC |
| aarch64     | ✅          | Raspberry Pi 3/4/5 (64-bit)  |
| armhf       | ✅          | Raspberry Pi (32-bit)        |
| armv7       | ✅          | Raspberry Pi 2/3             |
| i386        | ✅          | Ältere x86 PCs               |

## Konfigurationsoptionen

| Option          | Typ    | Standard       | Beschreibung                                          |
| --------------- | ------ | -------------- | ----------------------------------------------------- |
| `serial_port`   | string | `/dev/ttyUSB0` | Serieller Port für die Verbindung                     |
| `baud_rate`     | list   | `9600`         | Baudrate (2400, 4800, 9600, 19200, 38400, 115200)     |
| `protocol`      | list   | `vbus`         | Protokoll-Typ (vbus, kw, p300, km)                    |
| `serial_config` | list   | `8N1`          | Serielle Konfiguration (8N1, 8E2)                     |
| `log_level`     | string | `info`         | Log-Level (trace, debug, info, warning, error, fatal) |

## Web-Interface

Das Add-on bietet ein Web-Interface auf Port 8099, das über das Home Assistant Ingress-System zugänglich ist.

- **Status-Übersicht**: Aktuelle Temperaturen, Pumpen, Relays
- **Konfiguration**: Echtzeit-Konfigurationsänderungen
- **Statistiken**: Betriebsstunden, Wärmemenge
- **Diagnose**: Fehlermasken, Systemzeit

## Hardware-Anforderungen

- USB-zu-Serial-Adapter oder integrierter Serial-Port
- Verbindung zum Viessmann-Heizungssystem
- Kompatible Viessmann-Steuerung

## Support & Dokumentation

- **GitHub Repository**: https://github.com/MrTir1995/Viessmann-HA-Addon
- **Issues**: https://github.com/MrTir1995/Viessmann-HA-Addon/issues
- **Dokumentation**: Siehe `/doc` Verzeichnis im Repository

## Lizenz

MIT License - siehe LICENSE Datei

## Changelog

Siehe [CHANGELOG.md](CHANGELOG.md) für Details zu Updates und Änderungen.
