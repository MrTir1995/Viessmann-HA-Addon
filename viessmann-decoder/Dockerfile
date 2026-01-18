ARG BUILD_FROM=alpine:3.19
FROM $BUILD_FROM

# Set labels for better container management
LABEL \
    io.hass.name="Viessmann Decoder" \
    io.hass.description="Viessmann VBUS/KW/P300/KM-Bus protocol decoder" \
    io.hass.type="addon" \
    io.hass.version="2.1.5"

# CRITICAL: Disable s6-overlay - HA Supervisor uses tini as PID 1
# Setting S6_CMD_WAIT_FOR_SERVICES=0 and using direct CMD execution
ENV S6_CMD_WAIT_FOR_SERVICES=0 \
    S6_LOGGING=0 \
    S6_KEEP_ENV=1

# Install build dependencies and runtime libraries in single layer
RUN apk add --no-cache --virtual .build-deps \
    build-base \
    g++ \
    make \
    linux-headers \
    libmicrohttpd-dev && \
    apk add --no-cache \
    libmicrohttpd \
    libstdc++ \
    libgcc \
    curl \
    bash \
    jq \
    sysfsutils-dev

# Set working directory for build
WORKDIR /build

# Copy source files with proper structure validation
COPY linux/src /build/src/
COPY linux/include /build/include/
COPY src /build/library_src/
COPY webserver /build/webserver/

# Validate source files exist
RUN test -f /build/library_src/vbusdecoder.cpp && \
    test -f /build/src/LinuxSerial.cpp && \
    test -f /build/src/Arduino.cpp && \
    test -f /build/webserver/main.cpp

# Build components with error checking and optimization
WORKDIR /build

# Build the library with proper flags
RUN cd library_src && \
    g++ -c -fPIC -std=c++17 -O2 -Wall -Wextra \
    -I. -I../include vbusdecoder.cpp -o vbusdecoder.o

# Build the Linux abstraction layer
RUN cd src && \
    g++ -c -fPIC -std=c++17 -O2 -Wall -Wextra \
    -I../include -I../library_src LinuxSerial.cpp -o LinuxSerial.o && \
    g++ -c -fPIC -std=c++17 -O2 -Wall -Wextra \
    -I../include -I../library_src Arduino.cpp -o Arduino.o

# Build the webserver application with full optimization
RUN cd webserver && \
    g++ -std=c++17 -O2 -Wall -Wextra \
    -o /usr/local/bin/viessmann_webserver \
    main.cpp \
    ../library_src/vbusdecoder.o \
    ../src/LinuxSerial.o \
    ../src/Arduino.o \
    -I../include \
    -I../library_src \
    -lmicrohttpd \
    -lpthread && \
    strip /usr/local/bin/viessmann_webserver && \
    chmod 755 /usr/local/bin/viessmann_webserver

# Verify the built binary
RUN ldd /usr/local/bin/viessmann_webserver && \
    /usr/local/bin/viessmann_webserver -h 2>&1 | head -5 || true

# Clean up build environment in single layer
WORKDIR /
RUN rm -rf /build /tmp/* /var/cache/apk/* /root/.cache && \
    apk del --no-cache .build-deps

# Optional: Prepare USB/IP Python-based client wrapper for remote USB forwarding
RUN mkdir -p /usr/local/bin && \
    cat > /usr/local/bin/usbip_client.sh << 'EOFSCRIPT'
#!/bin/bash
# USB/IP Client Wrapper - Uses nc/socat for fallback if usbip unavailable
# This is a compatibility wrapper that attempts to connect to remote USB devices

REMOTE_HOST="$1"
REMOTE_PORT="${2:-3240}"
BUSID="$3"

if [[ -z "$REMOTE_HOST" ]] || [[ -z "$BUSID" ]]; then
echo "Usage: $0 <host> [port] <busid>"
exit 1
fi

# Try using socat if available (netcat alternative)
if command -v socat >/dev/null 2>&1; then
echo "Attempting USB/IP connection via socat to $REMOTE_HOST:$REMOTE_PORT device $BUSID"
socat UNIX-LISTEN:/dev/shm/usbip_$BUSID.sock TCP:$REMOTE_HOST:$REMOTE_PORT &
sleep 1
exit 0
fi

# Fallback: Try nc
if command -v nc >/dev/null 2>&1; then
echo "Attempting USB/IP connection via nc to $REMOTE_HOST:$REMOTE_PORT device $BUSID"
nc -k -l -p 3241 -c "nc $REMOTE_HOST $REMOTE_PORT" &
sleep 1
exit 0
fi

echo "ERROR: Neither socat nor nc found. Install one of them for USB/IP forwarding."
exit 1
EOFSCRIPT
chmod 755 /usr/local/bin/usbip_client.sh && \
    apk add --no-cache socat || true

# Copy the main run script (NOT using s6-overlay, direct execution under tini)
COPY run.sh /run.sh
RUN chmod 755 /run.sh

# Create required directories
RUN mkdir -p /data /tmp && \
    chmod 755 /data /tmp

# Validate final setup
RUN test -x /usr/local/bin/viessmann_webserver && \
    test -x /run.sh

# Health check to monitor webserver availability
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=5 \
    CMD curl -f http://localhost:8099/health || exit 1

# Expose web interface port
EXPOSE 8099

# Direct execution - HA Supervisor uses tini as PID 1, then runs CMD
CMD ["/run.sh"]
