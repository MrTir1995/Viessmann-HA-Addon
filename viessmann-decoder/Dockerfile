ARG BUILD_FROM=ghcr.io/home-assistant/amd64-base:3.18
FROM $BUILD_FROM

# Set labels for better container management
LABEL \
    io.hass.name="Viessmann Decoder" \
    io.hass.description="Viessmann VBUS/KW/P300/KM-Bus protocol decoder" \
    io.hass.type="addon" \
    io.hass.version="1.0.0"

# Install build dependencies and runtime libraries in single layer
RUN apk add --no-cache --virtual .build-deps \
    build-base \
    g++ \
    make \
    linux-headers \
    libmicrohttpd-dev && \
    apk add --no-cache \
    libmicrohttpd \
    curl \
    bash

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

# Copy service files for s6-overlay v3
COPY rootfs/ /

# Set proper permissions for all service scripts
RUN chmod 755 /etc/s6-overlay/s6-rc.d/viessmann-decoder/run && \
    chmod 644 /etc/s6-overlay/s6-rc.d/viessmann-decoder/type && \
    touch /etc/s6-overlay/s6-rc.d/user/contents.d/viessmann-decoder && \
    chmod 644 /etc/s6-overlay/s6-rc.d/user/contents.d/viessmann-decoder && \
    chmod 755 /etc/services.d/viessmann-decoder/run 2>/dev/null || true

# Create required directories
RUN mkdir -p /data /tmp && \
    chmod 755 /data /tmp

# Validate final setup
RUN test -x /usr/local/bin/viessmann_webserver && \
    test -x /etc/s6-overlay/s6-rc.d/viessmann-decoder/run && \
    test -f /etc/s6-overlay/s6-rc.d/viessmann-decoder/type

# Health check to monitor webserver availability
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=5 \
    CMD curl -f http://localhost:8099/health || exit 1

# Expose web interface port
EXPOSE 8099

# S6-Overlay v3 init system will automatically start services
# Base image provides /init as entrypoint
