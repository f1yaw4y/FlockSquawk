#ifndef TELEMETRY_REPORTER_H
#define TELEMETRY_REPORTER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "EventBus.h"
#include "DetectorTypes.h"

class TelemetryReporter {
public:
    void initialize() {
        bootTime = millis();
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
        target["category"] = threat.category;
        target["should_alert"] = threat.shouldAlert;

        // Detector details from matchFlags
        JsonObject detectors = target.createNestedObject("detectors");

        static const char* const detectorNames[] = {
            "ssid_format", "ssid_keyword", "mac_oui",
            "ble_name", "raven_custom_uuid", "raven_std_uuid",
            "rssi_modifier"
        };

        for (uint8_t bit = 0; bit < 7; bit++) {
            if (threat.matchFlags & (1 << bit)) {
                if (bit == 6) {
                    // rssi_modifier is signed
                    detectors[detectorNames[bit]] = threat.rssiModifier;
                } else {
                    detectors[detectorNames[bit]] = threat.detectorWeights[bit];
                }
            }
        }

        serializeJson(doc, Serial);
        Serial.println();
    }

private:
    unsigned long bootTime;
};

#endif
