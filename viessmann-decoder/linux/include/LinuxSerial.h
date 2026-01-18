/*
 * Linux Serial Port implementation
 * Provides Arduino-like Serial interface for Linux serial ports
 */

#pragma once
#ifndef LINUX_SERIAL_H
#define LINUX_SERIAL_H

#include "Arduino.h"
#include <termios.h>

class LinuxSerial : public Stream {
public:
    LinuxSerial();
    ~LinuxSerial();
    
    // Open serial port with baud rate
    bool begin(const char* port, unsigned long baud, uint8_t config = SERIAL_8N1);
    void end();
    
    // Stream interface implementation
    int available() override;
    int read() override;
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    void flush() override;
    
    // Additional methods
    bool isOpen() const { return fd >= 0; }
    
private:
    int fd;
    struct termios oldtio;
    
    bool configure(unsigned long baud, uint8_t config);
    speed_t getBaudRate(unsigned long baud);
};

#endif // LINUX_SERIAL_H
