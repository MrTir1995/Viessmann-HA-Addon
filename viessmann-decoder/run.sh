#!/bin/bash
# Viessmann Decoder Add-on and standalone container startup script
# Direct execution under tini (no s6-overlay, no bashio dependency)

set -e

# Simple logging functions
log_info() { echo "[INFO] $*"; }
log_warning() { echo "[WARNING] $*"; }
log_error() { echo "[ERROR] $*"; }

log_info "======================================"
log_info " Viessmann Decoder Add-on / Standalone v2.3.1"
log_info "======================================"

# Read configuration from options.json
CONFIG_FILE="/data/options.json"

if [[ -f "${CONFIG_FILE}" ]]; then
    SERIAL_PORT=$(jq -r '.serial_port // "/dev/ttyUSB0"' "${CONFIG_FILE}")
    BAUD_RATE=$(jq -r '.baud_rate // 1200' "${CONFIG_FILE}")
    PROTOCOL=$(jq -r '.protocol // "km_remote"' "${CONFIG_FILE}")
    SERIAL_CONFIG=$(jq -r '.serial_config // "8E1"' "${CONFIG_FILE}")
    REMOTE_MODEL=$(jq -r '.remote_model // "vitotrol300"' "${CONFIG_FILE}")
    REMOTE_SLOT=$(jq -r '.remote_slot // 1' "${CONFIG_FILE}")
    INVERT_SERIAL=$(jq -r '.invert_serial // false' "${CONFIG_FILE}")
    LOG_LEVEL=$(jq -r '.log_level // "info"' "${CONFIG_FILE}")
    USBIP_ENABLE=$(jq -r '.usbip_enable // false' "${CONFIG_FILE}")
    USBIP_HOST=$(jq -r '.usbip_host // ""' "${CONFIG_FILE}")
    USBIP_BUSID=$(jq -r '.usbip_busid // ""' "${CONFIG_FILE}")
    USBIP_PORT=$(jq -r '.usbip_port // 3240' "${CONFIG_FILE}")
else
    log_info "No options.json found, using standalone environment configuration"
    SERIAL_PORT="${SERIAL_PORT:-/dev/ttyUSB0}"
    BAUD_RATE="${BAUD_RATE:-1200}"
    PROTOCOL="${PROTOCOL:-km_remote}"
    SERIAL_CONFIG="${SERIAL_CONFIG:-8E1}"
    REMOTE_MODEL="${REMOTE_MODEL:-vitotrol300}"
    REMOTE_SLOT="${REMOTE_SLOT:-1}"
    INVERT_SERIAL="${INVERT_SERIAL:-false}"
    LOG_LEVEL="${LOG_LEVEL:-info}"
    USBIP_ENABLE="${USBIP_ENABLE:-false}"
    USBIP_HOST="${USBIP_HOST:-}"
    USBIP_BUSID="${USBIP_BUSID:-}"
    USBIP_PORT="${USBIP_PORT:-3240}"
fi

