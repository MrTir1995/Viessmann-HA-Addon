# Changelog - Viessmann Home Assistant Addon Repository

All notable changes to this repository and its add-ons will be documented in this file.

## [2.3.0] - 2026-10-09

### Added
- Experimental `km_remote` slave mode with Vitotrol 200/300 profiles and heating-circuit slots 1–3.
- Master-driven KM-Bus frame handling with length and CRC-16/Kermit validation, register access, and queued datasets.
- Remote web interface at `/remote` and GET/POST API at `/api/remote` for room temperature, setpoint, and basic operating modes.
- Optional GitHub release publication after successful Docker Hub builds for all five architectures.

### Fixed
- Initialized KM-Bus remote communication with fixed 1200 baud and 8E1 settings.
- Validate release versions against add-on metadata before publishing Docker images.

### Changed
- Synchronized add-on version and image labels to 2.3.0.
- Revised README with experimental KM-Bus limitations, configuration, REST integration, and Home Assistant update instructions.

### Notes
- KM-Bus remote emulation is experimental; compatibility with Vitotronic 200 KM1 and Linux response timing must be verified on the target hardware.
- Includes all changes from 2.2.1, including hardened VBUS parsing, CRC/frame-boundary fixes, synchronized decoder access, and KM-Bus compatibility polling.

## [2.2.1] - 2026-10-09

### Fixed
- Hardened VBUS frame parsing against incomplete reads and oversized frames.
- Corrected frame-boundary and CRC handling to preserve queued datagrams.
- Serialized decoder access in the webserver and avoided recursive locking during KM-Bus polling.
- Added KM-Bus status polling during compatibility checks.

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
