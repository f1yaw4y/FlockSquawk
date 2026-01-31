#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#define FLOCK_RGB_AVAILABLE 1
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "EventBus.h"
#include "DeviceSignatures.h"
#include "src/RadioScanner.h"
#include "ThreatAnalyzer.h"
#include "src/TelemetryReporter.h"

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
TelemetryReporter reporter;

// ESP32-S2 UART pin mapping for the official Flipper WiFi Dev Board.
// UART0 is wired to the Flipper GPIO header as TX=GPIO43, RX=GPIO44.
// For non-S2 builds, default Serial pins are used.
#if FLOCK_TARGET_ESP32S2
static const int UART_TX_PIN = 43;
static const int UART_RX_PIN = 44;
// Official WiFi Dev Board uses a discrete RGB LED (active-low) on GPIO 4/5/6.
// This matches the Marauder implementation for Flipper dev boards.
static const int LED_B_PIN = 4;
static const int LED_G_PIN = 5;
static const int LED_R_PIN = 6;
#endif

enum class LedMode {
    Boot,
    Scanning,
    Alert
};

static LedMode ledMode = LedMode::Boot;
static unsigned long lastLedToggleMs = 0;
static bool ledOn = false;

#if FLOCK_TARGET_ESP32S2 && FLOCK_RGB_AVAILABLE
static void initRgb() {
    pinMode(LED_B_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_R_PIN, OUTPUT);
    // Active-low: HIGH = off.
    digitalWrite(LED_B_PIN, HIGH);
    digitalWrite(LED_G_PIN, HIGH);
    digitalWrite(LED_R_PIN, HIGH);
}

static void setRgb(bool r_on, bool g_on, bool b_on) {
    // Active-low LED control.
    digitalWrite(LED_R_PIN, r_on ? LOW : HIGH);
    digitalWrite(LED_G_PIN, g_on ? LOW : HIGH);
    digitalWrite(LED_B_PIN, b_on ? LOW : HIGH);
}

static void ledBootAnimation() {
    // Cycle RGB at boot to confirm the LED is alive.
    for (uint8_t i = 0; i < 3; i++) {
        setRgb(true, false, false);
        delay(120);
        setRgb(false, true, false);
        delay(120);
        setRgb(false, false, true);
        delay(120);
    }
    setRgb(false, false, false);
}

static void updateLed() {
    const unsigned long now = millis();
    switch (ledMode) {
        case LedMode::Alert:
            // Solid red when an alert is active.
            setRgb(true, false, false);
            break;
        case LedMode::Scanning:
            // Flash blue while scanning.
            if (now - lastLedToggleMs >= 400) {
                lastLedToggleMs = now;
                ledOn = !ledOn;
                if (ledOn) {
                    setRgb(false, false, true);
                } else {
                    setRgb(false, false, false);
                }
            }
            break;
        case LedMode::Boot:
        default:
            break;
    }
}
#endif

// Event bus handler implementations
EventBus::WiFiFrameHandler EventBus::wifiHandler = nullptr;
EventBus::BluetoothHandler EventBus::bluetoothHandler = nullptr;
EventBus::ThreatHandler EventBus::threatHandler = nullptr;
EventBus::SystemEventHandler EventBus::systemReadyHandler = nullptr;
EventBus::AudioHandler EventBus::audioHandler = nullptr;

void EventBus::publishWifiFrame(const WiFiFrameEvent& event) {
    if (wifiHandler) wifiHandler(event);
}

void EventBus::publishBluetoothDevice(const BluetoothDeviceEvent& event) {
    if (bluetoothHandler) bluetoothHandler(event);
}

void EventBus::publishThreat(const ThreatEvent& event) {
    if (threatHandler) threatHandler(event);
}

void EventBus::publishSystemReady() {
    if (systemReadyHandler) systemReadyHandler();
}

void EventBus::publishAudioRequest(const AudioEvent& event) {
    if (audioHandler) audioHandler(event);
}

