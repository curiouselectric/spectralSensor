#include <Wire.h>
#include <TFT_eSPI.h>
#include <DFRobot_AS7341.h>

// AS7341 I2C Pin Assignments & Address
#define EXPANSION_SDA_PIN 22
#define EXPANSION_SCL_PIN 21
#define AS7341_ADDR       0x39

#define NUM_CHANNELS 10
#define SENSOR_POLL_INTERVAL 100    // Poll sensor every 100ms
#define DISPLAY_UPDATE_INTERVAL 250 // Average and update screen every 250ms

// Interfaces
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite graphSprite = TFT_eSprite(&tft);
DFRobot_AS7341 as7341;

unsigned long lastSensorPollTime = 0;
unsigned long lastDisplayUpdateTime = 0;
unsigned long lastTouchTime = 0;

// Channel Data, Averaging Accumulators & Display Labels
uint16_t rawValues[NUM_CHANNELS] = {0};
uint32_t accumulatedValues[NUM_CHANNELS] = {0};
uint16_t sampleCount = 0;
uint16_t peakHoldValues[NUM_CHANNELS] = {0};

const char* channelLabels[NUM_CHANNELS] = {
  "415", "445", "480", "515", "555", "590", "630", "680", "CLR", "NIR"
};

// Rainbow Spectrum Colours + White (Clear) + Magenta (NIR)
const uint16_t barColors[NUM_CHANNELS] = {
  0x781F, // F1: 415nm (Violet)
  0x001F, // F2: 445nm (Blue)
  0x07FF, // F3: 480nm (Cyan)
  0x07E0, // F4: 515nm (Green)
  0xAFE5, // F5: 555nm (Yellow-Green)
  0xFFE0, // F6: 590nm (Yellow)
  0xFD20, // F7: 630nm (Orange)
  0xF800, // F8: 680nm (Red)
  0xFFFF, // Clear: White
  0xF81F  // NIR: Magenta
};

// Graph Layout Dimensions (320x240 Landscape)
const int GRAPH_TOP = 32;
const int GRAPH_BOTTOM = 190;
const int GRAPH_MAX_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP; // 158px
const int BAR_WIDTH = 22;
const int BAR_SPACING = 8;
const int START_X = 14;
const int SPRITE_W = (BAR_WIDTH + BAR_SPACING) * NUM_CHANNELS; // 300px
const int SPRITE_H = GRAPH_MAX_HEIGHT + 2; // 160px

// Three Touchscreen Button Coordinates (320px width)
const int BTN_Y = 210;
const int BTN_H = 26;

const int BTN_SCALE_X = 8;
const int BTN_SCALE_W = 104;

const int BTN_LED_X = 118;
const int BTN_LED_W = 84;

const int BTN_RESET_X = 208;
const int BTN_RESET_W = 104;

// State Flags
bool isAutoScale = true;
bool isLedOn = false;
uint16_t currentScaleMax = 1000;
uint16_t prevScaleMax = 0;
int prevVisiblePeakIdx = -1;

uint16_t calculateNiceScaleMax(uint16_t maxReading) {
  const uint16_t steps[] = {250, 500, 1000, 2000, 3000, 4000, 5000, 10000, 20000, 40000, 65535};
  for (int i = 0; i < 11; i++) {
    if (maxReading <= steps[i]) {
      return steps[i];
    }
  }
  return 65535;
}

