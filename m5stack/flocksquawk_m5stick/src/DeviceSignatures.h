#ifndef DEVICE_SIGNATURES_H
#define DEVICE_SIGNATURES_H

#include <Arduino.h>

namespace DeviceProfiles {
    
    // Network name patterns for target identification
    const char* const NetworkNames[] = {
        "flock",
        "Flock", 
        "FLOCK",
        "FS Ext Battery",
        "Penguin",
        "Pigvision",
    };
    const size_t NetworkNameCount = sizeof(NetworkNames) / sizeof(NetworkNames[0]);

    // MAC address OUI prefixes for target devices
    const char* const MACPrefixes[] = {
        "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84",
        "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
        "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc",
        "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea"
    };
    const size_t MACPrefixCount = sizeof(MACPrefixes) / sizeof(MACPrefixes[0]);

    // Bluetooth device name patterns
    const char* const BLEIdentifiers[] = {
        "FS Ext Battery",
        "Penguin",
        "Flock",
        "Pigvision"
    };
    const size_t BLEIdentifierCount = sizeof(BLEIdentifiers) / sizeof(BLEIdentifiers[0]);

    // Raven acoustic detection device service UUIDs
    const char* const RavenServices[] = {
        "0000180a-0000-1000-8000-00805f9b34fb",  // Device info (all versions)
        "00003100-0000-1000-8000-00805f9b34fb",  // GPS (1.2.0+)
        "00003200-0000-1000-8000-00805f9b34fb",  // Power/Battery (1.2.0+)
        "00003300-0000-1000-8000-00805f9b34fb",  // Network (1.2.0+)
        "00003400-0000-1000-8000-00805f9b34fb",  // Upload stats (1.2.0+)
        "00003500-0000-1000-8000-00805f9b34fb",  // Error tracking (1.2.0+)
        "00001809-0000-1000-8000-00805f9b34fb",  // Health/Temp (legacy 1.1.7)
        "00001819-0000-1000-8000-00805f9b34fb"   // Location (legacy 1.1.7)
    };
    const size_t RavenServiceCount = sizeof(RavenServices) / sizeof(RavenServices[0]);
}

#endif
