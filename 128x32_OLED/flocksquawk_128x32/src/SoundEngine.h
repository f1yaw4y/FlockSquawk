#ifndef SOUND_ENGINE_H
#define SOUND_ENGINE_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <FS.h>
#include <LittleFS.h>
#include "EventBus.h"

class SoundEngine {
public:
    static const uint8_t PIN_BCLK = 27;
    static const uint8_t PIN_LRC = 26;
    static const uint8_t PIN_DATA = 25;
    static constexpr float DEFAULT_VOLUME = 0.4f;

    void initialize();
    void setVolume(float level);
    void playSound(const char* filename);
    void handleAudioRequest(const AudioEvent& event);
    
private:
    float volumeLevel;
    
    void setupI2SInterface();
    void streamAudioFile(File& audioFile);
    void applyVolumeControl(uint8_t* buffer, size_t sampleCount);
};

#endif