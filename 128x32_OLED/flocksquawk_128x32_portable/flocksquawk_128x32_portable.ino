#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "EventBus.h"
#include "DeviceSignatures.h"
#include "src/RadioScanner.h"
#include "ThreatAnalyzer.h"
#include "TelemetryReporter.h"

// 0.91" 128x32 SSD1306 OLED (I2C)
static constexpr int kScreenWidth = 128;
static constexpr int kScreenHeight = 32;
static constexpr int kOledReset = -1; // reset not used on most I2C modules
static constexpr uint8_t kI2cAddress = 0x3C;

// Seeed Studio XIAO ESP32-S3 default I2C pins.
// Change these if your wiring uses different GPIOs.
static constexpr int kSdaPin = 21;
static constexpr int kSclPin = 22;

static constexpr uint16_t kDisplayUpdateMs = 500;
static constexpr uint16_t kRadarUpdateMs = 40;
static constexpr uint16_t kReadyHoldMs = 2000;

// Piezo buzzer (active or passive) on a GPIO pin.
// Change this if your wiring uses a different GPIO.
static constexpr int kBuzzerPin = 23;

Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, kOledReset);
static bool displayReady = false;
static unsigned long lastDisplayUpdateMs = 0;
static unsigned long lastRadarUpdateMs = 0;
static int radarX = 0;
static int radarDir = 1;
static char lastStatusLine1[20] = "";
static char lastStatusLine2[20] = "";
enum class ScreenMode { Booting, ReadyHold, Radar };
static ScreenMode screenMode = ScreenMode::Booting;

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
TelemetryReporter reporter;

static void displayShowStatus(const char* line1, const char* line2 = nullptr) {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(line1);
    if (line2 && line2[0] != '\0') {
        display.println(line2);
    }
    display.display();
}

static void displayShowLargeText(const char* text) {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(text);
    display.display();
}

static void displayShowTextSize(const char* text, uint8_t size) {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextSize(size);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(text);
    display.display();
}

static void displayShowStartingAnimation() {
    if (!displayReady) return;
    const char* frames[] = {"Starting.", "Starting..", "Starting..."};
    for (int i = 0; i < 9; ++i) {
        displayShowTextSize(frames[i % 3], 1);
        delay(200);
    }
}

static void buzzerInit() {
    pinMode(kBuzzerPin, OUTPUT);
    digitalWrite(kBuzzerPin, LOW);
}

static void buzzerBeep(uint16_t frequency, uint16_t durationMs) {
    tone(kBuzzerPin, frequency);
    delay(durationMs);
    noTone(kBuzzerPin);
}

static void displayShowRadarOverlay(const char* line1, const char* line2) {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(line1);
    display.println(line2);

    int midY = kScreenHeight / 2;
    int startY = midY + 1;
    int endY = kScreenHeight - 1;
    if (radarX < 0) radarX = 0;
    if (radarX >= kScreenWidth) radarX = kScreenWidth - 1;
    display.drawLine(radarX, startY, radarX, endY, SSD1306_WHITE);
    display.display();
}

static void updateStatusLines(const char* line1, const char* line2) {
    strncpy(lastStatusLine1, line1, sizeof(lastStatusLine1) - 1);
    lastStatusLine1[sizeof(lastStatusLine1) - 1] = '\0';
    strncpy(lastStatusLine2, line2, sizeof(lastStatusLine2) - 1);
    lastStatusLine2[sizeof(lastStatusLine2) - 1] = '\0';
}

static void updateRadarSweep() {
    if (screenMode != ScreenMode::Radar || !displayReady) return;
    unsigned long now = millis();
    if (now - lastRadarUpdateMs < kRadarUpdateMs) return;
    radarX += radarDir;
    if (radarX <= 0) {
        radarX = 0;
        radarDir = 1;
    } else if (radarX >= kScreenWidth - 1) {
        radarX = kScreenWidth - 1;
        radarDir = -1;
    }
    lastRadarUpdateMs = now;
}

