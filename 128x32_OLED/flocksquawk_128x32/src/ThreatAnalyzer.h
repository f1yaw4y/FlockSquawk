#ifndef THREAT_ANALYZER_H
#define THREAT_ANALYZER_H

#include <Arduino.h>
#include "EventBus.h"
#include "DeviceSignatures.h"

class ThreatAnalyzer {
public:
    void initialize();
    void analyzeWiFiFrame(const WiFiFrameEvent& frame);
    void analyzeBluetoothDevice(const BluetoothDeviceEvent& device);
    
private:
    bool matchesNetworkName(const char* ssid);
    bool matchesMACPrefix(const uint8_t* mac);
    bool matchesBLEName(const char* name);
    bool matchesRavenService(const char* uuid);
    uint8_t calculateCertainty(bool nameMatch, bool macMatch, bool uuidMatch);
    const char* determineCategory(bool isRaven);
    void emitThreatDetection(const WiFiFrameEvent& frame, const char* radio, uint8_t certainty);
    void emitThreatDetection(const BluetoothDeviceEvent& device, const char* radio, uint8_t certainty, const char* category);
    void formatMACAddress(const uint8_t* mac, char* output);
    void extractOUI(const uint8_t* mac, char* output);
};

#endif