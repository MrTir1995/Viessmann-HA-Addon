/*
 * Arduino compatibility layer for Linux - Implementation
 */

#include "Arduino.h"

static struct timeval start_time;
static bool time_initialized = false;

static void init_time() {
    if (!time_initialized) {
        gettimeofday(&start_time, NULL);
        time_initialized = true;
    }
}

unsigned long millis() {
    init_time();
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    unsigned long ms = (current_time.tv_sec - start_time.tv_sec) * 1000UL;
    ms += (current_time.tv_usec - start_time.tv_usec) / 1000UL;
    
    return ms;
}

unsigned long micros() {
    init_time();
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    unsigned long us = (current_time.tv_sec - start_time.tv_sec) * 1000000UL;
    us += (current_time.tv_usec - start_time.tv_usec);
    
    return us;
}

void delay(unsigned long ms) {
    usleep(ms * 1000);
}

void delayMicroseconds(unsigned int us) {
    usleep(us);
}
