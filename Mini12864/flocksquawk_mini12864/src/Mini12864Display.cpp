#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <math.h>

#include "Mini12864Display.h"
#include "RadioScanner.h"

// === Mini12864 wiring (ESP32 -> Mini12864) ===
static const uint8_t PIN_LCD_CS = 5;
static const uint8_t PIN_LCD_RST = 16;
static const uint8_t PIN_LCD_DC = 17;   // A0/RS
static const uint8_t PIN_LCD_MOSI = 23; // SDA/MOSI
static const uint8_t PIN_LCD_SCK = 18;  // SCL/SCK
static const int8_t PIN_LCD_MISO = 19;  // Not used by display

// Backlight wiring options:
// - FYSETC V2.1 typically uses WS2811 addressable LEDs (single DATA pin).
// - Some boards expose raw RGB pins (PWM).
// Select the backlight type and set pins below.
#define BACKLIGHT_NONE 0
#define BACKLIGHT_WS2811 1
#define BACKLIGHT_PWM_RGB 2
#define BACKLIGHT_TYPE BACKLIGHT_WS2811

#if BACKLIGHT_TYPE == BACKLIGHT_WS2811
  #if __has_include(<Adafruit_NeoPixel.h>)
    #include <Adafruit_NeoPixel.h>
    #define HAS_NEOPIXEL 1
  #else
    #define HAS_NEOPIXEL 0
    #warning "Adafruit_NeoPixel not found; backlight disabled. Install Adafruit NeoPixel via Library Manager."
    #undef BACKLIGHT_TYPE
    #define BACKLIGHT_TYPE BACKLIGHT_NONE
  #endif
#endif

#ifndef HAS_NEOPIXEL
  #define HAS_NEOPIXEL 0
#endif

#if BACKLIGHT_TYPE == BACKLIGHT_WS2811
  #if HAS_NEOPIXEL
    static const uint8_t PIN_BL_DATA = 4;   // Set to your WS2811/NeoPixel DIN pin
    static const uint16_t BL_LED_COUNT = 3; // Adjust to number of WS2811 LEDs
    Adafruit_NeoPixel backlight(BL_LED_COUNT, PIN_BL_DATA, NEO_GRB + NEO_KHZ800);
  #endif
#elif BACKLIGHT_TYPE == BACKLIGHT_PWM_RGB
  static const uint8_t PIN_BL_R = 32;
  static const uint8_t PIN_BL_G = 33;
  static const uint8_t PIN_BL_B = 4;
#endif

// Rotary encoder pins (change to match your wiring)
static const uint8_t PIN_ENC_A = 22;
static const uint8_t PIN_ENC_B = 14;
static const uint8_t PIN_ENC_BTN = 13;

