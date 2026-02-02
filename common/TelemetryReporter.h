#ifndef TELEMETRY_REPORTER_H
#define TELEMETRY_REPORTER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "EventBus.h"
#include "DetectorTypes.h"

class BleTransport;  // forward declaration

class TelemetryReporter {
public:
    void initialize() {
        bootTime = millis();
    }

    void setBleTransport(BleTransport* transport) {
        _bleTransport = transport;
    }

    void handleThreatDetection(const ThreatEvent& threat) {
        StaticJsonDocument<512> doc;

        doc["event"] = "target_detected";
        doc["ms_since_boot"] = millis() - bootTime;

        // Source info
        JsonObject source = doc.createNestedObject("source");
        source["radio"] = threat.radioType;
        source["channel"] = threat.channel;
        source["rssi"] = threat.rssi;

        // Target identity
        JsonObject target = doc.createNestedObject("target");

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 threat.mac[0], threat.mac[1], threat.mac[2],
                 threat.mac[3], threat.mac[4], threat.mac[5]);
        target["mac"] = macStr;
        target["label"] = threat.identifier;
        target["certainty"] = threat.certainty;
        target["alert_level"] = threat.alertLevel;
        target["category"] = threat.category;
        target["should_alert"] = threat.shouldAlert;

        // Detector details from matchFlags
        JsonObject detectors = target.createNestedObject("detectors");

        static const char* const detectorNames[] = {
            "ssid_format", "ssid_keyword", "mac_oui",
            "ble_name", "raven_custom_uuid", "raven_std_uuid",
            "rssi_modifier", "flock_oui", "surveillance_oui"
        };

        for (uint8_t bit = 0; bit < MAX_DETECTOR_WEIGHTS; bit++) {
            if (threat.matchFlags & (1 << bit)) {
                if (bit == 6) {
                    // rssi_modifier is signed
                    detectors[detectorNames[bit]] = threat.rssiModifier;
                } else {
                    detectors[detectorNames[bit]] = threat.detectorWeights[bit];
                }
            }
        }

        // +1 byte for the newline that _sendViaBle appends
        char buf[513];
        size_t len = serializeJson(doc, buf, sizeof(buf) - 1);

        // Always output to Serial (USB)
        Serial.write(buf, len);
        Serial.println();

        // Also send via BLE if a client is connected.
        // Skip if JSON was truncated (len == sizeof(buf)-1) since the
        // client would fail to parse it anyway.
        if (_bleTransport && len < sizeof(buf) - 1) {
            _sendViaBle(buf, len);
        }
    }

private:
    unsigned long bootTime;
    BleTransport* _bleTransport = nullptr;

    // Defined in .ino after BleTransport.h is included
    inline void _sendViaBle(char* buf, size_t len);
};

#endif
