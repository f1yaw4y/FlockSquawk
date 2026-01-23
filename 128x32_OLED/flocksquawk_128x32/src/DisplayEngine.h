#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "EventBus.h"

class DisplayEngine {
public:
    static const uint8_t DISPLAY_WIDTH = 128;
    static const uint8_t DISPLAY_HEIGHT = 32;
    static const uint8_t I2C_ADDRESS = 0x3C;  // Common SSD1306 I2C address
    
    enum DisplayState {
        STATE_STARTING,
        STATE_READY,
        STATE_SCANNING,
        STATE_ALERT
    };

    DisplayEngine();
    void initialize();
    void update();  // Call from main loop for animations
    void handleAudioRequest(const AudioEvent& event);
    
private:
    Adafruit_SSD1306* display;
    DisplayState currentState;
    unsigned long stateStartTime;
    int16_t radarPosition;  // Current position of radar sweep line
    int8_t radarDirection;  // 1 for right, -1 for left
    
    void showStarting();
    void showReady();
    void showScanning();
    void showAlert();
    void clearDisplay();
};

#endif
