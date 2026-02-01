// Minimal Arduino.h mock for host-side unit tests.
// Provides only what the testable production headers actually use.
#ifndef ARDUINO_H_MOCK
#define ARDUINO_H_MOCK

#include <stdint.h>
#include <stddef.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>

// On Linux, strcasestr needs _GNU_SOURCE (already available on macOS).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <strings.h>

// constrain macro (matches Arduino)
#ifndef constrain
#define constrain(x, low, high) \
    ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))
#endif

// Test-controllable millis() — defined in eventbus_impl.cpp
extern uint32_t mock_millis_value;
inline uint32_t millis() { return mock_millis_value; }

#endif // ARDUINO_H_MOCK
