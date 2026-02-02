#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "EventBus.h"
#include "DeviceSignatures.h"
#include "src/RadioScanner.h"
#include "ThreatAnalyzer.h"
#include "TelemetryReporter.h"

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
TelemetryReporter reporter;

// Event bus handler implementations
EventBus::WiFiFrameHandler EventBus::wifiHandler = nullptr;
EventBus::BluetoothHandler EventBus::bluetoothHandler = nullptr;
EventBus::ThreatHandler EventBus::threatHandler = nullptr;
EventBus::SystemEventHandler EventBus::systemReadyHandler = nullptr;
EventBus::AudioHandler EventBus::audioHandler = nullptr;

namespace {
    const uint16_t STARTUP_BEEP_FREQ = 2000;
    const uint16_t ALERT_BEEP_FREQ = 2600;
    const uint16_t BEEP_DURATION_MS = 80;
    const uint16_t BEEP_GAP_MS = 60;
    const uint16_t RADAR_LINE_COLOR = TFT_GREEN;
    const uint16_t STATUS_TEXT_COLOR = TFT_WHITE;
    const uint32_t DOT_UPDATE_MS = 400;
    const uint32_t BATTERY_UPDATE_MS = 3000;
    const uint32_t SWEEP_UPDATE_MS = 10;
    const uint8_t MAX_DOTS = 3;
    const uint32_t RSSI_UPDATE_MS = 300;
    const int8_t RSSI_MIN_DBM = -100;
    const int8_t RSSI_MAX_DBM = -20;
    const uint8_t RSSI_HISTORY_LEN = 80;
    const uint16_t RSSI_LINE_COLOR = TFT_CYAN;
    const uint32_t ALERT_DURATION_MS = 4000;
    const uint32_t ALERT_FLASH_MS = 300;
    const uint16_t ALERT_BEEP_MS = 180;
    const uint32_t SCREEN_ON_MS = 4000;
    const uint32_t POWER_SAVE_MSG_MS = 2000;
    const uint32_t STATUS_MSG_MS = 1500;
    const uint8_t DISPLAY_BRIGHTNESS_ON = 80;

    void playBeepPattern(uint16_t frequency, uint16_t durationMs, uint8_t count) {
        for (uint8_t i = 0; i < count; i++) {
            M5.Speaker.tone(frequency, durationMs);
            delay(durationMs + BEEP_GAP_MS);
        }
    }

    int8_t rssiHistory[RSSI_HISTORY_LEN];
    uint8_t rssiIndex = 0;
    bool rssiHistoryInitialized = false;
    int8_t latestRssi = RSSI_MIN_DBM;
    bool alertActive = false;
    bool alertVisible = false;
    uint32_t alertStartMs = 0;
    uint32_t alertLastFlashMs = 0;
    uint32_t alertUntilMs = 0;
    uint32_t detectionCount = 0;
    bool powerSaverEnabled = true;
    portMUX_TYPE threatMux = portMUX_INITIALIZER_UNLOCKED;
    volatile bool threatPending = false;
    ThreatEvent pendingThreat;
    portMUX_TYPE wifiMux = portMUX_INITIALIZER_UNLOCKED;
    volatile bool wifiFramePending = false;
    WiFiFrameEvent pendingWiFiFrame;
    portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;
    volatile bool bleDevicePending = false;
    BluetoothDeviceEvent pendingBleDevice;
    bool statusMessageActive = false;
    uint32_t statusMessageUntilMs = 0;

    enum class DisplayState {
        Awake,
        PowerSaveMessage,
        Off
    };

    DisplayState displayState = DisplayState::Awake;
    uint32_t displayStateMs = 0;

    int16_t graphTop() {
        int16_t lineHeight = M5.Display.fontHeight();
        return (lineHeight * 2) + 4 + 2;
    }


    void setDisplayOn() {
        M5.Display.wakeup();
        M5.Display.setBrightness(DISPLAY_BRIGHTNESS_ON);
    }

    void setDisplayOff() {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
    }

    void ensureRssiHistoryInitialized() {
        if (rssiHistoryInitialized) {
            return;
        }
        for (uint8_t i = 0; i < RSSI_HISTORY_LEN; i++) {
            rssiHistory[i] = -70;
        }
        rssiIndex = 0;
        rssiHistoryInitialized = true;
    }

