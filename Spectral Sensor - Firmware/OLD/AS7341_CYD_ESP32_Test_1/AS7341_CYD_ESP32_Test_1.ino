#include <Wire.h>
#include <TFT_eSPI.h>
#include <DFRobot_AS7341.h>

// Hardware Pin Assignments (Matches your working CYD setup)
#define EXPANSION_SDA_PIN 22
#define EXPANSION_SCL_PIN 21

#define NUM_CHANNELS 10
#define SENSOR_POLL_INTERVAL 150

// Interfaces
TFT_eSPI tft = TFT_eSPI();
DFRobot_AS7341 as7341;
unsigned long lastSensorPollTime = 0;

// Channel Data & Display Labels
uint16_t rawValues[NUM_CHANNELS] = {0};
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
const int GRAPH_BOTTOM = 212;
const int GRAPH_MAX_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP;
const int BAR_WIDTH = 22;
const int BAR_SPACING = 8;
const int START_X = 14;

uint16_t currentScaleMax = 1000;

// Picks a clean ceiling for the Y-axis based on the highest active reading
uint16_t calculateNiceScaleMax(uint16_t maxReading) {
  const uint16_t steps[] = {500, 1000, 2000, 5000, 10000, 20000, 40000, 65535};
  for (int i = 0; i < 8; i++) {
    if (maxReading <= steps[i]) {
      return steps[i];
    }
  }
  return 65535;
}

void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);
  
  // Title
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 8);
  tft.print("AS7341 SPECTRUM");

  // Baseline axis
  tft.drawFastHLine(START_X - 4, GRAPH_BOTTOM + 1, (BAR_WIDTH + BAR_SPACING) * NUM_CHANNELS, TFT_DARKGREY);
}

void drawReferenceGrid(uint16_t scaleMax) {
  int midY = GRAPH_TOP + (GRAPH_MAX_HEIGHT / 2);

  // Dashed horizontal lines at 100% (top) and 50% (middle)
  for (int x = START_X - 2; x < 310; x += 6) {
    tft.drawPixel(x, GRAPH_TOP, TFT_DARKGREY);
    tft.drawPixel(x, midY, TFT_DARKGREY);
  }

  // Top-right Y-axis scale indicator
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

void setup() {
  Serial.begin(115200);

  // Initialize TFT
  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);

  // Initialize I2C & AS7341
  Wire.begin(EXPANSION_SDA_PIN, EXPANSION_SCL_PIN);
  while (as7341.begin() != 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("AS7341 Init Failed!");
    Serial.println("AS7341 init failed, retrying...");
    delay(1000);
  }

  // Configure sensor integration time and gain
  as7341.setAtime(29);
  as7341.setAstep(599);
  as7341.setAGAIN(7); // 64x gain (adjust 0..10 if readings saturate at 65535)

  drawStaticUI();
}

void loop() {
  if (millis() - lastSensorPollTime < SENSOR_POLL_INTERVAL) {
    return;
  }
  lastSensorPollTime = millis();

  DFRobot_AS7341::sModeOneData_t data1;
  DFRobot_AS7341::sModeTwoData_t data2;

  // Read F1-F4
  as7341.startMeasure(as7341.eF1F4ClearNIR);
  data1 = as7341.readSpectralDataOne();

  // Read F5-F8 + Clear + NIR
  as7341.startMeasure(as7341.eF5F8ClearNIR);
  data2 = as7341.readSpectralDataTwo();

  rawValues[0] = data1.ADF1;
  rawValues[1] = data1.ADF2;
  rawValues[2] = data1.ADF3;
  rawValues[3] = data1.ADF4;
  rawValues[4] = data2.ADF5;
  rawValues[5] = data2.ADF6;
  rawValues[6] = data2.ADF7;
  rawValues[7] = data2.ADF8;
  rawValues[8] = data2.ADCLEAR;
  rawValues[9] = data2.ADNIR;

  // Find overall maximum (for auto-scaling) & peak visible channel (F1..F8)
  uint16_t overallMax = 0;
  uint16_t visiblePeakVal = 0;
  int visiblePeakIdx = 0;

  for (int i = 0; i < NUM_CHANNELS; i++) {
    // Update decaying peak hold
    if (rawValues[i] >= peakHoldValues[i]) {
      peakHoldValues[i] = rawValues[i];
    } else {
      // Decay by ~4% per frame
      uint16_t decay = max((uint16_t)10, (uint16_t)(peakHoldValues[i] / 25));
      peakHoldValues[i] = (peakHoldValues[i] > decay) ? (peakHoldValues[i] - decay) : 0;
    }

    if (peakHoldValues[i] > overallMax) {
      overallMax = peakHoldValues[i];
    }

    // Track strongest visible wavelength (indices 0..7 = 415nm..680nm)
    if (i < 8 && rawValues[i] > visiblePeakVal) {
      visiblePeakVal = rawValues[i];
      visiblePeakIdx = i;
    }
  }

  // Update dynamic scale ceiling
  currentScaleMax = calculateNiceScaleMax(overallMax);

  // Update Peak Wavelength Indicator in Header
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

  // Draw spectrum bars and peak hold ticks
  for (int i = 0; i < NUM_CHANNELS; i++) {
    int x = START_X + i * (BAR_WIDTH + BAR_SPACING);

    int barHeight = map(constrain(rawValues[i], 0, currentScaleMax), 0, currentScaleMax, 0, GRAPH_MAX_HEIGHT);
    int peakHeight = map(constrain(peakHoldValues[i], 0, currentScaleMax), 0, currentScaleMax, 0, GRAPH_MAX_HEIGHT);

    int emptyHeight = GRAPH_MAX_HEIGHT - barHeight;

    // Clear above bar
    if (emptyHeight > 0) {
      tft.fillRect(x, GRAPH_TOP, BAR_WIDTH, emptyHeight, TFT_BLACK);
    }

    // Draw active bar
    if (barHeight > 0) {
      tft.fillRect(x, GRAPH_BOTTOM - barHeight, BAR_WIDTH, barHeight, barColors[i]);
    }

    // Draw peak hold tick line
    if (peakHeight > 2) {
      int peakY = GRAPH_BOTTOM - peakHeight;
      tft.drawFastHLine(x, peakY, BAR_WIDTH, TFT_WHITE);
    }

    // Highlight dominant visible channel label in yellow
    tft.setTextSize(1);
    if (i == visiblePeakIdx && visiblePeakVal > 20) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    }
    tft.setCursor(x + 2, GRAPH_BOTTOM + 6);
    tft.print(channelLabels[i]);
  }

  // Redraw reference grid lines and scale labels on top
  drawReferenceGrid(currentScaleMax);
}
