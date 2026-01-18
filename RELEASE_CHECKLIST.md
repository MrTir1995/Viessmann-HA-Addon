# Release Checklist

Verwenden Sie diese Checkliste bei jedem neuen Release.

## Pre-Release

- [ ] Alle Tests sind erfolgreich
- [ ] Code-Review durchgeführt
- [ ] Dokumentation aktualisiert
- [ ] CHANGELOG.md aktualisiert

## Version Update

- [ ] Version in `viessmann-decoder/config.yaml` aktualisiert
- [ ] Version in `viessmann-decoder/build.json` aktualisiert
- [ ] Version in `viessmann-decoder/Dockerfile` aktualisiert
- [ ] Alle Versionen stimmen überein (z.B. 2.1.3)

## Build Test

- [ ] Lokaler Build erfolgreich: `docker build -t test .`
- [ ] Alle Architekturen getestet (optional)
- [ ] Container startet ohne Fehler
- [ ] Webserver erreichbar auf Port 8099

## Git Operations

```bash
# Änderungen committen
git add .
git commit -m "Release v2.1.3"
git push origin main

# Tag erstellen
git tag -a v2.1.3 -m "Release version 2.1.3"
git push origin v2.1.3
```

- [ ] Changes committed
- [ ] Tag erstellt und gepusht
- [ ] GitHub Release erstellt

## GitHub Release

1. Gehen Sie zu: https://github.com/MrTir1995/Viessmann-HA-Addon/releases
2. Klicken Sie "Create a new release"
3. Füllen Sie aus:
   - Tag: `v2.1.3`
   - Title: `Version 2.1.3`
   - Description: (aus CHANGELOG.md kopieren)
4. Klicken Sie "Publish release"

- [ ] GitHub Release erstellt
- [ ] Release Notes eingefügt

## Automatischer Build

- [ ] GitHub Actions Workflow gestartet
- [ ] Alle Architekturen erfolgreich gebaut:
  - [ ] amd64
  - [ ] aarch64
  - [ ] armhf
  - [ ] armv7
  - [ ] i386
- [ ] Keine Fehler in Actions Logs

## Container Registry

- [ ] Images in GHCR sichtbar
- [ ] Package Visibility auf "Public" gesetzt für alle Architekturen:
  - [ ] viessmann-decoder-amd64
  - [ ] viessmann-decoder-aarch64
  - [ ] viessmann-decoder-armhf
  - [ ] viessmann-decoder-armv7
  - [ ] viessmann-decoder-i386

## Verification

```bash
# Images testen
docker pull ghcr.io/mrtir1995/viessmann-decoder-amd64:2.1.3
docker pull ghcr.io/mrtir1995/viessmann-decoder-amd64:latest

# Container starten
docker run --rm -p 8099:8099 ghcr.io/mrtir1995/viessmann-decoder-amd64:2.1.3
```

- [ ] Images können gepullt werden
- [ ] Container startet erfolgreich
- [ ] Version korrekt angezeigt

## Home Assistant Test

- [ ] Add-on über Store installierbar
- [ ] Konfiguration funktioniert
- [ ] Add-on startet erfolgreich
- [ ] Webinterface erreichbar
- [ ] Logs zeigen keine Fehler

## Post-Release

- [ ] GitHub Issue/PR geschlossen (falls relevant)
- [ ] Community informiert (falls zutreffend)
- [ ] Dokumentation veröffentlicht

## Rollback Plan (bei Problemen)

```bash
# Tag löschen
git tag -d v2.1.3
git push origin :refs/tags/v2.1.3

# Release auf GitHub löschen
# (über GitHub UI)

# Vorherige Version wiederherstellen
docker pull ghcr.io/mrtir1995/viessmann-decoder-amd64:2.1.2
```

## Notes

- Release Datum: ****\_\_\_****
- Released von: ****\_\_\_****
- Besondere Hinweise: ****\_\_\_****
