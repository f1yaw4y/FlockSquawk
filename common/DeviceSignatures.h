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
}

#endif