// ST7567 128x64 display, 4-wire HW SPI (rotated 180 degrees)
U8G2_ST7567_JLX12864_F_4W_HW_SPI u8g2(U8G2_R2, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

static const uint8_t DEFAULT_CONTRAST = 170;
static const uint8_t RADAR_WIDTH = 128;
static const uint8_t RADAR_Y_TOP = 32;
static const uint8_t RADAR_Y_BOTTOM = 63;
static const uint8_t RADAR_DOT_STEP = 3;
static const uint16_t RADAR_DOT_TTL_MS = 8000;
static const uint8_t RADAR_DOT_MAX = 40;

#if BACKLIGHT_TYPE == BACKLIGHT_PWM_RGB
  static const uint8_t PWM_CHANNEL_R = 0;
  static const uint8_t PWM_CHANNEL_G = 1;
  static const uint8_t PWM_CHANNEL_B = 2;
  static const uint32_t PWM_FREQ = 5000;
  static const uint8_t PWM_RES_BITS = 8;
#endif

int32_t encoderValue = 0;
int32_t lastEncoderValue = 0;
uint8_t lastEncState = 0;
uint32_t lastButtonChangeMs = 0;
bool buttonPressed = false;
uint8_t backlightMode = 0;
bool displayActive = false;
float currentVolume = 0.4f;
int8_t volumeSteps = 4;
bool volumeDirty = false;
int32_t encoderRemainder = 0;
bool buttonPressEvent = false;
uint8_t displayRed = 255;
uint8_t displayGreen = 255;
uint8_t displayBlue = 255;
uint8_t ringRed = 255;
uint8_t ringGreen = 0;
uint8_t ringBlue = 0;
uint8_t mainMenuIndex = 0;
uint8_t backlightMenuIndex = 0;
uint8_t ringMenuIndex = 0;
uint8_t rgbEditIndex = 0;
bool alertTestRequested = false;
uint32_t alertStartMs = 0;
char lastWifiMac[18] = "--:--:--:--:--:--";
uint8_t lastWifiChannel = 0;
uint8_t nextRadarDotX = 0;
uint8_t radarDotIndex = 0;

struct RadarDot {
  uint8_t x;
  uint8_t y;
  uint8_t radius;
  uint32_t bornMs;
  bool active;
};

RadarDot radarDots[RADAR_DOT_MAX] = {};

enum class DisplayScreen {
  Startup,
  ReadyWait,
  Home,
  Alert,
  Menu,
  BacklightMenu,
  DisplayRgb,
  RingMenu,
  RingRgb
};

DisplayScreen currentScreen = DisplayScreen::Startup;
uint32_t startupStartMs = 0;
uint32_t readyStartMs = 0;
bool alertShown = false;

// Startup backlight timing (ms) - adjust these values to tune the sequence.
static const uint32_t STARTUP_RED_MS = 1000;
static const uint32_t STARTUP_GREEN_MS = 1000;
static const uint32_t STARTUP_BLUE_MS = 1000;
static const uint32_t STARTUP_NEO_MS = 1000;

static void updateStartupBacklight(uint32_t now);
static int32_t consumeEncoderSteps();
static void applyDisplayBacklight();
static void applyRingBacklight();
static void applyRingPreset(uint8_t presetIndex);
static void drawMenuList(const char* title, const char* const* items, uint8_t count, uint8_t selected);
static void drawRgbEditor(const char* title, uint8_t r, uint8_t g, uint8_t b, uint8_t selectedIndex);

static void setBacklight(uint8_t r, uint8_t g, uint8_t b) {
#if BACKLIGHT_TYPE == BACKLIGHT_WS2811
  #if HAS_NEOPIXEL
    for (uint16_t i = 0; i < BL_LED_COUNT; ++i) {
      backlight.setPixelColor(i, backlight.Color(r, g, b));
    }
    backlight.show();
  #endif
#elif BACKLIGHT_TYPE == BACKLIGHT_PWM_RGB
  #if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
    ledcWrite(PIN_BL_R, r);
    ledcWrite(PIN_BL_G, g);
    ledcWrite(PIN_BL_B, b);
  #else
    ledcWrite(PWM_CHANNEL_R, r);
    ledcWrite(PWM_CHANNEL_G, g);
    ledcWrite(PWM_CHANNEL_B, b);
  #endif
#else
  (void)r;
  (void)g;
  (void)b;
#endif
}

static void updateStartupBacklight(uint32_t now) {
  const uint32_t elapsed = now - startupStartMs;
  const uint32_t redEnd = STARTUP_RED_MS;
  const uint32_t greenEnd = redEnd + STARTUP_GREEN_MS;
  const uint32_t blueEnd = greenEnd + STARTUP_BLUE_MS;
  const uint32_t neoEnd = blueEnd + STARTUP_NEO_MS;

  if (elapsed < redEnd) {
    setBacklight(255, 0, 0);
    return;
  }
  if (elapsed < greenEnd) {
    setBacklight(0, 255, 0);
    return;
  }
  if (elapsed < blueEnd) {
    setBacklight(0, 0, 255);
    return;
  }
  if (elapsed < neoEnd) {
    const uint32_t phase = (elapsed - blueEnd) / 100;
    const uint8_t idx = phase % 6;
    switch (idx) {
      case 0: setBacklight(255, 0, 0); break;
      case 1: setBacklight(0, 255, 0); break;
      case 2: setBacklight(0, 0, 255); break;
      case 3: setBacklight(255, 255, 0); break;
      case 4: setBacklight(0, 255, 255); break;
      default: setBacklight(255, 0, 255); break;
    }
  }
}
static void updateBacklightMode(uint8_t mode) {
  switch (mode % 6) {
    case 0: setBacklight(255, 0, 0); break;   // red
    case 1: setBacklight(0, 255, 0); break;   // green
    case 2: setBacklight(0, 0, 255); break;   // blue
    case 3: setBacklight(255, 255, 0); break; // yellow
    case 4: setBacklight(0, 255, 255); break; // cyan
    default: setBacklight(255, 0, 255); break;// magenta
  }
}

static int32_t consumeEncoderSteps() {
  if (encoderValue == lastEncoderValue) {
    return 0;
  }
  const int32_t delta = -(encoderValue - lastEncoderValue);
  lastEncoderValue = encoderValue;
  encoderRemainder += delta;
  const int32_t steps = encoderRemainder / 2;
  encoderRemainder = encoderRemainder % 2;
  return steps;
}

static void readEncoder() {
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);
  uint8_t state = (a << 1) | b;

  if (state != lastEncState) {
    // Simple quadrature decode
    if ((lastEncState == 0b00 && state == 0b01) ||
        (lastEncState == 0b01 && state == 0b11) ||
        (lastEncState == 0b11 && state == 0b10) ||
        (lastEncState == 0b10 && state == 0b00)) {
      encoderValue++;
    } else if ((lastEncState == 0b00 && state == 0b10) ||
               (lastEncState == 0b10 && state == 0b11) ||
               (lastEncState == 0b11 && state == 0b01) ||
               (lastEncState == 0b01 && state == 0b00)) {
      encoderValue--;
    }
    lastEncState = state;
  }
}

