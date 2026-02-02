#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>
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
#include "src/SoundEngine.h"
#include "TelemetryReporter.h"

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
SoundEngine audioSystem;
TelemetryReporter reporter;

// UI toggle (easy to remove if you don't like it)
#define ENABLE_HOME_UI 1

// UI state (home screen)
static bool homeScreenActive = false;
static bool homeScreenPending = false;
static unsigned long homeReadyTimestamp = 0;
static unsigned long lastUiUpdate = 0;
static unsigned long lastRadarUpdate = 0;
static unsigned long lastScanAnimUpdate = 0;
static int radarX = 0;
static int radarDir = 1;
static const int radarStep = 6;
static uint32_t alertCount = 0;
static char lastMacAddress[18] = "--";
static int8_t lastRssi = -100;
static const size_t RSSI_GRAPH_POINTS = 60;
static int8_t rssiHistory[RSSI_GRAPH_POINTS] = {0};
static size_t rssiIndex = 0;
static bool rssiFilled = false;
static uint16_t accentColor = TFT_GREEN;
static uint8_t displayBrightness = 160;
static const char* configPath = "/flocksquawk.json";

static const int radarBoxX = 4;
static const int radarBoxW = 312;
static const int radarBoxH = 72;
static const int radarBoxBottomMargin = 4;
static const int radarBoxY = 240 - radarBoxH - radarBoxBottomMargin;
static const int radarLineInsetX = 2;
static const int radarLineInsetY = 2;

static const int rssiBoxX = 200;
static const int rssiBoxY = 70;
static const int rssiBoxW = 116;
static const int rssiBoxH = 70;

static const int scanTextX = 4;
static const int scanTextY = 24;
static uint8_t scanAnimStep = 0;
static const int infoTextBaseY = scanTextY + 18;

enum class MenuMode {
    None,
    Main,
    AdjustBacklight,
    AdjustAccent,
    ResetMenu
};

static MenuMode menuMode = MenuMode::None;
static int menuIndex = 0;
static const char* menuItems[] = {
    "Backlight",
    "Accent Color",
    "Test Alert",
    "Battery Saver",
    "Save Settings",
    "Reset",
    "Back"
};
static const size_t menuItemCount = sizeof(menuItems) / sizeof(menuItems[0]);
static const int menuVisibleCount = 5;
static int resetMenuIndex = 0;
static const char* resetMenuItems[] = {
    "Reset Alerts",
    "Factory Reset",
    "Back"
};
static const size_t resetMenuItemCount = sizeof(resetMenuItems) / sizeof(resetMenuItems[0]);

struct AccentOption {
    const char* name;
    uint16_t color;
};

static const AccentOption accentOptions[] = {
    {"Green", TFT_GREEN},
    {"Cyan", TFT_CYAN},
    {"Blue", TFT_BLUE},
    {"Purple", TFT_MAGENTA},
    {"Amber", TFT_ORANGE},
    {"Red", TFT_RED}
};
static const size_t accentOptionCount = sizeof(accentOptions) / sizeof(accentOptions[0]);
static size_t accentIndex = 0;
static bool menuJustOpened = false;
static bool alertActive = false;
static unsigned long alertStart = 0;
static unsigned long lastAlertFlash = 0;
static bool alertFlashOn = true;
static bool batterySaverEnabled = false;
static bool displayIsOff = false;
static bool infoPopupActive = false;
static unsigned long infoPopupStart = 0;
static const char* infoPopupText = "";

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

uint8_t RadioScannerManager::getCurrentWifiChannel() {
    return currentWifiChannel;
}

