#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <FS.h>
#include <LittleFS.h>
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
#include "src/DisplayEngine.h"

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
SoundEngine audioSystem;
TelemetryReporter reporter;
DisplayEngine displaySystem;

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
    uint8_t frameSubtype = (header->frameControl & 0x0F) >> 4;
    
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
    
    if (!LittleFS.begin()) {
        Serial.println("[Audio] Failed to mount filesystem");
        return;
    }
    
    setupI2SInterface();
    Serial.println("[Audio] Sound system initialized");
}

void SoundEngine::setVolume(float level) {
    if (level >= 0.0f && level <= 1.0f) {
        volumeLevel = level;
    }
}

void SoundEngine::setupI2SInterface() {
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 16,
        .dma_buf_len = 1024,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    i2s_pin_config_t pinConfig = {
        .bck_io_num = PIN_BCLK,
        .ws_io_num = PIN_LRC,
        .data_out_num = PIN_DATA,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pinConfig);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void SoundEngine::playSound(const char* filename) {
    File audioFile = LittleFS.open(filename, "r");
    if (!audioFile) {
        Serial.printf("[Audio] Cannot open: %s\n", filename);
        return;
    }
    
    audioFile.seek(44);
    streamAudioFile(audioFile);
    audioFile.close();
}

void SoundEngine::streamAudioFile(File& audioFile) {
    uint8_t buffer[512];
    size_t bytesWritten;
    
    if (volumeLevel >= 1.0f) {
        while (audioFile.available()) {
            size_t bytesRead = audioFile.read(buffer, sizeof(buffer));
            if (bytesRead > 0) {
                i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
            }
        }
    } else {
        while (audioFile.available()) {
            size_t bytesRead = audioFile.read(buffer, sizeof(buffer));
            if (bytesRead == 0) break;
            
            size_t sampleCount = (bytesRead / 2) * 2;
            applyVolumeControl(buffer, sampleCount / 2);
            i2s_write(I2S_NUM_0, buffer, sampleCount, &bytesWritten, portMAX_DELAY);
        }
    }
}

void SoundEngine::applyVolumeControl(uint8_t* buffer, size_t sampleCount) {
    int16_t* samples = (int16_t*)buffer;
    
    for (size_t i = 0; i < sampleCount; i++) {
        int32_t scaled = (int32_t)((int32_t)samples[i] * volumeLevel);
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        samples[i] = (int16_t)scaled;
    }
}

void SoundEngine::handleAudioRequest(const AudioEvent& event) {
    playSound(event.soundFile);
}

// DisplayEngine implementation
DisplayEngine::DisplayEngine() : display(nullptr), currentState(STATE_STARTING), 
                                  stateStartTime(0), radarPosition(0), radarDirection(1) {
}

void DisplayEngine::initialize() {
    Wire.begin(21, 22);
    display = new Adafruit_SSD1306(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
    
    if (!display->begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
        Serial.println("[Display] SSD1306 allocation failed");
        return;
    }
    
    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    display->setTextWrap(false);
    
    currentState = STATE_STARTING;
    stateStartTime = millis();
    radarPosition = 0;
    radarDirection = 1;
    
    showStarting();
    Serial.println("[Display] OLED display initialized");
}

void DisplayEngine::update() {
    if (!display) return;
    
    // Handle state transitions based on time
    unsigned long now = millis();
    
    if (currentState == STATE_READY) {
        // Show "Ready" for 2 seconds, then switch to scanning
        if (now - stateStartTime >= 2000) {
            currentState = STATE_SCANNING;
            stateStartTime = now;
        }
    } else if (currentState == STATE_ALERT) {
        // Show alert for 3 seconds, then return to scanning
        if (now - stateStartTime >= 3000) {
            currentState = STATE_SCANNING;
            stateStartTime = now;
        }
    }
    
    // Update display based on current state
    switch (currentState) {
        case STATE_STARTING:
            showStarting();
            break;
        case STATE_READY:
            showReady();
            break;
        case STATE_SCANNING:
            showScanning();
            break;
        case STATE_ALERT:
            showAlert();
            break;
    }
}

void DisplayEngine::handleAudioRequest(const AudioEvent& event) {
    if (!display) return;
    
    // Determine state based on audio file
    if (strcmp(event.soundFile, "/startup.wav") == 0) {
        currentState = STATE_STARTING;
        stateStartTime = millis();
        showStarting();
    } else if (strcmp(event.soundFile, "/ready.wav") == 0) {
        currentState = STATE_READY;
        stateStartTime = millis();
        showReady();
    } else if (strcmp(event.soundFile, "/alert.wav") == 0) {
        currentState = STATE_ALERT;
        stateStartTime = millis();
        showAlert();
    }
}

void DisplayEngine::showStarting() {
    display->clearDisplay();
    display->setTextSize(2);  // Large text
    display->setCursor(10, 8);  // Center horizontally, vertically
    display->print("Starting...");
    display->display();
}

void DisplayEngine::showReady() {
    display->clearDisplay();
    display->setTextSize(2);  // Large text
    display->setCursor(32, 8);  // Center "Ready" (5 chars * 12 pixels = 60, (128-60)/2 = 34, but adjust for centering)
    display->print("Ready");
    display->display();
}

void DisplayEngine::showScanning() {
    display->clearDisplay();
    
    // Draw radar sweep line (vertical line sweeping horizontally)
    display->drawLine(radarPosition, 0, radarPosition, DISPLAY_HEIGHT - 1, SSD1306_WHITE);
    
    // Update radar position
    radarPosition += radarDirection * 2;  // Move 2 pixels per update
    
    // Reverse direction at edges
    if (radarPosition >= DISPLAY_WIDTH - 1) {
        radarPosition = DISPLAY_WIDTH - 1;
        radarDirection = -1;
    } else if (radarPosition <= 0) {
        radarPosition = 0;
        radarDirection = 1;
    }
    
    display->display();
}

void DisplayEngine::showAlert() {
    display->clearDisplay();
    display->setTextSize(2);  // Large text
    display->setCursor(16, 8);  // Center "ALERT!" (6 chars * 12 pixels = 72, (128-72)/2 = 28, adjust to 16)
    display->print("ALERT!");
    display->display();
}

void DisplayEngine::clearDisplay() {
    if (display) {
        display->clearDisplay();
    }
}

// Main system initialization
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Initializing Threat Detection System...");
    Serial.println();
    
    displaySystem.initialize();
    audioSystem.initialize();
    
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
        displaySystem.handleAudioRequest(event);
    });
    
    EventBus::subscribeSystemReady([]() {
        AudioEvent audioEvent;
        audioEvent.soundFile = "/ready.wav";
        EventBus::publishAudioRequest(audioEvent);
    });
    
    // Trigger display + audio for startup now that handlers are wired up.
    AudioEvent startupEvent;
    startupEvent.soundFile = "/startup.wav";
    EventBus::publishAudioRequest(startupEvent);

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
        if (threatCopy.shouldAlert) {
            AudioEvent audioEvent;
            audioEvent.soundFile = "/alert.wav";
            EventBus::publishAudioRequest(audioEvent);
        }
    }
    // SUSPICIOUS events are captured via telemetry; display-only variants
    // show them through the normal DisplayEngine state machine.

    displaySystem.update();
    delay(100);
}