static void readButton() {
  const uint32_t now = millis();
  bool pressed = (digitalRead(PIN_ENC_BTN) == LOW);

  if (pressed != buttonPressed && (now - lastButtonChangeMs) > 30) {
    lastButtonChangeMs = now;
    buttonPressed = pressed;
    if (buttonPressed) {
      buttonPressEvent = true;
    }
  }
}

static void applyDisplayBacklight() {
  setBacklight(displayRed, displayGreen, displayBlue);
}

static void applyRingBacklight() {
  setBacklight(ringRed, ringGreen, ringBlue);
}

static void applyRingPreset(uint8_t presetIndex) {
  switch (presetIndex) {
    case 0: // Red
      ringRed = 255; ringGreen = 0; ringBlue = 0;
      applyRingBacklight();
      break;
    case 1: // Green
      ringRed = 0; ringGreen = 255; ringBlue = 0;
      applyRingBacklight();
      break;
    case 2: // Blue
      ringRed = 0; ringGreen = 0; ringBlue = 255;
      applyRingBacklight();
      break;
    case 3: // Rainbow
      #if BACKLIGHT_TYPE == BACKLIGHT_WS2811 && HAS_NEOPIXEL
        if (BL_LED_COUNT >= 1) backlight.setPixelColor(0, backlight.Color(255, 0, 0));
        if (BL_LED_COUNT >= 2) backlight.setPixelColor(1, backlight.Color(0, 255, 0));
        if (BL_LED_COUNT >= 3) backlight.setPixelColor(2, backlight.Color(0, 0, 255));
        for (uint16_t i = 3; i < BL_LED_COUNT; ++i) {
          backlight.setPixelColor(i, backlight.Color(255, 255, 0));
        }
        backlight.show();
      #else
        ringRed = 255; ringGreen = 0; ringBlue = 255;
        applyRingBacklight();
      #endif
      break;
    default:
      break;
  }
}

static void drawMenuList(const char* title, const char* const* items, uint8_t count, uint8_t selected) {
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 8, title);

  const uint8_t boxX = 0;
  const uint8_t boxWidth = 128;
  const uint8_t boxHeight = 10;
  const uint8_t boxYs[3] = {16, 28, 40};
  const uint8_t windowSize = 3;
  uint8_t startIndex = 0;

  if (count > windowSize) {
    if (selected >= windowSize) {
      startIndex = selected - (windowSize - 1);
      if (startIndex + windowSize > count) {
        startIndex = count - windowSize;
      }
    }
  }

  for (uint8_t i = 0; i < windowSize; ++i) {
    const uint8_t itemIndex = startIndex + i;
    u8g2.drawFrame(boxX, boxYs[i], boxWidth, boxHeight);
    if (itemIndex >= count) {
      continue;
    }
    const bool isSelected = (itemIndex == selected);
    if (isSelected) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(boxX + 1, boxYs[i] + 1, boxWidth - 2, boxHeight - 2);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(4, boxYs[i] + 8, items[itemIndex]);
    if (isSelected) {
      u8g2.setDrawColor(1);
    }
  }
}

