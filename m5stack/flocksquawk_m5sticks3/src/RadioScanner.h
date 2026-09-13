#ifndef RADIO_SCANNER_H
#define RADIO_SCANNER_H

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "EventBus.h"

class RadioScannerManager {
public:
    static const uint8_t MAX_WIFI_CHANNEL = 13;
    static uint16_t CHANNEL_SWITCH_MS;
    static uint8_t BLE_SCAN_SECONDS;
    static uint32_t BLE_SCAN_INTERVAL_MS;

    void initialize();
    void update();  // Call from main loop
    static uint8_t getCurrentWifiChannel();

    // Switch between battery-optimized and high-performance scanning
    static void setPerformanceMode(bool highPerformance) {
        if (highPerformance) {
            CHANNEL_SWITCH_MS   = 200;
            BLE_SCAN_SECONDS    = 3;
            BLE_SCAN_INTERVAL_MS = 4000;
        } else {
            CHANNEL_SWITCH_MS   = 300;
            BLE_SCAN_SECONDS    = 2;
            BLE_SCAN_INTERVAL_MS = 5000;
        }
    }

private:
    static volatile uint8_t currentWifiChannel;
    static unsigned long lastChannelSwitch;
    static unsigned long lastBLEScan;
    static NimBLEScan* bleScanner;
    static bool isScanningBLE;

    void configureWiFiSniffer();
    void configureBluetoothScanner();
    void switchWifiChannel();
    void performBLEScan();
    static void wifiPacketHandler(void* buffer, wifi_promiscuous_pkt_type_t type);

    // BLE callback handler
    class BLEDeviceObserver;
    friend class BLEDeviceObserver;
};

#endif