    void addRssiSample(int8_t rssi) {
        ensureRssiHistoryInitialized();
        if (rssi < RSSI_MIN_DBM) rssi = RSSI_MIN_DBM;
        if (rssi > RSSI_MAX_DBM) rssi = RSSI_MAX_DBM;
        rssiHistory[rssiIndex] = rssi;
        rssiIndex = (rssiIndex + 1) % RSSI_HISTORY_LEN;
    }

    void drawRssiChart() {
        int16_t top = graphTop();
        int16_t bottom = M5.Display.height() - 1;
        if (bottom <= top + 6) return;

        int16_t width = M5.Display.width();
        int16_t height = bottom - top;

        M5.Display.fillRect(0, top, width, height, TFT_BLACK);
        M5.Display.drawRect(0, top, width, height, TFT_WHITE);

        int16_t plotTop = top + 1;
        int16_t plotBottom = bottom - 1;
        int16_t plotHeight = plotBottom - plotTop;

        int16_t lastX = -1;
        int16_t lastY = -1;
        for (uint8_t i = 0; i < RSSI_HISTORY_LEN; i++) {
            uint8_t idx = (rssiIndex + i) % RSSI_HISTORY_LEN;
            int16_t x = (int32_t)i * (width - 3) / (RSSI_HISTORY_LEN - 1) + 1;
            int16_t rssi = rssiHistory[idx];
            int32_t norm = (int32_t)(rssi - RSSI_MIN_DBM) * (plotHeight - 1) / (RSSI_MAX_DBM - RSSI_MIN_DBM);
            int16_t y = plotBottom - norm;
            if (lastX >= 0) {
                M5.Display.drawLine(lastX, lastY, x, y, RSSI_LINE_COLOR);
            }
            lastX = x;
            lastY = y;
        }
    }

    void drawGraphBox() {
        int16_t top = graphTop();
        int16_t bottom = M5.Display.height() - 1;
        int16_t height = bottom - top + 1;
        M5.Display.drawRect(0, top, M5.Display.width(), height, TFT_WHITE);
    }

    uint16_t batteryColor(uint8_t percent) {
        if (percent <= 35) {
            return TFT_RED;
        }
        if (percent <= 75) {
            return TFT_BLUE;
        }
        return TFT_GREEN;
    }

    void drawBatteryPercent(uint8_t percent) {
        char text[8];
        snprintf(text, sizeof(text), "%u%%", percent);
        int16_t textWidth = M5.Display.textWidth(text);
        int16_t height = M5.Display.fontHeight();
        int16_t x = M5.Display.width() - textWidth;
        M5.Display.fillRect(x - 2, 0, textWidth + 2, height, TFT_BLACK);
        M5.Display.setCursor(x, 0);
        M5.Display.setTextColor(batteryColor(percent), TFT_BLACK);
        M5.Display.print(text);
    }

    void drawDetectionCount(uint32_t count) {
        char text[12];
        snprintf(text, sizeof(text), "%u", count);
        int16_t textWidth = M5.Display.textWidth(text);
        int16_t height = M5.Display.fontHeight();
        int16_t x = M5.Display.width() - textWidth;
        int16_t y = height + 2;
        M5.Display.fillRect(x - 2, y, textWidth + 2, height, TFT_BLACK);
        M5.Display.setCursor(x, y);
        M5.Display.setTextColor(RADAR_LINE_COLOR, TFT_BLACK);
        M5.Display.print(text);
    }

    void drawBatteryArea(uint8_t percent, uint32_t count) {
        drawBatteryPercent(percent);
        drawDetectionCount(count);
    }

