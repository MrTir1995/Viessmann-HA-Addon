# GitHub Container Registry Setup

Diese Anleitung zeigt, wie Sie das Viessmann Decoder Add-on über GitHub Container Registry (GHCR) veröffentlichen.

## Voraussetzungen

1. GitHub Account mit Repository
2. Aktivierte GitHub Container Registry
3. GitHub Personal Access Token (PAT) mit `write:packages` Berechtigung

## Setup-Schritte

### 1. GitHub Actions aktivieren

Die GitHub Actions Workflows sind bereits konfiguriert:

- `.github/workflows/publish.yml` - Veröffentlicht bei Releases
- `.github/workflows/builder.yml` - Testet Builds bei Commits/PRs

### 2. Container Registry Sichtbarkeit

Nach dem ersten Build müssen Sie die Sichtbarkeit des Pakets ändern:

1. Gehen Sie zu: `https://github.com/users/MrTir1995/packages`
2. Wählen Sie `viessmann-decoder-amd64` (und andere Architekturen)
3. Klicken Sie auf **Package settings**
4. Unter **Danger Zone** → **Change visibility**
5. Wählen Sie **Public**
6. Bestätigen Sie die Änderung

Wiederholen Sie dies für alle Architekturen:

- `viessmann-decoder-amd64`
- `viessmann-decoder-aarch64`
- `viessmann-decoder-armhf`
- `viessmann-decoder-armv7`
- `viessmann-decoder-i386`

### 3. Release erstellen

Um ein neues Release zu veröffentlichen:

```bash
# Tag erstellen
git tag -a v2.1.3 -m "Release version 2.1.3"
git push origin v2.1.3

# Oder über GitHub UI:
# 1. Gehen Sie zu "Releases"
# 2. Klicken Sie "Create a new release"
# 3. Tag: v2.1.3
# 4. Title: Version 2.1.3
# 5. Beschreibung hinzufügen
# 6. "Publish release" klicken
```

### 4. Automatischer Build-Prozess

Nach dem Release:

1. GitHub Actions startet automatisch
2. Baut Images für alle Architekturen
3. Pusht zu `ghcr.io/mrtir1995/viessmann-decoder-{arch}:2.1.3`
4. Erstellt auch `latest` Tag

### 5. Überprüfung

Prüfen Sie den Build-Status:

```bash
# Workflow Status ansehen
https://github.com/MrTir1995/Viessmann-HA-Addon/actions

# Images überprüfen
docker pull ghcr.io/mrtir1995/viessmann-decoder-amd64:2.1.3
```

## Manuelle Veröffentlichung

Falls Sie manuell veröffentlichen möchten:

```bash
# Login
echo $GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin

# Build
cd viessmann-decoder
docker buildx build \
  --platform linux/amd64,linux/arm64,linux/arm/v7 \
  -t ghcr.io/mrtir1995/viessmann-decoder:2.1.3 \
  -t ghcr.io/mrtir1995/viessmann-decoder:latest \
  --push \
  .
```

## Troubleshooting

### Build schlägt fehl

- Überprüfen Sie GitHub Actions Logs
- Stellen Sie sicher, dass `GITHUB_TOKEN` Berechtigung hat
- Prüfen Sie Dockerfile Syntax

### Images nicht verfügbar

- Stellen Sie sicher, dass Packages auf "Public" gesetzt sind
- Warten Sie 1-2 Minuten nach dem Build
- Überprüfen Sie die Image-URLs in `config.yaml`

### Home Assistant kann Image nicht laden

1. Prüfen Sie die Logs: `ha addons logs local_viessmann_decoder`
2. Verifizieren Sie Image-Namen in `config.yaml`
3. Stellen Sie sicher, dass die Architektur unterstützt wird
4. Prüfen Sie Netzwerkverbindung zu ghcr.io

## Version aktualisieren

Bei neuen Versionen:

1. Aktualisieren Sie Version in:
   - `viessmann-decoder/config.yaml` (version: 2.1.4)
   - `viessmann-decoder/build.json` (io.hass.version)
   - `viessmann-decoder/Dockerfile` (io.hass.version)
   - `CHANGELOG.md`

2. Commit und Push:

   ```bash
   git add .
   git commit -m "Release v2.1.4"
   git push
   ```

3. Erstellen Sie neuen Release (siehe oben)

4. GitHub Actions baut und veröffentlicht automatisch

## Best Practices

1. **Testen vor Release**: Nutzen Sie `workflow_dispatch` für Test-Builds
2. **Semantic Versioning**: v{MAJOR}.{MINOR}.{PATCH}
3. **Changelog pflegen**: Dokumentieren Sie alle Änderungen
4. **Tags synchron halten**: Alle Versionen in allen Dateien gleich
5. **Public Packages**: Vergessen Sie nicht, Packages öffentlich zu machen

## Links

- [GitHub Container Registry Docs](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry)
- [Home Assistant Builder](https://github.com/home-assistant/builder)
- [Docker Buildx](https://docs.docker.com/buildx/working-with-buildx/)
