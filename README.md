# M16
Arduino Audio Library for ESP8266 and ESP32

M16 is a 16-bit audio synthesis library for ESP microprocessors and I2S audio DACs/ADCs.

Default I2S DAC board connections are:

ESP8266 - GPIO 15 -> BCLK, GPIO 2 -> LRCLK (WS), and GPIO 3 (RX) -> DOUT (to DIN)

ESP32 - GPIO 25 -> BCLK, GPIO 27 -> LRCLK (WS), and GPIO 12 -> DOUT (to DIN)

ESP32-S2 - GPIO 35 -> BCLK, GPIO 36 -> LRCLK (WS), and GPIO 37 -> DOUT (to DIN)

Some I2S DAC boards often require other terminals to be grounded.

Always include the M16.h file and add a void audioUpdate() function that ends with a call to i2s_write_samples(leftVal, rightVal). For ESP32 (inc S2) programs, this function is automatically called in the background, for ESP8266 programs a call to audioUpdate() is explicitly required in the main loop() function.

M16 prioritises audio processing and may not play well with other libraries where timing is critical, such as wifi, and file i/o. The temporary stopping of audio during these tasks may help coordination between them.

Designed for use with the Arduino IDE.

M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012

It is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
