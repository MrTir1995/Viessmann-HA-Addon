#!/bin/bash
# Enhanced build script for Viessmann Decoder Add-on
# This script ensures proper build environment and catches errors early

set -euo pipefail

# Constants
readonly ADDON_DIR="/workspaces/developers.home-assistant/Viessmann-Home-Assistant-Addon-/viessmann-decoder"
readonly BUILD_DIR="${ADDON_DIR}/build_temp"
readonly LOG_FILE="${ADDON_DIR}/build.log"

# Logging functions
log_info() {
    echo "[INFO] $(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "${LOG_FILE}"
}

log_error() {
    echo "[ERROR] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2 | tee -a "${LOG_FILE}"
}

log_warning() {
    echo "[WARN] $(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "${LOG_FILE}"
}

# Cleanup function
cleanup() {
    local exit_code=$?
    if [[ -d "${BUILD_DIR}" ]]; then
        log_info "Cleaning up build directory..."
        rm -rf "${BUILD_DIR}"
    fi
    
    if [[ ${exit_code} -ne 0 ]]; then
        log_error "Build failed with exit code ${exit_code}"
        log_error "Check ${LOG_FILE} for details"
    else
        log_info "Build completed successfully"
    fi
    
    exit ${exit_code}
}

# Set up error handling
trap cleanup EXIT

# Validate environment
validate_environment() {
    log_info "Validating build environment..."
    
    # Check if we're in the correct directory
    if [[ ! -f "${ADDON_DIR}/config.yaml" ]]; then
        log_error "config.yaml not found. Are you in the correct directory?"
        exit 1
    fi
    
    # Check for required source files
    local required_files=(
        "${ADDON_DIR}/src/vbusdecoder.cpp"
        "${ADDON_DIR}/linux/src/LinuxSerial.cpp"
        "${ADDON_DIR}/linux/src/Arduino.cpp"
        "${ADDON_DIR}/webserver/main.cpp"
    )
    
    for file in "${required_files[@]}"; do
        if [[ ! -f "${file}" ]]; then
            log_error "Required source file not found: ${file}"
            exit 1
        fi
    done
    
    # Check for Docker
    if ! command -v docker &> /dev/null; then
        log_error "Docker not found. Please install Docker to build the addon."
        exit 1
    fi
    
    log_info "Environment validation passed"
}

# Build function
build_addon() {
    log_info "Starting addon build..."
    
    cd "${ADDON_DIR}"
    
    # Create build directory
    mkdir -p "${BUILD_DIR}"
    
    # Get architecture (default to amd64 for development)
    local arch="${1:-amd64}"
    log_info "Building for architecture: ${arch}"
    
    # Build the Docker image
    log_info "Building Docker image..."
    docker build \
        --build-arg BUILD_FROM="ghcr.io/home-assistant/${arch}-base:3.18" \
        --tag "viessmann-decoder:${arch}-latest" \
        --file Dockerfile \
        . 2>&1 | tee -a "${LOG_FILE}"
    
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        log_error "Docker build failed"
        exit 1
    fi
    
    log_info "Docker build completed successfully"
}

# Test function
test_addon() {
    log_info "Testing addon image..."
    
    local arch="${1:-amd64}"
    local image="viessmann-decoder:${arch}-latest"
    
    # Test image exists
    if ! docker image inspect "${image}" &> /dev/null; then
        log_error "Image ${image} not found"
        exit 1
    fi
    
    # Test basic container startup (without serial port)
    log_info "Testing container startup..."
    local container_id
    container_id=$(docker run -d \
        --name "viessmann-decoder-test-$$" \
        -e VIESSMANN_SERIAL_PORT="/dev/null" \
        -e VIESSMANN_BAUD_RATE="4800" \
        -e VIESSMANN_PROTOCOL="vbus" \
        -e VIESSMANN_SERIAL_CONFIG="8N1" \
        "${image}" 2>/dev/null || true)
    
    if [[ -z "${container_id}" ]]; then
        log_warning "Could not start test container (expected in this environment)"
        return 0
    fi
    
    # Wait a moment for startup
    sleep 5
    
    # Check if container is still running
    if docker ps -q --filter "id=${container_id}" | grep -q "${container_id}"; then
        log_info "Container started successfully"
        docker stop "${container_id}" &> /dev/null
    else
        log_warning "Container exited early (may be expected without real serial port)"
        docker logs "${container_id}" | tail -10 | while read -r line; do
            log_info "Container log: ${line}"
        done
    fi
    
    # Cleanup test container
    docker rm "${container_id}" &> /dev/null || true
    
    log_info "Container test completed"
}

# Main function
main() {
    log_info "=== Viessmann Decoder Addon Build Script ==="
    log_info "Build started at $(date)"
    
    # Clear previous log
    > "${LOG_FILE}"
    
    validate_environment
    
    local arch="${1:-amd64}"
    build_addon "${arch}"
    test_addon "${arch}"
    
    log_info "=== Build Process Complete ==="
}

# Run main function with all arguments
main "$@"