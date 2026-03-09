#include "doctest.h"
#include "ConnectionStatus.h"

// ============================================================
// computeSerialState
// ============================================================
TEST_CASE("computeSerialState: no serial ever (lastSerialRxMs == 0)") {
    CHECK(computeSerialState(0, 10000) == CONN_NONE);
}

TEST_CASE("computeSerialState: serial within timeout") {
    // 10000 - 8000 = 2000ms < 5000ms timeout
    CHECK(computeSerialState(8000, 10000) == CONN_SERIAL);
}

TEST_CASE("computeSerialState: serial expired") {
    // 10000 - 4000 = 6000ms > 5000ms timeout
    CHECK(computeSerialState(4000, 10000) == CONN_NONE);
}

TEST_CASE("computeSerialState: exactly at timeout boundary") {
    // now - lastSerialRxMs == 5000, which is NOT < 5000, so not alive
    CHECK(computeSerialState(5000, 10000) == CONN_NONE);
}

TEST_CASE("computeSerialState: one ms before timeout") {
    // now - lastSerialRxMs == 4999, which IS < 5000
    CHECK(computeSerialState(5001, 10000) == CONN_SERIAL);
}

// ============================================================
// isBatteryRising
// ============================================================
TEST_CASE("isBatteryRising: level increased") {
    CHECK(isBatteryRising(80, 79) == true);
}

TEST_CASE("isBatteryRising: level unchanged") {
    CHECK(isBatteryRising(80, 80) == false);
}

TEST_CASE("isBatteryRising: level decreased") {
    CHECK(isBatteryRising(79, 80) == false);
}

TEST_CASE("isBatteryRising: held at 100%") {
    CHECK(isBatteryRising(100, 100) == true);
}

TEST_CASE("isBatteryRising: rose to 100%") {
    CHECK(isBatteryRising(100, 99) == true);
}

TEST_CASE("isBatteryRising: dropped from 100%") {
    CHECK(isBatteryRising(99, 100) == false);
}

TEST_CASE("isBatteryRising: both zero") {
    CHECK(isBatteryRising(0, 0) == false);
}
