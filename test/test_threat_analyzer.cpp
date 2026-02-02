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
// WiFi basic detection
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

TEST_CASE("ThreatAnalyzer: WiFi RSSI modifier applied") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("Flock-a1b2c3", -30);
    frame.mac[5] = 0x10;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.rssiModifier == 10);
}

TEST_CASE("ThreatAnalyzer: WiFi certainty clamped to 100") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("Flock-a1b2c3", -30);
    frame.mac[0] = 0x58; frame.mac[1] = 0x8E; frame.mac[2] = 0x81;
    frame.mac[3] = 0x99; frame.mac[4] = 0x99; frame.mac[5] = 0x99;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.certainty == 100);
}

// ============================================================
// WiFi alert level tiers
// ============================================================

TEST_CASE("ThreatAnalyzer: SSID format match is CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    analyzer.analyzeWiFiFrame(makeWiFiFrame("Flock-a1b2c3", -60));
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
    CHECK(lastThreat.shouldAlert == true);
    CHECK((lastThreat.matchFlags & DET_SSID_FORMAT) != 0);
}

TEST_CASE("ThreatAnalyzer: SSID format without OUI is still CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // MAC doesn't match any known OUI
    auto frame = makeWiFiFrame("Flock-a1b2c3", -60);
    frame.mac[0] = 0xAA; frame.mac[1] = 0xBB; frame.mac[2] = 0xCC;
    frame.mac[3] = 0x01; frame.mac[4] = 0x02; frame.mac[5] = 0x03;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
}

TEST_CASE("ThreatAnalyzer: SSID keyword + Lite-On OUI is CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("test_flck", -60);
    frame.mac[0] = 0x58; frame.mac[1] = 0x8E; frame.mac[2] = 0x81;
    frame.mac[3] = 0xD1; frame.mac[4] = 0xD2; frame.mac[5] = 0xD3;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
    CHECK(lastThreat.shouldAlert == true);
}

TEST_CASE("ThreatAnalyzer: SSID keyword alone is SUSPICIOUS") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // "flock" keyword but no known OUI
    auto frame = makeWiFiFrame("Flockdale WiFi", -60);
    frame.mac[0] = 0xAA; frame.mac[1] = 0xBB; frame.mac[2] = 0xCC;
    frame.mac[3] = 0xE1; frame.mac[4] = 0xE2; frame.mac[5] = 0xE3;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_SUSPICIOUS);
    CHECK(lastThreat.shouldAlert == false);
}

TEST_CASE("ThreatAnalyzer: Lite-On OUI + hidden SSID is SUSPICIOUS") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("", -60);
    frame.ssid[0] = '\0';
    frame.mac[0] = 0x58; frame.mac[1] = 0x8E; frame.mac[2] = 0x81;
    frame.mac[3] = 0xC0; frame.mac[4] = 0xC0; frame.mac[5] = 0xC0;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_SUSPICIOUS);
    CHECK(lastThreat.shouldAlert == false);
}

TEST_CASE("ThreatAnalyzer: Lite-On OUI + visible non-matching SSID is NONE") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("SomeNetwork", -60);
    frame.mac[0] = 0x58; frame.mac[1] = 0x8E; frame.mac[2] = 0x81;
    frame.mac[3] = 0xC4; frame.mac[4] = 0xC4; frame.mac[5] = 0xC4;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_NONE);
    CHECK(lastThreat.shouldAlert == false);
}

TEST_CASE("ThreatAnalyzer: B4:1E:52 Flock Safety OUI is CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("SomeSSID", -60);
    frame.mac[0] = 0xB4; frame.mac[1] = 0x1E; frame.mac[2] = 0x52;
    frame.mac[3] = 0xAA; frame.mac[4] = 0xBB; frame.mac[5] = 0xCC;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
    CHECK((lastThreat.matchFlags & DET_FLOCK_OUI) != 0);
    CHECK(lastThreat.shouldAlert == true);
}

// ============================================================
// shouldAlert / firstDetection logic
// ============================================================

TEST_CASE("ThreatAnalyzer: shouldAlert true on first CONFIRMED detection") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    analyzer.analyzeWiFiFrame(makeWiFiFrame("Flock-a1b2c3", -60));
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.shouldAlert == true);
    CHECK(lastThreat.firstDetection == true);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
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
    CHECK(lastThreat.firstDetection == false);
}

TEST_CASE("ThreatAnalyzer: shouldAlert false for SUSPICIOUS level") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // Hidden SSID + OUI = SUSPICIOUS, not CONFIRMED
    auto frame = makeWiFiFrame("", -60);
    frame.ssid[0] = '\0';
    frame.mac[0] = 0xCC; frame.mac[1] = 0xCC; frame.mac[2] = 0xCC;
    frame.mac[3] = 0xAA; frame.mac[4] = 0xAA; frame.mac[5] = 0xAA;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_SUSPICIOUS);
    CHECK(lastThreat.shouldAlert == false);
    CHECK(lastThreat.firstDetection == true);
}

