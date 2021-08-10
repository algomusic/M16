/*
 * M16.h
 *
 * M16 is a 16-bit audio synthesis library for ESP8266 microprocessors and I2S audio DACs/ADCs.
 *
 * by Andrew R. Brown 2021
 *
 *
 * M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

// #ifndef M16_H_
// #define M16_H_

#include <I2S.h>
#include "ESP8266WiFi.h"
// ESP8266 I2S interface (D1 mini pins) BCLK->BCK (D8), I2SO->DOUT (RX), and LRCLK(WS)->LCK (D4) [SCK to GND on some boards]
// #if not defined (SAMPLE_RATE)
#define SAMPLE_RATE 44100 // supports about 2x 2 osc voices at 48000, 3 x 3 osc voices at 22050
#define MAX_16 32767
#define MIN_16 -32767
// #endif

//void setSampleRate(

const uint16_t TABLE_SIZE = 2048; //4096;

//#define SINE 0
//#define TRIANGLE 1
//#define SQUARE 2
//#define SAWTOOTH 3
//#define NOISE 4

uint16_t prevWaveVal = 0;

/** Define function signature here only
* audioUpdate function must be defined in your main file
* In it, calulate the next sample data values for left and right channels
* Typically using aOsc.next() or similar calls.
* The function must end with i2s_write_lr(leftVal, rightVal);
* Keep output vol to max 50% for mono MAX98357 which sums both channels!
*/
void audioUpdate();

/** Setup audio output callback */
void ICACHE_RAM_ATTR onTimerISR() { //Code needs to be in IRAM because its a ISR
  while (!(i2s_is_full())) { //Don’t block the ISR if the buffer is full
    //i2s_write_sample(audioUpdate());
    audioUpdate();
  }
  timer1_write(1500);//Next callback in 2mS
}

/** Start the audio callback
 *  This function is typically called in setup() in the main file
 */
void audioStart() {
  WiFi.forceSleepBegin(); // not necessary, but can't hurt
  delay(1);
  i2s_rxtx_begin(true, true); // Enable I2S RX only
  i2s_set_rate(SAMPLE_RATE);
  timer1_attachInterrupt(onTimerISR); //Attach our sampling ISR
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  timer1_write(1500);
}

/** Return freq from a MIDI pitch */
float mtof(float midival) {
    float f = 0.0;
    if(midival) f = 8.1757989156 * pow(2.0, midival/12.0);
    return f;
}

/** Return left amount for a pan position 0.0-1.0 */
float panLeft(float panVal) {
  return cos(6.219 * panVal * 0.25);
}

/** Return right amount for a pan position 0.0-1.0 */
float panRight(float panVal) {
  return cos(6.219 * (panVal * 0.25 + 0.75));
}

// Rand from Mozzi library
static unsigned long randX=132456789, randY=362436069, randZ=521288629;

unsigned long xorshift96()
{ //period 2^96-1
  // static unsigned long x=123456789, y=362436069, z=521288629;
  unsigned long t;

  randX ^= randX << 16;
  randX ^= randX >> 5;
  randX ^= randX << 1;

  t = randX;
  randX = randY;
  randY = randZ;
  randZ = t ^ randX ^ randY;

  return randZ;
}

/** @ingroup random
Ranged random number generator, faster than Arduino's built-in random function, which is too slow for generating at audio rate with Mozzi.
@param maxval the maximum signed int value of the range to be chosen from.  Maxval-1 will be the largest value possibly returned by the function.
@return a random int between 0 and maxval-1 inclusive.
*/
int rand(int maxval)
{
  return (int) (((xorshift96() & 0xFFFF) * maxval)>>16);
}
// #endif /* M16_H_ */
