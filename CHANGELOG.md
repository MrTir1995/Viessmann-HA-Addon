# Changelog - Viessmann Home Assistant Addon Repository

All notable changes to this repository and its add-ons will be documented in this file.

## [Unreleased]

## [2.1.5] - 2026-01-19

### Fixed
- Fixed critical S6-Overlay v3 permissions issue - added execute permissions to service run script
- Updated AppArmor profile to include supervisor socket access

## [2.1.1] - 2026-01-18

### Changed
- Updated Viessmann Decoder addon to version 2.1.1
- Fixed S6-Overlay v3 compatibility by adding `init: false` to config.yaml

### Fixed
- Resolved "[FATAL tini (7)] exec /init failed: Permission denied" error
- Corrected service script permissions for S6-Overlay v3

## [2.1.0] - 2026-01-17

### Added
- Initial public release of Viessmann Decoder addon
- Multi-protocol support (VBUS, KW-Bus, P300, KM-Bus)
- Web-based dashboard for monitoring
- RESTful API for Home Assistant integration
- Multi-architecture support (armhf, armv7, aarch64, amd64, i386)
