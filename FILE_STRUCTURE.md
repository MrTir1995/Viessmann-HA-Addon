# Viessmann Decoder - File Structure for GitHub Addon Store

Diese Übersicht zeigt alle relevanten Dateien, die für die Veröffentlichung über den GitHub Addon Store benötigt werden.

## ✅ Erforderliche Dateien (Alle vorhanden)

### Root-Verzeichnis

```
/
├── .github/
│   └── workflows/
│       ├── publish.yml          ✅ Automatischer Build bei Release
│       ├── builder.yml          ✅ Test-Builds bei Commits/PRs
│       └── test.yml             ✅ Vorhandener Test-Workflow
│
├── viessmann-decoder/           ✅ Addon-Verzeichnis
│   ├── config.yaml              ✅ Addon-Konfiguration (v2.1.3)
│   ├── build.json               ✅ Build-Konfiguration für alle Architekturen
│   ├── Dockerfile               ✅ Multi-Arch Build Unterstützung
│   ├── INSTALL.md               ✅ Installations-Anleitung
│   ├── README.md                ✅ Addon-Dokumentation
│   ├── CHANGELOG.md             ✅ Versions-Historie
│   ├── run.sh                   ✅ Start-Skript (veraltet, s6 wird verwendet)
│   ├── rootfs/                  ✅ Container-Dateisystem
│   │   └── etc/
│   │       ├── s6-overlay/      ✅ Init-System (s6-overlay v3)
│   │       │   └── s6-rc.d/
│   │       │       └── viessmann-decoder/
│   │       │           ├── run  ✅ Service-Script
│   │       │           └── type ✅ Service-Typ
│   │       └── services.d/      ✅ Legacy-Service-Support
│   │           └── viessmann-decoder/
│   │               └── run      ✅ Fallback-Script
│   ├── src/                     ✅ Quellcode
│   ├── linux/                   ✅ Linux-Abstraktion
│   ├── webserver/               ✅ Web-Interface
│   └── translations/            ✅ Übersetzungen
│
├── repository.json              ✅ Repository-Metadaten
├── repository.yaml              ✅ Repository-Metadaten (Alternative)
├── README.md                    ✅ Haupt-Dokumentation mit Installation-Button
├── CHANGELOG.md                 ✅ Versions-Historie
├── LICENSE                      ✅ MIT-Lizenz
├── GHCR_SETUP.md                ✅ GitHub Container Registry Setup-Anleitung
├── RELEASE_CHECKLIST.md         ✅ Release-Prozess Checkliste
└── .gitignore                   ✅ Git Ignore-Datei
```

## 📋 Architektur-Unterstützung

Alle 5 Architekturen sind vollständig konfiguriert:

| Architektur | Base-Image                               | Status |
| ----------- | ---------------------------------------- | ------ |
| amd64       | ghcr.io/home-assistant/amd64-base:3.18   | ✅     |
| aarch64     | ghcr.io/home-assistant/aarch64-base:3.18 | ✅     |
| armhf       | ghcr.io/home-assistant/armhf-base:3.18   | ✅     |
| armv7       | ghcr.io/home-assistant/armv7-base:3.18   | ✅     |
| i386        | ghcr.io/home-assistant/i386-base:3.18    | ✅     |

## 🔄 GitHub Actions Workflows

### 1. **publish.yml** (Release)

- Trigger: Release veröffentlicht oder manuell
- Action: Baut und veröffentlicht alle Architekturen
- Target: `ghcr.io/mrtir1995/viessmann-decoder-{arch}:2.1.3`

### 2. **builder.yml** (Test)

- Trigger: Push zu main oder Pull Request
- Action: Test-Build für alle Architekturen
- Matrix: Paralleler Build aller 5 Architekturen

### 3. **test.yml** (Vorhanden)

- Vorhandener Test-Workflow

## 📦 Veröffentlichungs-Prozess

### Schritt 1: Code committen

```bash
git add .
git commit -m "Release v2.1.3"
git push origin main
```

### Schritt 2: Tag erstellen

```bash
git tag -a v2.1.3 -m "Release version 2.1.3"
git push origin v2.1.3
```

### Schritt 3: GitHub Release erstellen

- Gehen Sie zu Releases auf GitHub
- "Create a new release"
- Tag: v2.1.3
- Publish → Automatischer Build startet!

### Schritt 4: Packages öffentlich machen

Nach dem ersten Build:

1. Gehen Sie zu GitHub Packages
2. Für jede Architektur: Package settings → Change visibility → Public

## 🌍 Addon Store Integration

### Repository URL für Benutzer:

```
https://github.com/MrTir1995/Viessmann-HA-Addon
```

### Installation für Endbenutzer:

1. Click on button in README
2. Oder manuell: Settings → Add-ons → Add-on Store → ⋮ → Repositories
3. URL hinzufügen
4. "Viessmann Decoder" installieren

## 🔧 Wichtige Konfigurationen

### config.yaml

- ✅ Version: 2.1.3
- ✅ Image: `ghcr.io/mrtir1995/viessmann-decoder-{arch}`
- ✅ Alle 5 Architekturen aufgelistet
- ✅ Ingress aktiviert (Port 8099)

### build.json

- ✅ Alle Base-Images definiert
- ✅ Labels korrekt gesetzt
- ✅ Version: 2.1.3

### Dockerfile

- ✅ Multi-Stage Build
- ✅ Version: 2.1.3
- ✅ Optimiert für alle Architekturen

## ✨ Neue Dateien für Store-Integration

1. **INSTALL.md** - Schritt-für-Schritt Installation vom Store
2. **GHCR_SETUP.md** - GitHub Container Registry Setup
3. **RELEASE_CHECKLIST.md** - Checkliste für neue Releases
4. **.gitignore** - Git Ignore-Regeln
5. **builder.yml** - Matrix-Build für alle Architekturen

## 🚀 Nächste Schritte

1. ✅ Alle Dateien sind erstellt und konfiguriert
2. ⏭️ Code committen und pushen
3. ⏭️ Release v2.1.3 erstellen
4. ⏭️ Warten auf automatischen Build
5. ⏭️ Packages auf "Public" setzen
6. ✅ Addon ist verfügbar über Store!

## 📝 Wartung

Bei zukünftigen Updates:

1. Versionen in allen 3 Dateien aktualisieren (config.yaml, build.json, Dockerfile)
2. CHANGELOG.md aktualisieren
3. Commit, Tag, Release erstellen
4. Automatischer Build durch GitHub Actions

## 🔗 Referenzen

- [Home Assistant Builder](https://github.com/home-assistant/builder)
- [GHCR Documentation](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry)
- [Add-on Documentation](https://developers.home-assistant.io/docs/add-ons/)
