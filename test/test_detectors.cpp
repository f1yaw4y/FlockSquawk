#include "doctest.h"
#include "Detectors.h"

// ============================================================
// Helper: build WiFiFrameEvent
// ============================================================
static WiFiFrameEvent makeWiFiFrame(const char* ssid, int8_t rssi = -60,
                                     uint8_t channel = 6) {
    WiFiFrameEvent f;
    memset(&f, 0, sizeof(f));
    strncpy(f.ssid, ssid, sizeof(f.ssid) - 1);
    f.rssi = rssi;
    f.channel = channel;
    f.frameSubtype = 0x20;
    return f;
}

// Helper: build WiFiFrameEvent with specific MAC
static WiFiFrameEvent makeWiFiFrameMAC(const uint8_t mac[6],
                                        const char* ssid = "",
                                        int8_t rssi = -60) {
    WiFiFrameEvent f;
    memset(&f, 0, sizeof(f));
    memcpy(f.mac, mac, 6);
    strncpy(f.ssid, ssid, sizeof(f.ssid) - 1);
    f.rssi = rssi;
    f.channel = 6;
    f.frameSubtype = 0x20;
    return f;
}

// Helper: build BluetoothDeviceEvent
static BluetoothDeviceEvent makeBLEDevice(const char* name = "",
                                           int8_t rssi = -60,
                                           const char* uuid = "",
                                           bool hasUUID = false) {
    BluetoothDeviceEvent d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, name, sizeof(d.name) - 1);
    d.rssi = rssi;
    d.hasServiceUUID = hasUUID;
    if (uuid[0] != '\0') {
        strncpy(d.serviceUUID, uuid, sizeof(d.serviceUUID) - 1);
        d.hasServiceUUID = true;
    }
    return d;
}

// Helper: build BLE device with specific MAC
static BluetoothDeviceEvent makeBLEDeviceMAC(const uint8_t mac[6],
                                              const char* name = "",
                                              int8_t rssi = -60) {
    BluetoothDeviceEvent d;
    memset(&d, 0, sizeof(d));
    memcpy(d.mac, mac, 6);
    strncpy(d.name, name, sizeof(d.name) - 1);
    d.rssi = rssi;
    d.hasServiceUUID = false;
    return d;
}

// ============================================================
// isHexChar
// ============================================================
TEST_CASE("isHexChar") {
    CHECK(isHexChar('0'));
    CHECK(isHexChar('9'));
    CHECK(isHexChar('a'));
    CHECK(isHexChar('f'));
    CHECK(isHexChar('A'));
    CHECK(isHexChar('F'));
    CHECK_FALSE(isHexChar('g'));
    CHECK_FALSE(isHexChar('G'));
    CHECK_FALSE(isHexChar('z'));
    CHECK_FALSE(isHexChar('-'));
    CHECK_FALSE(isHexChar(' '));
}

// ============================================================
// isHexSuffix
// ============================================================
TEST_CASE("isHexSuffix") {
    CHECK(isHexSuffix("abcdef", 6));
    CHECK(isHexSuffix("ABCDEF", 6));
    CHECK(isHexSuffix("012345", 6));
    CHECK_FALSE(isHexSuffix("abcdeg", 6));
    CHECK_FALSE(isHexSuffix("abcde", 6));  // too short — char at [5] is '\0' but len=6 checks s[5]
    CHECK(isHexSuffix("ab", 2));
    CHECK(isHexSuffix("", 0));             // zero-length suffix — s[0] must be '\0'
}

// ============================================================
// isDecimalSuffix
// ============================================================
TEST_CASE("isDecimalSuffix") {
    CHECK(isDecimalSuffix("1234567890", 10));
    CHECK(isDecimalSuffix("0000000000", 10));
    CHECK_FALSE(isDecimalSuffix("123456789a", 10));
    CHECK_FALSE(isDecimalSuffix("12345", 10));  // char at index 5+ isn't a digit
    CHECK(isDecimalSuffix("42", 2));
    CHECK(isDecimalSuffix("", 0));
}

// ============================================================
// ouiMatchesKnownPrefix
// ============================================================
TEST_CASE("ouiMatchesKnownPrefix") {
    // 58:8e:81 is in DeviceProfiles::MACPrefixes
    uint8_t known[] = { 0x58, 0x8E, 0x81, 0x11, 0x22, 0x33 };
    CHECK(ouiMatchesKnownPrefix(known));

    // cc:cc:cc is in the list
    uint8_t known2[] = { 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00 };
    CHECK(ouiMatchesKnownPrefix(known2));

    // aa:bb:cc is NOT in the list
    uint8_t unknown[] = { 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33 };
    CHECK_FALSE(ouiMatchesKnownPrefix(unknown));
}

