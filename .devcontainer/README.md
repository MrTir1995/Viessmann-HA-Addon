# Development Container Setup

Dieses Repository enthält eine VS Code Dev Container Konfiguration für die Entwicklung des Viessmann Home Assistant Addons.

## Voraussetzungen

- [Docker](https://docs.docker.com/get-docker/)
- [Visual Studio Code](https://code.visualstudio.com/)
- [Remote - Containers Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

## Getting Started

1. Repository klonen
2. In VS Code öffnen
3. Wenn die Benachrichtigung erscheint, auf "Reopen in Container" klicken
   - Alternativ: Command Palette (`Ctrl+Shift+P`) → "Dev Containers: Reopen in Container"
4. Warten, bis der Container gebaut und gestartet ist

## Verwendung

### VS Code Tasks

Öffne die Command Palette (`Ctrl+Shift+P`) und wähle `Tasks: Run Task`, dann:

- **Build Addon** - Addon bauen
- **Install Addon** - Addon installieren
- **Start Addon** - Addon starten
- **Stop Addon** - Addon stoppen
- **Restart Addon** - Addon neu starten
- **View Addon Logs** - Addon Logs anzeigen
- **Check Addon Config** - Addon Konfiguration prüfen

### Manuelle Befehle

```bash
# Addon-Verwaltung
ha addons reload              # Addons neu laden
ha addons build local_viessmann_decoder
ha addons install local_viessmann_decoder
ha addons start local_viessmann_decoder
ha addons stop local_viessmann_decoder
ha addons restart local_viessmann_decoder
ha addons logs local_viessmann_decoder
ha addons info local_viessmann_decoder

# Supervisor
ha supervisor info
ha supervisor logs
```

## Zugriff

- **Home Assistant UI**: http://localhost:7123
- **Addon Port**: http://localhost:7357 (wenn im Addon konfiguriert)

## Struktur

```
.devcontainer/
  devcontainer.json          # Dev Container Konfiguration
.vscode/
  tasks.json                 # VS Code Tasks für Addon-Entwicklung
devcontainer_bootstrap       # Bootstrap-Skript für Container-Start
```

## Troubleshooting

### Container baut nicht

```bash
# Docker Cache löschen
docker system prune -a

# Container neu bauen
Ctrl+Shift+P → "Dev Containers: Rebuild Container"
```

### Addon wird nicht erkannt

```bash
# Addons neu laden
ha addons reload

# Addon-Verzeichnis prüfen
ls -la /addons/local/
```

### Port bereits belegt

Wenn Port 7123 bereits verwendet wird, passe die `appPort` in [.devcontainer/devcontainer.json](.devcontainer/devcontainer.json) an.

## Weitere Informationen

- [Home Assistant Add-on Development](https://developers.home-assistant.io/docs/add-ons)
- [VS Code Dev Containers](https://code.visualstudio.com/docs/devcontainers/containers)
