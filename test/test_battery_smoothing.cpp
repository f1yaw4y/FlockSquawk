#include "doctest.h"
#include "BatterySmoothing.h"

// ============================================================
// BatteryFilter::seed
// ============================================================
TEST_CASE("BatteryFilter: seed fills buffer and sets smoothed") {
    BatteryFilter f;
    f.seed(80);

    CHECK(f.smoothed == 80);
    CHECK(f.full == true);
    for (uint8_t i = 0; i < BatteryFilter::HISTORY_SIZE; i++) {
        CHECK(f.history[i] == 80);
    }
}

TEST_CASE("BatteryFilter: seed with zero") {
    BatteryFilter f;
    f.seed(0);
    CHECK(f.smoothed == 0);
}

TEST_CASE("BatteryFilter: seed with 100") {
    BatteryFilter f;
    f.seed(100);
    CHECK(f.smoothed == 100);
}

// ============================================================
// BatteryFilter::addSample — single sample
// ============================================================
TEST_CASE("BatteryFilter: single sample on unseeded filter") {
    BatteryFilter f;
    f.addSample(75);

    CHECK(f.smoothed == 75);
    CHECK(f.idx == 1);
    CHECK(f.full == false);
}

// ============================================================
// BatteryFilter::addSample — partial fill
// ============================================================
TEST_CASE("BatteryFilter: partial fill returns median of available samples") {
    BatteryFilter f;

    SUBCASE("two samples") {
        f.addSample(70);
        f.addSample(80);
        // sorted: [70, 80], count=2, median index=1 -> 80
        CHECK(f.smoothed == 80);
    }

    SUBCASE("three samples") {
        f.addSample(70);
        f.addSample(80);
        f.addSample(75);
        // sorted: [70, 75, 80], count=3, median index=1 -> 75
        CHECK(f.smoothed == 75);
    }

    SUBCASE("four samples") {
        f.addSample(70);
        f.addSample(80);
        f.addSample(75);
        f.addSample(72);
        // sorted: [70, 72, 75, 80], count=4, median index=2 -> 75
        CHECK(f.smoothed == 75);
    }
}

// ============================================================
// BatteryFilter::addSample — full buffer
// ============================================================
TEST_CASE("BatteryFilter: full buffer computes correct median") {
    BatteryFilter f;
    // Fill: 80, 79, 80, 79, 80, 79, 80, 79
    for (int i = 0; i < 8; i++) {
        f.addSample(i % 2 == 0 ? 80 : 79);
    }
    CHECK(f.full == true);
    // sorted: [79, 79, 79, 79, 80, 80, 80, 80], median index=4 -> 80
    CHECK(f.smoothed == 80);
}

TEST_CASE("BatteryFilter: all same values") {
    BatteryFilter f;
    for (int i = 0; i < 8; i++) f.addSample(50);
    CHECK(f.smoothed == 50);
}

TEST_CASE("BatteryFilter: ascending values") {
    BatteryFilter f;
    for (int i = 0; i < 8; i++) f.addSample(40 + i);
    // sorted: [40, 41, 42, 43, 44, 45, 46, 47], median index=4 -> 44
    CHECK(f.smoothed == 44);
}

TEST_CASE("BatteryFilter: descending values") {
    BatteryFilter f;
    for (int i = 0; i < 8; i++) f.addSample(47 - i);
    // sorted: [40, 41, 42, 43, 44, 45, 46, 47], median index=4 -> 44
    CHECK(f.smoothed == 44);
}

// ============================================================
// BatteryFilter — oscillation smoothing (the actual use case)
// ============================================================
TEST_CASE("BatteryFilter: oscillating 79/80 stabilizes to 80") {
    BatteryFilter f;
    f.seed(80);

    // Simulate boundary oscillation: 79, 80, 79, 80, 79, 80, 79, 80
    for (int i = 0; i < 8; i++) {
        f.addSample(i % 2 == 0 ? 79 : 80);
    }
    // 4x 79, 4x 80 -> sorted: [79,79,79,79,80,80,80,80] -> index 4 = 80
    CHECK(f.smoothed == 80);
}

TEST_CASE("BatteryFilter: gradual discharge") {
    BatteryFilter f;
    f.seed(80);

    // Mostly 79 with occasional 80 noise — should converge to 79
    // After seed, buffer is [80, 80, 80, 80, 80, 80, 80, 80]
    f.addSample(79); // [79, 80, 80, 80, 80, 80, 80, 80] -> median: 80
    CHECK(f.smoothed == 80);
    f.addSample(79); // [79, 79, 80, 80, 80, 80, 80, 80] -> median: 80
    CHECK(f.smoothed == 80);
    f.addSample(79);
    f.addSample(79);
    // [79, 79, 79, 79, 80, 80, 80, 80] -> median index 4: 80
    CHECK(f.smoothed == 80);
    f.addSample(79);
    // [79, 79, 79, 79, 79, 80, 80, 80] -> median index 4: 79
    CHECK(f.smoothed == 79);
}

// ============================================================
// BatteryFilter — single outlier rejected
// ============================================================
TEST_CASE("BatteryFilter: single outlier does not change smoothed") {
    BatteryFilter f;
    f.seed(75);

    // Add one wildly different reading
    f.addSample(50);
    // Buffer: [50, 75, 75, 75, 75, 75, 75, 75] -> median: 75
    CHECK(f.smoothed == 75);
}

// ============================================================
// BatteryFilter — wrapping
// ============================================================
TEST_CASE("BatteryFilter: buffer wraps correctly past HISTORY_SIZE") {
    BatteryFilter f;
    // Add 12 samples (wraps once at 8)
    for (int i = 0; i < 12; i++) {
        f.addSample(60 + (i % 3)); // 60, 61, 62, 60, 61, 62, ...
    }
    CHECK(f.full == true);
    // Last 8 samples: 61, 62, 60, 61, 62, 60, 61, 62
    // sorted: [60, 60, 61, 61, 61, 62, 62, 62], median index 4 -> 61
    CHECK(f.smoothed == 61);
}

// ============================================================
// BatteryFilter — edge values
// ============================================================
TEST_CASE("BatteryFilter: handles 0 and 100") {
    BatteryFilter f;
    f.seed(0);
    f.addSample(100);
    // Buffer: [100, 0, 0, 0, 0, 0, 0, 0] -> median: 0
    CHECK(f.smoothed == 0);

    // Fill with 100
    for (int i = 0; i < 8; i++) f.addSample(100);
    CHECK(f.smoothed == 100);
}

// ============================================================
// BatteryFilter — re-seed resets state
// ============================================================
TEST_CASE("BatteryFilter: re-seed overrides previous state") {
    BatteryFilter f;
    f.seed(50);
    f.addSample(60);
    f.addSample(70);

    f.seed(90);
    CHECK(f.smoothed == 90);
    CHECK(f.full == true);
    // All history should be 90
    for (uint8_t i = 0; i < BatteryFilter::HISTORY_SIZE; i++) {
        CHECK(f.history[i] == 90);
    }
}
