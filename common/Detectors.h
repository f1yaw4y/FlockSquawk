#ifndef DETECTORS_H
#define DETECTORS_H

#include "DetectorTypes.h"
#include "EventBus.h"
#include "DeviceSignatures.h"
#include <string.h>
#include <ctype.h>

// ============================================================
// Helpers
// ============================================================

inline bool isHexChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

inline bool isHexSuffix(const char* s, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!isHexChar(s[i])) return false;
    }
    return s[len] == '\0';
}

inline bool isDecimalSuffix(const char* s, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return s[len] == '\0';
}

inline bool ouiMatchesKnownPrefix(const uint8_t* mac) {
    char macStr[9];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
    for (size_t i = 0; i < DeviceProfiles::MACPrefixCount; i++) {
        if (strncasecmp(macStr, DeviceProfiles::MACPrefixes[i], 8) == 0)
            return true;
    }
    return false;
}

// ============================================================
// WiFi Detectors
// ============================================================

// SSID Format Match (weight 75)
// Validates highly specific patterns:
//   "Flock-" + exactly 6 hex chars
//   "Penguin-" + exactly 10 decimal digits
//   Exact "FS Ext Battery"
inline DetectorResult detectSsidFormat(const WiFiFrameEvent& frame) {
    DetectorResult r = { false, 75, "ssid_format" };
    const char* ssid = frame.ssid;
    if (ssid[0] == '\0') return r;
    size_t len = strlen(ssid);

    if (len == 12 && strncmp(ssid, "Flock-", 6) == 0 &&
        isHexSuffix(ssid + 6, 6)) {
        r.matched = true;
        return r;
    }

    if (len == 18 && strncmp(ssid, "Penguin-", 8) == 0 &&
        isDecimalSuffix(ssid + 8, 10)) {
        r.matched = true;
        return r;
    }

    if (strcmp(ssid, "FS Ext Battery") == 0) {
        r.matched = true;
        return r;
    }

    return r;
}

// SSID Keyword Match (weight 45)
// Case-insensitive substring search for known keywords.
inline DetectorResult detectSsidKeyword(const WiFiFrameEvent& frame) {
    DetectorResult r = { false, 45, "ssid_keyword" };
    const char* ssid = frame.ssid;
    if (ssid[0] == '\0') return r;

    static const char* const keywords[] = {
        "flock", "penguin", "pigvision"
    };
    static const uint8_t count = sizeof(keywords) / sizeof(keywords[0]);

    for (uint8_t i = 0; i < count; i++) {
        if (strcasestr(ssid, keywords[i])) {
            r.matched = true;
            return r;
        }
    }
    return r;
}

// WiFi MAC OUI Match (weight 20)
inline DetectorResult detectWifiMacOui(const WiFiFrameEvent& frame) {
    DetectorResult r = { false, 20, "mac_oui" };
    if (ouiMatchesKnownPrefix(frame.mac)) r.matched = true;
    return r;
}

// ============================================================
// BLE Detectors
// ============================================================

// BLE Device Name Match (weight 55)
inline DetectorResult detectBleName(const BluetoothDeviceEvent& device) {
    DetectorResult r = { false, 55, "ble_name" };
    if (device.name[0] == '\0') return r;

    static const char* const names[] = {
        "Flock", "Penguin", "FS Ext Battery", "Pigvision"
    };
    static const uint8_t count = sizeof(names) / sizeof(names[0]);

    for (uint8_t i = 0; i < count; i++) {
        if (strcasestr(device.name, names[i])) {
            r.matched = true;
            return r;
        }
    }
    return r;
}

// Raven Custom UUID Match (weight 80)
// Matches UUIDs with 16-bit short IDs 0x3100 through 0x3500.
// Format: "0000XXXX-0000-1000-8000-00805f9b34fb"
inline DetectorResult detectRavenCustomUuid(const BluetoothDeviceEvent& device) {
    DetectorResult r = { false, 80, "raven_custom_uuid" };
    if (!device.hasServiceUUID || device.serviceUUID[0] == '\0') return r;

    const char* uuid = device.serviceUUID;
    if (strlen(uuid) < 8) return r;

    // Check prefix "00003X00" where X is 1-5
    if (uuid[0] == '0' && uuid[1] == '0' && uuid[2] == '0' && uuid[3] == '0' &&
        uuid[4] == '3' && uuid[5] >= '1' && uuid[5] <= '5' &&
        uuid[6] == '0' && uuid[7] == '0') {
        r.matched = true;
    }
    return r;
}

// Raven Standard UUID Match (weight 10)
// Matches standard BLE SIG UUIDs that Raven also uses.
// Low weight because these are very common across consumer devices.
// 0x180A = Device Information, 0x1809 = Health Thermometer, 0x1819 = Location/Navigation
inline DetectorResult detectRavenStdUuid(const BluetoothDeviceEvent& device) {
    DetectorResult r = { false, 10, "raven_std_uuid" };
    if (!device.hasServiceUUID || device.serviceUUID[0] == '\0') return r;

    if (strncasecmp(device.serviceUUID, "0000180a", 8) == 0 ||
        strncasecmp(device.serviceUUID, "00001809", 8) == 0 ||
        strncasecmp(device.serviceUUID, "00001819", 8) == 0) {
        r.matched = true;
    }
    return r;
}

// BLE MAC OUI Match (weight 20)
inline DetectorResult detectBleMacOui(const BluetoothDeviceEvent& device) {
    DetectorResult r = { false, 20, "mac_oui" };
    if (ouiMatchesKnownPrefix(device.mac)) r.matched = true;
    return r;
}

// ============================================================
// RSSI Modifier
// ============================================================

inline int8_t rssiModifier(int8_t rssi) {
    if (rssi > -50)  return  10;
    if (rssi > -70)  return   0;
    if (rssi > -85)  return  -5;
    return -10;
}

#endif
