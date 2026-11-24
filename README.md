This project was to create a nice interface and display for the data from the Osram AS7341 11-channel visible light spectral sensor. 
This is an 11 channel spectral sensor with a response range of 380nm to 1000nm. 
It has 8 channels centred in the visible spectrum (VIS), plus one near-infrared (NIR) and a clear channel. 
Data is transferred via an I2C bus. 

To read and display the information a 'CYD' ESP32 board with a 320 x 240 pixel colour LCD display, SD card slot and resistive touch screen. 
The ones I obtained use an ESP32-WROOM-32 processor with reference name "CYD_2.4inch_ESP32-2432S024". 

This repository contains:

# Firmware

Code for uploading to the ESP board via the Arduino IDE is available in this folder. You need to edit the User_Setup.h file for the LCD/touchscreen driver libary.

# Hardware

The design files for a simple little enclosure for the board, a battery holder and the spectral sensor is available here.


