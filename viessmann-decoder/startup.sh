#!/command/with-contenv bashio
# Viessmann Decoder Startup Script with enhanced error handling

set -e

# Set up logging
exec 1> >(bashio::log.info)
exec 2> >(bashio::log.error)

bashio::log.info "Starting Viessmann Decoder Add-on..."

# Function to validate configuration
validate_config() {
    local errors=0
    
    # Check required configuration
    if ! bashio::config.exists 'serial_port'; then
        bashio::log.error "Serial port configuration is required"
        errors=$((errors + 1))
    fi
    
    if ! bashio::config.exists 'baud_rate'; then
        bashio::log.error "Baud rate configuration is required"  
        errors=$((errors + 1))
    fi
    
    if ! bashio::config.exists 'protocol'; then
        bashio::log.error "Protocol configuration is required"
        errors=$((errors + 1))
    fi
    
    if ! bashio::config.exists 'serial_config'; then
        bashio::log.error "Serial config configuration is required"
        errors=$((errors + 1))
    fi
    
    return $errors
}

# Function to check system requirements
check_requirements() {
    bashio::log.info "Checking system requirements..."
    
    # Check if webserver binary exists and is executable
    if [[ ! -x "/usr/local/bin/viessmann_webserver" ]]; then
        bashio::log.fatal "Viessmann webserver binary not found or not executable"
        exit 1
    fi
    
    # Check for required libraries
    if ! ldd /usr/local/bin/viessmann_webserver | grep -q libmicrohttpd; then
        bashio::log.fatal "Required library libmicrohttpd not found"
        exit 1
    fi
    
    bashio::log.info "System requirements check passed"
}

# Function to setup environment
setup_environment() {
    bashio::log.info "Setting up environment..."
    
    # Set log level
    if bashio::config.has_value 'log_level'; then
        bashio::log.level "$(bashio::config 'log_level')"
    fi
    
    # Create data directory if it doesn't exist
    if [[ ! -d "/data" ]]; then
        mkdir -p /data
        bashio::log.info "Created /data directory"
    fi
    
    # Set up timezone if available
    if [[ -f "/etc/timezone" ]]; then
        export TZ="$(cat /etc/timezone)"
        bashio::log.info "Timezone set to: $TZ"
    fi
}

# Main startup sequence
main() {
    bashio::log.info "==================================="
    bashio::log.info "Viessmann Decoder Add-on Starting"
    bashio::log.info "==================================="
    
    # Validate configuration first
    if ! validate_config; then
        bashio::log.fatal "Configuration validation failed"
        exit 1
    fi
    
    # Check system requirements
    check_requirements
    
    # Setup environment
    setup_environment
    
    # Read configuration
    SERIAL_PORT=$(bashio::config 'serial_port')
    BAUD_RATE=$(bashio::config 'baud_rate')
    PROTOCOL=$(bashio::config 'protocol')
    SERIAL_CONFIG=$(bashio::config 'serial_config')
    
    bashio::log.info "Configuration:"
    bashio::log.info "  Serial Port: ${SERIAL_PORT}"
    bashio::log.info "  Baud Rate: ${BAUD_RATE}"
    bashio::log.info "  Protocol: ${PROTOCOL}"
    bashio::log.info "  Serial Config: ${SERIAL_CONFIG}"
    
    # Check serial port availability (informational)
    if bashio::fs.file_exists "${SERIAL_PORT}"; then
        bashio::log.info "Serial port ${SERIAL_PORT} is available"
        
        # Test if we can access the port
        if [[ -r "${SERIAL_PORT}" ]] && [[ -w "${SERIAL_PORT}" ]]; then
            bashio::log.info "Serial port permissions OK"
        else
            bashio::log.warning "Serial port exists but may not be accessible"
        fi
    else
        bashio::log.warning "Serial port ${SERIAL_PORT} not found"
        bashio::log.warning "Available serial devices:"
        find /dev -name "tty*" -type c 2>/dev/null | head -10 | while read -r port; do
            bashio::log.warning "  $port"
        done
    fi
    
    # Export environment variables
    export VIESSMANN_SERIAL_PORT="${SERIAL_PORT}"
    export VIESSMANN_BAUD_RATE="${BAUD_RATE}"
    export VIESSMANN_PROTOCOL="${PROTOCOL}"
    export VIESSMANN_SERIAL_CONFIG="${SERIAL_CONFIG}"
    export VIESSMANN_WEB_PORT="8099"
    
    bashio::log.info "Starting Viessmann webserver..."
    bashio::log.info "Web interface will be available at: http://localhost:8099"
    
    # Start the webserver with exec to handle signals properly
    exec /usr/local/bin/viessmann_webserver \
        -p "${SERIAL_PORT}" \
        -b "${BAUD_RATE}" \
        -t "${PROTOCOL}" \
        -c "${SERIAL_CONFIG}" \
        -w 8099
}

# Trap signals to ensure clean shutdown
trap 'bashio::log.info "Received shutdown signal"; exit 0' SIGTERM SIGINT

# Start main function
main "$@"