#pragma once

#include "../../include/Arduino.h"
#include <algorithm>
#include <string>
#include <type_traits>

using std::min;

// Only the Arduino String operations used by the optional helpers.
class String {
public:
    String(const char* text = "") : _value(text) {}

    template<class T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    String(T number) : _value(std::to_string(number)) {}

    String(float number, int precision) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.*f", precision, double(number));
        _value = buffer;
    }

    String& operator+=(const String& other) {
        _value += other._value;
        return *this;
    }

    friend String operator+(String left, const String& right) {
        left += right;
        return left;
    }

private:
    std::string _value;
};
