# M16
 Arduino Audio Library for ESP8266 and ESP32

 M16 is a 16-bit audio synthesis library for ESP microprocessors and I2S audio DACs/ADCs.

 I2S DAC board connections are:
 ESP8266 - GPIO 15 -> BCLK, GPIO 3 (RX) -> DIN, and GPIO 2 -> LRCLK (WS)
 ESP32 - GPIO 25 -> BCLK, GPIO 12 -> DIN, and GPIO 27 -> LRCLK (WS)
 Some I2S DAC boards also require other terminals to be grounded.

 Include the M16.h file when using an ESP8266 or the M32.h file when using an ESP32.

 Designed for use with the Arduino IDE.

 M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012

 It is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