// ============================================================
// rssiModifier
// ============================================================
TEST_CASE("rssiModifier") {
    CHECK(rssiModifier(-30) == 10);    // very close
    CHECK(rssiModifier(-49) == 10);    // just above -50
    CHECK(rssiModifier(-50) == 0);     // boundary: > -50 is false, > -70 is true
    CHECK(rssiModifier(-60) == 0);     // medium range
    CHECK(rssiModifier(-70) == -5);    // boundary: > -70 is false, > -85 is true
    CHECK(rssiModifier(-80) == -5);    // weak
    CHECK(rssiModifier(-85) == -10);   // boundary: > -85 is false
    CHECK(rssiModifier(-90) == -10);   // very weak
}

// ============================================================
// detectSsidFormat
// ============================================================
TEST_CASE("detectSsidFormat") {
    SUBCASE("Flock- pattern matches") {
        auto r = detectSsidFormat(makeWiFiFrame("Flock-a1b2c3"));
        CHECK(r.matched);
        CHECK(r.weight == 75);
    }
    SUBCASE("Flock- with uppercase hex matches") {
        CHECK(detectSsidFormat(makeWiFiFrame("Flock-ABCDEF")).matched);
    }
    SUBCASE("Flock- wrong length rejects") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("Flock-a1b2c")).matched);
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("Flock-a1b2c3d")).matched);
    }
    SUBCASE("Flock- non-hex suffix rejects") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("Flock-a1b2gX")).matched);
    }
    SUBCASE("Penguin- pattern matches") {
        CHECK(detectSsidFormat(makeWiFiFrame("Penguin-1234567890")).matched);
    }
    SUBCASE("Penguin- wrong length rejects") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("Penguin-123456789")).matched);
    }
    SUBCASE("Penguin- non-decimal rejects") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("Penguin-12345678ab")).matched);
    }
    SUBCASE("FS Ext Battery exact match") {
        CHECK(detectSsidFormat(makeWiFiFrame("FS Ext Battery")).matched);
    }
    SUBCASE("FS Ext Battery prefix does not match") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("FS Ext Battery v2")).matched);
    }
    SUBCASE("Empty SSID rejects") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("")).matched);
    }
    SUBCASE("Unrelated SSID rejects") {
        CHECK_FALSE(detectSsidFormat(makeWiFiFrame("MyHomeWiFi")).matched);
    }
}

// ============================================================
// detectSsidKeyword
// ============================================================
TEST_CASE("detectSsidKeyword") {
    SUBCASE("flock keyword matches (case-insensitive)") {
        CHECK(detectSsidKeyword(makeWiFiFrame("Flock-a1b2c3")).matched);
        CHECK(detectSsidKeyword(makeWiFiFrame("MyFLOCKnet")).matched);
    }
    SUBCASE("penguin keyword matches") {
        CHECK(detectSsidKeyword(makeWiFiFrame("Penguin-1234567890")).matched);
    }
    SUBCASE("pigvision keyword matches") {
        CHECK(detectSsidKeyword(makeWiFiFrame("PigVision_AP")).matched);
    }
    SUBCASE("weight is 45") {
        auto r = detectSsidKeyword(makeWiFiFrame("flock-test"));
        CHECK(r.weight == 45);
    }
    SUBCASE("no match for unrelated SSID") {
        CHECK_FALSE(detectSsidKeyword(makeWiFiFrame("Starbucks WiFi")).matched);
    }
    SUBCASE("empty SSID rejects") {
        CHECK_FALSE(detectSsidKeyword(makeWiFiFrame("")).matched);
    }
}

// ============================================================
// detectWifiMacOui
// ============================================================
TEST_CASE("detectWifiMacOui") {
    SUBCASE("known OUI matches") {
        uint8_t mac[] = { 0x58, 0x8E, 0x81, 0x11, 0x22, 0x33 };
        auto f = makeWiFiFrameMAC(mac);
        auto r = detectWifiMacOui(f);
        CHECK(r.matched);
        CHECK(r.weight == 20);
    }
    SUBCASE("unknown OUI does not match") {
        uint8_t mac[] = { 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33 };
        auto f = makeWiFiFrameMAC(mac);
        CHECK_FALSE(detectWifiMacOui(f).matched);
    }
}