UI_SETTINGS_FILE="/data/ui_settings.json"
if [[ -f "${UI_SETTINGS_FILE}" ]]; then
    SERIAL_PORT=$(jq -r --arg default "${SERIAL_PORT}" '.serial_port // $default' "${UI_SETTINGS_FILE}")
    BAUD_RATE=$(jq -r --arg default "${BAUD_RATE}" '.baud_rate // $default' "${UI_SETTINGS_FILE}")
    PROTOCOL=$(jq -r --arg default "${PROTOCOL}" '.protocol // $default' "${UI_SETTINGS_FILE}")
    SERIAL_CONFIG=$(jq -r --arg default "${SERIAL_CONFIG}" '.serial_config // $default' "${UI_SETTINGS_FILE}")
    REMOTE_MODEL=$(jq -r --arg default "${REMOTE_MODEL}" '.remote_model // $default' "${UI_SETTINGS_FILE}")
    REMOTE_SLOT=$(jq -r --arg default "${REMOTE_SLOT}" '.remote_slot // $default' "${UI_SETTINGS_FILE}")
    INVERT_SERIAL=$(jq -r --arg default "${INVERT_SERIAL}" 'if (.invert_serial | type) == "boolean" then .invert_serial else ($default == "true") end' "${UI_SETTINGS_FILE}")
    LOG_LEVEL=$(jq -r --arg default "${LOG_LEVEL}" '.log_level // $default' "${UI_SETTINGS_FILE}")
    USBIP_ENABLE=$(jq -r --argjson default "${USBIP_ENABLE}" 'if (.usbip_enable | type) == "boolean" then .usbip_enable else $default end' "${UI_SETTINGS_FILE}")
    USBIP_HOST=$(jq -r --arg default "${USBIP_HOST}" '.usbip_host // $default' "${UI_SETTINGS_FILE}")
    USBIP_BUSID=$(jq -r --arg default "${USBIP_BUSID}" '.usbip_busid // $default' "${UI_SETTINGS_FILE}")
    USBIP_PORT=$(jq -r --argjson default "${USBIP_PORT}" 'if (.usbip_port | type) == "number" then .usbip_port else $default end' "${UI_SETTINGS_FILE}")
fi

case "${PROTOCOL}" in
    vbus|kw|p300|km|km_remote) ;;
    *) log_error "Invalid PROTOCOL: ${PROTOCOL}"; exit 1 ;;
esac
case "${BAUD_RATE}" in
    1200|2400|4800|9600|19200|38400|57600|115200) ;;
    *) log_error "Invalid BAUD_RATE: ${BAUD_RATE}"; exit 1 ;;
esac
case "${SERIAL_CONFIG}" in
    8N1|8E1|8E2) ;;
    *) log_error "Invalid SERIAL_CONFIG: ${SERIAL_CONFIG}"; exit 1 ;;
esac
case "${REMOTE_MODEL}" in
    vitotrol200|vitotrol300) ;;
    *) log_error "Invalid REMOTE_MODEL: ${REMOTE_MODEL}"; exit 1 ;;
esac
case "${REMOTE_SLOT}" in
    1|2|3) ;;
    *) log_error "Invalid REMOTE_SLOT: ${REMOTE_SLOT}"; exit 1 ;;
esac
case "${INVERT_SERIAL}" in
    true|false) ;;
    *) log_error "Invalid INVERT_SERIAL: ${INVERT_SERIAL}"; exit 1 ;;
esac
case "${LOG_LEVEL}" in
    trace|debug|info|notice|warning|error|fatal) ;;
    *) log_error "Invalid LOG_LEVEL: ${LOG_LEVEL}"; exit 1 ;;
esac
case "${USBIP_ENABLE}" in
    true|false) ;;
    *) log_error "Invalid USBIP_ENABLE: ${USBIP_ENABLE}"; exit 1 ;;
esac
if ! [[ "${USBIP_PORT}" =~ ^[0-9]+$ ]] || (( USBIP_PORT < 1 || USBIP_PORT > 65535 )); then
    log_error "Invalid USBIP_PORT: ${USBIP_PORT}"
    exit 1
fi

log_info "Configuration:"
log_info "  Serial Port: ${SERIAL_PORT}"
log_info "  Baud Rate: ${BAUD_RATE}"
log_info "  Protocol: ${PROTOCOL}"
log_info "  Serial Config: ${SERIAL_CONFIG}"
log_info "  Remote Model: ${REMOTE_MODEL} (slot ${REMOTE_SLOT})"
log_info "  Invert Serial: ${INVERT_SERIAL}"
log_info "  Log Level: ${LOG_LEVEL}"
log_info "  USB/IP Enabled: ${USBIP_ENABLE}"
log_info "  USB/IP Host: ${USBIP_HOST}"
log_info "  USB/IP Bus ID: ${USBIP_BUSID}"
export LOG_LEVEL USBIP_ENABLE USBIP_HOST USBIP_BUSID USBIP_PORT
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
    -m "${REMOTE_MODEL}" \
    -s "${REMOTE_SLOT}" \
    -i "${INVERT_SERIAL}" \
    -w 8099
