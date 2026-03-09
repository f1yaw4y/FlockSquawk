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
    static bool isBleScanning() { return isScanningBLE; }

    // Switch between battery-optimized and high-performance scanning
    static void setPerformanceMode(bool highPerformance) {
        _highPerformance = highPerformance;
        _applyDutyCycle();
    }

    /// Notify the scanner that a BLE GATT client connected / disconnected.
    /// Safe to call from any task (e.g. NimBLE callback); the actual scan
    /// parameter update is deferred to the main loop via a volatile flag.
    static void setBleClientConnected(bool connected) {
        _bleClientConnected = connected;
        _dutyCycleDirty = true;
    }

    /// Call from the main loop to apply any pending duty-cycle changes.
    static void applyPendingDutyCycle() {
        if (_dutyCycleDirty) {
            _dutyCycleDirty = false;
            _applyDutyCycle();
        }
    }

private:
    static bool _highPerformance;
    static volatile bool _bleClientConnected;
    static volatile bool _dutyCycleDirty;

    /// Recompute scan parameters from current performance mode and BLE client
    /// connection state.
    static void _applyDutyCycle() {
        if (_highPerformance && !_bleClientConnected) {
            // Full performance, no BLE client — maximum scan duty
            CHANNEL_SWITCH_MS    = 200;
            BLE_SCAN_SECONDS     = 3;
            BLE_SCAN_INTERVAL_MS = 3000;
        } else if (_highPerformance && _bleClientConnected) {
            // High performance but sharing radio with BLE client
            CHANNEL_SWITCH_MS    = 200;
            BLE_SCAN_SECONDS     = 2;
            BLE_SCAN_INTERVAL_MS = 4000;
        } else if (!_highPerformance && !_bleClientConnected) {
            // Battery mode, no BLE client — moderate boost
            CHANNEL_SWITCH_MS    = 300;
            BLE_SCAN_SECONDS     = 3;
            BLE_SCAN_INTERVAL_MS = 3000;
        } else {
            // Battery mode + BLE client — conservative (original defaults)
            CHANNEL_SWITCH_MS    = 300;
            BLE_SCAN_SECONDS     = 2;
            BLE_SCAN_INTERVAL_MS = 5000;
        }
    }

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