// ============================================================
// detectBleName
// ============================================================
TEST_CASE("detectBleName") {
    SUBCASE("Flock matches") {
        auto r = detectBleName(makeBLEDevice("Flock Tracker"));
        CHECK(r.matched);
        CHECK(r.weight == 55);
    }
    SUBCASE("Penguin matches case-insensitively") {
        CHECK(detectBleName(makeBLEDevice("PENGUIN-unit")).matched);
    }
    SUBCASE("FS Ext Battery matches") {
        CHECK(detectBleName(makeBLEDevice("FS Ext Battery")).matched);
    }
    SUBCASE("Pigvision matches") {
        CHECK(detectBleName(makeBLEDevice("pigvision-3")).matched);
    }
    SUBCASE("unrelated name does not match") {
        CHECK_FALSE(detectBleName(makeBLEDevice("AirPods Pro")).matched);
    }
    SUBCASE("empty name does not match") {
        CHECK_FALSE(detectBleName(makeBLEDevice("")).matched);
    }
}

// ============================================================
// detectRavenCustomUuid
// ============================================================
TEST_CASE("detectRavenCustomUuid") {
    SUBCASE("0x3100 matches") {
        auto d = makeBLEDevice("", -60, "00003100-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenCustomUuid(d).matched);
    }
    SUBCASE("0x3500 matches") {
        auto d = makeBLEDevice("", -60, "00003500-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenCustomUuid(d).matched);
    }
    SUBCASE("0x3000 does not match (digit 0 out of 1-5 range)") {
        auto d = makeBLEDevice("", -60, "00003000-0000-1000-8000-00805f9b34fb");
        CHECK_FALSE(detectRavenCustomUuid(d).matched);
    }
    SUBCASE("0x3600 does not match (digit 6 out of range)") {
        auto d = makeBLEDevice("", -60, "00003600-0000-1000-8000-00805f9b34fb");
        CHECK_FALSE(detectRavenCustomUuid(d).matched);
    }
    SUBCASE("no UUID does not match") {
        auto d = makeBLEDevice("SomeName", -60);
        CHECK_FALSE(detectRavenCustomUuid(d).matched);
    }
    SUBCASE("weight is 80") {
        auto d = makeBLEDevice("", -60, "00003200-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenCustomUuid(d).weight == 80);
    }
}

// ============================================================
// detectRavenStdUuid
// ============================================================
TEST_CASE("detectRavenStdUuid") {
    SUBCASE("0x180A (Device Information) matches") {
        auto d = makeBLEDevice("", -60, "0000180a-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenStdUuid(d).matched);
    }
    SUBCASE("0x1809 (Health Thermometer) matches") {
        auto d = makeBLEDevice("", -60, "00001809-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenStdUuid(d).matched);
    }
    SUBCASE("0x1819 (Location/Navigation) matches") {
        auto d = makeBLEDevice("", -60, "00001819-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenStdUuid(d).matched);
    }
    SUBCASE("0x180A uppercase matches") {
        auto d = makeBLEDevice("", -60, "0000180A-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenStdUuid(d).matched);
    }
    SUBCASE("unrelated UUID does not match") {
        auto d = makeBLEDevice("", -60, "0000180f-0000-1000-8000-00805f9b34fb");
        CHECK_FALSE(detectRavenStdUuid(d).matched);
    }
    SUBCASE("weight is 10") {
        auto d = makeBLEDevice("", -60, "0000180a-0000-1000-8000-00805f9b34fb");
        CHECK(detectRavenStdUuid(d).weight == 10);
    }
}

// ============================================================
// detectBleMacOui
// ============================================================
TEST_CASE("detectBleMacOui") {
    SUBCASE("known OUI matches") {
        uint8_t mac[] = { 0xEC, 0x1B, 0xBD, 0x44, 0x55, 0x66 };
        auto d = makeBLEDeviceMAC(mac);
        auto r = detectBleMacOui(d);
        CHECK(r.matched);
        CHECK(r.weight == 20);
    }
    SUBCASE("unknown OUI does not match") {
        uint8_t mac[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
        CHECK_FALSE(detectBleMacOui(makeBLEDeviceMAC(mac)).matched);
    }
}
