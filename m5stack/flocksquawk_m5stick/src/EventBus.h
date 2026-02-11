#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <Arduino.h>
#include <functional>

enum class EventType {
    WifiFrameCaptured,
    BluetoothDeviceFound,
    ThreatIdentified,
    SystemReady
};

struct WiFiFrameEvent {
    uint8_t mac[6];
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t frameSubtype;  // 0x20 = probe, 0x80 = beacon
};

struct BluetoothDeviceEvent {
    uint8_t mac[6];
    char name[64];
    int8_t rssi;
    bool hasServiceUUID;
    char serviceUUID[64];
};

struct ThreatEvent {
    uint8_t mac[6];
    char identifier[64];
    int8_t rssi;
    uint8_t channel;
    char radioType[16];
    uint8_t certainty;
    char category[24];
    uint16_t matchFlags;
    uint8_t  detectorWeights[8];
    int8_t   rssiModifier;
    bool     shouldAlert;
};

class EventBus {
public:
    typedef std::function<void(const WiFiFrameEvent&)> WiFiFrameHandler;
    typedef std::function<void(const BluetoothDeviceEvent&)> BluetoothHandler;
    typedef std::function<void(const ThreatEvent&)> ThreatHandler;
    typedef std::function<void()> SystemEventHandler;

    static void publishWifiFrame(const WiFiFrameEvent& event);
    static void publishBluetoothDevice(const BluetoothDeviceEvent& event);
    static void publishThreat(const ThreatEvent& event);
    static void publishSystemReady();

    static void subscribeWifiFrame(WiFiFrameHandler handler);
    static void subscribeBluetoothDevice(BluetoothHandler handler);
    static void subscribeThreat(ThreatHandler handler);
    static void subscribeSystemReady(SystemEventHandler handler);

private:
    static WiFiFrameHandler wifiHandler;
    static BluetoothHandler bluetoothHandler;
    static ThreatHandler threatHandler;
    static SystemEventHandler systemReadyHandler;
};

#endif