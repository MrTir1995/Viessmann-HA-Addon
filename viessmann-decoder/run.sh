#!/bin/bash
# Viessmann Decoder Add-on Startup Script
# Direct execution under tini (no s6-overlay, no bashio dependency)

set -e

# Simple logging functions
log_info() { echo "[INFO] $*"; }
log_warning() { echo "[WARNING] $*"; }
log_error() { echo "[ERROR] $*"; }

log_info "======================================"
log_info " Viessmann Decoder Add-on v2.1.9"
log_info "======================================"

# Read configuration from options.json
CONFIG_FILE="/data/options.json"

if [[ -f "${CONFIG_FILE}" ]]; then
    SERIAL_PORT=$(jq -r '.serial_port // "/dev/ttyUSB0"' "${CONFIG_FILE}")
    BAUD_RATE=$(jq -r '.baud_rate // 9600' "${CONFIG_FILE}")
    PROTOCOL=$(jq -r '.protocol // "vbus"' "${CONFIG_FILE}")
    SERIAL_CONFIG=$(jq -r '.serial_config // "8N1"' "${CONFIG_FILE}")
    LOG_LEVEL=$(jq -r '.log_level // "info"' "${CONFIG_FILE}")
    USBIP_ENABLE=$(jq -r '.usbip_enable // false' "${CONFIG_FILE}")
    USBIP_HOST=$(jq -r '.usbip_host // ""' "${CONFIG_FILE}")
    USBIP_BUSID=$(jq -r '.usbip_busid // ""' "${CONFIG_FILE}")
    USBIP_PORT=$(jq -r '.usbip_port // 3240' "${CONFIG_FILE}")
else
    log_warning "Config file not found, using defaults"
    SERIAL_PORT="/dev/ttyUSB0"
    BAUD_RATE="9600"
    PROTOCOL="vbus"
    SERIAL_CONFIG="8N1"
    LOG_LEVEL="info"
    USBIP_ENABLE="false"
    USBIP_HOST=""
    USBIP_BUSID=""
    USBIP_PORT="3240"
fi

log_info "Configuration:"
log_info "  Serial Port: ${SERIAL_PORT}"
log_info "  Baud Rate: ${BAUD_RATE}"
log_info "  Protocol: ${PROTOCOL}"
log_info "  Serial Config: ${SERIAL_CONFIG}"
log_info "  Log Level: ${LOG_LEVEL}"
log_info "  USB/IP Enabled: ${USBIP_ENABLE}"
log_info "  USB/IP Host: ${USBIP_HOST}"
log_info "  USB/IP Bus ID: ${USBIP_BUSID}"
log_info "  USB/IP Port: ${USBIP_PORT}"

if [[ "${USBIP_ENABLE}" == "true" ]]; then
    log_info "Initializing USB/IP client..."

    if [[ -z "${USBIP_HOST}" ]]; then
        log_warning "USB/IP host is empty. Skipping USB/IP attach."
    elif [[ -z "${USBIP_BUSID}" ]]; then
        log_warning "USB/IP busid is empty. Skipping USB/IP attach."
        log_info "USB/IP Configuration:"
        log_info "  Host: ${USBIP_HOST}:${USBIP_PORT}"
        log_info "  BusID: (not configured)"
    else
        log_info "Attempting USB/IP connection..."
        log_info "  Host: ${USBIP_HOST}:${USBIP_PORT}"
        log_info "  BusID: ${USBIP_BUSID}"

        # Try socat first (more reliable)
        if command -v socat >/dev/null 2>&1; then
            log_info "Using socat for USB/IP forwarding"
            socat UNIX-LISTEN:/dev/shm/usbip_${USBIP_BUSID}.sock TCP:${USBIP_HOST}:${USBIP_PORT} &
            sleep 2
        elif command -v nc >/dev/null 2>&1; then
            log_warning "socat not available, using nc (netcat)"
            nc -k -l -p 3241 -c "nc ${USBIP_HOST} ${USBIP_PORT}" &
            sleep 2
        else
            log_warning "Neither socat nor nc found. USB/IP forwarding unavailable."
            log_warning "Install socat or netcat to enable USB/IP support."
        fi
    fi
fi

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