bool RadioScannerManager::isBluetoothScanning() {
    return isScanningBLE;
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

uint8_t RadioScannerManager::currentWifiChannel = 1;
unsigned long RadioScannerManager::lastChannelSwitch = 0;
unsigned long RadioScannerManager::lastBLEScan = 0;
NimBLEScan* RadioScannerManager::bleScanner = nullptr;
bool RadioScannerManager::isScanningBLE = false;

// SoundEngine implementation
void SoundEngine::initialize() {
    volumeLevel = DEFAULT_VOLUME;
    
    SPI.begin(SoundEngine::SD_SCK, SoundEngine::SD_MISO, SoundEngine::SD_MOSI, SoundEngine::SD_CS);
    if (!SD.begin(SoundEngine::SD_CS, SPI)) {
        Serial.println("[Audio] Failed to mount SD card");
        return;
    }
    
    M5.Speaker.begin();
    setVolume(volumeLevel);
    Serial.println("[Audio] Sound system initialized");
}

void SoundEngine::setVolume(float level) {
    if (level >= 0.0f && level <= 1.0f) {
        volumeLevel = level;
        uint8_t scaled = static_cast<uint8_t>(roundf(volumeLevel * 255.0f));
        M5.Speaker.setVolume(scaled);
    }
}

float SoundEngine::getVolumeLevel() const {
    return volumeLevel;
}

bool SoundEngine::loadWavFromSd(const char* filename, uint8_t** outData, size_t* outLength) {
    if (!SD.exists(filename)) {
        Serial.printf("[Audio] Cannot open: %s\n", filename);
        return false;
    }
    
    File audioFile = SD.open(filename, FILE_READ);
    if (!audioFile) {
        Serial.printf("[Audio] Failed to open: %s\n", filename);
        return false;
    }
    
    size_t dataSize = audioFile.size();
    if (dataSize == 0) {
        Serial.printf("[Audio] Empty file: %s\n", filename);
        audioFile.close();
        return false;
    }
    
#if defined(ps_malloc)
    uint8_t* wavData = static_cast<uint8_t*>(ps_malloc(dataSize));
#else
    uint8_t* wavData = static_cast<uint8_t*>(malloc(dataSize));
#endif
    if (!wavData) {
        Serial.printf("[Audio] Out of memory loading: %s\n", filename);
        audioFile.close();
        return false;
    }
    
    size_t bytesRead = audioFile.read(wavData, dataSize);
    audioFile.close();
    if (bytesRead != dataSize) {
        Serial.printf("[Audio] Read error: %s\n", filename);
        free(wavData);
        return false;
    }
    
    *outData = wavData;
    *outLength = dataSize;
    return true;
}

void SoundEngine::playSound(const char* filename) {
    uint8_t* wavData = nullptr;
    size_t dataSize = 0;
    if (!loadWavFromSd(filename, &wavData, &dataSize)) {
        return;
    }
    
    M5.Speaker.playWav(wavData, dataSize);
    while (M5.Speaker.isPlaying()) {
        M5.update();
        delay(5);
    }
    free(wavData);
}

void SoundEngine::playSoundAsync(const char* filename) {
    if (asyncActive && M5.Speaker.isPlaying()) {
        return;
    }
    if (asyncActive && asyncBuffer) {
        free(asyncBuffer);
        asyncBuffer = nullptr;
        asyncLength = 0;
        asyncActive = false;
    }
    
    if (!loadWavFromSd(filename, &asyncBuffer, &asyncLength)) {
        return;
    }
    
    asyncActive = true;
    M5.Speaker.playWav(asyncBuffer, asyncLength);
}

void SoundEngine::update() {
    if (asyncActive && !M5.Speaker.isPlaying()) {
        if (asyncBuffer) {
            free(asyncBuffer);
            asyncBuffer = nullptr;
        }
        asyncLength = 0;
        asyncActive = false;
    }
}

void SoundEngine::handleAudioRequest(const AudioEvent& event) {
    playSound(event.soundFile);
}

// Main system initialization
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Initializing Threat Detection System...");
    Serial.println();

    M5.begin();
    M5.Display.clear();
    M5.Display.setTextSize(2);
    M5.Display.setCursor(0, 0);
    
    audioSystem.initialize();
    loadSettingsFromSd();
    setDisplayPower(true);
    M5.Display.println("System starting up...");
    delay(300);
    M5.Display.println("Setting up radios...");
    delay(300);
    M5.Display.println("Loading database...");
    audioSystem.playSound("/startup.wav");
    
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
    
    EventBus::subscribeAudioRequest([](const AudioEvent& event) {
        audioSystem.handleAudioRequest(event);
    });
    
    EventBus::subscribeSystemReady([]() {
        M5.Display.println("System ready! Scanning...");
        homeReadyTimestamp = millis();
        homeScreenPending = true;
        audioSystem.playSound("/ready.wav");
    });
    
    threatEngine.initialize();
    reporter.initialize();
    rfScanner.initialize();
    
    Serial.println("System operational - scanning for targets");
    Serial.println();
    
    EventBus::publishSystemReady();
}

