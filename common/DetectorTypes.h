#ifndef DETECTOR_TYPES_H
#define DETECTOR_TYPES_H

#include <Arduino.h>
#include <stdint.h>

// Result returned by every detector function. Stack-allocated, no heap.
struct DetectorResult {
    bool        matched;
    uint8_t     weight;
    const char* detectorName;
};

// Bitmask flags for tracking which detectors fired.
// Each detector gets one bit; stored in ThreatEvent::matchFlags.
enum DetectorFlag : uint16_t {
    DET_NONE              = 0,
    DET_SSID_FORMAT       = (1 << 0),
    DET_SSID_KEYWORD      = (1 << 1),
    DET_MAC_OUI           = (1 << 2),
    DET_BLE_NAME          = (1 << 3),
    DET_RAVEN_CUSTOM_UUID = (1 << 4),
    DET_RAVEN_STD_UUID    = (1 << 5),
    DET_RSSI_MODIFIER     = (1 << 6),
};

// Forward declarations (defined in EventBus.h)
struct WiFiFrameEvent;
struct BluetoothDeviceEvent;

// Detector function pointer types
typedef DetectorResult (*WiFiDetectorFn)(const WiFiFrameEvent& frame);
typedef DetectorResult (*BLEDetectorFn)(const BluetoothDeviceEvent& device);

// Registry entries pair a function with its flag bit
struct WiFiDetectorEntry {
    WiFiDetectorFn fn;
    DetectorFlag   flag;
};

struct BLEDetectorEntry {
    BLEDetectorFn fn;
    DetectorFlag  flag;
};

// Device presence tracking
enum class DeviceState : uint8_t {
    EMPTY,
    NEW_DETECT,
    IN_RANGE,
    DEPARTED,
};

struct TrackedDevice {
    uint8_t     mac[6];
    uint32_t    firstSeenMs;
    uint32_t    lastSeenMs;
    uint8_t     maxCertainty;
    DeviceState state;
    // 6 + 4 + 4 + 1 + 1 = 16 bytes per slot
};

// Constants
static const uint8_t  MAX_TRACKED_DEVICES   = 32;
static const uint32_t DEVICE_TIMEOUT_MS     = 60000;
static const uint32_t HEARTBEAT_INTERVAL_MS = 10000;
static const uint8_t  ALERT_THRESHOLD       = 65;

#endif
