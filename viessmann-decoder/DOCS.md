# Viessmann Decoder Add-on

Überwachen Sie Ihre Viessmann-Heizungsanlage aus Home Assistant

![Supports aarch64 Architecture][aarch64-shield]
![Supports amd64 Architecture][amd64-shield]
![Supports armhf Architecture][armhf-shield]
![Supports armv7 Architecture][armv7-shield]
![Supports i386 Architecture][i386-shield]

## Über das Add-on

Dieses Add-on ermöglicht die Überwachung und Datenerfassung von Viessmann-Heizungssystemen unter Verwendung verschiedener Protokolle (VBUS, KW-Bus, P300, KM-Bus). Es bietet eine saubere Web-Oberfläche und REST API für die Integration mit Home Assistant.

### Features

- 🌡️ Echtzeit-Temperaturüberwachung
- 🔄 Pumpenstatus und Leistungsstufen
- 💡 Relaiszustände
- 📊 Live-Dashboard mit automatischer Aktualisierung
- 🔌 Mehrere Protokoll-Unterstützung
- 🐳 Docker-basiert für einfache Bereitstellung
- 📱 Responsive Web-Oberfläche

## Installation

1. Navigieren Sie zu **Supervisor > Add-on Store** in Home Assistant
2. Klicken Sie auf die drei Punkte in der oberen rechten Ecke
3. Wählen Sie **Repositories**
4. Fügen Sie diese Repository-URL hinzu: `https://github.com/MrTir1995/Viessmann-HA-Addon`
5. Klicken Sie auf **Hinzufügen**, dann schließen
6. Suchen Sie "Viessmann Decoder" in der Add-on-Liste und klicken Sie auf **Installieren**

## Konfiguration

### Optionen

| Option          | Beschreibung                      | Standard       |
| --------------- | --------------------------------- | -------------- |
| `serial_port`   | Serieller Port für die Verbindung | `/dev/ttyUSB0` |
| `baud_rate`     | Kommunikationsgeschwindigkeit     | `9600`         |
| `protocol`      | Protokolltyp                      | `vbus`         |
| `serial_config` | Serielle Konfiguration            | `8N1`          |
| `log_level`     | Log-Detailstufe                   | `info`         |
| `usbip_enable`  | USB/IP Remote-Zugriff             | `false`        |
| `usbip_host`    | USB/IP Server IP                  | -              |
| `usbip_port`    | USB/IP Server Port                | `3240`         |
| `usbip_busid`   | USB Bus ID                        | -              |

### Protokoll-Optionen

- **vbus**: RESOL VBUS-Protokoll (am häufigsten)
- **kw**: KW-Bus (VS1)-Protokoll
- **p300**: P300 (VS2/Optolink)-Protokoll
- **km**: KM-Bus-Protokoll

### Baudrate-Optionen

- 2400, 4800, 9600 (Standard), 19200, 38400, 115200

### Serielle Konfiguration

- **8N1**: 8 Datenbits, keine Parität, 1 Stoppbit (Standard)
- **8E2**: 8 Datenbits, gerade Parität, 2 Stoppbits

## Hardware-Einrichtung

### Erforderliche Hardware

1. USB-zu-TTL-Adapter (3,3 V oder 5 V je nach Viessmann-Steuerung)
2. Verbindungskabel zur Viessmann-Bus-Schnittstelle

### Verkabelung

Verbinden Sie Ihren USB-Adapter mit dem entsprechenden Bus-Port an Ihrer Viessmann-Steuerung:

- GND → Masse
- RX → Datenleitung (TX am Adapter)
- TX → Datenleitung (RX am Adapter)

**Hinweis:** Überprüfen Sie die Dokumentation Ihrer spezifischen Viessmann-Steuerung für genaue Pinbelegungen.

## USB/IP Remote-Zugriff

Das Add-on unterstützt USB-Geräte über das Netzwerk via USB/IP. Dies ermöglicht es, den USB-Serial-Adapter an einem anderen Rechner im Netzwerk anzuschließen.

### Server-Einrichtung

Auf dem Rechner mit dem USB-Adapter:

```bash
# USB/IP installieren
sudo apt-get install usbip

# Server starten
sudo modprobe usbip-host
sudo usbipd -D

# USB-Geräte auflisten
usbip list -l

# Gerät freigeben
sudo usbip bind -b 1-1.3
```

### Add-on-Konfiguration

```yaml
usbip_enable: true
usbip_host: "192.168.1.100"
usbip_port: 3240
usbip_busid: "1-1.3"
```

## Web-Oberfläche

Greifen Sie auf die Web-Oberfläche zu unter: `http://homeassistant.local:8099`

Oder verwenden Sie das **Ingress**-Panel direkt in Home Assistant.

### API-Endpunkte

| Endpunkt    | Beschreibung        |
| ----------- | ------------------- |
| `/`         | Haupt-Dashboard     |
| `/data`     | JSON-Datenendpunkt  |
| `/health`   | Gesundheitsprüfung  |
| `/settings` | Konfigurationsseite |
| `/devices`  | Geräteverwaltung    |
| `/status`   | Systemstatus        |

### Beispiel Datenantwort

```json
{
  "serialConnected": true,
  "compatible": true,
  "serialPort": "/dev/ttyUSB0",
  "ready": true,
  "status": "OK",
  "protocol": 0,
  "temperatures": [45.2, 38.1, 22.5],
  "pumps": [100, 0],
  "relays": [true, false, false]
}
```

## Troubleshooting

### Serial Port Not Found

- Verify the USB adapter is connected
- Check the serial port name: `ls /dev/ttyUSB*`
- Ensure the correct port is configured

### No Data Received

- Verify the protocol setting matches your Viessmann system
- Check baud rate settings
- Verify wiring connections

### Permission Denied

- The add-on requires `uart: true` and `privileged: SYS_RAWIO`
- These are configured automatically in the add-on

## Support

For issues and feature requests, visit:
<https://github.com/MrTir1995/Viessmann-HA-Addon/issues>

[aarch64-shield]: https://img.shields.io/badge/aarch64-yes-green.svg
[amd64-shield]: https://img.shields.io/badge/amd64-yes-green.svg
[armhf-shield]: https://img.shields.io/badge/armhf-yes-green.svg
[armv7-shield]: https://img.shields.io/badge/armv7-yes-green.svg
[i386-shield]: https://img.shields.io/badge/i386-yes-green.svg
