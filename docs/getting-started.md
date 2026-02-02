# Getting Started

Common setup steps for all FlockSquawk variants. After completing these steps, return to your variant's README for board-specific configuration and upload instructions.

## Prerequisites

1. **Arduino IDE** (version 1.8.19 or later, or Arduino IDE 2.x)
2. **ESP32 Board Support** installed in Arduino IDE

## Installing ESP32 Board Support

1. Open Arduino IDE
2. Go to **File** > **Preferences**
3. In "Additional Boards Manager URLs", add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools** > **Board** > **Boards Manager**
5. Search for "ESP32" and install **esp32 by Espressif Systems**

**Important:** Use version **3.0.7 or older**. Newer versions fail to compile due to an IRAM overflow issue.

## Shared Libraries

All variants require these libraries (install via **Tools** > **Manage Libraries**):

| Library | Author | Notes |
|---------|--------|-------|
| ArduinoJson | Benoit Blanchon | Version 6.x or 7.x |
| NimBLE-Arduino | h2zero | Not needed for Flipper Zero (ESP32-S2, no BLE) |

Individual variants require additional libraries -- see the variant README for the complete list.

## Clone and Open

```bash
git clone <repository-url>
cd FlockSquawk
```

Each variant is a self-contained Arduino sketch. Open the `.ino` file for your variant directly in the Arduino IDE:

| Variant | Open this file |
|---------|---------------|
| M5StickC Plus2 | `m5stack/flocksquawk_m5stick/flocksquawk_m5stick.ino` |
| M5Stack FIRE | `m5stack/flocksquawk_m5fire/flocksquawk_m5fire.ino` |
| Mini12864 | `Mini12864/flocksquawk_mini12864/flocksquawk_mini12864.ino` |
| 128x32 OLED | `128x32_OLED/flocksquawk_128x32/flocksquawk_128x32.ino` |
| 128x32 Portable | `128x32_OLED/flocksquawk_128x32_portable/flocksquawk_128x32_portable.ino` |
| Flipper Zero | `flipper-zero/dev-board-firmware/flocksquawk-flipper/flocksquawk-flipper.ino` |

## Next Steps

Return to your variant's README for:
- Board selection and FQBN settings
- Variant-specific library requirements
- Pin wiring (if applicable)
- Audio file preparation (if applicable)
- Upload and serial monitor instructions

See also: [Build System](build-system.md) for Makefile and Docker-based builds.