    void drawStatusText(uint8_t channel, uint8_t dots) {
        char text[16];
        char dotText[4] = "...";
        dotText[dots] = '\0';
        snprintf(text, sizeof(text), "Scanning%s", dotText);

        int16_t lineHeight = M5.Display.fontHeight();
        int16_t width = M5.Display.width();
        int16_t statusHeight = (lineHeight * 2) + 4;

        M5.Display.fillRect(0, 0, width, statusHeight, TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.setTextColor(STATUS_TEXT_COLOR, TFT_BLACK);
        M5.Display.print(text);
        M5.Display.setCursor(0, lineHeight + 4);
        M5.Display.setTextColor(STATUS_TEXT_COLOR, TFT_BLACK);
        M5.Display.printf("WiFi ch: %u", channel);
    }

    void initScanningUi(uint8_t channel, uint32_t nowMs) {
        setDisplayOn();
        M5.Display.clear(TFT_BLACK);
        M5.Display.setTextColor(STATUS_TEXT_COLOR, TFT_BLACK);
        M5.Display.setTextSize(2);
        drawStatusText(channel, 1);
        drawBatteryArea(M5.Power.getBatteryLevel(), detectionCount);
        ensureRssiHistoryInitialized();
        drawRssiChart();
        drawGraphBox();
        displayState = DisplayState::Awake;
        displayStateMs = nowMs;
    }

    void drawCenteredMessage(const char* line1, const char* line2, uint16_t bgColor, uint16_t textColor) {
        M5.Display.fillScreen(bgColor);
        M5.Display.setTextColor(textColor, bgColor);
        M5.Display.setTextSize(2);
        int16_t lineHeight = M5.Display.fontHeight();
        int16_t totalHeight = (line2 && line2[0] != '\0') ? (lineHeight * 2) + 6 : lineHeight;
        int16_t startY = (M5.Display.height() - totalHeight) / 2;
        if (startY < 0) startY = 0;
        M5.Display.setCursor(0, startY);
        M5.Display.println(line1);
        if (line2 && line2[0] != '\0') {
            M5.Display.println(line2);
        }
    }

    void drawAlertText(bool visible) {
        const char* text = "Flock Detected";
        M5.Display.setTextSize(2);
        int16_t textWidth = M5.Display.textWidth(text);
        int16_t textHeight = M5.Display.fontHeight();
        int16_t x = (M5.Display.width() - textWidth) / 2;
        int16_t y = (M5.Display.height() - textHeight) / 2;
        if (x < 0) x = 0;
        if (visible) {
            M5.Display.setTextColor(TFT_BLACK, TFT_RED);
            M5.Display.setCursor(x, y);
            M5.Display.print(text);
        } else {
            M5.Display.fillRect(x, y, textWidth, textHeight, TFT_RED);
        }
    }

    void setAlertLed(bool on) {
        M5.Power.setLed(on);
    }

    void startAlert(uint32_t nowMs) {
        setDisplayOn();
        detectionCount++;
        alertActive = true;
        alertVisible = false;
        alertStartMs = nowMs;
        alertLastFlashMs = 0;
        alertUntilMs = nowMs + ALERT_DURATION_MS;
        M5.Display.fillScreen(TFT_RED);
        drawAlertText(true);
    }

    bool updateAlert(uint32_t nowMs) {
        if (!alertActive) return false;
        if (nowMs >= alertUntilMs) {
            alertActive = false;
            setAlertLed(false);
            return false;
        }

        if (nowMs - alertLastFlashMs >= ALERT_FLASH_MS) {
            alertVisible = !alertVisible;
            if (alertVisible) {
                drawAlertText(true);
                M5.Speaker.tone(ALERT_BEEP_FREQ, ALERT_BEEP_MS);
            } else {
                drawAlertText(false);
            }
            setAlertLed(alertVisible);
            alertLastFlashMs = nowMs;
        }

        return true;
    }

    void triggerAlert(uint32_t nowMs) {
        if (!alertActive) {
            startAlert(nowMs);
            return;
        }
        detectionCount++;
        alertUntilMs = nowMs + ALERT_DURATION_MS;
    }

    void showStatusMessage(const char* line1, const char* line2) {
        setDisplayOn();
        drawCenteredMessage(line1, line2, TFT_BLACK, TFT_WHITE);
        statusMessageActive = true;
        statusMessageUntilMs = millis() + STATUS_MSG_MS;
    }

    void showPowerSaveMessage() {
        setDisplayOn();
        drawCenteredMessage("Entering power", "saving mode", TFT_BLACK, TFT_WHITE);
        displayState = DisplayState::PowerSaveMessage;
        displayStateMs = millis();
    }
}

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

void EventBus::publishAudioRequest(const AudioEvent& event) {
    if (audioHandler) audioHandler(event);
}

void EventBus::subscribeAudioRequest(AudioHandler handler) {
    audioHandler = handler;
}

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

uint8_t RadioScannerManager::getCurrentWifiChannel() {
    return currentWifiChannel;
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
    bool isProbeResponse = (frameSubtype == 0x05);
    bool isBeacon = (frameSubtype == 0x08);

    if (!isProbeRequest && !isProbeResponse && !isBeacon) return;

    WiFiFrameEvent event;
    memset(&event, 0, sizeof(event));

    memcpy(event.mac, header->source, 6);
    event.rssi = packet->rx_ctrl.rssi;
    event.frameSubtype = frameSubtype;
    event.channel = RadioScannerManager::currentWifiChannel;

    const uint8_t* payload = rawData + sizeof(WiFi80211Header);

    if (isBeacon || isProbeResponse) {
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

volatile uint8_t RadioScannerManager::currentWifiChannel = 1;
unsigned long RadioScannerManager::lastChannelSwitch = 0;
unsigned long RadioScannerManager::lastBLEScan = 0;
NimBLEScan* RadioScannerManager::bleScanner = nullptr;
bool RadioScannerManager::isScanningBLE = false;
uint16_t RadioScannerManager::CHANNEL_SWITCH_MS = 300;
uint8_t RadioScannerManager::BLE_SCAN_SECONDS = 2;
uint32_t RadioScannerManager::BLE_SCAN_INTERVAL_MS = 5000;


// Main system initialization
void setup() {
    M5.begin();
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.clear(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("Flock Detector V1.0");
    M5.Display.println("");
    M5.Display.println("Starting...");

    playBeepPattern(STARTUP_BEEP_FREQ, BEEP_DURATION_MS, 3);

    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Initializing Threat Detection System...");
    Serial.println();
    
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
    
    threatEngine.initialize();
    reporter.initialize();
    rfScanner.initialize();
    
    Serial.println("System operational - scanning for targets");
    Serial.println();
    
    initScanningUi(RadioScannerManager::getCurrentWifiChannel(), millis());
    EventBus::publishSystemReady();
}

void loop() {
    M5.update();
    rfScanner.update();
    static uint8_t lastChannel = 0;
    static uint8_t lastBattery = 255;
    static uint8_t dots = 1;
    static int16_t sweepX = 0;
    static int16_t lastSweepX = -1;
    static int8_t sweepDir = 1;
    static uint32_t lastDotMs = 0;
    static uint32_t lastBatteryMs = 0;
    static uint32_t lastSweepMs = 0;
    static uint32_t lastRssiMs = 0;
    static bool wasAlertActive = false;
    static bool powerToggleHandled = false;
    static bool lastShouldPowerSave = false;
    static bool lastOnExternalPower = false;
    static uint32_t lastPowerCheckMs = 0;
    uint8_t channel = RadioScannerManager::getCurrentWifiChannel();
    uint32_t now = millis();

    // Check external power periodically and adjust scan/display modes
    if (now - lastPowerCheckMs >= BATTERY_UPDATE_MS) {
        bool onExternalPower = M5.Power.isCharging();
        if (onExternalPower != lastOnExternalPower) {
            RadioScannerManager::setPerformanceMode(onExternalPower);
            lastOnExternalPower = onExternalPower;
        }
        lastPowerCheckMs = now;
    }

    bool shouldPowerSave = lastOnExternalPower ? false : powerSaverEnabled;

    if (wifiFramePending) {
        WiFiFrameEvent frameCopy;
        portENTER_CRITICAL(&wifiMux);
        frameCopy = pendingWiFiFrame;
        wifiFramePending = false;
        portEXIT_CRITICAL(&wifiMux);
        latestRssi = frameCopy.rssi;
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

    if (threatEngine.tick(now)) {
        M5.Speaker.tone(1800, 40);
    }

    if (threatPending) {
        ThreatEvent threatCopy;
        portENTER_CRITICAL(&threatMux);
        threatCopy = pendingThreat;
        threatPending = false;
        portEXIT_CRITICAL(&threatMux);
        reporter.handleThreatDetection(threatCopy);
        if (threatCopy.shouldAlert) {
            triggerAlert(now);
        } else if (threatCopy.alertLevel == ALERT_SUSPICIOUS && threatCopy.firstDetection) {
            M5.Speaker.tone(1800, 60);
        }
    }

    if (M5.BtnB.pressedFor(2000) && !powerToggleHandled) {
        powerSaverEnabled = !powerSaverEnabled;
        powerToggleHandled = true;
        showStatusMessage("Power saver", powerSaverEnabled ? "ON" : "OFF");
        shouldPowerSave = powerSaverEnabled;
        lastShouldPowerSave = shouldPowerSave;
    }

    if (M5.BtnB.wasReleased()) {
        powerToggleHandled = false;
    }

    if (M5.BtnA.wasPressed() && shouldPowerSave) {
        initScanningUi(channel, now);
        lastChannel = channel;
        lastDotMs = now;
        lastBatteryMs = now;
        lastSweepMs = now;
        lastRssiMs = now;
        lastSweepX = -1;
    }

    bool isAlerting = updateAlert(now);
    if (isAlerting) {
        wasAlertActive = true;
        return;
    }
    if (wasAlertActive && !isAlerting) {
        if (shouldPowerSave) {
            setDisplayOff();
            displayState = DisplayState::Off;
        } else {
            initScanningUi(channel, now);
            lastChannel = channel;
            lastDotMs = now;
            lastBatteryMs = now;
            lastSweepMs = now;
            lastRssiMs = now;
            lastSweepX = -1;
        }
    }
    wasAlertActive = isAlerting;

    if (statusMessageActive && now >= statusMessageUntilMs) {
        statusMessageActive = false;
        if (shouldPowerSave) {
            initScanningUi(channel, now);
        } else {
            initScanningUi(channel, now);
        }
    }

    if (!isAlerting && !statusMessageActive && shouldPowerSave != lastShouldPowerSave) {
        lastShouldPowerSave = shouldPowerSave;
        if (shouldPowerSave) {
            initScanningUi(channel, now);
        } else {
            initScanningUi(channel, now);
        }
    }

    if (!isAlerting && !statusMessageActive) {
        if (shouldPowerSave) {
            if (displayState == DisplayState::Awake && now - displayStateMs >= SCREEN_ON_MS) {
                showPowerSaveMessage();
            } else if (displayState == DisplayState::PowerSaveMessage && now - displayStateMs >= POWER_SAVE_MSG_MS) {
                setDisplayOff();
                displayState = DisplayState::Off;
            }
        } else if (displayState != DisplayState::Awake) {
            initScanningUi(channel, now);
        }
    }

    if (!isAlerting && !statusMessageActive && displayState == DisplayState::Awake && channel != lastChannel) {
        drawStatusText(channel, dots);
        drawBatteryArea(M5.Power.getBatteryLevel(), detectionCount);
        lastChannel = channel;
    }

    if (!isAlerting && !statusMessageActive && displayState == DisplayState::Awake && now - lastDotMs >= DOT_UPDATE_MS) {
        dots = (dots % MAX_DOTS) + 1;
        drawStatusText(channel, dots);
        drawBatteryArea(M5.Power.getBatteryLevel(), detectionCount);
        lastDotMs = now;
    }

    if (!isAlerting && !statusMessageActive && displayState == DisplayState::Awake && now - lastBatteryMs >= BATTERY_UPDATE_MS) {
        uint8_t battery = M5.Power.getBatteryLevel();
        if (battery != lastBattery) {
            drawBatteryArea(battery, detectionCount);
            lastBattery = battery;
        }
        lastBatteryMs = now;
    }

    if (!isAlerting && !statusMessageActive && now - lastRssiMs >= RSSI_UPDATE_MS) {
        addRssiSample(latestRssi);
        if (displayState == DisplayState::Awake) {
            drawRssiChart();
            drawGraphBox();
        }
        lastRssiMs = now;
    }

    if (!isAlerting && !statusMessageActive && displayState == DisplayState::Awake && now - lastSweepMs >= SWEEP_UPDATE_MS) {
        int16_t width = M5.Display.width();
        int16_t height = M5.Display.height();
        int16_t sweepTopValue = graphTop();

        if (lastSweepX >= 0) {
            M5.Display.drawLine(lastSweepX, sweepTopValue, lastSweepX, height - 1, TFT_BLACK);
        }
        drawGraphBox();

        M5.Display.drawLine(sweepX, sweepTopValue, sweepX, height - 1, RADAR_LINE_COLOR);
        lastSweepX = sweepX;

        sweepX += sweepDir;
        if (sweepX <= 0 || sweepX >= width - 1) {
            sweepDir = -sweepDir;
            sweepX += sweepDir;
        }

        lastSweepMs = now;
    }
    delay(30);
}