void drawButtons(bool flashReset = false) {
  tft.setTextSize(1);

  // 1. Left Button: Auto-Scale vs Fixed 3000 Scale
  uint16_t scaleFill = isAutoScale ? 0x03EF : 0x4208;
  tft.fillRoundRect(BTN_SCALE_X, BTN_Y, BTN_SCALE_W, BTN_H, 4, scaleFill);
  tft.drawRoundRect(BTN_SCALE_X, BTN_Y, BTN_SCALE_W, BTN_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, scaleFill);
  tft.setCursor(BTN_SCALE_X + 12, BTN_Y + 9);
  tft.print(isAutoScale ? "SCALE: AUTO" : "FIXED: 3k");

  // 2. Middle Button: Onboard White LED Toggle
  uint16_t ledFill = isLedOn ? 0xC600 : 0x2104;
  tft.fillRoundRect(BTN_LED_X, BTN_Y, BTN_LED_W, BTN_H, 4, ledFill);
  tft.drawRoundRect(BTN_LED_X, BTN_Y, BTN_LED_W, BTN_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, ledFill);
  tft.setCursor(BTN_LED_X + 18, BTN_Y + 9);
  tft.print(isLedOn ? "LED: ON" : "LED: OFF");

  // 3. Right Button: Reset Peak Hold
  uint16_t resetFill = flashReset ? TFT_DARKGREEN : 0x8000;
  tft.fillRoundRect(BTN_RESET_X, BTN_Y, BTN_RESET_W, BTN_H, 4, resetFill);
  tft.drawRoundRect(BTN_RESET_X, BTN_Y, BTN_RESET_W, BTN_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, resetFill);
  tft.setCursor(BTN_RESET_X + 18, BTN_Y + 9);
  tft.print("RESET PEAKS");
}

void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 8);
  tft.print("AS7341 SPECTRUM");

  for (int i = 0; i < NUM_CHANNELS; i++) {
    int x = START_X + i * (BAR_WIDTH + BAR_SPACING);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(x + 2, GRAPH_BOTTOM + 5);
    tft.print(channelLabels[i]);
  }

  drawButtons(false);
}

void updateScaleReadout(uint16_t scaleMax) {
  if (scaleMax == prevScaleMax) {
    return;
  }
  prevScaleMax = scaleMax;

  tft.fillRect(235, 4, 82, 22, TFT_BLACK);
  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(238, 6);
  tft.print("Max: ");
  if (scaleMax >= 1000) {
    tft.print(scaleMax / 1000.0, (scaleMax % 1000 == 0) ? 0 : 1);
    tft.print("k");
  } else {
    tft.print(scaleMax);
  }

  tft.setCursor(238, 17);
  tft.print("50%: ");
  uint16_t halfScale = scaleMax / 2;
  if (halfScale >= 1000) {
    tft.print(halfScale / 1000.0, (halfScale % 1000 == 0) ? 0 : 1);
    tft.print("k");
  } else {
    tft.print(halfScale);
  }
}

void handleTouch() {
  uint16_t z = tft.getTouchRawZ();

  // Require a deliberate touch (z >= 80) to ignore shared-SPI bus noise
  if (z < 80 || z > 3900) {
    return;
  }

  // Simple 350ms cooldown prevents double-toggles without ever getting stuck
  unsigned long now = millis();
  if (now - lastTouchTime < 350) {
    return;
  }

  // 3-sample median filter to eliminate contact bounce
  delay(8);
  uint16_t x0, y0, x1, y1, x2, y2;
  tft.getTouchRaw(&x0, &y0);
  tft.getTouchRaw(&x1, &y1);
  tft.getTouchRaw(&x2, &y2);

  uint16_t medX = max(min(x0, x1), min(max(x0, x1), x2));
  uint16_t medY = max(min(y0, y1), min(max(y0, y1), y2));

  int touchX = map(medX, 380, 3880, 0, 320);
  int touchY = map(medY, 240, 3800, 0, 240);

  if (touchY >= BTN_Y - 12 && touchY <= BTN_Y + BTN_H + 12) {
    // 1. Scale Toggle Button
    if (touchX >= BTN_SCALE_X && touchX <= BTN_SCALE_X + BTN_SCALE_W) {
      lastTouchTime = now;
      isAutoScale = !isAutoScale;
      prevScaleMax = 0;
      drawButtons(false);
    }
    // 2. LED On/Off Button
    else if (touchX >= BTN_LED_X && touchX <= BTN_LED_X + BTN_LED_W) {
      lastTouchTime = now;
      isLedOn = !isLedOn;
      as7341.enableLed(isLedOn);
      if (isLedOn) {
        as7341.controlLed(5);
      }
      drawButtons(false);
    }
    // 3. Reset Peaks Button
    else if (touchX >= BTN_RESET_X && touchX <= BTN_RESET_X + BTN_RESET_W) {
      lastTouchTime = now;
      for (int i = 0; i < NUM_CHANNELS; i++) {
        peakHoldValues[i] = rawValues[i];
      }
      drawButtons(true);
      delay(20);
      drawButtons(false);
    }
  }
}

