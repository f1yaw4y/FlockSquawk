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
#include "BleTransport.h"
#include "BatterySmoothing.h"
#include "ConnectionStatus.h"
#include "TelemetryReporter.h"

// TelemetryReporter::_sendViaBle requires BleTransport to be fully defined.
// Both headers are now included above, so we can provide the inline definition.
inline void TelemetryReporter::_sendViaBle(char* buf, size_t len) {
    if (_bleTransport->isClientConnected()) {
        buf[len] = '\n';
        _bleTransport->sendLine(buf, len + 1);
    }
}

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
TelemetryReporter reporter;
BleTransport bleTransport;

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
    const uint16_t STATUS_TEXT_COLOR = TFT_WHITE;
    const uint32_t DOT_UPDATE_MS = 400;
    const uint32_t BATTERY_UPDATE_MS = 3000;
    const uint8_t MAX_DOTS = 3;
    const uint32_t LIST_REFRESH_MS = 2000;
    const uint32_t DEVICE_DEPART_MS = 90000;
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

    // Device list display
    const uint8_t LIST_HEADER_H = 16;
    const uint8_t LIST_SEPARATOR_Y = 16;
    const uint8_t LIST_TOP_Y = 17;
    const uint8_t LIST_AREA_H = 116;   // 17..132 = 116px
    const uint8_t LIST_ROW_H = 14;
    const uint8_t LIST_VISIBLE_ROWS = 8;
    const uint8_t LIST_SCROLL_BAR_H = 2;
    const uint8_t MAX_DISPLAY_DEVICES = 32;

    struct DisplayDevice {
        uint8_t  mac[6];
        char     label[21];
        int8_t   rssi;
        uint8_t  alertLevel;
        char     radioType;
        uint32_t firstSeenMs;
        uint32_t lastSeenMs;
        bool     active;
    };

    DisplayDevice displayDevices[MAX_DISPLAY_DEVICES];
    uint8_t displayDeviceCount = 0;
    uint8_t scrollOffset = 0;
    LGFX_Sprite* listSprite = nullptr;
    LGFX_Sprite* headerSprite = nullptr;
    bool spriteCreated = false;

    bool alertActive = false;
    bool alertVisible = false;
    uint32_t alertStartMs = 0;
    uint32_t alertLastFlashMs = 0;
    uint32_t alertUntilMs = 0;
    uint32_t detectionCount = 0;
    bool powerSaverEnabled = false;
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

    // Battery smoothing
    BatteryFilter batteryFilter;
    uint32_t lastBatteryReadMs = 0;

    void updateBattery(uint32_t now) {
        if (now - lastBatteryReadMs < BATTERY_UPDATE_MS) return;
        lastBatteryReadMs = now;
        batteryFilter.addSample(M5.Power.getBatteryLevel());
    }

    enum class DisplayState {
        Awake,
        Debug,
        PowerSaveMessage,
        Off
    };

    DisplayState displayState = DisplayState::Awake;
    uint32_t displayStateMs = 0;

    void setDisplayOn() {
        M5.Display.wakeup();
        M5.Display.setBrightness(DISPLAY_BRIGHTNESS_ON);
    }

    void setDisplayOff() {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
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

    // --- Device list management ---

    void updateDisplayDevice(const ThreatEvent& threat, uint32_t nowMs) {
        if (threat.alertLevel == ALERT_NONE) return;

        // Find existing device by MAC
        for (uint8_t i = 0; i < displayDeviceCount; i++) {
            if (memcmp(displayDevices[i].mac, threat.mac, 6) == 0) {
                displayDevices[i].rssi = threat.rssi;
                displayDevices[i].lastSeenMs = nowMs;
                displayDevices[i].active = true;
                if (threat.alertLevel > displayDevices[i].alertLevel) {
                    displayDevices[i].alertLevel = threat.alertLevel;
                }
                // Update label if we got a better identifier
                if (threat.identifier[0] != '\0') {
                    strncpy(displayDevices[i].label, threat.identifier, 20);
                    displayDevices[i].label[20] = '\0';
                }
                return;
            }
        }

        // Add new device
        uint8_t slot = displayDeviceCount;
        if (displayDeviceCount >= MAX_DISPLAY_DEVICES) {
            // Evict oldest inactive device, or oldest overall
            uint32_t oldestMs = UINT32_MAX;
            slot = 0;
            for (uint8_t i = 0; i < MAX_DISPLAY_DEVICES; i++) {
                if (!displayDevices[i].active && displayDevices[i].lastSeenMs < oldestMs) {
                    oldestMs = displayDevices[i].lastSeenMs;
                    slot = i;
                }
            }
            if (oldestMs == UINT32_MAX) {
                // All active — evict oldest active
                for (uint8_t i = 0; i < MAX_DISPLAY_DEVICES; i++) {
                    if (displayDevices[i].lastSeenMs < oldestMs) {
                        oldestMs = displayDevices[i].lastSeenMs;
                        slot = i;
                    }
                }
            }
        } else {
            displayDeviceCount++;
        }

        DisplayDevice& dev = displayDevices[slot];
        memcpy(dev.mac, threat.mac, 6);
        dev.rssi = threat.rssi;
        dev.alertLevel = threat.alertLevel;
        dev.radioType = (threat.radioType[0] == 'B') ? 'B' : 'W';
        dev.firstSeenMs = nowMs;
        dev.lastSeenMs = nowMs;
        dev.active = true;

        if (threat.identifier[0] != '\0') {
            strncpy(dev.label, threat.identifier, 20);
            dev.label[20] = '\0';
        } else {
            snprintf(dev.label, sizeof(dev.label), "%02X:%02X:%02X:%02X:%02X:%02X",
                     threat.mac[0], threat.mac[1], threat.mac[2],
                     threat.mac[3], threat.mac[4], threat.mac[5]);
        }
    }

    void ageDisplayDevices(uint32_t nowMs) {
        for (uint8_t i = 0; i < displayDeviceCount; i++) {
            if (displayDevices[i].active && (nowMs - displayDevices[i].lastSeenMs) >= DEVICE_DEPART_MS) {
                displayDevices[i].active = false;
            }
        }
    }

    uint8_t countActiveDevices() {
        uint8_t count = 0;
        for (uint8_t i = 0; i < displayDeviceCount; i++) {
            if (displayDevices[i].active) count++;
        }
        return count;
    }

    void buildSortedIndices(uint8_t* indices, uint8_t& count) {
        count = displayDeviceCount;
        for (uint8_t i = 0; i < count; i++) indices[i] = i;

        // Simple insertion sort: active first, then by alertLevel desc, then by lastSeenMs desc
        for (uint8_t i = 1; i < count; i++) {
            uint8_t key = indices[i];
            const DisplayDevice& keyDev = displayDevices[key];
            int8_t j = i - 1;
            while (j >= 0) {
                const DisplayDevice& jDev = displayDevices[indices[j]];
                bool swap = false;
                if (keyDev.active && !jDev.active) {
                    swap = true;
                } else if (keyDev.active == jDev.active) {
                    if (keyDev.alertLevel > jDev.alertLevel) {
                        swap = true;
                    } else if (keyDev.alertLevel == jDev.alertLevel) {
                        if (keyDev.lastSeenMs > jDev.lastSeenMs) {
                            swap = true;
                        }
                    }
                }
                if (!swap) break;
                indices[j + 1] = indices[j];
                j--;
            }
            indices[j + 1] = key;
        }
    }

    // --- Drawing functions ---

    uint16_t alertColor(uint8_t level) {
        switch (level) {
            case ALERT_CONFIRMED:  return TFT_RED;
            case ALERT_SUSPICIOUS: return TFT_YELLOW;
            case ALERT_INFO:       return TFT_CYAN;
            default:               return TFT_DARKGREY;
        }
    }

    void formatAge(uint32_t ms, char* buf, uint8_t bufSize) {
        uint32_t secs = ms / 1000;
        if (secs < 60) {
            snprintf(buf, bufSize, "%us", (unsigned)secs);
        } else if (secs < 3600) {
            snprintf(buf, bufSize, "%um", (unsigned)(secs / 60));
        } else {
            snprintf(buf, bufSize, "%uh", (unsigned)(secs / 3600));
        }
    }

    void drawDeviceListHeader(uint8_t dots, uint8_t battery,
                              bool bleConnected, uint8_t serialState, bool batteryRising) {
        if (!spriteCreated) return;

        headerSprite->fillSprite(TFT_BLACK);
        headerSprite->setTextSize(2);

        // Left: "Scan..." with animated dots
        char scanText[12];
        char dotStr[4] = "...";
        dotStr[dots] = '\0';
        snprintf(scanText, sizeof(scanText), "Scan%s", dotStr);
        headerSprite->setCursor(0, 0);
        headerSprite->setTextColor(STATUS_TEXT_COLOR, TFT_BLACK);
        headerSprite->print(scanText);

        // Right side, drawn right-to-left:
        //   "X  +80%"
        int16_t x = 240;

        // Battery percentage (color-coded) — reserve fixed width for "100%"
        // so elements to the left don't shift as digit count changes.
        char battText[6];
        snprintf(battText, sizeof(battText), "%u%%", battery);
        int16_t battMaxW = headerSprite->textWidth("100%");
        int16_t battActualW = headerSprite->textWidth(battText);
        x -= battMaxW;
        headerSprite->setCursor(x + (battMaxW - battActualW), 0);
        headerSprite->setTextColor(batteryColor(battery), TFT_BLACK);
        headerSprite->print(battText);

        // Charging indicator "+" — always reserve space, only draw when rising
        x -= headerSprite->textWidth("+");
        if (batteryRising) {
            headerSprite->setCursor(x, 0);
            headerSprite->setTextColor(TFT_GREEN, TFT_BLACK);
            headerSprite->print("+");
        }

        // Gap
        x -= 8;

        // Data connection: B (BLE), S (serial), or X (none)
        char connChar;
        uint16_t connColor;
        if (bleConnected) {
            connChar = 'B';
            connColor = TFT_BLUE;
        } else if (serialState == CONN_SERIAL) {
            connChar = 'S';
            connColor = TFT_CYAN;
        } else {
            connChar = 'X';
            connColor = TFT_RED;
        }
        x -= headerSprite->textWidth("B");
        headerSprite->setCursor(x, 0);
        headerSprite->setTextColor(connColor, TFT_BLACK);
        headerSprite->print(connChar);

        // Separator line at bottom of header sprite
        headerSprite->drawFastHLine(0, LIST_SEPARATOR_Y, 240, TFT_DARKGREY);

        headerSprite->pushSprite(0, 0);
    }

    void drawDeviceList(uint32_t nowMs) {
        if (!spriteCreated) return;

        uint8_t sortedIdx[MAX_DISPLAY_DEVICES];
        uint8_t sortedCount = 0;
        buildSortedIndices(sortedIdx, sortedCount);

        // Clamp scroll offset
        if (sortedCount <= LIST_VISIBLE_ROWS) {
            scrollOffset = 0;
        } else if (scrollOffset > sortedCount - LIST_VISIBLE_ROWS) {
            scrollOffset = sortedCount - LIST_VISIBLE_ROWS;
        }

        listSprite->fillSprite(TFT_BLACK);
        listSprite->setTextSize(1);

        if (sortedCount == 0) {
            listSprite->setTextColor(TFT_DARKGREY);
            listSprite->setCursor(20, (LIST_AREA_H - LIST_SCROLL_BAR_H) / 2 - 4);
            listSprite->print("No devices detected");
            listSprite->pushSprite(0, LIST_TOP_Y);
            return;
        }

        uint8_t rowsAvailable = LIST_AREA_H - LIST_SCROLL_BAR_H;
        for (uint8_t row = 0; row < LIST_VISIBLE_ROWS && (scrollOffset + row) < sortedCount; row++) {
            const DisplayDevice& dev = displayDevices[sortedIdx[scrollOffset + row]];
            int16_t y = row * LIST_ROW_H;
            uint16_t color = dev.active ? alertColor(dev.alertLevel) : TFT_DARKGREY;

            // Color dot (4x8)
            listSprite->fillRect(0, y + 3, 4, 8, color);

            // Radio type
            listSprite->setTextColor(color, TFT_BLACK);
            listSprite->setCursor(6, y + 3);
            listSprite->print(dev.radioType);

            // Label (truncated)
            listSprite->setCursor(18, y + 3);
            listSprite->print(dev.label);

            // RSSI
            char rssiStr[8];
            snprintf(rssiStr, sizeof(rssiStr), "%ddB", dev.rssi);
            int16_t rssiWidth = listSprite->textWidth(rssiStr);
            listSprite->setCursor(195 - rssiWidth, y + 3);
            listSprite->print(rssiStr);

            // Age
            char ageStr[6];
            uint32_t ageMs = nowMs - dev.firstSeenMs;
            formatAge(ageMs, ageStr, sizeof(ageStr));
            int16_t ageWidth = listSprite->textWidth(ageStr);
            listSprite->setCursor(240 - ageWidth - 1, y + 3);
            listSprite->print(ageStr);
        }

        // Scroll indicator bar
        if (sortedCount > LIST_VISIBLE_ROWS) {
            int16_t barY = rowsAvailable;
            int16_t barWidth = (int32_t)LIST_VISIBLE_ROWS * 240 / sortedCount;
            if (barWidth < 10) barWidth = 10;
            int16_t barX = (int32_t)scrollOffset * (240 - barWidth) / (sortedCount - LIST_VISIBLE_ROWS);
            listSprite->fillRect(barX, barY, barWidth, LIST_SCROLL_BAR_H, TFT_DARKGREY);
        }

        listSprite->pushSprite(0, LIST_TOP_Y);
    }

    void initScanningUi(uint32_t nowMs, bool bleConn = false,
                        uint8_t serSt = 0, bool batRising = false) {
        setDisplayOn();
        M5.Display.clear(TFT_BLACK);
        M5.Display.setTextColor(STATUS_TEXT_COLOR, TFT_BLACK);
        M5.Display.setTextSize(2);

        if (!spriteCreated) {
            headerSprite = new LGFX_Sprite(&M5.Display);
            headerSprite->setColorDepth(16);
            headerSprite->createSprite(240, LIST_TOP_Y);
            listSprite = new LGFX_Sprite(&M5.Display);
            listSprite->setColorDepth(16);
            listSprite->createSprite(240, LIST_AREA_H);
            spriteCreated = true;
        }

        drawDeviceListHeader(1, batteryFilter.smoothed, bleConn, serSt, batRising);
        drawDeviceList(nowMs);
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

    const uint32_t DEBUG_REFRESH_MS = 400;

    // Draws debug info directly to display, overwriting in place.
    // Call fillScreen(BLACK) once before the first call.
    void drawDebugScreen() {
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(0, 0);

        // Scan status
        M5.Display.printf("WiFi CH: %-4u\n", RadioScannerManager::getCurrentWifiChannel());
        M5.Display.printf("BLE:     %-4s\n", RadioScannerManager::isBleScanning() ? "scan" : "idle");

        // System health
        M5.Display.printf("Heap:    %-4ukB\n", (unsigned)(ESP.getFreeHeap() / 1024));
        M5.Display.printf("Up:      %-6us\n", (unsigned)(millis() / 1000));

        // Battery — raw vs smoothed
        M5.Display.printf("Batt:    %u/%-4u%%\n", batteryFilter.lastRaw, batteryFilter.smoothed);

        // Detection stats
        uint8_t active = countActiveDevices();
        M5.Display.printf("Devs:    %u/%-4u\n", active, displayDeviceCount);
        M5.Display.printf("Alerts:  %-6u\n", (unsigned)detectionCount);
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
    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init("");
    }
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
bool RadioScannerManager::_highPerformance = false;
volatile bool RadioScannerManager::_bleClientConnected = false;
volatile bool RadioScannerManager::_dutyCycleDirty = false;


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

    // Initialize NimBLE once, then set up GATT server before radio scanner.
    // RadioScanner::configureBluetoothScanner() will skip NimBLEDevice::init()
    // if already initialized.
    NimBLEDevice::init("");
    bleTransport.setClientStateCallback([](bool connected) {
        RadioScannerManager::setBleClientConnected(connected);
    });
    bleTransport.initialize();
    reporter.setBleTransport(&bleTransport);

    rfScanner.initialize();
    
    Serial.println("System operational - scanning for targets");
    Serial.println();
    
    // Seed battery smoothing buffer
    batteryFilter.seed(M5.Power.getBatteryLevel());

    initScanningUi(millis());
    EventBus::publishSystemReady();
}

void loop() {
    M5.update();
    RadioScannerManager::applyPendingDutyCycle();
    rfScanner.update();
    static uint8_t dots = 1;
    static uint32_t lastDotMs = 0;
    static uint32_t lastListRefreshMs = 0;
    static uint32_t lastDebugRefreshMs = 0;
    static bool wasAlertActive = false;
    static bool powerToggleHandled = false;
    static bool lastShouldPowerSave = false;
    uint32_t now = millis();

    // Update smoothed battery reading (gated by BATTERY_UPDATE_MS internally)
    updateBattery(now);

    // Drain incoming serial data (heartbeat pings from app)
    static uint32_t lastSerialRxMs = 0;
    while (Serial.available()) {
        Serial.read();
        lastSerialRxMs = now;
    }

    // Battery trend tracking for indirect charge detection (uses raw for fast response)
    static uint8_t prevBatteryRaw = 0;
    static bool batteryRising = false;
    static uint32_t lastBatteryTrendMs = 0;
    if (now - lastBatteryTrendMs >= BATTERY_UPDATE_MS) {
        // Skip trend comparison on first iteration (prev is unseeded)
        if (lastBatteryTrendMs > 0) {
            batteryRising = isBatteryRising(batteryFilter.lastRaw, prevBatteryRaw);
        }
        prevBatteryRaw = batteryFilter.lastRaw;
        lastBatteryTrendMs = now;
    }

    // Connection indicators for header
    uint8_t serialState = computeSerialState(lastSerialRxMs, now);
    bool bleConn = bleTransport.isClientConnected();

    bool shouldPowerSave = powerSaverEnabled;

    if (wifiFramePending) {
        WiFiFrameEvent frameCopy;
        portENTER_CRITICAL(&wifiMux);
        frameCopy = pendingWiFiFrame;
        wifiFramePending = false;
        portEXIT_CRITICAL(&wifiMux);
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
        reporter.handleThreatDetection(threatCopy);
        updateDisplayDevice(threatCopy, now);
        if (threatCopy.shouldAlert) {
            triggerAlert(now);
        } else if (threatCopy.alertLevel == ALERT_SUSPICIOUS && threatCopy.firstDetection) {
            M5.Speaker.tone(1800, 60);
        }
        // Immediate list refresh on new detection (main screen only)
        if (!alertActive && !statusMessageActive && displayState == DisplayState::Awake) {
            drawDeviceListHeader(dots, batteryFilter.smoothed, bleConn, serialState, batteryRising);
            drawDeviceList(now);
        }
    }

    // Scroll buttons — main screen only
    if (M5.BtnPWR.wasClicked() && !alertActive && !statusMessageActive && displayState == DisplayState::Awake) {
        if (scrollOffset > 0) {
            scrollOffset--;
            drawDeviceList(now);
        }
    }

    if (M5.BtnB.wasClicked() && !alertActive && !statusMessageActive && displayState == DisplayState::Awake) {
        uint8_t total = displayDeviceCount;
        if (total > LIST_VISIBLE_ROWS && scrollOffset < total - LIST_VISIBLE_ROWS) {
            scrollOffset++;
            drawDeviceList(now);
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

    // BtnA: toggle between main screen and debug screen
    if (M5.BtnA.wasPressed()) {
        if (shouldPowerSave && displayState == DisplayState::Off) {
            initScanningUi(now, bleConn, serialState, batteryRising);
            lastDotMs = now;
            lastListRefreshMs = now;
        } else if (displayState == DisplayState::Debug) {
            initScanningUi(now, bleConn, serialState, batteryRising);
            lastDotMs = now;
            lastListRefreshMs = now;
        } else if (displayState == DisplayState::Awake) {
            setDisplayOn();
            M5.Display.fillScreen(TFT_BLACK);
            drawDebugScreen();
            displayState = DisplayState::Debug;
            lastDebugRefreshMs = now;
        }
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
            initScanningUi(now, bleConn, serialState, batteryRising);
            lastDotMs = now;
            lastListRefreshMs = now;
        }
    }
    wasAlertActive = isAlerting;

    if (statusMessageActive && now >= statusMessageUntilMs) {
        statusMessageActive = false;
        initScanningUi(now, bleConn, serialState, batteryRising);
    }

    if (!isAlerting && !statusMessageActive && shouldPowerSave != lastShouldPowerSave) {
        lastShouldPowerSave = shouldPowerSave;
        initScanningUi(now, bleConn, serialState, batteryRising);
    }

    if (!isAlerting && !statusMessageActive) {
        if (shouldPowerSave) {
            if (displayState == DisplayState::Awake && now - displayStateMs >= SCREEN_ON_MS) {
                showPowerSaveMessage();
            } else if (displayState == DisplayState::PowerSaveMessage && now - displayStateMs >= POWER_SAVE_MSG_MS) {
                setDisplayOff();
                displayState = DisplayState::Off;
            }
        } else if (displayState == DisplayState::Off) {
            initScanningUi(now, bleConn, serialState, batteryRising);
        }
    }

    // Debug screen refresh — skip during status messages and alerts
    if (displayState == DisplayState::Debug && !statusMessageActive && !isAlerting
        && now - lastDebugRefreshMs >= DEBUG_REFRESH_MS) {
        drawDebugScreen();
        lastDebugRefreshMs = now;
    }

    // Header updates (dots animation + battery/count) — main screen only
    if (displayState == DisplayState::Awake && !isAlerting && !statusMessageActive && now - lastDotMs >= DOT_UPDATE_MS) {
        dots = (dots % MAX_DOTS) + 1;
        drawDeviceListHeader(dots, batteryFilter.smoothed, bleConn, serialState, batteryRising);
        lastDotMs = now;
    }

    // Periodic device list refresh — main screen only
    if (!isAlerting && !statusMessageActive && now - lastListRefreshMs >= LIST_REFRESH_MS) {
        ageDisplayDevices(now);
        if (displayState == DisplayState::Awake) {
            drawDeviceListHeader(dots, batteryFilter.smoothed, bleConn, serialState, batteryRising);
            drawDeviceList(now);
        }
        lastListRefreshMs = now;
    }

    delay(30);
}