void EventBus::subscribeWifiFrame(WiFiFrameHandler handler) {
    wifiHandler = handler;
}

void EventBus::subscribeBluetoothDevice(BluetoothHandler handler) {
    bluetoothHandler = handler;
}

void EventBus::subscribeThreat(ThreatHandler handler) {
    threatHandler = handler;
}

void EventBus::subscribeSystemReady(SystemEventHandler handler) {
    systemReadyHandler = handler;
}

void EventBus::subscribeAudioRequest(AudioHandler handler) {
    audioHandler = handler;
}

// Thread-safe deferred event processing
static portMUX_TYPE wifiMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool wifiFramePending = false;
static WiFiFrameEvent pendingWiFiFrame;
static portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool bleDevicePending = false;
static BluetoothDeviceEvent pendingBleDevice;
static portMUX_TYPE threatMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool threatPending = false;
static ThreatEvent pendingThreat;

// RadioScannerManager implementation
void RadioScannerManager::initialize() {
    configureWiFiSniffer();
    configureBluetoothScanner();
}

void RadioScannerManager::configureWiFiSniffer() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(wifiPacketHandler);
    esp_wifi_set_channel(currentWifiChannel, WIFI_SECOND_CHAN_NONE);
    // Keep UART clean for line-based protocol output; no debug prints here.
}

void RadioScannerManager::configureBluetoothScanner() {
#if FLOCK_BLE_SUPPORTED
    NimBLEDevice::init("");
    bleScanner = NimBLEDevice::getScan();
    bleScanner->setActiveScan(true);
    bleScanner->setInterval(100);
    bleScanner->setWindow(99);
    
    class BLEDeviceObserver : public NimBLEScanCallbacks {
        void onResult(const NimBLEAdvertisedDevice* device) override {
            BluetoothDeviceEvent event;
            memset(&event, 0, sizeof(event));
            
            NimBLEAddress addr = device->getAddress();
            std::string addrStr = addr.toString();
            sscanf(addrStr.c_str(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &event.mac[0], &event.mac[1], &event.mac[2],
                   &event.mac[3], &event.mac[4], &event.mac[5]);
            
            event.rssi = device->getRSSI();
            
            if (device->haveName()) {
                strncpy(event.name, device->getName().c_str(), sizeof(event.name) - 1);
            }
            
            event.hasServiceUUID = device->haveServiceUUID();
            if (event.hasServiceUUID && device->getServiceUUIDCount() > 0) {
                NimBLEUUID uuid = device->getServiceUUID(0);
                strncpy(event.serviceUUID, uuid.toString().c_str(), sizeof(event.serviceUUID) - 1);
            }
            
            EventBus::publishBluetoothDevice(event);
        }
        
        void onScanEnd(const NimBLEScanResults& results, int reason) override {
            // Scan completed
        }
    };
    
    bleScanner->setScanCallbacks(new BLEDeviceObserver());
    // Keep UART clean for line-based protocol output; no debug prints here.
#else
    // ESP32-S2 has no BLE; inform the Flipper app once via STATUS.
    Serial.println("STATUS,BLE_UNSUPPORTED");
#endif
}

void RadioScannerManager::update() {
    switchWifiChannel();
    performBLEScan();
}

void RadioScannerManager::switchWifiChannel() {
    unsigned long now = millis();
    if (now - lastChannelSwitch >= CHANNEL_SWITCH_MS) {
        currentWifiChannel++;
        if (currentWifiChannel > MAX_WIFI_CHANNEL) {
            currentWifiChannel = 1;
        }
        esp_wifi_set_channel(currentWifiChannel, WIFI_SECOND_CHAN_NONE);
        lastChannelSwitch = now;
    }
}

void RadioScannerManager::performBLEScan() {
#if FLOCK_BLE_SUPPORTED
    unsigned long now = millis();
    if (now - lastBLEScan >= BLE_SCAN_INTERVAL_MS && !isScanningBLE) {
        if (bleScanner && !bleScanner->isScanning()) {
            bleScanner->start(BLE_SCAN_SECONDS, false);
            isScanningBLE = true;
            lastBLEScan = now;
        }
    }
    
    if (isScanningBLE && bleScanner && !bleScanner->isScanning()) {
        if (now - lastBLEScan > BLE_SCAN_SECONDS * 1000) {
            bleScanner->clearResults();
            isScanningBLE = false;
        }
    }
#else
    (void)0;
#endif
}

struct WiFi80211Header {
    uint16_t frameControl;
    uint16_t duration;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t bssid[6];
    uint16_t sequence;
};

void RadioScannerManager::wifiPacketHandler(void* buffer, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*)buffer;
    const uint8_t* rawData = packet->payload;
    
    if (packet->rx_ctrl.sig_len < 24) return;
    
    const WiFi80211Header* header = (const WiFi80211Header*)rawData;
    uint8_t frameSubtype = (header->frameControl & 0x00F0) >> 4;
    
    bool isProbeRequest = (frameSubtype == 0x04);
    bool isBeacon = (frameSubtype == 0x08);
    
    if (!isProbeRequest && !isBeacon) return;
    
    WiFiFrameEvent event;
    memset(&event, 0, sizeof(event));
    
    memcpy(event.mac, header->source, 6);
    event.rssi = packet->rx_ctrl.rssi;
    event.frameSubtype = frameSubtype;
    event.channel = RadioScannerManager::currentWifiChannel;
    
    const uint8_t* payload = rawData + sizeof(WiFi80211Header);
    
    if (isBeacon) {
        payload += 12;
    }
    
    if (packet->rx_ctrl.sig_len > (payload - rawData) + 2) {
        if (payload[0] == 0 && payload[1] <= 32) {
            size_t ssidLen = payload[1];
            memcpy(event.ssid, payload + 2, ssidLen);
            event.ssid[ssidLen] = '\0';
        }
    }
    
    EventBus::publishWifiFrame(event);
}

