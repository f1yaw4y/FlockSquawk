#include "doctest.h"
#include "ThreatAnalyzer.h"

extern uint32_t mock_millis_value;

// ============================================================
// Helpers
// ============================================================

static void setMAC(uint8_t* dst, uint8_t a, uint8_t b, uint8_t c,
                   uint8_t d, uint8_t e, uint8_t f) {
    dst[0] = a; dst[1] = b; dst[2] = c;
    dst[3] = d; dst[4] = e; dst[5] = f;
}

// ============================================================
// DeviceTracker tests
// ============================================================

TEST_CASE("DeviceTracker: initialize clears all slots") {
    DeviceTracker tracker;
    tracker.initialize();
    // After initialize, no device should be in range
    CHECK_FALSE(tracker.hasHighConfidenceInRange());
}

TEST_CASE("DeviceTracker: first detection returns EMPTY") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);
    DeviceState prev = tracker.recordDetection(mac, 1000, 80);
    CHECK(prev == DeviceState::EMPTY);
}

TEST_CASE("DeviceTracker: second detection returns NEW_DETECT") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);
    tracker.recordDetection(mac, 1000, 80);
    DeviceState prev = tracker.recordDetection(mac, 2000, 80);
    CHECK(prev == DeviceState::NEW_DETECT);
}

TEST_CASE("DeviceTracker: third detection returns IN_RANGE") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);
    tracker.recordDetection(mac, 1000, 80);
    tracker.recordDetection(mac, 2000, 80);
    DeviceState prev = tracker.recordDetection(mac, 3000, 80);
    CHECK(prev == DeviceState::IN_RANGE);
}

TEST_CASE("DeviceTracker: timeout transitions to DEPARTED") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);
    tracker.recordDetection(mac, 1000, 80);
    tracker.recordDetection(mac, 2000, 80);  // now IN_RANGE, lastSeenMs=2000
    // Tick past the 60s timeout from last-seen time
    tracker.tick(2000 + DEVICE_TIMEOUT_MS + 1);
    CHECK_FALSE(tracker.hasHighConfidenceInRange());
}

TEST_CASE("DeviceTracker: hasHighConfidenceInRange") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);

    SUBCASE("above threshold and IN_RANGE returns true") {
        tracker.recordDetection(mac, 1000, 80);
        tracker.recordDetection(mac, 2000, 80);  // IN_RANGE
        CHECK(tracker.hasHighConfidenceInRange());
    }
    SUBCASE("below threshold returns false") {
        tracker.recordDetection(mac, 1000, 30);
        tracker.recordDetection(mac, 2000, 30);
        CHECK_FALSE(tracker.hasHighConfidenceInRange());
    }
    SUBCASE("NEW_DETECT alone is not IN_RANGE") {
        // NEW_DETECT with high certainty shouldn't count
        tracker.recordDetection(mac, 1000, 90);
        CHECK_FALSE(tracker.hasHighConfidenceInRange());
    }
}

TEST_CASE("DeviceTracker: max certainty updates on higher value") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);
    tracker.recordDetection(mac, 1000, 30);  // NEW_DETECT
    tracker.recordDetection(mac, 2000, 80);  // IN_RANGE, certainty bumped to 80
    CHECK(tracker.hasHighConfidenceInRange());
}

TEST_CASE("DeviceTracker: LRU eviction prefers empty slots") {
    DeviceTracker tracker;
    tracker.initialize();
    // Fill all 32 slots
    for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
        uint8_t mac[6];
        setMAC(mac, 0x10, 0x20, 0x30, 0x00, 0x00, i);
        tracker.recordDetection(mac, 1000 + i, 50);
    }
    // 33rd device should evict the oldest departed (none departed)
    // so it evicts oldest active device (slot 0, lastSeen=1000)
    uint8_t newMac[6];
    setMAC(newMac, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01);
    DeviceState prev = tracker.recordDetection(newMac, 5000, 70);
    CHECK(prev == DeviceState::EMPTY);
}

TEST_CASE("DeviceTracker: eviction prefers departed over active") {
    DeviceTracker tracker;
    tracker.initialize();
    // Fill 32 slots
    for (uint8_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
        uint8_t mac[6];
        setMAC(mac, 0x10, 0x20, 0x30, 0x00, 0x00, i);
        tracker.recordDetection(mac, 1000, 50);
    }
    // Timeout all → DEPARTED
    tracker.tick(1000 + DEVICE_TIMEOUT_MS + 1);
    // New device should reuse a departed slot
    uint8_t newMac[6];
    setMAC(newMac, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01);
    DeviceState prev = tracker.recordDetection(newMac, 200000, 70);
    CHECK(prev == DeviceState::EMPTY);
}

TEST_CASE("DeviceTracker: NEW_DETECT times out") {
    DeviceTracker tracker;
    tracker.initialize();
    uint8_t mac[6];
    setMAC(mac, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33);
    tracker.recordDetection(mac, 1000, 80);  // NEW_DETECT
    // Tick past timeout — NEW_DETECT should also depart
    tracker.tick(1000 + DEVICE_TIMEOUT_MS + 1);
    // Re-detect should return EMPTY (departed slot doesn't match)
    DeviceState prev = tracker.recordDetection(mac, 200000, 80);
    CHECK(prev == DeviceState::EMPTY);
}
