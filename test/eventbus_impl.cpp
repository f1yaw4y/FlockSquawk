// Provides storage for EventBus static members and the mock millis value.
// Linked into the test binary so ThreatAnalyzer's calls to
// EventBus::publishThreat() resolve without pulling in real hardware code.

#include "EventBus.h"

// Test-controllable time
uint32_t mock_millis_value = 0;

// EventBus static member definitions (no-op handlers by default)
EventBus::WiFiFrameHandler   EventBus::wifiHandler;
EventBus::BluetoothHandler   EventBus::bluetoothHandler;
EventBus::ThreatHandler      EventBus::threatHandler;
EventBus::SystemEventHandler EventBus::systemReadyHandler;

void EventBus::publishWifiFrame(const WiFiFrameEvent& event) {
    if (wifiHandler) wifiHandler(event);
}
void EventBus::publishBluetoothDevice(const BluetoothDeviceEvent& event) {
    if (bluetoothHandler) bluetoothHandler(event);
}
void EventBus::publishThreat(const ThreatEvent& event) {
    if (threatHandler) threatHandler(event);
}
void EventBus::publishSystemReady() {
    if (systemReadyHandler) systemReadyHandler();
}

void EventBus::subscribeWifiFrame(WiFiFrameHandler handler) {
    wifiHandler = handler;
}
void EventBus::subscribeBluetoothDevice(BluetoothHandler handler) {
    bluetoothHandler = handler;
}
void EventBus::subscribeThreat(ThreatHandler handler) {
    threatHandler = handler;
}
void EventBus::subscribeSystemReady(SystemEventHandler handler) {
    systemReadyHandler = handler;
}
