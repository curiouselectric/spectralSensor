/*!
 * @file DIYTZT_TFT_eSPI_AS7341.ino
 * @brief Dynamic bar graph rendering using raw TFT_eSPI (No LVGL required).
 * @details Reads 10 optical channels from the AS7341 via alternative expansion I2C pins.
 *
 * @copyright Copyright (c) 2010 DFRobot Co.Ltd (http://dfrobot.com)
 * @license The MIT license (MIT)
 * @author [fengli](li.feng@dfrobot.com)
 * @version V3.0
 * @date 2026-10-25
 */
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include "DFRobot_AS7341.h"

// Hardware Pin Assignments
#define EXPANSION_SDA_PIN 22
#define EXPANSION_SCL_PIN 21

#define NUM_CHANNELS 10
#define SENSOR_POLL_INTERVAL 150 

// Interfaces
TFT_eSPI tft = TFT_eSPI();
DFRobot_AS7341 as7341(&Wire);
unsigned long lastSensorPollTime = 0;

// Data Storage Arrays
int channelValues[NUM_CHANNELS] = {0};
int previousBarHeights[NUM_CHANNELS] = {0}; // Used to prevent screen flickering
const char* channelLabels[NUM_CHANNELS] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "Clr", "NIR"};

// Array of 10 distinct colors matching the physical spectrum wavelengths
uint16_t spectralColors[NUM_CHANNELS] = {
  0x780F, // F1: Violet (Deep Indigo)
  0x001F, // F2: Blue
  0x03DF, // F3: Cyan
  0x07E0, // F4: Green
  0xAFE0, // F5: Yellow-Green
  0xFFE0, // F6: Yellow
  0xFD20, // F7: Orange
  0xF800, // F8: Red
  0xFFFF, // Clear: White
  0x7BEF  // NIR: Grey
};

void setup(void) {
  Serial.begin(115200);

  // Initialize the Display screen hardware
  tft.init();
  tft.setRotation(1); // Set to Landscape Mode
  tft.fillScreen(TFT_BLACK);

  // Setup Title Text
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("AS7341 Spectral Analysis", 160, 10, 1);

  // Initialize Swapped I2C hardware bus setup
  Wire.begin(EXPANSION_SDA_PIN, EXPANSION_SCL_PIN); 
  
  while (as7341.begin() != 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("I2C Init Failed! Check Pins.", 160, 120, 1);
    delay(1000);
  }
  
  // Clear any errors on success and build layout grid
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("AS7341 Wavelength Spectrum", 160, 10, 1);
  
  // Set up sensor variables
  as7341.setAtime(29);
  as7341.setAstep(599);
  as7341.setAGAIN(7);
  as7341.enableLed(true);
  as7341.controlLed(10);

  // Draw the graph bottom axis static baseline labels
  tft.setTextSize(1);
  for(int i = 0; i < NUM_CHANNELS; i++) {
    int xPos = 25 + (i * 28);
    tft.drawCentreString(channelLabels[i], xPos + 10, 225, 1);
  }
}

void loop(void) {
  // Non-blocking timer tracking execution cycles cleanly
  if (millis() - lastSensorPollTime >= SENSOR_POLL_INTERVAL) {
    lastSensorPollTime = millis();
    
    DFRobot_AS7341::sModeOneData_t data1;
    DFRobot_AS7341::sModeTwoData_t data2;

    // Read First Spectrum Tier (F1 - F4)
    as7341.startMeasure(as7341.eF1F4ClearNIR);
    while(!as7341.measureComplete()) { delay(1); }
    data1 = as7341.readSpectralDataOne();
    channelValues[0] = data1.ADF1;
    channelValues[1] = data1.ADF2;
    channelValues[2] = data1.ADF3;
    channelValues[3] = data1.ADF4;

    // Read Second Spectrum Tier (F5 - F8, Clear, NIR)
    as7341.startMeasure(as7341.eF5F8ClearNIR);
    while(!as7341.measureComplete()) { delay(1); }
    data2 = as7341.readSpectralDataTwo();
    channelValues[4] = data2.ADF5;
    channelValues[5] = data2.ADF6;
    channelValues[6] = data2.ADF7;
    channelValues[7] = data2.ADF8;
    channelValues[8] = data2.ADCLEAR;
    channelValues[9] = data2.ADNIR;

    // Dynamic rendering adjustments via local loop parsing
    int graphBaselineY = 215; 
    int maxBarHeight = 160;

    for (int i = 0; i < NUM_CHANNELS; i++) {
      // Map 16-bit sensor data into pixels. Adjust 15000 based on your lighting conditions
      int targetBarHeight = map(channelValues[i], 0, 15000, 0, maxBarHeight);
      targetBarHeight = constrain(targetBarHeight, 0, maxBarHeight);

      int xPos = 25 + (i * 28);
      int currentY = graphBaselineY - targetBarHeight;

      // Draw the bar graph layout updates using a non-flicker strategy
      if (targetBarHeight > previousBarHeights[i]) {
        // Bar grew: fill the new space upward with the spectrum color
        int tallDelta = targetBarHeight - previousBarHeights[i];
        tft.fillRect(xPos, currentY, 20, tallDelta, spectralColors[i]);
      } 
      else if (targetBarHeight < previousBarHeights[i]) {
        // Bar shrank: clear the empty overhead space downward with black pixels
        int shortDelta = previousBarHeights[i] - targetBarHeight;
        int oldY = graphBaselineY - previousBarHeights[i];
        tft.fillRect(xPos, oldY, 20, shortDelta, TFT_BLACK);
      }
      
      // Store value state for the next calculation loop
      previousBarHeights[i] = targetBarHeight;
    }
  }
}