// Direct fast bank trigger & 12-byte burst read
void readBankDirect(bool bankTwo, uint16_t* dest) {
  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x80);
  Wire.write((uint8_t)0x01);
  Wire.endTransmission();

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0xAF);
  Wire.write((uint8_t)0x10);
  Wire.endTransmission();

  const uint8_t smux1[20] = {
    0x30, 0x01, 0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x50, 0x00,
    0x00, 0x00, 0x20, 0x04, 0x00, 0x30, 0x01, 0x50, 0x00, 0x06
  };
  const uint8_t smux2[20] = {
    0x00, 0x00, 0x00, 0x40, 0x02, 0x00, 0x10, 0x03, 0x50, 0x10,
    0x03, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x50, 0x00, 0x06
  };

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x00);
  Wire.write(bankTwo ? smux2 : smux1, 20);
  Wire.endTransmission();

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x80);
  Wire.write((uint8_t)0x11);
  Wire.endTransmission();
  delayMicroseconds(300);

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x80);
  Wire.write((uint8_t)0x03);
  Wire.endTransmission();

  delay(9);
  unsigned long waitStart = millis();
  while (millis() - waitStart < 25) {
    Wire.beginTransmission(AS7341_ADDR);
    Wire.write(0xA3);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)AS7341_ADDR, (uint8_t)1);
    if (Wire.available() && (Wire.read() & 0x40)) {
      break;
    }
    delayMicroseconds(250);
  }

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x95);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)AS7341_ADDR, (uint8_t)12);
  for (int i = 0; i < 6; i++) {
    uint8_t lo = Wire.available() ? Wire.read() : 0;
    uint8_t hi = Wire.available() ? Wire.read() : 0;
    dest[i] = ((uint16_t)hi << 8) | lo;
  }
}

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  graphSprite.setColorDepth(8);
  graphSprite.createSprite(SPRITE_W, SPRITE_H);

  Wire.begin(EXPANSION_SDA_PIN, EXPANSION_SCL_PIN);
  Wire.setClock(400000);

  while (as7341.begin() != 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("AS7341 Init Failed!");
    delay(500);
  }

  as7341.enableSpectralMeasure(false);

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0xA9);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x80);
  Wire.write((uint8_t)0x01);
  Wire.endTransmission();

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0x81);
  Wire.write((uint8_t)4);
  Wire.endTransmission();

  Wire.beginTransmission(AS7341_ADDR);
  Wire.write(0xCA);
  Wire.write((uint8_t)(599 & 0xFF));
  Wire.write((uint8_t)(599 >> 8));
  Wire.endTransmission();

  as7341.setAGAIN(7);

  as7341.enableLed(false);
  as7341.controlLed(5);

  drawStaticUI();
}

