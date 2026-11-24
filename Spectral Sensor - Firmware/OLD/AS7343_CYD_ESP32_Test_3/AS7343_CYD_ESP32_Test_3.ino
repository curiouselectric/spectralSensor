/*!
 * @file DIYTZT_ESP32_LVGL_AS7341.ino
 * @brief Dynamic bar graph rendering of AS7341 sensor channels for DIYTZT ESP32.
 * @details Safely encapsulates LVGL graphics loops with non-blocking AS7341 I2C queries.
 *
 * @copyright Copyright (c) 2010 DFRobot Co.Ltd (http://dfrobot.com)
 * @license The MIT license (MIT)
 * @author [fengli](li.feng@dfrobot.com)
 * @version V2.0
 * @date 2026-10-25
 */
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "DFRobot_AS7341.h"

// =========================================================================
// HARDWARE / PIN ASSIGNMENT DEFINITIONS
// =========================================================================
#define EXPANSION_SDA_PIN 21
#define EXPANSION_SCL_PIN 22

#define NUM_CHANNELS 10
#define SENSOR_POLL_INTERVAL 250 // Poll sensor data every 250ms (Non-blocking)

// Driver & Hardware Interfaces
DFRobot_AS7341 as7341;
unsigned long lastSensorPollTime = 0;

// LVGL UI Element Pointers
lv_obj_t * barChart;
lv_chart_series_t * spectralSeries;

// Data tracking array mapping directly to channels: F1-F8, Clear, NIR
int channelValues[NUM_CHANNELS] = {0};
const char* channelLabels[NUM_CHANNELS] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "Clr", "NIR"};

// Initialization Declarations
void initSensor();
void createBarGraphUI();
void updateSensorDataAsync();

void setup(void) {
  Serial.begin(115200);

  // 1. Core LVGL Graphics Framework initialization
  lv_init();
  
  // NOTE: Ensure your local platform configurations (TFT_eSPI setup / lv_conf.h)
  // are properly initialized here via your standard display initialization calls:
  // e.g., tft.begin(); tft.setRotation(1); lv_disp_draw_buf_init(...);
  
  // 2. Swapped I2C hardware bus setup
  Wire.begin(EXPANSION_SDA_PIN, EXPANSION_SCL_PIN); 
  initSensor();

  // 3. Render Bar Graph Dashboard 
  createBarGraphUI();
}

void loop(void) {
  // Let the LVGL engine handle screen draws, touches, and refreshes natively
  lv_timer_handler(); 
  
  // Run our non-blocking sensor collection algorithm independently
  updateSensorDataAsync();
  
  delay(5); // Small delay to optimize CPU thread timing safely
}

// =========================================================================
// AS7341 SPECTRUM DATA MANAGEMENT FUNCTIONS
// =========================================================================
void initSensor() {
  while (as7341.begin() != 0) {
    Serial.println("[I2C Error] AS7341 connection failed. Check swapped pin config.");
    delay(1000);
  }
  Serial.println("[I2C Success] AS7341 connected!");
  
  as7341.setAtime(29);
  as7341.setAstep(599);
  as7341.setAGAIN(7);
  as7341.enableLed(true);
  as7341.controlLed(10);
}

void updateSensorDataAsync() {
  if (millis() - lastSensorPollTime >= SENSOR_POLL_INTERVAL) {
    lastSensorPollTime = millis();
    
    DFRobot_AS7341::sModeOneData_t data1;
    DFRobot_AS7341::sModeTwoData_t data2;

    // Read First Spectrum Tier (F1 - F4)
    as7341.startMeasure(as7341.eF1F4ClearNIR);
    while(!as7341.measureComplete()) { 
      // Safe exit loop fallback: avoid freezing the main screen thread 
      lv_timer_handler(); 
    }
    data1 = as7341.readSpectralDataOne();
    channelValues[0] = data1.ADF1;
    channelValues[1] = data1.ADF2;
    channelValues[2] = data1.ADF3;
    channelValues[3] = data1.ADF4;

    // Read Second Spectrum Tier (F5 - F8, Clear, NIR)
    as7341.startMeasure(as7341.eF5F8ClearNIR);
    while(!as7341.measureComplete()) { 
      lv_timer_handler(); 
    }
    data2 = as7341.readSpectralDataTwo();
    channelValues[4] = data2.ADF5;
    channelValues[5] = data2.ADF6;
    channelValues[6] = data2.ADF7;
    channelValues[7] = data2.ADF8;
    channelValues[8] = data2.ADCLEAR;
    channelValues[9] = data2.ADNIR;

    // Safely update the live LVGL chart interface memory stack
    for(int i = 0; i < NUM_CHANNELS; i++) {
      // Dynamic normalizer constraint (Converts raw 16-bit spectrum inputs to fit 0-100 chart bounds)
      int constrainedValue = map(channelValues[i], 0, 65535, 0, 100); 
      lv_chart_set_next_value(barChart, spectralSeries, constrainedValue);
    }
    
    // Command the canvas layer to force clear and draw new spectrum bars
    lv_chart_refresh(barChart); 
  }
}

// =========================================================================
// LVGL GRAPHICAL INTERFACE SETUP
// =========================================================================
void createBarGraphUI() {
  // Spawn a core container view centered on display active window
  lv_obj_t * scr = lv_scr_act();

  // Create Header Text Label
  lv_obj_t * titleLabel = lv_label_create(scr);
  lv_label_set_text(titleLabel, "AS7341 10-Channel Wavelength Analysis");
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 10);

  // Initialize Canvas Chart Object
  barChart = lv_chart_create(scr);
  lv_obj_set_size(barChart, 280, 180);
  lv_obj_align(barChart, LV_ALIGN_CENTER, 0, 10);
  
  // Set chart behavior parameters to mimic concrete bar metrics
  lv_chart_set_type(barChart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(barChart, NUM_CHANNELS);
  lv_chart_set_range(barChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

  // Apply visual styling themes
  lv_obj_set_style_pad_column(barChart, 4, LV_PART_MAIN); // Set gap spacing between bars

  // Attach a dynamic series allocation array to host the bars
  spectralSeries = lv_chart_add_series(barChart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

  // Force chart to show clean array distribution initialized at 0
  for(int i = 0; i < NUM_CHANNELS; i++) {
    lv_chart_set_next_value(barChart, spectralSeries, 0);
  }
  lv_chart_refresh(barChart);
}