static void drawRgbEditor(const char* title, uint8_t r, uint8_t g, uint8_t b, uint8_t selectedIndex) {
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 8, title);

  char rStr[8];
  char gStr[8];
  char bStr[8];
  snprintf(rStr, sizeof(rStr), "R:%03u", r);
  snprintf(gStr, sizeof(gStr), "G:%03u", g);
  snprintf(bStr, sizeof(bStr), "B:%03u", b);

  const uint8_t y = 32;
  const uint8_t rX = 8;
  const uint8_t gX = 46;
  const uint8_t bX = 84;

  if (selectedIndex == 0) {
    const uint8_t w = u8g2.getStrWidth(rStr) + 2;
    u8g2.drawBox(rX - 1, y - 8, w, 10);
    u8g2.setDrawColor(0);
  }
  u8g2.drawStr(rX, y, rStr);
  if (selectedIndex == 0) {
    u8g2.setDrawColor(1);
  }

  if (selectedIndex == 1) {
    const uint8_t w = u8g2.getStrWidth(gStr) + 2;
    u8g2.drawBox(gX - 1, y - 8, w, 10);
    u8g2.setDrawColor(0);
  }
  u8g2.drawStr(gX, y, gStr);
  if (selectedIndex == 1) {
    u8g2.setDrawColor(1);
  }

  if (selectedIndex == 2) {
    const uint8_t w = u8g2.getStrWidth(bStr) + 2;
    u8g2.drawBox(bX - 1, y - 8, w, 10);
    u8g2.setDrawColor(0);
  }
  u8g2.drawStr(bX, y, bStr);
  if (selectedIndex == 2) {
    u8g2.setDrawColor(1);
  }

  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 60, "Press to advance, press after B to go back");
}

void Mini12864DisplayBegin() {
  if (displayActive) {
    return;
  }

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);

  #if BACKLIGHT_TYPE == BACKLIGHT_WS2811
    #if HAS_NEOPIXEL
      backlight.begin();
      backlight.setBrightness(255);
      backlight.show();
    #endif
  #elif BACKLIGHT_TYPE == BACKLIGHT_PWM_RGB
    #if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
      ledcAttach(PIN_BL_R, PWM_FREQ, PWM_RES_BITS);
      ledcAttach(PIN_BL_G, PWM_FREQ, PWM_RES_BITS);
      ledcAttach(PIN_BL_B, PWM_FREQ, PWM_RES_BITS);
    #else
      ledcSetup(PWM_CHANNEL_R, PWM_FREQ, PWM_RES_BITS);
      ledcSetup(PWM_CHANNEL_G, PWM_FREQ, PWM_RES_BITS);
      ledcSetup(PWM_CHANNEL_B, PWM_FREQ, PWM_RES_BITS);
      ledcAttachPin(PIN_BL_R, PWM_CHANNEL_R);
      ledcAttachPin(PIN_BL_G, PWM_CHANNEL_G);
      ledcAttachPin(PIN_BL_B, PWM_CHANNEL_B);
    #endif
  #endif
  updateBacklightMode(backlightMode);

  SPI.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);
  u8g2.begin();
  u8g2.setBusClock(10000000);
  u8g2.setContrast(DEFAULT_CONTRAST);

  lastEncState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  currentVolume = 0.4f;
  volumeSteps = 4;
  volumeDirty = true;
  encoderRemainder = 0;
  buttonPressEvent = false;
  alertTestRequested = false;
  nextRadarDotX = 0;
  radarDotIndex = 0;
  for (uint8_t i = 0; i < RADAR_DOT_MAX; ++i) {
    radarDots[i].active = false;
  }
  startupStartMs = millis();
  currentScreen = DisplayScreen::Startup;
  displayActive = true;
}

