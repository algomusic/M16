# M16
Arduino Audio Library for ESP8266 and ESP32

M16 is a 16-bit audio synthesis library for ESP microprocessors and I2S audio DACs/ADCs.

Default I2S DAC board connections are:

ESP8266 - GPIO 15 -> BCLK, GPIO 3 (RX) -> DIN, and GPIO 2 -> LRCLK (WS)

ESP32 - GPIO 25 -> BCLK, GPIO 12 -> DIN, and GPIO 27 -> LRCLK (WS)

Some I2S DAC boards often require other terminals to be grounded.

Always include the M16.h file and add a void audioUpdate() function that ends with a call to i2s_write_samples(leftVal, rightVal). For ESP32 programs, this function is automatically called in the background, for ESP8266 programs a call to audioUpdate() is explicitly required in the main loop() function.

M16 prioritises audio processing and may not play well with other libraries where timing is critical, such as wifi, and file i/o. The temporary stopping of audio during these tasks may help coordination between them.

Designed for use with the Arduino IDE.

M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012

It is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
