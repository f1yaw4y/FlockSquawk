# FlockSquawk (M5Stack FIRE)

M5Stack FIRE variant with built-in speaker and SD card audio. Passively detects surveillance devices using WiFi promiscuous mode and Bluetooth Low Energy scanning.

## Features

- **WiFi Scanning**: Promiscuous mode detection of probe requests and beacons
- **Bluetooth Low Energy**: Active scanning for device names, MAC addresses, and service UUIDs
- **Pattern Matching**: Identifies devices based on SSID patterns, MAC prefixes, device names, and service UUIDs
- **Audio Alerts**: Built-in M5Stack FIRE speaker playback with volume control
- **JSON Telemetry**: Structured event reporting via serial output
- **Event-Driven Architecture**: Modular design for easy extension

## Hardware Requirements

- **M5Stack FIRE IoT Development Kit (PSRAM) V2.7**
- **USB Cable** for programming and power

## Setup

For Arduino IDE installation and ESP32 board support, see [Getting Started](../../docs/getting-started.md).

### Additional Libraries

Install via Arduino IDE Library Manager:

- **M5Unified** by M5Stack

### Audio Files

The project requires three WAV audio files on the SD card (root directory):

- `/startup.wav` -- Plays on boot
- `/ready.wav` -- Plays when system is ready
- `/alert.wav` -- Plays on threat detection

**Audio File Requirements:**
- Format: 16-bit PCM WAV
- Sample Rate: 16000 Hz
- Channels: Mono (1 channel)
- Header: Standard 44-byte WAV header

**Setup:**
1. Format the SD card as FAT32
2. Copy the WAV files to the root of the SD card
3. Insert the SD card into the M5Stack FIRE before powering on

### Board Settings

1. Select board: **Tools** > **Board** > **M5Stack Fire**
2. PSRAM: **Enabled**
3. Upload speed: **115200**
4. CPU frequency: **240MHz (WiFi/BT)**
5. Partition scheme: **Default 4MB with spiffs** or **Huge APP (3MB No OTA/1MB SPIFFS)**

### Upload

1. Connect via USB
2. Select port: **Tools** > **Port**
3. Click **Upload**

### Serial Monitor

Open at **115200** baud. See [Telemetry Format](../../docs/telemetry-format.md) for the JSON schema.

## Usage

1. Power on the M5Stack FIRE
2. The system will:
   - Initialize filesystem and audio
   - Play startup sound
   - Initialize WiFi sniffer and BLE scanner
   - Play ready sound
   - Begin scanning for targets

### Audio Alerts

- **Startup**: Plays when system boots (display shows "Startup")
- **Ready**: Plays when scanning begins (display shows "ready", then "scanning...")
- **Alert**: Plays when a threat is detected

### Volume Control

Default volume is 40% (0.4). To adjust, edit `src/SoundEngine.h`:

```cpp
static constexpr float DEFAULT_VOLUME = 0.4f;  // 0.0 to 1.0
```

## Variant-Specific Troubleshooting

### Audio Not Playing

1. **Check SD card**: Ensure audio files are on the SD card root and the card is inserted
2. **Check volume**: Try increasing `DEFAULT_VOLUME` in `SoundEngine.h`
3. **Check serial output**: Look for audio file open errors

### SD Card Not Detected

1. **Check format**: Use FAT32
2. **Check insertion**: Power off, insert card, then power on
3. **Try a smaller card**: 32GB or less is usually more reliable

## Project Structure

```
flocksquawk_m5fire/
├── flocksquawk_m5fire.ino      # Main orchestrator
├── README.md
├── src/
│   ├── RadioScanner.h           # Variant-specific RF scanning
│   └── SoundEngine.h            # M5Stack speaker audio
└── data/
    ├── startup.wav
    ├── ready.wav
    └── alert.wav
```

Shared headers (`EventBus.h`, `ThreatAnalyzer.h`, `Detectors.h`, etc.) are in [`common/`](../../common/).

## Further Reading

- [Configuration](../../docs/configuration.md) -- WiFi/BLE tuning, detection patterns, volume
- [Architecture](../../docs/architecture.md) -- pipeline, detectors, thread safety
- [Extending](../../docs/extending.md) -- adding detectors, patterns, new variants
- [Build System](../../docs/build-system.md) -- Makefile and Docker builds
- [Troubleshooting](../../docs/troubleshooting.md) -- common issues

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)

## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning
- ArduinoJson for flexible JSON handling