uint8_t RadioScannerManager::currentWifiChannel = 1;
unsigned long RadioScannerManager::lastChannelSwitch = 0;
unsigned long RadioScannerManager::lastBLEScan = 0;
#if FLOCK_BLE_SUPPORTED
NimBLEScan* RadioScannerManager::bleScanner = nullptr;
bool RadioScannerManager::isScanningBLE = false;
#endif

// TelemetryReporter implementation
void TelemetryReporter::initialize() {
    bootTime = millis();
    alertActive = false;
    lastAlertMs = 0;
    lastSeenMs = 0;
    emitStatus("SCANNING");
}

void TelemetryReporter::handleThreatDetection(const ThreatEvent& threat) {
    emitAlert(threat);
    alertActive = true;
    lastAlertMs = millis();
}

void TelemetryReporter::handleWiFiFrameSeen(const WiFiFrameEvent& frame) {
    const unsigned long now = millis();
    if (now - lastSeenMs < SEEN_THROTTLE_MS) return;
    lastSeenMs = now;
    emitSeen(frame);
}

void TelemetryReporter::update() {
    if (alertActive && (millis() - lastAlertMs > ALERT_CLEAR_MS)) {
        emitClear();
        alertActive = false;
        emitStatus("SCANNING");
    }
}

void TelemetryReporter::emitAlert(const ThreatEvent& threat) {
    // Line-based protocol for the Flipper app: ALERT,... + newline.
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             threat.mac[0], threat.mac[1], threat.mac[2],
             threat.mac[3], threat.mac[4], threat.mac[5]);

    Serial.printf("ALERT,RSSI=%d,MAC=%s", threat.rssi, macStr);
    if (threat.radioType[0] != '\0') {
        Serial.printf(",RADIO=%s", threat.radioType);
    }
    if (threat.channel > 0) {
        Serial.printf(",CH=%u", threat.channel);
    }
    if (strlen(threat.identifier) > 0) {
        Serial.printf(",ID=%s", threat.identifier);
    }
    if (threat.certainty > 0) {
        Serial.printf(",CERTAINTY=%u", threat.certainty);
    }
    Serial.println();
}

