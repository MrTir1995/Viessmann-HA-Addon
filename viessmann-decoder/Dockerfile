ARG BUILD_FROM=alpine:3.19
FROM $BUILD_FROM

# Set labels for better container management
LABEL \
    io.hass.name="Viessmann Decoder" \
    io.hass.description="Viessmann VBUS/KW/P300/KM-Bus protocol decoder" \
    io.hass.type="addon" \
    io.hass.version="2.2.0"

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

# Optional: Prepare USB/IP client wrapper for remote USB forwarding
RUN apk add --no-cache socat bash && \
    mkdir -p /usr/local/bin && \
    printf '#!/bin/bash\n\
    REMOTE_HOST="$1"\n\
    REMOTE_PORT="${2:-3240}"\n\
    BUSID="$3"\n\
    \n\
    if [[ -z "$REMOTE_HOST" ]] || [[ -z "$BUSID" ]]; then\n\
    echo "Usage: $0 <host> [port] <busid>"\n\
    exit 1\n\
    fi\n\
    \n\
    if command -v socat >/dev/null 2>&1; then\n\
    echo "USB/IP: Connecting via socat to $REMOTE_HOST:$REMOTE_PORT device $BUSID"\n\
    socat UNIX-LISTEN:/dev/shm/usbip_${BUSID}.sock TCP:${REMOTE_HOST}:${REMOTE_PORT} &\n\
    sleep 1\n\
    exit 0\n\
    fi\n\
    \n\
    if command -v nc >/dev/null 2>&1; then\n\
    echo "USB/IP: Connecting via nc to $REMOTE_HOST:$REMOTE_PORT device $BUSID"\n\
    nc -k -l -p 3241 -c "nc $REMOTE_HOST $REMOTE_PORT" &\n\
    sleep 1\n\
    exit 0\n\
    fi\n\
    \n\
    echo "ERROR: Neither socat nor nc available for USB/IP forwarding."\n\
    exit 1\n' > /usr/local/bin/usbip_client.sh && \
    chmod 755 /usr/local/bin/usbip_client.sh

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
