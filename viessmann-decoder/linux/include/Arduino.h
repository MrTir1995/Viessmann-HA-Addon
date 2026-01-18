/*
 * Arduino compatibility layer for Linux
 * Provides Arduino-like API for Linux applications
 */

#pragma once
#ifndef ARDUINO_LINUX_H
#define ARDUINO_LINUX_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

// Arduino-style type definitions
typedef bool boolean;
typedef uint8_t byte;

// Serial configuration constants
// Format: bits 0-1: data bits (always 8), bits 2-3: parity, bits 4-5: stop bits
// Parity: 00=None, 10=Even, 11=Odd
// Stop bits: 00=1, 01=2
#define SERIAL_8N1 0x00  // 8 data, no parity, 1 stop bit
#define SERIAL_8E2 0x18  // 8 data, even parity (10), 2 stop bits (01)

// Stream base class for serial communication
class Stream {
public:
    virtual ~Stream() {}
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) = 0;
    virtual void flush() = 0;
};

// Arduino timing functions
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

// F() macro for compatibility (no-op on Linux)
#define F(string_literal) (string_literal)

#endif // ARDUINO_LINUX_H
