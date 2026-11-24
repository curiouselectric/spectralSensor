/*!
 * @file getData.ino
 * @brief acquisition of spectral data
 * @details Read the values of 10 optical channels of the AS7341 spectral sensor, the more light of a certain wavelength of the light source,
 * the greater the corresponding channel value.
 *
 * @copyright   Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license     The MIT license (MIT)
 * @author [fengli](li.feng@dfrobot.com)
 * @version  V1.0
 * @date  2020-07-16
 * @url https://github.com/DFRobot/DFRobot_AS7341
 */
/*!
 * @file getData.ino
 * @brief acquisition of spectral data with custom I2C pins
 * @details Read the values of 10 optical channels of the AS7341 spectral sensor, the more light of a certain wavelength of the light source,
 * the greater the corresponding channel value.
 *
 * @copyright Copyright (c) 2010 DFRobot Co.Ltd (http://dfrobot.com)
 * @license The MIT license (MIT)
 * @author [fengli](li.feng@dfrobot.com)
 * @version V1.1
 * @date 2026-10-25
 * @url https://github.com
 */
#include "DFRobot_AS7341.h"
#include <Wire.h>

// =========================================================================
// CUSTOM I2C PIN CONFIGURATION (For ESP32 / ESP8266)
// =========================================================================
// Change these numbers to the physical GPIO pins you want to use on your board
#define CUSTOM_SDA_PIN 21  
#define CUSTOM_SCL_PIN 22  

// Construct the sensor object
DFRobot_AS7341 as7341;

void setup(void) {
  Serial.begin(115200);

  // 1. Force the Wire library to initialize on your newly swapped custom pins
  Serial.print("Initializing I2C bus on swapped pins... SDA:");
  Serial.print(CUSTOM_SDA_PIN);
  Serial.print(" | SCL:");
  Serial.println(CUSTOM_SCL_PIN);
  
  Wire.begin(CUSTOM_SDA_PIN, CUSTOM_SCL_PIN); 

  // 2. Detect if I2C can communicate with the AS7341 sensor properly
  while (as7341.begin() != 0) {
    Serial.println("I2C init failed, please check if your swapped wire connection is correct");
    delay(1000);
  }
  Serial.println("AS7341 Sensor connected successfully!");

  // Integration time = (ATIME + 1) x (ASTEP + 1) x 2.78µs
  // Set the value of register ATIME(1-255), through which the value of Integration time can be calculated.
  as7341.setAtime(29);
  
  // Set the value of register ASTEP(0-65534), through which the value of Integration time can be calculated.
  as7341.setAstep(599);
  
  // Set gain value(0~10 corresponds to X0.5,X1,X2,X4,X8,X16,X32,X64,X128,X256,X512)
  as7341.setAGAIN(7);
  
  // Enable LED
  as7341.enableLed(true);
  
  // Set pin current to control brightness (1~20 corresponds to current 4mA,6mA,8mA,10mA,12mA,......,42mA)
  as7341.controlLed(10);
}

void loop(void) {
  DFRobot_AS7341::sModeOneData_t data1;
  DFRobot_AS7341::sModeTwoData_t data2;

  // ----------------------------------------------------
  // STEP 1: Measure Channel Mapping Mode 1 (F1-F4, Clear, NIR)
  // ----------------------------------------------------
  as7341.startMeasure(as7341.eF1F4ClearNIR);
  
  // Dynamic non-blocking check: Wait safely for the 50ms integration cycle to finish
  while(!as7341.measureComplete()){
    delay(1); 
  }

  // Read validated data from the first tier of registers
  data1 = as7341.readSpectralDataOne();
  
  Serial.print("F1(405-425nm):");  Serial.println(data1.ADF1);
  Serial.print("F2(435-455nm):");  Serial.println(data1.ADF2);
  Serial.print("F3(470-490nm):");  Serial.println(data1.ADF3);
  Serial.print("F4(505-525nm):");  Serial.println(data1.ADF4);

  // ----------------------------------------------------
  // STEP 2: Measure Channel Mapping Mode 2 (F5-F8, Clear, NIR)
  // ----------------------------------------------------
  as7341.startMeasure(as7341.eF5F8ClearNIR);
  
  // Dynamic non-blocking check: Wait safely for the second integration cycle to finish
  while(!as7341.measureComplete()){
    delay(1);
  }

  // Read validated data from the second tier of registers
  data2 = as7341.readSpectralDataTwo();
  
  Serial.print("F5(545-565nm):");  Serial.println(data2.ADF5);
  Serial.print("F6(580-600nm):");  Serial.println(data2.ADF6);
  Serial.print("F7(620-640nm):");  Serial.println(data2.ADF7);
  Serial.print("F8(670-690nm):");  Serial.println(data2.ADF8);
  Serial.print("Clear:");          Serial.println(data2.ADCLEAR);
  Serial.print("NIR:");            Serial.println(data2.ADNIR);
  Serial.println("-------------------------------------");

  delay(1000);
}
