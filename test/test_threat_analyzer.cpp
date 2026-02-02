#include "doctest.h"
#include "ThreatAnalyzer.h"

extern uint32_t mock_millis_value;

// Capture the last ThreatEvent published by the analyzer
static ThreatEvent lastThreat;
static int threatCount;

static void resetCapture() {
    memset(&lastThreat, 0, sizeof(lastThreat));
    threatCount = 0;
    EventBus::subscribeThreat([](const ThreatEvent& t) {
        lastThreat = t;
        threatCount++;
    });
}

// ============================================================
// Helpers
// ============================================================

static WiFiFrameEvent makeWiFiFrame(const char* ssid, int8_t rssi = -60,
                                     uint8_t channel = 6) {
    WiFiFrameEvent f;
    memset(&f, 0, sizeof(f));
    strncpy(f.ssid, ssid, sizeof(f.ssid) - 1);
    f.rssi = rssi;
    f.channel = channel;
    f.frameSubtype = 0x20;
    // Use a unique-ish MAC based on first char so each test gets distinct devices
    f.mac[0] = 0xDE; f.mac[1] = 0xAD;
    f.mac[2] = (uint8_t)ssid[0]; f.mac[3] = (uint8_t)ssid[1];
    f.mac[4] = 0x00; f.mac[5] = 0x01;
    return f;
}

static BluetoothDeviceEvent makeBLEDevice(const char* name = "",
                                           int8_t rssi = -60,
                                           const char* uuid = "") {
    BluetoothDeviceEvent d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, name, sizeof(d.name) - 1);
    d.rssi = rssi;
    d.hasServiceUUID = (uuid[0] != '\0');
    if (d.hasServiceUUID) {
        strncpy(d.serviceUUID, uuid, sizeof(d.serviceUUID) - 1);
    }
    d.mac[0] = 0xBE; d.mac[1] = 0xEF;
    d.mac[2] = (uint8_t)name[0]; d.mac[3] = (uint8_t)(name[0] ? name[1] : 0);
    d.mac[4] = 0x00; d.mac[5] = 0x02;
    return d;
}

// ============================================================
// WiFi scoring
// ============================================================

TEST_CASE("ThreatAnalyzer: WiFi SSID format match produces threat") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    analyzer.analyzeWiFiFrame(makeWiFiFrame("Flock-a1b2c3", -60));
    CHECK(threatCount == 1);
    CHECK(lastThreat.certainty > 0);
    CHECK(strcmp(lastThreat.radioType, "wifi") == 0);
    CHECK(strcmp(lastThreat.category, "surveillance_device") == 0);
}

TEST_CASE("ThreatAnalyzer: WiFi no-match produces no threat") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    analyzer.analyzeWiFiFrame(makeWiFiFrame("Starbucks WiFi", -60));
    CHECK(threatCount == 0);
}

TEST_CASE("ThreatAnalyzer: WiFi subsumption removes keyword weight") {
    // "Flock-a1b2c3" matches both SSID_FORMAT (75) and SSID_KEYWORD (45).
    // Subsumption should remove the keyword weight.
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // RSSI -60 → rssiModifier = 0
    analyzer.analyzeWiFiFrame(makeWiFiFrame("Flock-a1b2c3", -60));
    REQUIRE(threatCount == 1);
    // Should be 75 (format) + 0 (rssi) = 75, NOT 75+45=120
    CHECK(lastThreat.certainty == 75);
    // SSID_KEYWORD flag should be cleared
    CHECK((lastThreat.matchFlags & DET_SSID_KEYWORD) == 0);
    CHECK((lastThreat.matchFlags & DET_SSID_FORMAT) != 0);
}

TEST_CASE("ThreatAnalyzer: WiFi RSSI modifier applied") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // "Flock-a1b2c3" at RSSI -30 → format=75, rssi=+10 → 85
    auto frame = makeWiFiFrame("Flock-a1b2c3", -30);
    // Give unique MAC to avoid hitting existing device
    frame.mac[5] = 0x10;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.certainty == 85);
    CHECK(lastThreat.rssiModifier == 10);
}

TEST_CASE("ThreatAnalyzer: WiFi certainty clamped to 100") {
    // Keyword-only at very close range: 45 + 10 = 55 (no clamp needed)
    // But let's verify format + OUI + close range doesn't exceed 100:
    // format=75, OUI=20, rssi=+10 = 105 → clamped to 100
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("Flock-a1b2c3", -30);
    // Set a known OUI
    frame.mac[0] = 0x58; frame.mac[1] = 0x8E; frame.mac[2] = 0x81;
    frame.mac[3] = 0x99; frame.mac[4] = 0x99; frame.mac[5] = 0x99;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.certainty == 100);
}

