# Dokumentationsübersicht

Diese Datei bietet einen Überblick über die Dokumentationsstruktur des Viessmann Decoder Add-ons.

## 📚 Dokumentationsstruktur

### Haupt-Dokumentation

- **[README.md](README.md)** - Hauptdokumentation für Endbenutzer
  - Schnellinstallation
  - Features und unterstützte Geräte
  - Konfigurationsanleitung
  - Hardware-Einrichtung
  - Verwendung und Integration
  - Fehlerbehebung

### Add-on Dokumentation

#### Benutzer-Dokumentation

- **[viessmann-decoder/README.md](viessmann-decoder/README.md)** - Add-on Hauptdokumentation
- **[viessmann-decoder/INSTALL.md](viessmann-decoder/INSTALL.md)** - Detaillierte Installationsanleitung
- **[viessmann-decoder/DOCS.md](viessmann-decoder/DOCS.md)** - Kurzübersicht für Add-on Store

#### Entwickler-Dokumentation

- **[viessmann-decoder/DEVELOPMENT.md](viessmann-decoder/DEVELOPMENT.md)** - Entwicklerdokumentation
  - Projektstruktur
  - Build-Konfiguration
  - Lokale Entwicklung
  - Multi-Architektur Builds

### Technische Dokumentation

#### Hardware & Protokolle

- **[doc/HARDWARE_SETUP.md](doc/HARDWARE_SETUP.md)** - Hardware-Einrichtungsanleitung
- **[doc/BUS_PARTICIPANT_DISCOVERY.md](doc/BUS_PARTICIPANT_DISCOVERY.md)** - Bus-Teilnehmer Erkennung
- **[doc/VITOTRONIC_200_KW1.md](doc/VITOTRONIC_200_KW1.md)** - Spezifische Vitotronic 200 Anleitung

#### Integration & Konfiguration

- **[doc/MQTT_SETUP.md](doc/MQTT_SETUP.md)** - MQTT-Integration
- **[doc/WEBSERVER_SETUP.md](doc/WEBSERVER_SETUP.md)** - Webserver-Konfiguration
- **[doc/SCHEDULER_GUIDE.md](doc/SCHEDULER_GUIDE.md)** - Scheduler-Anleitung
- **[doc/CONTROL_COMMANDS.md](doc/CONTROL_COMMANDS.md)** - Steuerungsbefehle

### Release & Deployment

- **[CHANGELOG.md](CHANGELOG.md)** - Versionshistorie
- **[RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)** - Release-Prozess Checkliste
- **[GHCR_SETUP.md](GHCR_SETUP.md)** - GitHub Container Registry Setup

### Beispiele

- **[examples/](examples/)** - Code-Beispiele für verschiedene Szenarien
  - `vbusdecoder/` - Basis VBUS Decoder
  - `mqtt_integration/` - MQTT Integration
  - `control_commands/` - Steuerungsbefehle
  - `webserver_config/` - Webserver Konfiguration
  - `advanced_automation/` - Erweiterte Automatisierung

## 🎯 Für Endbenutzer

Wenn Sie das Add-on einfach nutzen möchten, beginnen Sie hier:

1. **[README.md](README.md)** - Allgemeine Übersicht und Schnellstart
2. **[viessmann-decoder/INSTALL.md](viessmann-decoder/INSTALL.md)** - Detaillierte Installation
3. **[doc/HARDWARE_SETUP.md](doc/HARDWARE_SETUP.md)** - Hardware anschließen
4. **[CHANGELOG.md](CHANGELOG.md)** - Neueste Änderungen und Versionen

## 🔧 Für Entwickler

Wenn Sie zur Entwicklung beitragen möchten:

1. **[viessmann-decoder/DEVELOPMENT.md](viessmann-decoder/DEVELOPMENT.md)** - Entwicklungsumgebung
2. **[RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)** - Release-Prozess
3. **[GHCR_SETUP.md](GHCR_SETUP.md)** - Container Registry Setup
4. **[examples/](examples/)** - Code-Beispiele

## 🌐 Sprachen

Die Dokumentation ist primär auf Deutsch verfasst, mit englischen Code-Kommentaren und technischen Begriffen.

## 📝 Beiträge

Beiträge zur Dokumentation sind willkommen! Bitte beachten Sie:

- Halten Sie die Dokumentation aktuell
- Verwenden Sie klare, präzise Sprache
- Fügen Sie Beispiele und Diagramme hinzu wo sinnvoll
- Testen Sie alle Anleitungen vor der Veröffentlichung

## 📄 Lizenz

Siehe [LICENSE](LICENSE) für Details.
