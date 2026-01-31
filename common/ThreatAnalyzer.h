#ifndef THREAT_ANALYZER_H
#define THREAT_ANALYZER_H

#include <Arduino.h>
#include "EventBus.h"
#include "DetectorTypes.h"
#include "Detectors.h"

// ============================================================
// Detector Registry
// To add a detector: append one entry to the appropriate array.
// ============================================================

static const WiFiDetectorEntry wifiDetectors[] = {
    { detectSsidFormat,  DET_SSID_FORMAT  },
    { detectSsidKeyword, DET_SSID_KEYWORD },
    { detectWifiMacOui,  DET_MAC_OUI      },
};
static const uint8_t WIFI_DETECTOR_COUNT =
    sizeof(wifiDetectors) / sizeof(wifiDetectors[0]);

static const BLEDetectorEntry bleDetectors[] = {
    { detectBleName,         DET_BLE_NAME          },
    { detectRavenCustomUuid, DET_RAVEN_CUSTOM_UUID },
    { detectRavenStdUuid,    DET_RAVEN_STD_UUID    },
    { detectBleMacOui,       DET_MAC_OUI           },
};
static const uint8_t BLE_DETECTOR_COUNT =
    sizeof(bleDetectors) / sizeof(bleDetectors[0]);

// ============================================================
// Device Presence Tracker
// ============================================================

class DeviceTracker {
public:
    void initialize() {
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            slots[i].state = DeviceState::EMPTY;
        }
    }

    // Age out stale devices. Call every loop iteration.
    void tick(uint32_t nowMs) {
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            if (slots[i].state == DeviceState::IN_RANGE ||
                slots[i].state == DeviceState::NEW_DETECT) {
                if (nowMs - slots[i].lastSeenMs > DEVICE_TIMEOUT_MS) {
                    slots[i].state = DeviceState::DEPARTED;
                }
            }
        }
    }

    // Record a detection. Returns the state the device was in BEFORE
    // this update (EMPTY = first time seen).
    DeviceState recordDetection(const uint8_t* mac, uint32_t nowMs,
                                uint8_t certainty) {
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            if (slots[i].state != DeviceState::EMPTY &&
                slots[i].state != DeviceState::DEPARTED &&
                memcmp(slots[i].mac, mac, 6) == 0) {
                DeviceState prev = slots[i].state;
                slots[i].lastSeenMs = nowMs;
                slots[i].state = DeviceState::IN_RANGE;
                if (certainty > slots[i].maxCertainty)
                    slots[i].maxCertainty = certainty;
                return prev;
            }
        }

        uint8_t slot = findFreeSlot();
        memcpy(slots[slot].mac, mac, 6);
        slots[slot].firstSeenMs  = nowMs;
        slots[slot].lastSeenMs   = nowMs;
        slots[slot].maxCertainty = certainty;
        slots[slot].state        = DeviceState::NEW_DETECT;
        return DeviceState::EMPTY;
    }

    // Returns true if any tracked device is IN_RANGE and above threshold.
    bool hasHighConfidenceInRange() const {
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            if (slots[i].state == DeviceState::IN_RANGE &&
                slots[i].maxCertainty >= ALERT_THRESHOLD) {
                return true;
            }
        }
        return false;
    }

private:
    TrackedDevice slots[MAX_TRACKED_DEVICES];

    uint8_t findFreeSlot() {
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            if (slots[i].state == DeviceState::EMPTY) return i;
        }
        uint8_t oldest = 0;
        uint32_t oldestTime = UINT32_MAX;
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            if (slots[i].state == DeviceState::DEPARTED &&
                slots[i].lastSeenMs < oldestTime) {
                oldest = i;
                oldestTime = slots[i].lastSeenMs;
            }
        }
        if (oldestTime < UINT32_MAX) return oldest;
        // All slots active -- evict LRU
        oldest = 0;
        oldestTime = UINT32_MAX;
        for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
            if (slots[i].lastSeenMs < oldestTime) {
                oldest = i;
                oldestTime = slots[i].lastSeenMs;
            }
        }
        return oldest;
    }
};

// ============================================================
// Bit position helper
// ============================================================

inline uint8_t detectorBitPosition(uint16_t flag) {
    uint8_t pos = 0;
    while (flag > 1) { flag >>= 1; pos++; }
    return pos;
}

// ============================================================
// ThreatAnalyzer
// Pure logic -- no display/buzzer access. Safe from any context.
// ============================================================

class ThreatAnalyzer {
public:
    void initialize() {
        tracker.initialize();
        lastHeartbeatMs = 0;
    }

