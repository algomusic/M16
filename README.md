# M16
Arduino Audio Library for ESP8266 and ESP32. 
Tested on ESP8266, original ESP32, as well as ESP32-S2 and ESP32-S3

M16 is a 16-bit audio synthesis library for ESP microprocessors and I2S audio DACs/ADCs.

Default I2S DAC board connections are:

ESP8266 - GPIO 15 -> BCLK, GPIO 2 -> LRCLK (WS), and GPIO 3 (RX) -> DOUT (to DIN) [DIN not yet supported]
ESP32 - GPIO 16 -> BCLK, GPIO 17 -> LRCLK (WS), GPIO 18 -> DOUT (to DIN), and GPIO 21 -> DOUT (to DIN)

To change the default pins for ESP32 use: seti2sPinsOut(); 
e.g. seti2sPinsOut(25, 27, 12, 21);

Some I2S DAC and microphone boards require other terminals to be grounded.

Always include the M16.h file and add a void audioUpdate() function that ends with a call to i2s_write_samples(leftVal, rightVal). This function is automatically called in the background.

M16 prioritises audio processing and may not play well with other libraries where timing is critical, such as wifi, and file i/o. The temporary stopping of audio during these tasks may help coordination between them.

Designed for use with the Arduino IDE.

M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012

It is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
