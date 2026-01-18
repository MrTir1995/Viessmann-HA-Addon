#!/usr/bin/env bashio

# Set log level from configuration (check both 'log_level' and 'log' for compatibility)
if bashio::config.has_value 'log_level'; then
    bashio::log.level "$(bashio::config 'log_level')"
elif bashio::config.has_value 'log'; then
    bashio::log.level "$(bashio::config 'log')"
fi

# Read configuration from Home Assistant
CONFIG_FILE="/data/options.json"
SERIAL_PORT=$(bashio::config 'serial_port')
BAUD_RATE=$(bashio::config 'baud_rate')
PROTOCOL=$(bashio::config 'protocol')
SERIAL_CONFIG=$(bashio::config 'serial_config')

# Set defaults for missing configuration
if ! bashio::var.has_value "${SERIAL_PORT}"; then
    bashio::log.warning "Serial port configuration is missing, using default /dev/ttyUSB0"
    SERIAL_PORT="/dev/ttyUSB0"
fi

if ! bashio::var.has_value "${BAUD_RATE}"; then
    bashio::log.warning "Baud rate configuration is missing, using default 9600"
    BAUD_RATE="9600"
fi

if ! bashio::var.has_value "${PROTOCOL}"; then
    bashio::log.warning "Protocol configuration is missing, using default vbus"
    PROTOCOL="vbus"
fi

if ! bashio::var.has_value "${SERIAL_CONFIG}"; then
    bashio::log.warning "Serial config configuration is missing, using default 8N1"
    SERIAL_CONFIG="8N1"
fi

bashio::log.info "Starting Viessmann Decoder Webserver..."
bashio::log.info "Serial Port: ${SERIAL_PORT}"
bashio::log.info "Baud Rate: ${BAUD_RATE}"
bashio::log.info "Protocol: ${PROTOCOL}"
bashio::log.info "Serial Config: ${SERIAL_CONFIG}"

# Check serial port availability (informational only - webserver will handle reconnection)
if bashio::fs.file_exists "${SERIAL_PORT}"; then
    if exec 3<>"${SERIAL_PORT}" 2>/dev/null; then
        exec 3>&-
        bashio::log.info "Serial port ${SERIAL_PORT} is available"
    else
        bashio::log.warning "Serial port ${SERIAL_PORT} exists but cannot be opened"
        bashio::log.warning "The web interface will show 'Serial port not connected'"
    fi
else
    bashio::log.warning "Serial port ${SERIAL_PORT} not found"
    bashio::log.warning "The web interface will show 'Serial port not connected'"
    bashio::log.info "Available serial ports:"
    ls -la /dev/tty* 2>/dev/null | head -20 || bashio::log.warning "Could not list serial ports"
fi

# Log startup completion
bashio::log.info "Starting webserver..."
bashio::log.info "Webserver will be available at http://localhost:8099"
bashio::log.info "If serial port is not connected, the GUI will display 'Serial port not connected'"

# Export configuration as environment variables for webserver
export VIESSMANN_SERIAL_PORT="${SERIAL_PORT}"
export VIESSMANN_BAUD_RATE="${BAUD_RATE}"
export VIESSMANN_PROTOCOL="${PROTOCOL}"
export VIESSMANN_SERIAL_CONFIG="${SERIAL_CONFIG}"
export VIESSMANN_WEB_PORT="8099"

# Run the webserver (it will handle serial port connection/reconnection internally)
exec /usr/local/bin/viessmann_webserver \
    -p "${SERIAL_PORT}" \
    -b "${BAUD_RATE}" \
    -t "${PROTOCOL}" \
    -c "${SERIAL_CONFIG}" \
    -w 8099
