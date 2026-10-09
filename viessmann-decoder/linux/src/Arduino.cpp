/*
 * Arduino compatibility layer for Linux - Implementation
 */

#include "Arduino.h"
#include <chrono>

static std::chrono::steady_clock::time_point startTime() {
    static const auto start = std::chrono::steady_clock::now();
    return start;
}

unsigned long millis() {
    const auto start = startTime();
    return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
}

unsigned long micros() {
    const auto start = startTime();
    return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}

void delay(unsigned long ms) {
    usleep(ms * 1000);
}

void delayMicroseconds(unsigned int us) {
    usleep(us);
}