void TelemetryReporter::emitSeen(const WiFiFrameEvent& frame) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             frame.mac[0], frame.mac[1], frame.mac[2],
             frame.mac[3], frame.mac[4], frame.mac[5]);
    Serial.printf("SEEN,RSSI=%d,MAC=%s,CH=%u\n", frame.rssi, macStr, frame.channel);
}

void TelemetryReporter::emitClear() {
    Serial.println("CLEAR");
}

void TelemetryReporter::emitStatus(const char* state) {
    Serial.printf("STATUS,%s\n", state);
}

bool TelemetryReporter::isAlertActive() const {
    return alertActive;
}

// Main system initialization
void setup() {
#if FLOCK_TARGET_ESP32S2
    Serial.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
#else
    Serial.begin(115200);
#endif
    delay(1000);

#if FLOCK_TARGET_ESP32S2 && FLOCK_RGB_AVAILABLE
    initRgb();
    ledBootAnimation();
    ledMode = LedMode::Scanning;
#endif
    
    EventBus::subscribeWifiFrame([](const WiFiFrameEvent& event) {
        portENTER_CRITICAL(&wifiMux);
        pendingWiFiFrame = event;
        wifiFramePending = true;
        portEXIT_CRITICAL(&wifiMux);
    });

    EventBus::subscribeBluetoothDevice([](const BluetoothDeviceEvent& event) {
        portENTER_CRITICAL(&bleMux);
        pendingBleDevice = event;
        bleDevicePending = true;
        portEXIT_CRITICAL(&bleMux);
    });

    EventBus::subscribeThreat([](const ThreatEvent& event) {
        portENTER_CRITICAL(&threatMux);
        pendingThreat = event;
        threatPending = true;
        portEXIT_CRITICAL(&threatMux);
    });
    
    EventBus::subscribeSystemReady([]() {
        // Reserved for future system-ready hooks.
    });
    
    threatEngine.initialize();
    reporter.initialize();
    rfScanner.initialize();
    
    EventBus::publishSystemReady();
}

void loop() {
    rfScanner.update();
    uint32_t now = millis();

    if (wifiFramePending) {
        WiFiFrameEvent frameCopy;
        portENTER_CRITICAL(&wifiMux);
        frameCopy = pendingWiFiFrame;
        wifiFramePending = false;
        portEXIT_CRITICAL(&wifiMux);
        reporter.handleWiFiFrameSeen(frameCopy);
        threatEngine.analyzeWiFiFrame(frameCopy);
    }

    if (bleDevicePending) {
        BluetoothDeviceEvent bleCopy;
        portENTER_CRITICAL(&bleMux);
        bleCopy = pendingBleDevice;
        bleDevicePending = false;
        portEXIT_CRITICAL(&bleMux);
        threatEngine.analyzeBluetoothDevice(bleCopy);
    }

    threatEngine.tick(now);

    if (threatPending) {
        ThreatEvent threatCopy;
        portENTER_CRITICAL(&threatMux);
        threatCopy = pendingThreat;
        threatPending = false;
        portEXIT_CRITICAL(&threatMux);
        if (threatCopy.shouldAlert) {
            reporter.handleThreatDetection(threatCopy);
        }
    }

    reporter.update();
#if FLOCK_TARGET_ESP32S2 && FLOCK_RGB_AVAILABLE
    if (reporter.isAlertActive()) {
        ledMode = LedMode::Alert;
    } else {
        ledMode = LedMode::Scanning;
    }
    updateLed();
#endif
    delay(100);
}
