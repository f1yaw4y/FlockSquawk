#ifndef TELEMETRY_REPORTER_H
#define TELEMETRY_REPORTER_H

#include <Arduino.h>
#include "EventBus.h"

class TelemetryReporter {
public:
    void initialize();
    void handleThreatDetection(const ThreatEvent& threat);
    void handleWiFiFrameSeen(const WiFiFrameEvent& frame);
    void update();
    bool isAlertActive() const;
    
private:
    unsigned long bootTime;
    unsigned long lastAlertMs;
    bool alertActive;
    static const unsigned long ALERT_CLEAR_MS = 5000;
    unsigned long lastSeenMs;
    static const unsigned long SEEN_THROTTLE_MS = 200;

    void emitAlert(const ThreatEvent& threat);
    void emitClear();
    void emitStatus(const char* state);
    void emitSeen(const WiFiFrameEvent& frame);
};

#endif