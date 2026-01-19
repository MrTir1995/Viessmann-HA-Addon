# Docker Hub Veröffentlichung

Dieses Addon wird automatisch auf Docker Hub veröffentlicht und unterstützt alle Home Assistant Architekturen.

## Unterstützte Architekturen

Das Addon ist für folgende Architekturen verfügbar:
- `amd64` - x86-64 (Intel/AMD 64-bit)
- `aarch64` - ARM 64-bit (z.B. Raspberry Pi 4, 5)
- `armv7` - ARM 32-bit v7 (z.B. Raspberry Pi 3)
- `armhf` - ARM 32-bit hard-float (z.B. Raspberry Pi 2)
- `i386` - x86 32-bit (Legacy Intel/AMD)

## Docker Hub Repository

Das Image ist verfügbar unter:
```
docker.io/mrtir071/viessmann-decoder-{arch}
```

Wobei `{arch}` durch die entsprechende Architektur ersetzt wird.

## Automatische Veröffentlichung

### Voraussetzungen

1. **Docker Hub Token erstellen:**
   - Gehe zu https://hub.docker.com/settings/security
   - Klicke auf "New Access Token"
   - Name: `github-actions` (oder beliebig)
   - Permissions: `Read, Write, Delete`
   - Token kopieren

2. **GitHub Secret hinzufügen:**
   - Gehe zu GitHub Repository Settings → Secrets and variables → Actions
   - Klicke auf "New repository secret"
   - Name: `DOCKERHUB_TOKEN`
   - Value: [Dein Docker Hub Token]
   - Secret speichern

### Veröffentlichung via Release

1. Erstelle ein neues Release auf GitHub:
   ```bash
   git tag v2.1.5
   git push origin v2.1.5
   ```

2. Der GitHub Actions Workflow startet automatisch und:
   - Baut Images für alle 5 Architekturen parallel
   - Veröffentlicht auf Docker Hub als `mrtir071/viessmann-decoder-{arch}:2.1.5`
   - Erstellt ein Multi-Arch Manifest für einfache Nutzung
   - Erstellt einen `latest` Tag

### Manuelle Veröffentlichung

Du kannst auch manuell veröffentlichen:

1. Gehe zu GitHub Actions → "Publish to Docker Hub"
2. Klicke auf "Run workflow"
3. Gib die Version ein (z.B. `2.1.5`)
4. Klicke auf "Run workflow"

## Docker Hub Manifest

Nach der Veröffentlichung sind folgende Tags verfügbar:

- `mrtir071/viessmann-decoder:latest` - Neueste Version (Multi-Arch)
- `mrtir071/viessmann-decoder:2.1.5` - Spezifische Version (Multi-Arch)
- `mrtir071/viessmann-decoder-amd64:2.1.5` - Architektur-spezifisch
- `mrtir071/viessmann-decoder-aarch64:2.1.5` - Architektur-spezifisch
- `mrtir071/viessmann-decoder-armv7:2.1.5` - Architektur-spezifisch
- `mrtir071/viessmann-decoder-armhf:2.1.5` - Architektur-spezifisch
- `mrtir071/viessmann-decoder-i386:2.1.5` - Architektur-spezifisch

## Installation

Nutzer können das Addon direkt über Home Assistant installieren. Das richtige Image wird automatisch basierend auf der System-Architektur ausgewählt.

## Lokales Testen

Um ein Image lokal für eine bestimmte Architektur zu bauen:

```bash
cd viessmann-decoder

# Für AMD64
docker build --platform linux/amd64 -t mrtir071/viessmann-decoder-amd64:test .

# Für ARM64
docker build --platform linux/arm64 -t mrtir071/viessmann-decoder-aarch64:test .

# Für ARMv7
docker build --platform linux/arm/v7 -t mrtir071/viessmann-decoder-armv7:test .
```

## Troubleshooting

### Build schlägt fehl
- Prüfe, ob das Docker Hub Token korrekt als GitHub Secret hinterlegt ist
- Prüfe, ob der Token noch gültig ist
- Prüfe die Build-Logs in GitHub Actions

### Image nicht verfügbar
- Warte einige Minuten, bis der Build abgeschlossen ist
- Prüfe auf Docker Hub, ob das Image existiert: https://hub.docker.com/r/mrtir071/viessmann-decoder-amd64

### Falsche Architektur
- Home Assistant wählt automatisch die richtige Architektur
- Prüfe in der [config.yaml](viessmann-decoder/config.yaml), ob die `image:` Zeile korrekt ist

## Version aktualisieren

Bei einer neuen Version müssen folgende Dateien aktualisiert werden:

1. [viessmann-decoder/config.yaml](viessmann-decoder/config.yaml) - `version:` Feld
2. [viessmann-decoder/build.json](viessmann-decoder/build.json) - `io.hass.version` Label
3. Tag erstellen und pushen

## Repository-Struktur

```
.github/
  workflows/
    docker-hub-publish.yml    # Veröffentlichung auf Docker Hub
    builder.yml               # Test-Builds (GHCR)
    publish.yml               # Veröffentlichung auf GHCR
```
