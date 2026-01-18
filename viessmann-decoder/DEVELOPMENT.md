# Viessmann Decoder - Home Assistant Add-on

This directory contains the Home Assistant add-on for the Viessmann Multi-Protocol Library. This add-on runs as a Docker container and provides a web interface for monitoring Viessmann heating systems.

## Structure

```
viessmann-decoder/
├── config.yaml          # Add-on configuration and metadata
├── build.json           # Docker build configuration
├── Dockerfile           # Container build instructions
├── run.sh              # Entry point script
├── README.md           # User documentation
├── DOCS.md             # Brief add-on description
├── CHANGELOG.md        # Version history
├── linux/
│   ├── src/            # Linux platform implementations
│   │   ├── Arduino.cpp
│   │   ├── LinuxSerial.cpp
│   │   └── vbusdecoder.cpp
│   └── include/        # Linux platform headers
│       ├── Arduino.h
│       ├── LinuxSerial.h
│       └── vbusdecoder.h
├── src/                # Core library source
│   ├── VBUSDataLogger.cpp/.h
│   ├── VBUSMqttClient.cpp/.h
│   ├── VBUSScheduler.cpp/.h
│   └── vbusdecoder.cpp/.h
└── webserver/
    └── main.cpp        # C++ web server implementation
```

## Build Configuration

The `build.json` file specifies how Home Assistant Supervisor should build the Docker image:

```json
{
  "build_from": { ... },
  "squash": false,
  "args": {}
}
```

**Note**: The addon is self-contained with all necessary source files included in the addon directory. This ensures compatibility with Home Assistant Supervisor's build process which does not support parent directory context access.

All source files needed for building are copied from the repository root into the addon directory structure, making the addon fully portable and buildable without external dependencies.

## Building the Add-on

### Local Build

To build the add-on locally for testing:

```bash
# Navigate to the addon directory
cd viessmann-decoder

# Build for your architecture
docker build -t viessmann-decoder \
  --build-arg BUILD_FROM=ghcr.io/home-assistant/amd64-base:3.18 \
  .

# Run locally
docker run -it --rm \
  --device=/dev/ttyUSB0 \
  -e SERIAL_PORT=/dev/ttyUSB0 \
  -e BAUD_RATE=9600 \
  -e PROTOCOL=vbus \
  -e SERIAL_CONFIG=8N1 \
  -p 8099:8099 \
  viessmann-decoder
```

### Multi-Architecture Build

For publishing to multiple architectures:

```bash
# Install buildx if not already available
docker buildx create --name multiarch --use

# Navigate to the addon directory
cd viessmann-decoder

# Build for all architectures
docker buildx build \
  --platform linux/amd64,linux/arm64,linux/arm/v7 \
  -t ghcr.io/mrtir1995/viessmann-decoder:latest \
  --push .
```

## Installation in Home Assistant

### Method 1: Add Repository

1. Navigate to Settings → Add-ons → Add-on Store
2. Click the three dots (⋮) in the top right
3. Select "Repositories"
4. Add: `https://github.com/MrTir1995/Viessmann-decoder-library`
5. Find "Viessmann Decoder" in the add-on store
6. Click Install

### Method 2: Local Add-on

1. Copy the `viessmann-decoder` directory to `/addons/viessmann-decoder` on your Home Assistant system
2. Go to Settings → Add-ons → Add-on Store
3. Refresh the page
4. Find "Viessmann Decoder" under Local Add-ons
5. Click Install

## Configuration

Configure through the Home Assistant UI:

- **serial_port**: The serial device (e.g., `/dev/ttyUSB0`)
- **baud_rate**: Communication speed (9600 for VBUS, 4800 for KW/P300)
- **protocol**: Protocol type (vbus, kw, p300, km)
- **serial_config**: Serial settings (8N1 or 8E2)

## Development

### Prerequisites

- Docker
- C++ compiler (g++)
- libmicrohttpd

### Modifying the Webserver

The webserver source is in `webserver/main.cpp`. After making changes:

1. Test locally with the build command above
2. Verify functionality
3. Update version in `config.yaml`
4. Update `CHANGELOG.md`

### Testing

```bash
# Navigate to the addon directory
cd viessmann-decoder

# Build the container
docker build -t viessmann-decoder-test \
  --build-arg BUILD_FROM=ghcr.io/home-assistant/amd64-base:3.18 \
  .

# Run with test configuration
docker run -it --rm \
  --device=/dev/ttyUSB0 \
  -p 8099:8099 \
  viessmann-decoder-test
```

## Dependencies

The add-on uses:
- **Alpine Linux**: Lightweight base OS
- **libmicrohttpd**: HTTP server library
- **Viessmann Multi-Protocol Library**: Core protocol implementation

## Troubleshooting

### Build Fails

The addon is now self-contained with all source files included within the addon directory:
- `src/` - Core library source code
- `linux/src/` and `linux/include/` - Linux wrappers
- `webserver/main.cpp` - Webserver implementation

If the build fails, verify all these directories and files are present in the addon directory.

### Serial Port Access

Ensure the device is mapped in `config.yaml`:
```yaml
devices:
  - /dev/ttyUSB0
  - /dev/ttyUSB1
```

### Web Interface Not Accessible

- Check that port 8099 is not blocked
- Verify the container is running: `docker ps`
- Check logs: Settings → Add-ons → Viessmann Decoder → Log

## Contributing

Contributions are welcome! Please:
1. Test changes thoroughly
2. Update documentation
3. Follow existing code style
4. Submit pull requests to the main repository

## License

See the main repository LICENSE file.
