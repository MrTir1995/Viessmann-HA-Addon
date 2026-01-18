#!/bin/bash
# Build test script for the Home Assistant addon

set -e

echo "================================"
echo "Viessmann Decoder Addon Builder"
echo "================================"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Check if we're in the right directory
if [ ! -f "viessmann-decoder/config.yaml" ]; then
    echo -e "${RED}Error: Must be run from repository root${NC}"
    exit 1
fi

# Architecture to build
ARCH=${1:-amd64}
echo "Building for architecture: ${ARCH}"

# Get base image
case "${ARCH}" in
    amd64)
        BUILD_FROM="ghcr.io/home-assistant/amd64-base:3.18"
        ;;
    aarch64)
        BUILD_FROM="ghcr.io/home-assistant/aarch64-base:3.18"
        ;;
    armv7)
        BUILD_FROM="ghcr.io/home-assistant/armv7-base:3.18"
        ;;
    armhf)
        BUILD_FROM="ghcr.io/home-assistant/armhf-base:3.18"
        ;;
    i386)
        BUILD_FROM="ghcr.io/home-assistant/i386-base:3.18"
        ;;
    *)
        echo -e "${RED}Unknown architecture: ${ARCH}${NC}"
        echo "Supported: amd64, aarch64, armv7, armhf, i386"
        exit 1
        ;;
esac

echo "Base image: ${BUILD_FROM}"
echo ""

# Build the image
echo "Building Docker image..."
docker build \
    --build-arg BUILD_FROM="${BUILD_FROM}" \
    -t viessmann-decoder:${ARCH} \
    -f viessmann-decoder/Dockerfile \
    . || {
        echo -e "${RED}Build failed!${NC}"
        exit 1
    }

echo ""
echo -e "${GREEN}✓ Build successful!${NC}"
echo ""
echo "To run the container:"
echo "  docker run -it --rm \\"
echo "    --device=/dev/ttyUSB0 \\"
echo "    -e SERIAL_PORT=/dev/ttyUSB0 \\"
echo "    -e BAUD_RATE=9600 \\"
echo "    -e PROTOCOL=vbus \\"
echo "    -e SERIAL_CONFIG=8N1 \\"
echo "    -p 8099:8099 \\"
echo "    viessmann-decoder:${ARCH}"
echo ""