TEST_CASE("ThreatAnalyzer: WiFi certainty clamped to 0 minimum") {
    // Keyword alone (45) at very weak signal (-90 → -10) = 35
    // That's still positive. Use OUI alone (20) at -90 → 20-10 = 10.
    // Can't easily get negative with existing detectors, but verify >= 0.
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // MAC OUI match only (weight 20) at RSSI -90 → 20 + (-10) = 10
    auto frame = makeWiFiFrame("", -90);
    frame.ssid[0] = '\0';  // ensure no SSID match
    frame.mac[0] = 0x58; frame.mac[1] = 0x8E; frame.mac[2] = 0x81;
    frame.mac[3] = 0xBB; frame.mac[4] = 0xBB; frame.mac[5] = 0xBB;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.certainty == 10);
}

// ============================================================
// shouldAlert logic
// ============================================================

TEST_CASE("ThreatAnalyzer: shouldAlert true on first high-certainty detection") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    analyzer.analyzeWiFiFrame(makeWiFiFrame("Flock-a1b2c3", -60));
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.shouldAlert == true);
    CHECK(lastThreat.certainty >= ALERT_THRESHOLD);
}

TEST_CASE("ThreatAnalyzer: shouldAlert false on repeat detection") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("Flock-a1b2c3", -60);
    frame.mac[5] = 0x20;  // unique MAC
    analyzer.analyzeWiFiFrame(frame);
    CHECK(lastThreat.shouldAlert == true);

    // Same device again
    mock_millis_value = 6000;
    analyzer.analyzeWiFiFrame(frame);
    CHECK(lastThreat.shouldAlert == false);
}

TEST_CASE("ThreatAnalyzer: shouldAlert false when below threshold") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // OUI-only match at weak signal: certainty = 20 + (-10) = 10
    auto frame = makeWiFiFrame("", -90);
    frame.ssid[0] = '\0';
    frame.mac[0] = 0xCC; frame.mac[1] = 0xCC; frame.mac[2] = 0xCC;
    frame.mac[3] = 0xAA; frame.mac[4] = 0xAA; frame.mac[5] = 0xAA;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.shouldAlert == false);
}

// ============================================================
// BLE scoring
// ============================================================

TEST_CASE("ThreatAnalyzer: BLE Raven UUID produces acoustic_detector category") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("", -60, "00003100-0000-1000-8000-00805f9b34fb");
    device.mac[5] = 0x30;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(strcmp(lastThreat.category, "acoustic_detector") == 0);
    CHECK(strcmp(lastThreat.radioType, "bluetooth") == 0);
    CHECK(lastThreat.channel == 0);
}

TEST_CASE("ThreatAnalyzer: BLE name-only produces surveillance_device category") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("Flock Tracker", -60);
    device.mac[5] = 0x40;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(strcmp(lastThreat.category, "surveillance_device") == 0);
}

TEST_CASE("ThreatAnalyzer: BLE no-match produces no threat") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("AirPods Pro", -60);
    device.mac[5] = 0x50;
    analyzer.analyzeBluetoothDevice(device);
    CHECK(threatCount == 0);
}

// ============================================================
// Heartbeat tick
// ============================================================

TEST_CASE("ThreatAnalyzer: tick returns false when no devices tracked") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    CHECK_FALSE(analyzer.tick(0));
    CHECK_FALSE(analyzer.tick(HEARTBEAT_INTERVAL_MS));
}

TEST_CASE("ThreatAnalyzer: tick returns true when high-confidence device in range") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 1000;

    // Inject a high-certainty device
    auto frame = makeWiFiFrame("Flock-a1b2c3", -60);
    frame.mac[5] = 0x60;
    analyzer.analyzeWiFiFrame(frame);
    // Second detection to move to IN_RANGE
    mock_millis_value = 2000;
    analyzer.analyzeWiFiFrame(frame);

    // First tick at heartbeat interval should fire
    bool heartbeat = analyzer.tick(HEARTBEAT_INTERVAL_MS);
    CHECK(heartbeat == true);
}

TEST_CASE("ThreatAnalyzer: tick returns false before heartbeat interval") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 1000;

    auto frame = makeWiFiFrame("Flock-a1b2c3", -60);
    frame.mac[5] = 0x70;
    analyzer.analyzeWiFiFrame(frame);
    mock_millis_value = 2000;
    analyzer.analyzeWiFiFrame(frame);

    // Tick before interval elapses
    CHECK_FALSE(analyzer.tick(HEARTBEAT_INTERVAL_MS - 1));
}
