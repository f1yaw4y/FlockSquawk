#pragma once
#include <stdint.h>
#include <string.h>

// Rolling median filter for battery percentage readings.
// Smooths noisy getBatteryLevel() values that oscillate at charge
// boundaries (e.g. 79%<->80%).
struct BatteryFilter {
    static const uint8_t HISTORY_SIZE = 8;
    uint8_t history[HISTORY_SIZE] = {0};
    uint8_t idx = 0;
    bool full = false;
    uint8_t smoothed = 0;
    uint8_t lastRaw = 0;

    // Fill the entire buffer with a single value so the filter
    // produces a stable result immediately after boot.
    void seed(uint8_t value) {
        for (uint8_t i = 0; i < HISTORY_SIZE; i++) history[i] = value;
        full = true;
        smoothed = value;
        lastRaw = value;
    }

    // Push a new raw reading and recompute the median.
    void addSample(uint8_t raw) {
        lastRaw = raw;
        history[idx] = raw;
        idx = (idx + 1) % HISTORY_SIZE;
        if (!full && idx == 0) full = true;

        uint8_t count = full ? HISTORY_SIZE : idx;
        if (count == 0) { smoothed = raw; return; }

        // Insertion sort on a tiny copy, then take the median.
        uint8_t sorted[HISTORY_SIZE];
        memcpy(sorted, history, count);
        for (uint8_t i = 1; i < count; i++) {
            uint8_t val = sorted[i];
            int8_t j = i - 1;
            while (j >= 0 && sorted[j] > val) { sorted[j + 1] = sorted[j]; j--; }
            sorted[j + 1] = val;
        }
        smoothed = sorted[count / 2];
    }
};