    void analyzeWiFiFrame(const WiFiFrameEvent& frame) {
        uint16_t matchFlags = 0;
        uint8_t weights[8];
        memset(weights, 0, sizeof(weights));
        int16_t totalWeight = 0;

        for (uint8_t i = 0; i < WIFI_DETECTOR_COUNT; i++) {
            DetectorResult res = wifiDetectors[i].fn(frame);
            if (res.matched) {
                matchFlags |= wifiDetectors[i].flag;
                uint8_t bit = detectorBitPosition(wifiDetectors[i].flag);
                if (bit < sizeof(weights)) weights[bit] = res.weight;
                totalWeight += res.weight;
            }
        }

        // Subsumption: SSID format supersedes SSID keyword
        if ((matchFlags & DET_SSID_FORMAT) && (matchFlags & DET_SSID_KEYWORD)) {
            totalWeight -= weights[detectorBitPosition(DET_SSID_KEYWORD)];
            matchFlags &= ~DET_SSID_KEYWORD;
            weights[detectorBitPosition(DET_SSID_KEYWORD)] = 0;
        }

        if (matchFlags == DET_NONE) return;

        int8_t rssiMod = rssiModifier(frame.rssi);
        totalWeight += rssiMod;
        uint8_t certainty = (uint8_t)constrain(totalWeight, 0, 100);

        uint32_t nowMs = millis();
        DeviceState prevState = tracker.recordDetection(
            frame.mac, nowMs, certainty);

        ThreatEvent threat;
        memset(&threat, 0, sizeof(threat));
        memcpy(threat.mac, frame.mac, 6);
        strncpy(threat.identifier, frame.ssid,
                sizeof(threat.identifier) - 1);
        threat.rssi            = frame.rssi;
        threat.channel         = frame.channel;
        strncpy(threat.radioType, "wifi", sizeof(threat.radioType) - 1);
        threat.certainty       = certainty;
        strncpy(threat.category, "surveillance_device", sizeof(threat.category) - 1);
        threat.matchFlags      = matchFlags | DET_RSSI_MODIFIER;
        memcpy(threat.detectorWeights, weights, sizeof(weights));
        threat.rssiModifier    = rssiMod;
        threat.shouldAlert     = (certainty >= ALERT_THRESHOLD &&
                                  prevState == DeviceState::EMPTY);

        EventBus::publishThreat(threat);
    }

    void analyzeBluetoothDevice(const BluetoothDeviceEvent& device) {
        uint16_t matchFlags = 0;
        uint8_t weights[8];
        memset(weights, 0, sizeof(weights));
        int16_t totalWeight = 0;

        for (uint8_t i = 0; i < BLE_DETECTOR_COUNT; i++) {
            DetectorResult res = bleDetectors[i].fn(device);
            if (res.matched) {
                matchFlags |= bleDetectors[i].flag;
                uint8_t bit = detectorBitPosition(bleDetectors[i].flag);
                if (bit < sizeof(weights)) weights[bit] = res.weight;
                totalWeight += res.weight;
            }
        }

        if (matchFlags == DET_NONE) return;

        int8_t rssiMod = rssiModifier(device.rssi);
        totalWeight += rssiMod;
        uint8_t certainty = (uint8_t)constrain(totalWeight, 0, 100);

        uint32_t nowMs = millis();
        DeviceState prevState = tracker.recordDetection(
            device.mac, nowMs, certainty);

        const char* cat =
            (matchFlags & (DET_RAVEN_CUSTOM_UUID | DET_RAVEN_STD_UUID))
                ? "acoustic_detector"
                : "surveillance_device";

        ThreatEvent threat;
        memset(&threat, 0, sizeof(threat));
        memcpy(threat.mac, device.mac, 6);
        strncpy(threat.identifier, device.name,
                sizeof(threat.identifier) - 1);
        threat.rssi            = device.rssi;
        threat.channel         = 0;
        strncpy(threat.radioType, "bluetooth", sizeof(threat.radioType) - 1);
        threat.certainty       = certainty;
        strncpy(threat.category, cat, sizeof(threat.category) - 1);
        threat.matchFlags      = matchFlags | DET_RSSI_MODIFIER;
        memcpy(threat.detectorWeights, weights, sizeof(weights));
        threat.rssiModifier    = rssiMod;
        threat.shouldAlert     = (certainty >= ALERT_THRESHOLD &&
                                  prevState == DeviceState::EMPTY);

        EventBus::publishThreat(threat);
    }

    // Call from loop(). Ages out stale devices and returns true
    // if a heartbeat beep should be emitted (caller handles hardware).
    bool tick(uint32_t nowMs) {
        tracker.tick(nowMs);

        if (nowMs - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
            lastHeartbeatMs = nowMs;
            if (tracker.hasHighConfidenceInRange()) {
                return true;
            }
        }
        return false;
    }

private:
    DeviceTracker tracker;
    uint32_t lastHeartbeatMs;
};

#endif