// ============================================================
// BLE alert levels
// ============================================================

TEST_CASE("ThreatAnalyzer: BLE Raven custom UUID is CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("", -60, "00003100-0000-1000-8000-00805f9b34fb");
    device.mac[5] = 0x30;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
    CHECK(strcmp(lastThreat.category, "acoustic_detector") == 0);
    CHECK(strcmp(lastThreat.radioType, "bluetooth") == 0);
    CHECK(lastThreat.channel == 0);
}

TEST_CASE("ThreatAnalyzer: BLE name match is CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("Flock Tracker", -60);
    device.mac[5] = 0x40;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
    CHECK(strcmp(lastThreat.category, "surveillance_device") == 0);
}

TEST_CASE("ThreatAnalyzer: BLE Flock Safety OUI is CONFIRMED") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("", -60);
    device.mac[0] = 0xB4; device.mac[1] = 0x1E; device.mac[2] = 0x52;
    device.mac[3] = 0xDD; device.mac[4] = 0xEE; device.mac[5] = 0xFF;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_CONFIRMED);
    CHECK((lastThreat.matchFlags & DET_FLOCK_OUI) != 0);
}

TEST_CASE("ThreatAnalyzer: BLE Lite-On OUI only is SUSPICIOUS") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("", -60);
    device.mac[0] = 0x58; device.mac[1] = 0x8E; device.mac[2] = 0x81;
    device.mac[3] = 0xA1; device.mac[4] = 0xA2; device.mac[5] = 0xA3;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_SUSPICIOUS);
    CHECK(lastThreat.shouldAlert == false);
}

TEST_CASE("ThreatAnalyzer: BLE Raven std UUID only is SUSPICIOUS") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto device = makeBLEDevice("", -60, "0000180a-0000-1000-8000-00805f9b34fb");
    device.mac[0] = 0xAA; device.mac[1] = 0xBB; device.mac[2] = 0xCC;
    device.mac[3] = 0xB1; device.mac[4] = 0xB2; device.mac[5] = 0xB3;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_SUSPICIOUS);
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

TEST_CASE("ThreatAnalyzer: tick returns true when confirmed device in range") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 1000;

    // Inject a CONFIRMED device
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

// ============================================================
// test_flck keyword
// ============================================================

TEST_CASE("ThreatAnalyzer: test_flck keyword triggers detection") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    auto frame = makeWiFiFrame("test_flck", -60);
    frame.mac[3] = 0xF1; frame.mac[4] = 0xF2; frame.mac[5] = 0xF3;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK((lastThreat.matchFlags & DET_SSID_KEYWORD) != 0);
}

// ============================================================
// Surveillance camera OUI detection
// ============================================================

TEST_CASE("ThreatAnalyzer: Axis Communications OUI produces INFO alert") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // Axis Communications OUI: 00:40:8c
    auto frame = makeWiFiFrame("AxisCam", -60);
    frame.mac[0] = 0x00; frame.mac[1] = 0x40; frame.mac[2] = 0x8C;
    frame.mac[3] = 0x01; frame.mac[4] = 0x02; frame.mac[5] = 0x03;
    analyzer.analyzeWiFiFrame(frame);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_INFO);
    CHECK((lastThreat.matchFlags & DET_SURVEILLANCE_OUI) != 0);
    CHECK(strcmp(lastThreat.category, "surveillance_camera") == 0);
    CHECK(lastThreat.shouldAlert == false);
    // Verify weight stored at correct bit position (was an OOB bug when array was [8])
    CHECK(lastThreat.detectorWeights[detectorBitPosition(DET_SURVEILLANCE_OUI)] == 30);
}

TEST_CASE("ThreatAnalyzer: BLE Hanwha Vision OUI produces INFO alert") {
    ThreatAnalyzer analyzer;
    analyzer.initialize();
    resetCapture();
    mock_millis_value = 5000;

    // Hanwha Vision OUI: 44:b4:23
    auto device = makeBLEDevice("", -60);
    device.mac[0] = 0x44; device.mac[1] = 0xB4; device.mac[2] = 0x23;
    device.mac[3] = 0x01; device.mac[4] = 0x02; device.mac[5] = 0x03;
    analyzer.analyzeBluetoothDevice(device);
    REQUIRE(threatCount == 1);
    CHECK(lastThreat.alertLevel == ALERT_INFO);
    CHECK((lastThreat.matchFlags & DET_SURVEILLANCE_OUI) != 0);
    CHECK(strcmp(lastThreat.category, "surveillance_camera") == 0);
}
