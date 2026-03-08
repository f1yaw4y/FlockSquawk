#ifndef RADIO_SCANNER_H
#define RADIO_SCANNER_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "EventBus.h"

// ESP32-S2 (Flipper WiFi Dev Board) does not support Bluetooth/BLE.
// Gate BLE code so the project builds cleanly on ESP32-S2.
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(ARDUINO_ESP32S2_DEV) || \
    defined(ARDUINO_ESP32S2_WROVER)
#define FLOCK_TARGET_ESP32S2 1
#else
#define FLOCK_TARGET_ESP32S2 0
#endif

#if !FLOCK_TARGET_ESP32S2
#define FLOCK_BLE_SUPPORTED 1
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#else
#define FLOCK_BLE_SUPPORTED 0
#endif

class RadioScannerManager {
public:
    static const uint8_t MAX_WIFI_CHANNEL = 13;
    static const uint16_t CHANNEL_SWITCH_MS = 300;
    static const uint8_t BLE_SCAN_SECONDS = 2;
    static const uint32_t BLE_SCAN_INTERVAL_MS = 5000;

    void initialize();
    void update();  // Call from main loop
    
private:
    static uint8_t currentWifiChannel;
    static unsigned long lastChannelSwitch;
    static unsigned long lastBLEScan;
#if FLOCK_BLE_SUPPORTED
    static NimBLEScan* bleScanner;
    static bool isScanningBLE;
#endif
    
    void configureWiFiSniffer();
    void configureBluetoothScanner();
    void switchWifiChannel();
    void performBLEScan();
    static void wifiPacketHandler(void* buffer, wifi_promiscuous_pkt_type_t type);
    
    // BLE callback handler
#if FLOCK_BLE_SUPPORTED
    class BLEDeviceObserver;
    friend class BLEDeviceObserver;
#endif
};

#endif