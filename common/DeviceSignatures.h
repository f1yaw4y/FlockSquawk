#ifndef DEVICE_SIGNATURES_H
#define DEVICE_SIGNATURES_H

#include <Arduino.h>

namespace DeviceProfiles {

    // MAC address OUI prefixes for target devices (Lite-On Technology)
    const char* const MACPrefixes[] = {
        "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84",
        "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
        "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc",
        "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea"
    };
    const size_t MACPrefixCount = sizeof(MACPrefixes) / sizeof(MACPrefixes[0]);

    // Flock Safety (direct OUI registration — high confidence)
    const char* const FlockSafetyOUI = "b4:1e:52";

    // Surveillance camera manufacturers (curated from FlockOff database).
    // Dedicated security/surveillance companies only.
    struct SurveillanceOUI {
        const char* prefix;
        const char* manufacturer;
    };

    const SurveillanceOUI SurveillancePrefixes[] = {
        // Avigilon Alta
        { "70:1a:d5", "Avigilon Alta" },
        // Axis Communications
        { "00:40:8c", "Axis Communications" },
        { "ac:cc:8e", "Axis Communications" },
        { "b8:a4:4f", "Axis Communications" },
        { "e8:27:25", "Axis Communications" },
        // FLIR Systems
        { "00:13:56", "FLIR Systems" },
        { "00:40:7f", "FLIR Systems" },
        { "00:1b:d8", "FLIR Systems" },
        // GeoVision
        { "00:13:e2", "GeoVision" },
        // Hanwha Vision
        { "44:b4:23", "Hanwha Vision" },
        { "8c:1d:55", "Hanwha Vision" },
        { "e4:30:22", "Hanwha Vision" },
        // March Networks
        { "00:10:be", "March Networks" },
        { "00:12:81", "March Networks" },
        // Mobotix
        { "00:03:c5", "Mobotix" },
        // Sunell Electronics
        { "00:1c:27", "Sunell Electronics" },
    };
    const size_t SurveillancePrefixCount =
        sizeof(SurveillancePrefixes) / sizeof(SurveillancePrefixes[0]);
}

#endif
