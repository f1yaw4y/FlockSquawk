#ifndef BLE_TRANSPORT_H
#define BLE_TRANSPORT_H

#include <Arduino.h>
#include <NimBLEDevice.h>

// FlockSquawk BLE GATT UUIDs — must match the Flutter side.
#define FLOCKSQUAWK_SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef0123456789"
#define FLOCKSQUAWK_TX_CHAR_UUID        "a1b2c3d4-e5f6-7890-abcd-ef01234567aa"

/// NimBLE GATT server that streams newline-delimited JSON telemetry to a
/// connected BLE client (DeFlock app on iOS / Android).
///
/// Usage:
///   BleTransport bleTransport;
///   bleTransport.initialize();            // after NimBLEDevice::init()
///   reporter.setBleTransport(&bleTransport);
///
/// When a client connects, the ESP32 reduces BLE scan duty to share radio
/// time.  When the client disconnects (e.g. phone switches to USB), scan
/// duty returns to normal.
class BleTransport : public NimBLEServerCallbacks {
public:
    /// Optional callback invoked when a BLE client connects or disconnects.
    /// The bool parameter is true on connect, false on disconnect.
    typedef void (*ClientStateCallback)(bool connected);

    void setClientStateCallback(ClientStateCallback cb) { _clientCb = cb; }

    /// Call after NimBLEDevice::init("").
    /// Creates server, service, TX characteristic, and starts advertising.
    void initialize() {
        NimBLEDevice::setMTU(512);

        _server = NimBLEDevice::createServer();
        _server->setCallbacks(this);

        NimBLEService* service = _server->createService(FLOCKSQUAWK_SERVICE_UUID);

        _txChar = service->createCharacteristic(
            FLOCKSQUAWK_TX_CHAR_UUID,
            NIMBLE_PROPERTY::NOTIFY
        );

        service->start();

        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        advertising->addServiceUUID(service->getUUID());
        advertising->enableScanResponse(true);
        advertising->start();

        Serial.println("[BLE] GATT server started, advertising");
    }

    /// Send a newline-delimited JSON line to the connected client.
    /// If no client is connected, this is a no-op.
    /// If the payload exceeds MTU-3, it is chunked; the client reassembles
    /// via the newline delimiter in JsonLineParser.
    void sendLine(const char* data, size_t len) {
        if (!_connected || _txChar == nullptr) return;

        uint16_t maxPayload = _negotiatedMtu - 3;
        if (maxPayload < 20) maxPayload = 20;

        if (len <= maxPayload) {
            _txChar->setValue((const uint8_t*)data, len);
            _txChar->notify();
        } else {
            // Chunk the data to fit within the negotiated MTU
            size_t offset = 0;
            while (offset < len) {
                size_t chunkSize = len - offset;
                if (chunkSize > maxPayload) chunkSize = maxPayload;
                _txChar->setValue((const uint8_t*)(data + offset), chunkSize);
                _txChar->notify();
                offset += chunkSize;
            }
        }
    }

    bool isClientConnected() const { return _connected; }

    // -- NimBLEServerCallbacks --

    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        _connected = true;
        Serial.println("[BLE] Client connected");
        if (_clientCb) _clientCb(true);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        _connected = false;
        _negotiatedMtu = 23;
        Serial.println("[BLE] Client disconnected, restarting advertising");
        if (_clientCb) _clientCb(false);
        NimBLEDevice::getAdvertising()->start();
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        _negotiatedMtu = MTU;
        Serial.printf("[BLE] MTU changed to %u\n", MTU);
    }

private:
    NimBLEServer* _server = nullptr;
    NimBLECharacteristic* _txChar = nullptr;
    volatile bool _connected = false;
    volatile uint16_t _negotiatedMtu = 23;
    ClientStateCallback _clientCb = nullptr;
};

#endif
