# M16
Arduino Audio Library for ESP8266, ESP32, RP2040/RP2350, and Teensy 4

M16 is a 16-bit audio synthesis library for microprocessors and I2S audio DACs/ADCs.

Always include `M16.h` in your sketch and add an `audioUpdate()` function that ends with `audioBlockWrite(leftVal, rightVal)`. ESP32 defaults to one deterministic audio task on a dedicated core, leaving the other core available for `loop()`, controls, MIDI, USB, and UI work. Independent polyphonic voice arrays can explicitly enable dual-core rendering on ESP32 and Pi Pico with `setIsDualCore(true)` and must partition their voice loop with `audioPartitionOffset()` and `audioPartitionStride()`.

Default output connections to I2S DAC boards are:

- ESP8266 - GPIO 15 -> BCLK, GPIO 3 (RX) -> DOUT, and GPIO 2 -> LRCLK (WS) (no i2s Input support)

- ESP32 - GPIO 38 -> BCLK, GPIO 39 -> DOUT, GPIO 40 -> LRCLK (WS), and GPIO 18 -> DIN

- Pi Pico 2 - GPIO 16 -> BCLK, GPIO 18 -> DOUT, GPIO 17 -> LRCLK (WS), and GPIO 19 -> DIN

- Teensy 4 - GPIO 21 -> BCLK, GPIO 7 -> DOUT, and GPIO 20 -> LRCLK (WS), and GPIO 8 -> DIN

Some I2S DAC boards often require other terminals to be grounded.

The `seti2sPins(BCK, WS, DOUT, DIN)` function can be used on ESP32 and Pi Pico to redefine the pins. The value of -1 for DIN disables audio input.

Generic I2S microphones need no codec driver. The Teensy Audio Board additionally requires its SGTL5000
control object to be enabled and its line or microphone input selected. Teensy I2S input is enabled on demand with `audioInputStart()`. It uses the PJRC Audio pin routing: DIN 8, LRCLK 20, BCLK 21, and MCLK 23 when required.
`Mic::nextLeft()`, `nextRight()`, or `nextStereo(left, right)` read the captured input. 

M16 prioritises audio processing and may not play well with other libraries where timing is critical, such as wifi, and file i/o. The temporary stopping of audio during these tasks may help coordination between them.

Designed for use with the Arduino IDE.

M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012

It is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
