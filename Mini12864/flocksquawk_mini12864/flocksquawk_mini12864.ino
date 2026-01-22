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
#include <SPI.h>
#include <U8g2lib.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "src/EventBus.h"
#include "src/DeviceSignatures.h"
#include "src/RadioScanner.h"
#include "src/ThreatAnalyzer.h"
#include "src/SoundEngine.h"
#include "src/TelemetryReporter.h"
#include "src/Mini12864Display.h"

// Global system components
RadioScannerManager rfScanner;
ThreatAnalyzer threatEngine;
SoundEngine audioSystem;
TelemetryReporter reporter;

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

// RadioScannerManager implementation
void RadioScannerManager::initialize() {
    configureWiFiSniffer();
    configureBluetoothScanner();
}

void RadioScannerManager::configureWiFiSniffer() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filter);
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

uint8_t RadioScannerManager::getCurrentWifiChannel() {
    return currentWifiChannel;
}

// ThreatAnalyzer implementation
void ThreatAnalyzer::initialize() {
    // Analyzer ready
}

void ThreatAnalyzer::analyzeWiFiFrame(const WiFiFrameEvent& frame) {
    bool nameMatch = strlen(frame.ssid) > 0 && matchesNetworkName(frame.ssid);
    bool macMatch = matchesMACPrefix(frame.mac);
    
    if (nameMatch || macMatch) {
        uint8_t certainty = calculateCertainty(nameMatch, macMatch, false);
        emitThreatDetection(frame, "wifi", certainty);
    }
}

void ThreatAnalyzer::analyzeBluetoothDevice(const BluetoothDeviceEvent& device) {
    bool nameMatch = strlen(device.name) > 0 && matchesBLEName(device.name);
    bool macMatch = matchesMACPrefix(device.mac);
    bool uuidMatch = device.hasServiceUUID && matchesRavenService(device.serviceUUID);
    
    if (nameMatch || macMatch || uuidMatch) {
        uint8_t certainty = calculateCertainty(nameMatch, macMatch, uuidMatch);
        const char* category = determineCategory(uuidMatch);
        emitThreatDetection(device, "bluetooth", certainty, category);
    }
}

bool ThreatAnalyzer::matchesNetworkName(const char* ssid) {
    if (!ssid) return false;
    
    for (size_t i = 0; i < DeviceProfiles::NetworkNameCount; i++) {
        if (strcasestr(ssid, DeviceProfiles::NetworkNames[i])) {
            return true;
        }
    }
    return false;
}

bool ThreatAnalyzer::matchesMACPrefix(const uint8_t* mac) {
    char macStr[9];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
    
    for (size_t i = 0; i < DeviceProfiles::MACPrefixCount; i++) {
        if (strncasecmp(macStr, DeviceProfiles::MACPrefixes[i], 8) == 0) {
            return true;
        }
    }
    return false;
}

bool ThreatAnalyzer::matchesBLEName(const char* name) {
    if (!name) return false;
    
    for (size_t i = 0; i < DeviceProfiles::BLEIdentifierCount; i++) {
        if (strcasestr(name, DeviceProfiles::BLEIdentifiers[i])) {
            return true;
        }
    }
    return false;
}

bool ThreatAnalyzer::matchesRavenService(const char* uuid) {
    if (!uuid) return false;
    
    for (size_t i = 0; i < DeviceProfiles::RavenServiceCount; i++) {
        if (strcasecmp(uuid, DeviceProfiles::RavenServices[i]) == 0) {
            return true;
        }
    }
    return false;
}

uint8_t ThreatAnalyzer::calculateCertainty(bool nameMatch, bool macMatch, bool uuidMatch) {
    if (nameMatch && macMatch && uuidMatch) return 100;
    if (nameMatch && macMatch) return 95;
    if (uuidMatch) return 90;
    if (nameMatch || macMatch) return 85;
    return 70;
}

const char* ThreatAnalyzer::determineCategory(bool isRaven) {
    return isRaven ? "acoustic_detector" : "surveillance_device";
}

void ThreatAnalyzer::emitThreatDetection(const WiFiFrameEvent& frame, const char* radio, uint8_t certainty) {
    ThreatEvent threat;
    memset(&threat, 0, sizeof(threat));
    memcpy(threat.mac, frame.mac, 6);
    strncpy(threat.identifier, frame.ssid, sizeof(threat.identifier) - 1);
    threat.rssi = frame.rssi;
    threat.channel = frame.channel;
    threat.radioType = radio;
    threat.certainty = certainty;
    threat.category = "surveillance_device";
    
    EventBus::publishThreat(threat);
}