static void displayInit() {
    Wire.begin(kSdaPin, kSclPin);
    if (!display.begin(SSD1306_SWITCHCAPVCC, kI2cAddress)) {
        Serial.println("[OLED] init failed");
        displayReady = false;
        return;
    }
    displayReady = true;
    displayShowLargeText("Starting");
}

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
    
    Serial.println("[RF] WiFi sniffer activated");
}

void RadioScannerManager::configureBluetoothScanner() {
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
    Serial.println("[RF] Bluetooth scanner initialized");
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
NimBLEScan* RadioScannerManager::bleScanner = nullptr;
bool RadioScannerManager::isScanningBLE = false;


// Main system initialization
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Initializing Threat Detection System...");
    Serial.println();
    
    displayInit();
    screenMode = ScreenMode::Booting;
    displayShowStartingAnimation();
    buzzerInit();
    buzzerBeep(2000, 120);
    delay(80);
    buzzerBeep(2400, 120);
    
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
        screenMode = ScreenMode::ReadyHold;
        updateStatusLines("System ready", "Scanning...");
        displayShowStatus(lastStatusLine1, lastStatusLine2);
        buzzerBeep(2200, 150);
        delay(kReadyHoldMs);
        screenMode = ScreenMode::Radar;
        displayShowRadarOverlay(lastStatusLine1, lastStatusLine2);
    });
    
    threatEngine.initialize();
    reporter.initialize();
    rfScanner.initialize();
    
    Serial.println("System operational - scanning for targets");
    Serial.println();
    
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

        if (screenMode == ScreenMode::Radar && (now - lastDisplayUpdateMs >= kDisplayUpdateMs)) {
            char line1[20];
            char line2[20];
            snprintf(line1, sizeof(line1), "WiFi ch %u", frameCopy.channel);
            snprintf(line2, sizeof(line2), "RSSI %d", frameCopy.rssi);
            updateStatusLines(line1, line2);
            displayShowRadarOverlay(lastStatusLine1, lastStatusLine2);
            lastDisplayUpdateMs = now;
        }
        threatEngine.analyzeWiFiFrame(frameCopy);
    }

    if (bleDevicePending) {
        BluetoothDeviceEvent bleCopy;
        portENTER_CRITICAL(&bleMux);
        bleCopy = pendingBleDevice;
        bleDevicePending = false;
        portEXIT_CRITICAL(&bleMux);

        if (screenMode == ScreenMode::Radar && (now - lastDisplayUpdateMs >= kDisplayUpdateMs)) {
            char line1[20];
            char line2[20];
            snprintf(line1, sizeof(line1), "BLE device");
            snprintf(line2, sizeof(line2), "RSSI %d", bleCopy.rssi);
            updateStatusLines(line1, line2);
            displayShowRadarOverlay(lastStatusLine1, lastStatusLine2);
            lastDisplayUpdateMs = now;
        }
        threatEngine.analyzeBluetoothDevice(bleCopy);
    }

    if (threatEngine.tick(now)) {
        buzzerBeep(1800, 40);
    }

    if (threatPending) {
        ThreatEvent threatCopy;
        portENTER_CRITICAL(&threatMux);
        threatCopy = pendingThreat;
        threatPending = false;
        portEXIT_CRITICAL(&threatMux);
        reporter.handleThreatDetection(threatCopy);
        if (threatCopy.shouldAlert) {
            const char* label = (threatCopy.identifier[0] != '\0') ? threatCopy.identifier : "Target";
            screenMode = ScreenMode::ReadyHold;
            displayShowStatus("ALERT", label);
            buzzerBeep(2800, 120);
            delay(80);
            buzzerBeep(2800, 120);
        }
    }

    updateRadarSweep();
    if (screenMode == ScreenMode::Radar && displayReady) {
        displayShowRadarOverlay(lastStatusLine1, lastStatusLine2);
    }
    delay(20);
}