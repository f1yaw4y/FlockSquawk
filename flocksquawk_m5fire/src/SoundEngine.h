#ifndef SOUND_ENGINE_H
#define SOUND_ENGINE_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <M5Unified.h>
#include "EventBus.h"

class SoundEngine {
public:
    static const uint8_t SD_SCK = 18;
    static const uint8_t SD_MISO = 19;
    static const uint8_t SD_MOSI = 23;
    static const uint8_t SD_CS = 4;
    static constexpr float DEFAULT_VOLUME = 0.4f;

    void initialize();
    void setVolume(float level);
    float getVolumeLevel() const;
    void playSound(const char* filename);
    void playSoundAsync(const char* filename);
    void update();
    void handleAudioRequest(const AudioEvent& event);
    
private:
    float volumeLevel;
    uint8_t* asyncBuffer = nullptr;
    size_t asyncLength = 0;
    bool asyncActive = false;
    
    bool loadWavFromSd(const char* filename, uint8_t** outData, size_t* outLength);
};

#endif