void ThreatAnalyzer::emitThreatDetection(const BluetoothDeviceEvent& device, const char* radio, uint8_t certainty, const char* category) {
    ThreatEvent threat;
    memset(&threat, 0, sizeof(threat));
    memcpy(threat.mac, device.mac, 6);
    strncpy(threat.identifier, device.name, sizeof(threat.identifier) - 1);
    threat.rssi = device.rssi;
    threat.channel = 0;
    threat.radioType = radio;
    threat.certainty = certainty;
    threat.category = category;
    
    EventBus::publishThreat(threat);
}

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
                Mini12864DisplayUpdate();
            }
        }
    } else {
        while (audioFile.available()) {
            size_t bytesRead = audioFile.read(buffer, sizeof(buffer));
            if (bytesRead == 0) break;
            
            size_t sampleCount = (bytesRead / 2) * 2;
            applyVolumeControl(buffer, sampleCount / 2);
            i2s_write(I2S_NUM_0, buffer, sampleCount, &bytesWritten, portMAX_DELAY);
            Mini12864DisplayUpdate();
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

// TelemetryReporter implementation
void TelemetryReporter::initialize() {
    bootTime = millis();
}

void TelemetryReporter::handleThreatDetection(const ThreatEvent& threat) {
    DynamicJsonDocument doc(2048);
    
    doc["event"] = "target_detected";
    doc["ms_since_boot"] = millis() - bootTime;
    
    appendSourceInfo(threat, doc);
    appendTargetIdentity(threat, doc);
    appendIndicators(threat, doc);
    appendMetadata(threat, doc);
    
    outputJSON(doc);
}

void TelemetryReporter::appendSourceInfo(const ThreatEvent& threat, JsonDocument& doc) {
    JsonObject source = doc.createNestedObject("source");
    source["radio"] = threat.radioType;
    source["channel"] = threat.channel;
    source["rssi"] = threat.rssi;
}

void TelemetryReporter::appendTargetIdentity(const ThreatEvent& threat, JsonDocument& doc) {
    JsonObject target = doc.createNestedObject("target");
    JsonObject identity = target.createNestedObject("identity");
    
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             threat.mac[0], threat.mac[1], threat.mac[2],
             threat.mac[3], threat.mac[4], threat.mac[5]);
    identity["mac"] = macStr;
    
    char oui[9];
    snprintf(oui, sizeof(oui), "%02x:%02x:%02x", threat.mac[0], threat.mac[1], threat.mac[2]);
    identity["oui"] = oui;
    
    identity["label"] = threat.identifier;
}

void TelemetryReporter::appendIndicators(const ThreatEvent& threat, JsonDocument& doc) {
    JsonObject indicators = doc["target"].createNestedObject("indicators");
    
    bool hasName = strlen(threat.identifier) > 0;
    indicators["ssid_match"] = (hasName && strcmp(threat.radioType, "wifi") == 0);
    indicators["mac_match"] = true;
    indicators["name_match"] = (hasName && strcmp(threat.radioType, "bluetooth") == 0);
    indicators["service_uuid_match"] = (strcmp(threat.category, "acoustic_detector") == 0);
}

void TelemetryReporter::appendMetadata(const ThreatEvent& threat, JsonDocument& doc) {
    JsonObject metadata = doc.createNestedObject("metadata");
    
    if (strcmp(threat.radioType, "wifi") == 0) {
        metadata["frame_type"] = "beacon";
    } else {
        metadata["frame_type"] = "advertisement";
    }
    
    metadata["detection_method"] = "combined_signature";
}

void TelemetryReporter::outputJSON(const JsonDocument& doc) {
    serializeJson(doc, Serial);
    Serial.println();
}

// Main system initialization
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Initializing Threat Detection System...");
    Serial.println();
    
    Mini12864DisplayBegin();
    audioSystem.initialize();
    audioSystem.playSound("/startup.wav");
    
    EventBus::subscribeWifiFrame([](const WiFiFrameEvent& event) {
        threatEngine.analyzeWiFiFrame(event);
        Mini12864DisplayNotifyWifiFrame(event.mac, event.channel, event.rssi);
    });
    
    EventBus::subscribeBluetoothDevice([](const BluetoothDeviceEvent& event) {
        threatEngine.analyzeBluetoothDevice(event);
    });
    
    EventBus::subscribeThreat([](const ThreatEvent& event) {
        reporter.handleThreatDetection(event);
        Mini12864DisplayShowAlert();
        AudioEvent audioEvent;
        audioEvent.soundFile = "/alert.wav";
        EventBus::publishAudioRequest(audioEvent);
    });
    
    EventBus::subscribeAudioRequest([](const AudioEvent& event) {
        audioSystem.handleAudioRequest(event);
    });
    
    EventBus::subscribeSystemReady([]() {
    Mini12864DisplayNotifySystemReady();
        audioSystem.playSound("/ready.wav");
    });
    
    threatEngine.initialize();
    reporter.initialize();
    rfScanner.initialize();
    
    Serial.println("System operational - scanning for targets");
    Serial.println();
    
    EventBus::publishSystemReady();
}

void loop() {
    Mini12864DisplayUpdate();
    float newVolume = 0.0f;
    if (Mini12864DisplayConsumeVolume(&newVolume)) {
        audioSystem.setVolume(newVolume);
    }
    if (Mini12864DisplayConsumeAlertTest()) {
        Mini12864DisplayShowAlert();
        AudioEvent audioEvent;
        audioEvent.soundFile = "/alert.wav";
        EventBus::publishAudioRequest(audioEvent);
    }
    rfScanner.update();
}