void Mini12864DisplayNotifySystemReady() {
  if (!displayActive) {
    return;
  }
  readyStartMs = millis();
  alertShown = false;
  currentScreen = DisplayScreen::ReadyWait;
}

void Mini12864DisplayShowAlert() {
  currentScreen = DisplayScreen::Alert;
  setBacklight(255, 0, 0);
  alertStartMs = millis();
}

void Mini12864DisplayNotifyWifiFrame(const uint8_t mac[6], uint8_t channel, int8_t rssi) {
  if (!mac) {
    return;
  }
  snprintf(lastWifiMac, sizeof(lastWifiMac), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  lastWifiChannel = channel;

  const uint32_t now = millis();
  RadarDot& dot = radarDots[radarDotIndex];
  dot.x = nextRadarDotX;
  dot.y = static_cast<uint8_t>(random(RADAR_Y_TOP, RADAR_Y_BOTTOM + 1));
  int16_t strength = rssi;
  if (strength < -90) strength = -90;
  if (strength > -30) strength = -30;
  const uint8_t size = static_cast<uint8_t>(1 + ((strength + 90) * 2) / 60);
  dot.radius = size;
  dot.bornMs = now;
  dot.active = true;
  radarDotIndex = (radarDotIndex + 1) % RADAR_DOT_MAX;

  uint16_t nextX = nextRadarDotX + RADAR_DOT_STEP;
  if (nextX >= RADAR_WIDTH) {
    nextRadarDotX = static_cast<uint8_t>(random(1, 3));
  } else {
    nextRadarDotX = static_cast<uint8_t>(nextX);
  }
}

void Mini12864DisplayUpdate() {
  if (!displayActive) {
    return;
  }

  readEncoder();
  readButton();

  u8g2.clearBuffer();

  const uint32_t now = millis();

  if (currentScreen == DisplayScreen::Startup || currentScreen == DisplayScreen::ReadyWait) {
    if (currentScreen == DisplayScreen::Startup) {
      updateStartupBacklight(now);
    }
    static const char* const startupLines[] = {
      "Starting up...",
      "Setting up radio",
      "Loading database",
      "System test complete"
    };
    const uint8_t totalLines = sizeof(startupLines) / sizeof(startupLines[0]);
    uint8_t visibleLines = 1;
    if (now > startupStartMs) {
      visibleLines = 1 + ((now - startupStartMs) / 1000);
      if (visibleLines > totalLines) {
        visibleLines = totalLines;
      }
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    const uint8_t lineHeight = 10;
    uint8_t y = 12;
    for (uint8_t i = 0; i < visibleLines; ++i) {
      u8g2.drawStr(0, y, startupLines[i]);
      y += lineHeight;
    }

    if (currentScreen == DisplayScreen::ReadyWait) {
      if (now - readyStartMs >= 1000) {
        y += lineHeight;
        u8g2.drawStr(0, y, "Scan starting...");
      }
      if (now - readyStartMs >= 3000) {
        currentScreen = DisplayScreen::Home;
      }
    }
  }

  if (currentScreen == DisplayScreen::Alert) {
    if ((now / 500) % 2 == 0) {
      u8g2.setFont(u8g2_font_7x13B_tr);
      const char *alertLabel = "ALERT";
      const uint8_t textWidth = u8g2.getStrWidth(alertLabel);
      u8g2.drawStr((128 - textWidth) / 2, 36, alertLabel);
    }
    if (alertStartMs > 0 && (now - alertStartMs) >= 10000) {
      currentScreen = DisplayScreen::Home;
      alertStartMs = 0;
    }
  }

  if (currentScreen == DisplayScreen::Home) {
    // Header: animated text (upper left)
    const uint8_t dotCount = (now / 500) % 3 + 1;
    char header[32];
    snprintf(header, sizeof(header), "Flock signatures%.*s", dotCount, "...");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 8, "Scanning for");
    u8g2.drawStr(0, 16, header);

    u8g2.setFont(u8g2_font_4x6_tr);
    char macLine[32];
    snprintf(macLine, sizeof(macLine), "MAC %s CH %u", lastWifiMac, lastWifiChannel);
    u8g2.drawStr(0, 24, macLine);

    // Small channel label (upper right)
    u8g2.setFont(u8g2_font_4x6_tr);
    char chLabel[12];
    snprintf(chLabel, sizeof(chLabel), "CH %u", RadioScannerManager::getCurrentWifiChannel());
    const uint8_t chWidth = u8g2.getStrWidth(chLabel);
    u8g2.drawStr(128 - chWidth, 6, chLabel);
    char volLabel[12];
    snprintf(volLabel, sizeof(volLabel), "Vol %d", volumeSteps);
    const uint8_t volWidth = u8g2.getStrWidth(volLabel);
    u8g2.drawStr(128 - volWidth, 14, volLabel);

    // Draw radar dots and scanning line on the lower half.
    for (uint8_t i = 0; i < RADAR_DOT_MAX; ++i) {
      if (!radarDots[i].active) {
        continue;
      }
      if (now - radarDots[i].bornMs >= RADAR_DOT_TTL_MS) {
        radarDots[i].active = false;
        continue;
      }
      if (radarDots[i].radius <= 1) {
        u8g2.drawPixel(radarDots[i].x, radarDots[i].y);
      } else {
        u8g2.drawDisc(radarDots[i].x, radarDots[i].y, radarDots[i].radius);
      }
    }

    const uint8_t yMid = RADAR_Y_TOP;

    const float speed = 0.02f; // pixels per ms
    const float t = now * speed;
    const float period = 2.0f * (RADAR_WIDTH - 1);
    float pos = fmodf(t, period);
    if (pos > (RADAR_WIDTH - 1)) {
      pos = period - pos;
    }
    const uint8_t x = static_cast<uint8_t>(pos);
    u8g2.drawLine(x, RADAR_Y_BOTTOM, x, yMid);
  }

  if (currentScreen == DisplayScreen::Menu) {
    static const char* const mainItems[] = {"Backlight", "Test Alert", "Back"};
    drawMenuList("Menu", mainItems, 3, mainMenuIndex);
  }

  if (currentScreen == DisplayScreen::BacklightMenu) {
    static const char* const backlightItems[] = {"Display", "LED Ring", "Back"};
    drawMenuList("Backlight", backlightItems, 3, backlightMenuIndex);
  }

  if (currentScreen == DisplayScreen::RingMenu) {
    static const char* const ringItems[] = {"Red", "Green", "Blue", "Rainbow", "Custom", "Back"};
    drawMenuList("LED Ring", ringItems, 6, ringMenuIndex);
  }

  if (currentScreen == DisplayScreen::DisplayRgb) {
    drawRgbEditor("Display RGB", displayRed, displayGreen, displayBlue, rgbEditIndex);
  }

  if (currentScreen == DisplayScreen::RingRgb) {
    drawRgbEditor("LED Ring RGB", ringRed, ringGreen, ringBlue, rgbEditIndex);
  }

  u8g2.sendBuffer();

  const int32_t stepDelta = consumeEncoderSteps();
  if (currentScreen == DisplayScreen::Home && stepDelta != 0) {
    int16_t nextSteps = volumeSteps + stepDelta;
    if (nextSteps < 0) nextSteps = 0;
    if (nextSteps > 10) nextSteps = 10;
    if (nextSteps != volumeSteps) {
      volumeSteps = static_cast<int8_t>(nextSteps);
      currentVolume = static_cast<float>(volumeSteps) / 10.0f;
      volumeDirty = true;
    }
  } else if (currentScreen == DisplayScreen::Menu && stepDelta != 0) {
    int16_t nextIndex = static_cast<int16_t>(mainMenuIndex) + stepDelta;
    if (nextIndex < 0) nextIndex = 0;
    if (nextIndex > 2) nextIndex = 2;
    mainMenuIndex = static_cast<uint8_t>(nextIndex);
  } else if (currentScreen == DisplayScreen::BacklightMenu && stepDelta != 0) {
    int16_t nextIndex = static_cast<int16_t>(backlightMenuIndex) + stepDelta;
    if (nextIndex < 0) nextIndex = 0;
    if (nextIndex > 2) nextIndex = 2;
    backlightMenuIndex = static_cast<uint8_t>(nextIndex);
  } else if (currentScreen == DisplayScreen::RingMenu && stepDelta != 0) {
    int16_t nextIndex = static_cast<int16_t>(ringMenuIndex) + stepDelta;
    if (nextIndex < 0) nextIndex = 0;
    if (nextIndex > 5) nextIndex = 5;
    ringMenuIndex = static_cast<uint8_t>(nextIndex);
  } else if ((currentScreen == DisplayScreen::DisplayRgb || currentScreen == DisplayScreen::RingRgb) && stepDelta != 0) {
    uint8_t* r = (currentScreen == DisplayScreen::DisplayRgb) ? &displayRed : &ringRed;
    uint8_t* g = (currentScreen == DisplayScreen::DisplayRgb) ? &displayGreen : &ringGreen;
    uint8_t* b = (currentScreen == DisplayScreen::DisplayRgb) ? &displayBlue : &ringBlue;
    uint8_t* target = (rgbEditIndex == 0) ? r : (rgbEditIndex == 1 ? g : b);
    int16_t nextValue = static_cast<int16_t>(*target) + stepDelta;
    if (nextValue < 0) nextValue = 0;
    if (nextValue > 255) nextValue = 255;
    *target = static_cast<uint8_t>(nextValue);
    if (currentScreen == DisplayScreen::DisplayRgb) {
      applyDisplayBacklight();
    } else {
      applyRingBacklight();
    }
  }

  if (buttonPressEvent) {
    buttonPressEvent = false;
    if (currentScreen == DisplayScreen::Home) {
      currentScreen = DisplayScreen::Menu;
    } else if (currentScreen == DisplayScreen::Alert) {
      currentScreen = DisplayScreen::Home;
      alertStartMs = 0;
    } else if (currentScreen == DisplayScreen::Menu) {
      if (mainMenuIndex == 0) {
        currentScreen = DisplayScreen::BacklightMenu;
      } else if (mainMenuIndex == 1) {
        alertTestRequested = true;
        currentScreen = DisplayScreen::Home;
      } else if (mainMenuIndex == 2) {
        currentScreen = DisplayScreen::Home;
      }
    } else if (currentScreen == DisplayScreen::BacklightMenu) {
      if (backlightMenuIndex == 0) {
        rgbEditIndex = 0;
        currentScreen = DisplayScreen::DisplayRgb;
      } else if (backlightMenuIndex == 1) {
        currentScreen = DisplayScreen::RingMenu;
      } else if (backlightMenuIndex == 2) {
        currentScreen = DisplayScreen::Menu;
      }
    } else if (currentScreen == DisplayScreen::RingMenu) {
      if (ringMenuIndex <= 3) {
        applyRingPreset(ringMenuIndex);
        currentScreen = DisplayScreen::BacklightMenu;
      } else if (ringMenuIndex == 4) {
        rgbEditIndex = 0;
        currentScreen = DisplayScreen::RingRgb;
      } else if (ringMenuIndex == 5) {
        currentScreen = DisplayScreen::BacklightMenu;
      }
    } else if (currentScreen == DisplayScreen::DisplayRgb || currentScreen == DisplayScreen::RingRgb) {
      rgbEditIndex++;
      if (rgbEditIndex > 2) {
        rgbEditIndex = 0;
        currentScreen = (currentScreen == DisplayScreen::DisplayRgb) ? DisplayScreen::BacklightMenu : DisplayScreen::RingMenu;
      }
    }
  }
}

bool Mini12864DisplayConsumeVolume(float* volumeOut) {
  if (!volumeDirty) {
    return false;
  }
  volumeDirty = false;
  if (volumeOut) {
    *volumeOut = currentVolume;
  }
  return true;
}

bool Mini12864DisplayConsumeAlertTest() {
  if (!alertTestRequested) {
    return false;
  }
  alertTestRequested = false;
  return true;
}

bool Mini12864DisplayIsActive() {
  return displayActive;
}
