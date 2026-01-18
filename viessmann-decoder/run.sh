#!/bin/bash
# Viessmann Decoder Add-on Startup Script
# Direct execution under tini (no s6-overlay, no bashio dependency)

set -e

# Simple logging functions
log_info() { echo "[INFO] $*"; }
log_warning() { echo "[WARNING] $*"; }
log_error() { echo "[ERROR] $*"; }

log_info "======================================"
log_info " Viessmann Decoder Add-on v2.1.3"
log_info "======================================"

# Read configuration from options.json
CONFIG_FILE="/data/options.json"

if [[ -f "${CONFIG_FILE}" ]]; then
    SERIAL_PORT=$(jq -r '.serial_port // "/dev/ttyUSB0"' "${CONFIG_FILE}")
    BAUD_RATE=$(jq -r '.baud_rate // 9600' "${CONFIG_FILE}")
    PROTOCOL=$(jq -r '.protocol // "vbus"' "${CONFIG_FILE}")
    SERIAL_CONFIG=$(jq -r '.serial_config // "8N1"' "${CONFIG_FILE}")
    LOG_LEVEL=$(jq -r '.log_level // "info"' "${CONFIG_FILE}")
else
    log_warning "Config file not found, using defaults"
    SERIAL_PORT="/dev/ttyUSB0"
    BAUD_RATE="9600"
    PROTOCOL="vbus"
    SERIAL_CONFIG="8N1"
    LOG_LEVEL="info"
fi

log_info "Configuration:"
log_info "  Serial Port: ${SERIAL_PORT}"
log_info "  Baud Rate: ${BAUD_RATE}"
log_info "  Protocol: ${PROTOCOL}"
log_info "  Serial Config: ${SERIAL_CONFIG}"
log_info "  Log Level: ${LOG_LEVEL}"

# Check serial port availability
if [[ -e "${SERIAL_PORT}" ]]; then
    log_info "Serial port ${SERIAL_PORT} found"
else
    log_warning "Serial port ${SERIAL_PORT} not found!"
    log_warning "Available serial ports:"
    ls -la /dev/tty* 2>/dev/null | grep -E "(USB|ACM|AMA)" || log_warning "  No USB/ACM/AMA ports found"
fi

# Ensure data directory exists
mkdir -p /data

log_info "Starting Viessmann Webserver on port 8099..."

# Execute the webserver with configuration
exec /usr/local/bin/viessmann_webserver \
    -p "${SERIAL_PORT}" \
    -b "${BAUD_RATE}" \
    -t "${PROTOCOL}" \
    -c "${SERIAL_CONFIG}" \
    -w 8099