#if ENABLE_HOME_UI
static void drawHomeFrame() {
    M5.Display.clear();
    M5.Display.setTextSize(1);
    M5.Display.setCursor(0, 0);
    
    // Top bar
    M5.Display.drawFastHLine(0, 20, 320, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("VOL", 4, 4);
    M5.Display.drawString("RAM", 110, 4);
    M5.Display.drawString("BAT", 250, 4);
    
    // Info labels
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    int textBaseY = infoTextBaseY;
    M5.Display.drawString("WiFi CH:", 4, textBaseY);
    M5.Display.drawString("Last MAC:", 4, textBaseY + 18);
    M5.Display.drawString("Alerts:", 4, textBaseY + 36);
    M5.Display.drawString("RSSI", rssiBoxX, rssiBoxY - 12);
    M5.Display.drawString("BT", 292, scanTextY);
    
    // RSSI graph frame
    M5.Display.drawRect(rssiBoxX, rssiBoxY, rssiBoxW, rssiBoxH, TFT_DARKGREY);
    
    // Radar frame (empty box)
    M5.Display.drawRect(radarBoxX, radarBoxY, radarBoxW, radarBoxH, TFT_DARKGREY);
}

static void updateScanningBanner() {
    static const char* animFrames[] = {
        "", ".", "..", "..."
    };
    const uint8_t frameCount = sizeof(animFrames) / sizeof(animFrames[0]);
    const char* dots = animFrames[scanAnimStep % frameCount];
    scanAnimStep++;
    
    M5.Display.fillRect(scanTextX, scanTextY, 260, 16, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(scanTextX, scanTextY);
    M5.Display.print("Scanning for Flock signatures");
    M5.Display.print(dots);
}

static void updateRssiGraph() {
    M5.Display.fillRect(rssiBoxX + 1, rssiBoxY + 1, rssiBoxW - 2, rssiBoxH - 2, TFT_BLACK);
    
    size_t points = rssiFilled ? RSSI_GRAPH_POINTS : rssiIndex;
    if (points < 2) return;
    
    int graphW = rssiBoxW - 2;
    int graphH = rssiBoxH - 2;
    int x0 = rssiBoxX + 1;
    int y0 = rssiBoxY + 1;
    
    auto mapRssi = [graphH](int8_t rssi) {
        if (rssi < -100) rssi = -100;
        if (rssi > -30) rssi = -30;
        int norm = rssi + 100; // 0..70
        return graphH - 1 - (norm * (graphH - 1) / 70);
    };
    
    for (size_t i = 1; i < points; i++) {
        size_t idx0 = (rssiIndex + RSSI_GRAPH_POINTS - points + i - 1) % RSSI_GRAPH_POINTS;
        size_t idx1 = (rssiIndex + RSSI_GRAPH_POINTS - points + i) % RSSI_GRAPH_POINTS;
        
        int xA = x0 + static_cast<int>((i - 1) * (graphW - 1) / (points - 1));
        int xB = x0 + static_cast<int>(i * (graphW - 1) / (points - 1));
        int yA = y0 + mapRssi(rssiHistory[idx0]);
        int yB = y0 + mapRssi(rssiHistory[idx1]);
        
        M5.Display.drawLine(xA, yA, xB, yB, accentColor);
    }
}

static void updateHomeStats() {
    // Volume
    uint8_t volPercent = static_cast<uint8_t>(roundf(audioSystem.getVolumeLevel() * 100.0f));
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillRect(40, 2, 60, 16, TFT_BLACK);
    M5.Display.drawString(String(volPercent) + "%", 40, 4);
    
    // RAM utilization
    uint32_t heapSize = ESP.getHeapSize();
    uint32_t freeHeap = ESP.getFreeHeap();
    uint8_t ramPercent = heapSize > 0 ? static_cast<uint8_t>(((heapSize - freeHeap) * 100) / heapSize) : 0;
    M5.Display.fillRect(140, 2, 60, 16, TFT_BLACK);
    M5.Display.drawString(String(ramPercent) + "%", 140, 4);
    
    // Battery
    int battery = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();
    int barX = 270;
    int barY = 2;
    int barW = 36;
    int barH = 14;
    M5.Display.drawRect(barX, barY, barW, barH, TFT_WHITE);
    int fillW = (battery * (barW - 2)) / 100;
    M5.Display.fillRect(barX + 1, barY + 1, barW - 2, barH - 2, TFT_BLACK);
    M5.Display.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_GREEN);
    M5.Display.fillRect(barX + barW, barY + 4, 3, 6, TFT_WHITE);
    M5.Display.fillRect(310, 2, 10, 16, TFT_BLACK);
    if (charging) {
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.drawString("+", 310, 3);
    }
    
    // WiFi channel
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillRect(70, infoTextBaseY - 2, 50, 16, TFT_BLACK);
    M5.Display.drawString(String(RadioScannerManager::getCurrentWifiChannel()), 70, infoTextBaseY);
    
    // Bluetooth indicator
    bool btActive = RadioScannerManager::isBluetoothScanning();
    uint16_t btColor = btActive ? TFT_BLUE : TFT_DARKGREY;
    M5.Display.fillCircle(312, scanTextY + 6, 5, btColor);
    
    // Last MAC
    M5.Display.fillRect(70, infoTextBaseY + 16, 120, 16, TFT_BLACK);
    M5.Display.drawString(String(lastMacAddress), 70, infoTextBaseY + 18);
    
    // Alerts count
    M5.Display.fillRect(60, infoTextBaseY + 34, 80, 16, TFT_BLACK);
    M5.Display.drawString(String(alertCount), 60, infoTextBaseY + 36);
}

static void updateRadarSweep() {
    int radarX0 = radarBoxX + radarLineInsetX;
    int radarY0 = radarBoxY + radarLineInsetY;
    int radarW = radarBoxW - (radarLineInsetX * 2);
    int radarH = radarBoxH - (radarLineInsetY * 2);
    
    // Erase previous line
    M5.Display.drawFastVLine(radarX0 + radarX, radarY0, radarH, TFT_BLACK);
    
    radarX += radarDir * radarStep;
    if (radarX <= 0) {
        radarX = 0;
        radarDir = 1;
    } else if (radarX >= radarW - 1) {
        radarX = radarW - 1;
        radarDir = -1;
    }
    
    M5.Display.drawFastVLine(radarX0 + radarX, radarY0, radarH, accentColor);
}

static void drawMenuFrame() {
    M5.Display.fillRect(20, 40, 280, 160, TFT_BLACK);
    M5.Display.drawRect(20, 40, 280, 160, TFT_DARKGREY);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Menu", 30, 46);
    
    int visibleCount = menuVisibleCount;
    if ((int)menuItemCount < visibleCount) {
        visibleCount = (int)menuItemCount;
    }
    int scrollOffset = 0;
    if ((int)menuItemCount > visibleCount && menuIndex >= visibleCount) {
        scrollOffset = menuIndex - (visibleCount - 1);
    }
    int maxOffset = (int)menuItemCount - visibleCount;
    if (scrollOffset > maxOffset) scrollOffset = maxOffset;
    
    for (int i = 0; i < visibleCount; i++) {
        int itemIndex = scrollOffset + i;
        int y = 70 + (i * 24);
        if (itemIndex == menuIndex) {
            M5.Display.fillRect(28, y - 2, 264, 18, TFT_DARKGREY);
            M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
        } else {
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        }
        if (itemIndex == 3) {
            String label = String(menuItems[itemIndex]) + (batterySaverEnabled ? ": On" : ": Off");
            M5.Display.drawString(label, 36, y);
        } else {
            M5.Display.drawString(menuItems[itemIndex], 36, y);
        }
    }
}

static void drawResetMenuFrame() {
    M5.Display.fillRect(20, 40, 280, 160, TFT_BLACK);
    M5.Display.drawRect(20, 40, 280, 160, TFT_DARKGREY);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Reset", 30, 46);
    
    for (size_t i = 0; i < resetMenuItemCount; i++) {
        int y = 70 + (i * 24);
        if ((int)i == resetMenuIndex) {
            M5.Display.fillRect(28, y - 2, 264, 18, TFT_DARKGREY);
            M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
        } else {
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        }
        M5.Display.drawString(resetMenuItems[i], 36, y);
    }
}

static void showInfoPopup(const char* text) {
    infoPopupActive = true;
    infoPopupStart = millis();
    infoPopupText = text;
    
    const int boxW = 200;
    const int boxH = 60;
    const int boxX = (320 - boxW) / 2;
    const int boxY = (240 - boxH) / 2;
    
    M5.Display.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
    M5.Display.drawRect(boxX, boxY, boxW, boxH, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int textW = M5.Display.textWidth(text);
    int textH = M5.Display.fontHeight();
    int textX = boxX + (boxW - textW) / 2;
    int textY = boxY + (boxH - textH) / 2;
    M5.Display.setCursor(textX, textY);
    M5.Display.print(text);
}

static void drawAdjustBacklight() {
    M5.Display.fillRect(20, 40, 280, 160, TFT_BLACK);
    M5.Display.drawRect(20, 40, 280, 160, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Backlight", 30, 46);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("Use left/right to adjust", 30, 70);
    M5.Display.drawString("Center to save", 30, 88);
    
    int barX = 30;
    int barY = 120;
    int barW = 240;
    int barH = 14;
    M5.Display.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
    int fillW = (displayBrightness * (barW - 2)) / 255;
    M5.Display.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_CYAN);
}

static void drawAdjustAccent() {
    M5.Display.fillRect(20, 40, 280, 160, TFT_BLACK);
    M5.Display.drawRect(20, 40, 280, 160, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Accent Color", 30, 46);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("Use left/right to adjust", 30, 70);
    M5.Display.drawString("Center to save", 30, 88);
    
    M5.Display.drawRect(30, 120, 240, 30, TFT_DARKGREY);
    M5.Display.fillRect(32, 122, 236, 26, accentOptions[accentIndex].color);
    M5.Display.setTextColor(TFT_BLACK, accentOptions[accentIndex].color);
    M5.Display.drawString(accentOptions[accentIndex].name, 36, 126);
}

static void resetHomeUi() {
    homeScreenActive = true;
    drawHomeFrame();
    updateHomeStats();
    updateRssiGraph();
    updateScanningBanner();
    lastUiUpdate = millis();
    lastRadarUpdate = millis();
    lastScanAnimUpdate = millis();
}

static void applyDefaultSettings() {
    audioSystem.setVolume(SoundEngine::DEFAULT_VOLUME);
    displayBrightness = 160;
    accentIndex = 0;
    accentColor = accentOptions[accentIndex].color;
    batterySaverEnabled = false;
    alertCount = 0;
}

static void setDisplayPower(bool on) {
    if (on) {
        if (displayIsOff) {
            M5.Display.wakeup();
            displayIsOff = false;
        }
        M5.Display.setBrightness(displayBrightness);
    } else {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
        displayIsOff = true;
    }
}

static bool saveSettingsToSd() {
    DynamicJsonDocument doc(512);
    doc["volume"] = audioSystem.getVolumeLevel();
    doc["brightness"] = displayBrightness;
    doc["accent_index"] = static_cast<int>(accentIndex);
    doc["battery_saver"] = batterySaverEnabled;
    doc["alert_count"] = alertCount;
    
    File file = SD.open(configPath, FILE_WRITE);
    if (!file) {
        Serial.println("[Config] Failed to open config for write");
        return false;
    }
    file.seek(0);
    file.print("");
    file.seek(0);
    serializeJson(doc, file);
    file.close();
    return true;
}

static bool loadSettingsFromSd() {
    if (!SD.exists(configPath)) {
        applyDefaultSettings();
        return saveSettingsToSd();
    }
    
    File file = SD.open(configPath, FILE_READ);
    if (!file) {
        Serial.println("[Config] Failed to open config for read");
        return false;
    }
    
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.println("[Config] Failed to parse config, using defaults");
        applyDefaultSettings();
        return saveSettingsToSd();
    }
    
    float volume = doc["volume"] | SoundEngine::DEFAULT_VOLUME;
    audioSystem.setVolume(volume);
    displayBrightness = doc["brightness"] | displayBrightness;
    
    int idx = doc["accent_index"] | 0;
    if (idx < 0 || idx >= (int)accentOptionCount) idx = 0;
    accentIndex = idx;
    accentColor = accentOptions[accentIndex].color;
    
    batterySaverEnabled = doc["battery_saver"] | false;
    alertCount = doc["alert_count"] | 0u;
    return true;
}

static void triggerAlert(bool incrementCount) {
    if (incrementCount) {
        alertCount++;
    }
    alertActive = true;
    alertStart = millis();
    lastAlertFlash = 0;
    alertFlashOn = true;
    if (batterySaverEnabled) {
        setDisplayPower(true);
    }
    audioSystem.playSoundAsync("/alert.wav");
}

static void drawAlertPopup() {
    const int boxX = 20;
    const int boxY = 40;
    const int boxW = 280;
    const int boxH = 160;
    
    M5.Display.fillRect(boxX, boxY, boxW, boxH, TFT_RED);
    M5.Display.drawRect(boxX, boxY, boxW, boxH, TFT_WHITE);
    M5.Display.drawRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, TFT_DARKGREY);
    
    if (alertFlashOn) {
        M5.Display.setTextColor(TFT_BLACK, TFT_RED);
        M5.Display.setTextSize(3);
        const char* alertText = "ALERT";
        int textW = M5.Display.textWidth(alertText);
        int textH = M5.Display.fontHeight();
        int textX = boxX + (boxW - textW) / 2;
        int textY = boxY + (boxH - textH) / 2;
        M5.Display.setCursor(textX, textY);
        M5.Display.print(alertText);
        M5.Display.setTextSize(1);
    }
}

static void restoreScreenAfterAlert() {
    if (batterySaverEnabled) {
        setDisplayPower(false);
        return;
    }
    if (menuMode == MenuMode::Main) {
        drawMenuFrame();
    } else if (menuMode == MenuMode::ResetMenu) {
        drawResetMenuFrame();
    } else if (menuMode == MenuMode::AdjustBacklight) {
        drawAdjustBacklight();
    } else if (menuMode == MenuMode::AdjustAccent) {
        drawAdjustAccent();
    } else {
        resetHomeUi();
    }
}

static void handleAlertPopup() {
    if (!alertActive) return;
    
    unsigned long now = millis();
    if (now - alertStart >= 7000) {
        alertActive = false;
        restoreScreenAfterAlert();
        return;
    }
    
    if (now - lastAlertFlash >= 400 || lastAlertFlash == 0) {
        alertFlashOn = !alertFlashOn;
        lastAlertFlash = now;
        drawAlertPopup();
    }
}

static void handleInfoPopup() {
    if (!infoPopupActive) return;
    if (millis() - infoPopupStart >= 3000) {
        infoPopupActive = false;
        if (menuMode == MenuMode::Main) {
            drawMenuFrame();
        } else if (menuMode == MenuMode::ResetMenu) {
            drawResetMenuFrame();
        }
    }
}

static void handleMenuButtons() {
    if (menuJustOpened) {
        if (!M5.BtnB.isPressed()) {
            menuJustOpened = false;
        }
        return;
    }
    if (infoPopupActive) {
        return;
    }
    
    if (menuMode == MenuMode::Main) {
        if (M5.BtnA.wasPressed()) {
            menuIndex = (menuIndex - 1 + (int)menuItemCount) % (int)menuItemCount;
            drawMenuFrame();
        } else if (M5.BtnC.wasPressed()) {
            menuIndex = (menuIndex + 1) % (int)menuItemCount;
            drawMenuFrame();
        } else if (M5.BtnB.wasPressed()) {
            if (menuIndex == 0) {
                menuMode = MenuMode::AdjustBacklight;
                drawAdjustBacklight();
            } else if (menuIndex == 1) {
                menuMode = MenuMode::AdjustAccent;
                drawAdjustAccent();
            } else if (menuIndex == 2) {
                menuMode = MenuMode::None;
                resetHomeUi();
                triggerAlert(true);
            } else if (menuIndex == 3) {
                batterySaverEnabled = !batterySaverEnabled;
                menuMode = MenuMode::None;
                if (batterySaverEnabled) {
                    setDisplayPower(false);
                } else {
                    setDisplayPower(true);
                    resetHomeUi();
                }
            } else if (menuIndex == 4) {
                bool ok = saveSettingsToSd();
                if (ok) {
                    ok = loadSettingsFromSd();
                }
                showInfoPopup(ok ? "Settings saved" : "Save failed");
            } else if (menuIndex == 5) {
                menuMode = MenuMode::ResetMenu;
                resetMenuIndex = 0;
                drawResetMenuFrame();
            } else if (menuIndex == 6) {
                menuMode = MenuMode::None;
                resetHomeUi();
            }
        }
    } else if (menuMode == MenuMode::AdjustBacklight) {
        if (M5.BtnA.wasPressed()) {
            displayBrightness = (displayBrightness > 5) ? displayBrightness - 5 : 0;
            M5.Display.setBrightness(displayBrightness);
            drawAdjustBacklight();
        } else if (M5.BtnC.wasPressed()) {
            displayBrightness = (displayBrightness < 250) ? displayBrightness + 5 : 255;
            M5.Display.setBrightness(displayBrightness);
            drawAdjustBacklight();
        } else if (M5.BtnB.wasPressed()) {
            menuMode = MenuMode::Main;
            drawMenuFrame();
        }
    } else if (menuMode == MenuMode::AdjustAccent) {
        if (M5.BtnA.wasPressed()) {
            accentIndex = (accentIndex + accentOptionCount - 1) % accentOptionCount;
            accentColor = accentOptions[accentIndex].color;
            drawAdjustAccent();
        } else if (M5.BtnC.wasPressed()) {
            accentIndex = (accentIndex + 1) % accentOptionCount;
            accentColor = accentOptions[accentIndex].color;
            drawAdjustAccent();
        } else if (M5.BtnB.wasPressed()) {
            menuMode = MenuMode::Main;
            drawMenuFrame();
        }
    } else if (menuMode == MenuMode::ResetMenu) {
        if (M5.BtnA.wasPressed()) {
            resetMenuIndex = (resetMenuIndex - 1 + (int)resetMenuItemCount) % (int)resetMenuItemCount;
            drawResetMenuFrame();
        } else if (M5.BtnC.wasPressed()) {
            resetMenuIndex = (resetMenuIndex + 1) % (int)resetMenuItemCount;
            drawResetMenuFrame();
        } else if (M5.BtnB.wasPressed()) {
            if (resetMenuIndex == 0) {
                alertCount = 0;
                bool ok = saveSettingsToSd();
                showInfoPopup(ok ? "Alerts reset" : "Save failed");
            } else if (resetMenuIndex == 1) {
                applyDefaultSettings();
                saveSettingsToSd();
                setDisplayPower(true);
                menuMode = MenuMode::None;
                resetHomeUi();
            } else if (resetMenuIndex == 2) {
                menuMode = MenuMode::Main;
                drawMenuFrame();
            }
        }
    }
}

static void handleVolumeButtons() {
    if (menuMode != MenuMode::None) return;
    
    float vol = audioSystem.getVolumeLevel();
    if (M5.BtnA.wasPressed()) {
        vol -= 0.05f;
        if (vol < 0.0f) vol = 0.0f;
        audioSystem.setVolume(vol);
    } else if (M5.BtnC.wasPressed()) {
        vol += 0.05f;
        if (vol > 1.0f) vol = 1.0f;
        audioSystem.setVolume(vol);
    } else if (M5.BtnB.wasPressed()) {
        menuMode = MenuMode::Main;
        drawMenuFrame();
        menuJustOpened = true;
    }
}

static void handleHomeScreen() {
    if (!homeScreenActive && homeScreenPending && (millis() - homeReadyTimestamp >= 4000)) {
        homeScreenActive = true;
        homeScreenPending = false;
        radarX = 0;
        radarDir = 1;
        drawHomeFrame();
        lastUiUpdate = 0;
        lastRadarUpdate = 0;
    }
    
    if (!homeScreenActive) return;
    if (batterySaverEnabled) {
        setDisplayPower(false);
        return;
    }
    if (alertActive) return;
    if (menuMode != MenuMode::None) return;
    
    unsigned long now = millis();
    if (now - lastUiUpdate >= 500) {
        updateHomeStats();
        updateRssiGraph();
        lastUiUpdate = now;
    }
    
    if (now - lastScanAnimUpdate >= 300) {
        updateScanningBanner();
        lastScanAnimUpdate = now;
    }
    
    if (now - lastRadarUpdate >= 15) {
        updateRadarSweep();
        lastRadarUpdate = now;
    }
}
#endif

void loop() {
    M5.update();
    rfScanner.update();
    audioSystem.update();
    uint32_t now = millis();

    if (wifiFramePending) {
        WiFiFrameEvent frameCopy;
        portENTER_CRITICAL(&wifiMux);
        frameCopy = pendingWiFiFrame;
        wifiFramePending = false;
        portEXIT_CRITICAL(&wifiMux);
        snprintf(lastMacAddress, sizeof(lastMacAddress),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 frameCopy.mac[0], frameCopy.mac[1], frameCopy.mac[2],
                 frameCopy.mac[3], frameCopy.mac[4], frameCopy.mac[5]);
        lastRssi = frameCopy.rssi;
        rssiHistory[rssiIndex] = lastRssi;
        rssiIndex = (rssiIndex + 1) % RSSI_GRAPH_POINTS;
        if (rssiIndex == 0) rssiFilled = true;
        threatEngine.analyzeWiFiFrame(frameCopy);
    }

    if (bleDevicePending) {
        BluetoothDeviceEvent bleCopy;
        portENTER_CRITICAL(&bleMux);
        bleCopy = pendingBleDevice;
        bleDevicePending = false;
        portEXIT_CRITICAL(&bleMux);
        lastRssi = bleCopy.rssi;
        rssiHistory[rssiIndex] = lastRssi;
        rssiIndex = (rssiIndex + 1) % RSSI_GRAPH_POINTS;
        if (rssiIndex == 0) rssiFilled = true;
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
        if (threatCopy.shouldAlert) triggerAlert(true);
    }

#if ENABLE_HOME_UI
    if (batterySaverEnabled && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed())) {
        batterySaverEnabled = false;
        setDisplayPower(true);
        resetHomeUi();
    }
    handleAlertPopup();
    handleInfoPopup();
    handleVolumeButtons();
    handleMenuButtons();
    handleHomeScreen();
#endif
    delay(100);
}

