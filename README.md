# FlockSquawk

FlockSquawk is a modular, event-driven ESP32 project for **passive RF awareness**. It listens to nearby WiFi and Bluetooth Low Energy activity, analyzes patterns against known signatures, and provides feedback through audio alerts, on-device displays, and structured telemetry.

---

## What the Project Does

At runtime, FlockSquawk:

- Sniffs WiFi management frames using ESP32 promiscuous mode
- Scans nearby BLE advertisements using NimBLE
- Compares observations against signature patterns
  - SSIDs
  - MAC prefixes (OUIs)
  - BLE device names
  - Service UUIDs
- Emits structured JSON events over Serial
- Plays audible alerts via I2S audio
- Displays live status on an attached screen (variant dependent)

---

## Project Goals

- Educational: provide a readable, real-world RF + embedded systems codebase
- Experimental: make it easy to modify detection logic and behaviors
- Modular: allow different hardware front-ends (displays, controls, indicators)

---

## ESP32 Core Version Requirement

Use **esp32 by Espressif Systems** version **3.0.7 or older**. Newer versions fail to compile due to an **IRAM overflow** issue.


---

## Display Variants

FlockSquawk supports multiple hardware front-ends. Each variant lives in its own folder and includes its **own dedicated README with wiring and setup instructions**.

Current variants:

- **Mini12864 (ST7567 LCD + Encoder)**  
  A full-featured UI with menus, rotary encoder input, RGB backlight support, and a rich visual interface.

- **128x32 I2C OLED (SSD1306/SH1106)**  
  Compact display option with I2C wiring. Includes a portable buzzer-only build.

- **M5Stack Fire**  
  Uses the built-in speaker and SD card storage on the M5Stack FIRE.

- **M5StickC Plus2**  
  Compact handheld variant with buzzer and built-in display.

- **Flipper Zero WiFi Dev Board (ESP32-S2)**  
  ESP32-S2 firmware that streams UART telemetry to a Flipper Zero app (WiFi only, no BLE).

Each variant is self-contained and can be opened directly in the Arduino IDE.

---

## Repository Structure

```
FlockSquawk/
├── Mini12864/
│   └── flocksquawk_mini12864/
│       ├── flocksquawk_mini12864.ino
│       ├── src/
│       ├── data/
│       └── README.md
├── 128x32_OLED/
│   ├── flocksquawk_128x32/
│   │   ├── flocksquawk_128x32.ino
│   │   ├── src/
│   │   ├── data/
│   │   └── README.md
│   └── flocksquawk_128x32_portable/
│       ├── flocksquawk_128x32_portable.ino
│       ├── src/
│       └── README.md
├── m5stack/
│   ├── flocksquawk_m5fire/
│   │   ├── flocksquawk_m5fire.ino
│   │   ├── src/
│   │   ├── data/
│   │   └── README.md
│   └── flocksquawk_m5stick/
│       ├── flocksquawk_m5stick.ino
│       ├── src/
│       └── README.md
├── flipper-zero/
│   ├── dev-board-firmware/
│   │   ├── flocksquawk-flipper/
│   │   │   └── flocksquawk-flipper.ino
│   │   └── src/
│   │       └── ...
│   └── README.md
└── README.md   ← you are here (project overview)
```

If you are trying to build the project, start by entering one of the variant folders and follow that README.

---

## Core Architecture

All variants share the same core subsystems:

- **RadioScannerManager**  
  Handles WiFi promiscuous mode and BLE scanning

- **ThreatAnalyzer**  
  Compares observed data against signature patterns

- **EventBus**  
  Lightweight publish/subscribe system connecting components

- **SoundEngine**  
  I2S-based WAV playback using LittleFS

- **TelemetryReporter**  
  Emits structured JSON output over Serial

Because of this structure, new interfaces can be added cleanly: displays, LEDs, network reporting, logging, etc.


---

## License

[GNU GENERAL PUBLIC LICENSE](https://github.com/f1yaw4y/FlockSquawk/blob/main/LICENSE)

---

## Acknowledgments

- Inspired by [flock-you](https://github.com/colonelpanichacks/flock-you)
- ESP32 community for excellent hardware support
- NimBLE-Arduino for efficient BLE scanning
- ArduinoJson for flexible JSON handling
