#pragma once
#include <stdint.h>

const uint8_t CONN_NONE           = 0;
const uint8_t CONN_SERIAL         = 1;

const uint32_t SERIAL_ALIVE_TIMEOUT_MS = 5000;

// Pure function: determine serial connection state from timing inputs.
inline uint8_t computeSerialState(uint32_t lastSerialRxMs, uint32_t now) {
    bool alive = lastSerialRxMs > 0 && (now - lastSerialRxMs < SERIAL_ALIVE_TIMEOUT_MS);
    return alive ? CONN_SERIAL : CONN_NONE;
}

// Indirect charging detection via battery level trend.
// Returns true when battery is likely charging (level rising or held at 100%).
inline bool isBatteryRising(uint8_t currentLevel, uint8_t previousLevel) {
    return (currentLevel > previousLevel)
        || (currentLevel == 100 && previousLevel == 100);
}
