/* Hardware Defines
 * Copied from Mozzi Library by Tim Barrass 2012
*/
#ifndef HARDWARE_DEFINES_H_
#define HARDWARE_DEFINES_H_

#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
#endif

/* Macros to tell apart the supported platforms. The advantages of using these are, rather than the underlying defines
- Easier to read and write
- Compiler protection against typos
- Easy to extend for new but compatible boards */

// #define IS_AVR() (defined(__AVR__))  // "Classic" Arduino boards
// #define IS_SAMD21() (defined(ARDUINO_ARCH_SAMD))
// #define IS_TEENSY3() (defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__) || defined(__MKL26Z64__) )  // 32bit arm-based Teensy
// #define IS_STM32() (defined(__arm__) && !IS_TEENSY3() && !IS_SAMD21())  // STM32 boards (note that only the maple based core is supported at this time. If another cores is to be supported in the future, this define should be split.
#define IS_ESP8266() (defined(ESP8266))
#define IS_ESP32() (defined(ESP32))

// #if !(IS_AVR() || IS_TEENSY3() || IS_STM32() || IS_ESP8266() || IS_SAMD21() || IS_ESP32())
#if !(IS_ESP8266() || IS_ESP32())
#error Your hardware is not supported by M16 or not recognized. Edit Hardware_defines.h to proceed.
#endif

#endif /* HARDWARE_DEFINES_H_ */