void loop() {
  handleTouch();

  unsigned long now = millis();

  // 1. Poll sensor every 100ms and accumulate samples
  if (now - lastSensorPollTime >= SENSOR_POLL_INTERVAL) {
    lastSensorPollTime = now;

    uint16_t bank1[6] = {0};
    uint16_t bank2[6] = {0};
    uint16_t latest[NUM_CHANNELS] = {0};

    readBankDirect(false, bank1);
    readBankDirect(true, bank2);

    memcpy(latest, bank1, 4 * sizeof(uint16_t));     // F1..F4 (415..515nm)
    memcpy(latest + 4, bank2, 6 * sizeof(uint16_t)); // F5..F8 + CLR + NIR

    for (int i = 0; i < NUM_CHANNELS; i++) {
      accumulatedValues[i] += latest[i];
    }
    sampleCount++;
  }

  // 2. Average accumulated samples and refresh display every 250ms
  if (now - lastDisplayUpdateTime >= DISPLAY_UPDATE_INTERVAL && sampleCount > 0) {
    lastDisplayUpdateTime = now;

    uint16_t overallMax = 0;
    uint16_t visiblePeakVal = 0;
    int visiblePeakIdx = 0;

    for (int i = 0; i < NUM_CHANNELS; i++) {
      // Compute mean across the samples collected in this 250ms window
      rawValues[i] = (uint16_t)(accumulatedValues[i] / sampleCount);

      // Reset accumulator for the next 250ms window
      accumulatedValues[i] = 0;

      if (rawValues[i] >= peakHoldValues[i]) {
        peakHoldValues[i] = rawValues[i];
      } else {
        // Decay by ~5% of current peak (or at least 15 counts) every 250ms frame
        uint16_t decay = max((uint16_t)15, (uint16_t)(peakHoldValues[i] / 20));
        if (peakHoldValues[i] > decay + rawValues[i]) {
          peakHoldValues[i] -= decay;
        } else {
          peakHoldValues[i] = rawValues[i];
        }
      }

      if (rawValues[i] > overallMax) {
        overallMax = rawValues[i];
      }

      if (i < 8 && rawValues[i] > visiblePeakVal) {
        visiblePeakVal = rawValues[i];
        visiblePeakIdx = i;
      }
    }
    sampleCount = 0;

    if (isAutoScale) {
      currentScaleMax = calculateNiceScaleMax(overallMax);
    } else {
      currentScaleMax = 3000;
    }

    graphSprite.fillSprite(TFT_BLACK);

    int midY = GRAPH_MAX_HEIGHT / 2;
    for (int x = 0; x < SPRITE_W - 6; x += 6) {
      graphSprite.drawPixel(x, 0, TFT_DARKGREY);
      graphSprite.drawPixel(x, midY, TFT_DARKGREY);
    }

    graphSprite.drawFastHLine(0, GRAPH_MAX_HEIGHT + 1, SPRITE_W - BAR_SPACING, TFT_DARKGREY);

    for (int i = 0; i < NUM_CHANNELS; i++) {
      int localX = i * (BAR_WIDTH + BAR_SPACING);

      int barHeight = map(constrain(rawValues[i], 0, currentScaleMax), 0, currentScaleMax, 0, GRAPH_MAX_HEIGHT);
      int peakHeight = map(constrain(peakHoldValues[i], 0, currentScaleMax), 0, currentScaleMax, 0, GRAPH_MAX_HEIGHT);

      if (barHeight > 0) {
        graphSprite.fillRect(localX, GRAPH_MAX_HEIGHT - barHeight, BAR_WIDTH, barHeight, barColors[i]);
      }

      if (peakHeight > 2) {
        int peakY = GRAPH_MAX_HEIGHT - peakHeight;
        graphSprite.drawFastHLine(localX, peakY, BAR_WIDTH, TFT_WHITE);
      }
    }

    graphSprite.pushSprite(START_X, GRAPH_TOP);

    tft.fillRect(10, 18, 220, 11, TFT_BLACK);
    tft.setCursor(10, 19);
    tft.setTextSize(1);
    if (visiblePeakVal > 20) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.print("Peak: ");
      tft.print(channelLabels[visiblePeakIdx]);
      tft.print("nm (");
      tft.print(visiblePeakVal);
      tft.print(")");
    } else {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.print("Peak: Low Light");
    }

    if (visiblePeakIdx != prevVisiblePeakIdx) {
      for (int i = 0; i < NUM_CHANNELS; i++) {
        int x = START_X + i * (BAR_WIDTH + BAR_SPACING);
        tft.setTextSize(1);
        if (i == visiblePeakIdx && visiblePeakVal > 20) {
          tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        } else {
          tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        }
        tft.setCursor(x + 2, GRAPH_BOTTOM + 5);
        tft.print(channelLabels[i]);
      }
      prevVisiblePeakIdx = visiblePeakIdx;
    }

    updateScaleReadout(currentScaleMax);
  }
}
