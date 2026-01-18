# Changelog

All notable changes to the Viessmann Decoder Home Assistant Add-on will be documented in this file.

## [2.1.3] - 2026-01-18

### Fixed
- Fixed immediate container failure on startup by updating service script shebang for s6-overlay v3 compatibility
  - Changed shebang from `#!/usr/bin/with-contenv bashio` to `#!/command/with-contenv bashio`
  - Resolves watchdog restart loop issue (Rate limit exceeded error)
  - S6-Overlay v3 requires `/command/` path prefix for with-contenv binary
  - See: https://github.com/just-containers/s6-overlay/blob/master/MOVING-TO-V3.md

## [2.1.2] - 2026-01-18

### Improved
- Enhanced Docker build process to ensure proper permissions for all service scripts
  - Updated Dockerfile to use `chmod -R +x /etc/services.d/*/` for comprehensive permission setting
  - Ensures permissions are correctly applied regardless of host filesystem state
  - More robust permission handling for future service additions
  - Maintains S6-Overlay v3 compatibility with improved reliability

### Technical Details
- Changed from specific file chmod to recursive directory chmod for better maintainability
- Follows current Docker best practices for permission management (2024/2025)
- Ensures all scripts in services.d have executable permissions during build
- Prevents permission-related issues across different build environments

## [2.1.1] - 2026-01-18

### Fixed
- Fixed "[FATAL tini (7)] exec /init failed: Permission denied" error by properly configuring S6-Overlay v3 integration
  - Added `init: false` to config.yaml to prevent Docker's tini from conflicting with S6-Overlay
  - S6-Overlay v3 in Home Assistant base images requires `init: false` so it can run as PID 1
  - Service script already has proper executable permissions (755) in `/etc/services.d/viessmann-decoder/run`
  - Verified against official Home Assistant addon examples (mosquitto, mariadb, example addon)
  - See: https://developers.home-assistant.io/blog/2022/05/12/s6-overlay-base-images/

## [2.1.0] - 2026-01-17

### Added
- Initial release of Home Assistant add-on
- Web-based dashboard for monitoring heating system
- Support for VBUS, KW-Bus, P300, and KM-Bus protocols
- Real-time data display (temperatures, pumps, relays)
- Configuration through Home Assistant UI
- Docker-based deployment
- Alpine Linux for minimal resource usage
- RESTful API at `/data` endpoint for Home Assistant integration
- Status page with system information
- Automatic serial port configuration
- Multi-architecture support (armhf, armv7, aarch64, amd64, i386)

### Features
- Dashboard with auto-refreshing data (2-second interval)
- JSON API for programmatic access
- Status monitoring and error reporting
- Lightweight C++ implementation with libmicrohttpd
- Based on proven Viessmann Multi-Protocol Library

## Protocol Support

### VBUS Protocol
- Viessmann Vitosolic 200
- RESOL DeltaSol BX/BX Plus/MX
- Up to 32 temperature sensors
- Up to 32 pumps
- Up to 32 relays

### KW-Bus Protocol
- Vitotronic 100/200/300 series
- Temperature monitoring
- Status information

### P300 Protocol
- Modern Vitodens boilers
- Vitocrossal systems
- Optolink interface support

### KM-Bus Protocol
- Vitotrol remote controls
- Expansion modules
- Full status decoding
