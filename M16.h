/*
 * M16.h
 *
 * M16 is a 16-bit audio synthesis library for ESP8266 and ESP32 microprocessors using I2S audio DACs/ADCs.
 *
 * by Andrew R. Brown 2021
 *
 * M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#include "Arduino.h"

// based on "Hardware_defines.h" in Mozzi
#define IS_ESP8266() (defined(ESP8266))
#define IS_ESP32() (defined(ESP32))
#define IS_ESP32S2() (defined(CONFIG_IDF_TARGET_ESP32S2))
#define IS_ESP32C3() (defined(CONFIG_IDF_TARGET_ESP32C3))

// globals
#define SAMPLE_RATE 48000
const float SAMPLE_RATE_INV  = 1.0f / SAMPLE_RATE;
#define MAX_16 32767
#define MIN_16 -32767
#define MAX_16_INV 0.00003052

const int16_t TABLE_SIZE = 4096; //8192; // 2048 // 4096 // 8192 // 16384 //32768 // 65536 //uint16_t
const float TABLE_SIZE_INV = 1.0f / TABLE_SIZE;
const int16_t HALF_TABLE_SIZE = 2048; //TABLE_SIZE / 2;

int16_t prevWaveVal = 0;
int16_t leftAudioOuputValue = 0;
int16_t rightAudioOuputValue = 0;

// ESP32 - GPIO 25 -> BCLK, GPIO 12 -> DIN, and GPIO 27 -> LRCLK (WS)
// ESP8266 I2S interface (D1 mini pins) BCLK->BCK (D8 GPIO15), I2SO->DOUT (RX GPIO3), and LRCLK(WS)->LCK (D4 GPIO2) [SCK to GND on some boards]

#if IS_ESP8266()
  // to flash Wemos D1 R1 with I2S board connected, seems you need to disconnect D4 & RX???
  #include <I2S.h>

  void audioUpdate(); // overridden by function in program code

  /** Setup audio output callback for ESP8266*/
  // void ICACHE_RAM_ATTR onTimerISR() { //Code needs to be in IRAM because its a ISR
  void IRAM_ATTR onTimerISR() { //Code needs to be in IRAM because its a ISR
    while (!(i2s_is_full())) { //Don’t block the ISR if the buffer is full
      audioUpdate();
    }
    timer1_write(2000);//Next callback in 2mS
  }

  /** Start the audio callback
   *  This function is typically called in setup() in the main file
   */
  void audioStart() {
    I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE, 16);
    timer1_attachInterrupt(onTimerISR); //Attach our sampling ISR
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
    timer1_write(2000); //Service at 2mS intervall
    Serial.println("M16 is running");
  }

  void i2s_write_samples(int16_t leftSample, int16_t rightSample) {
    leftAudioOuputValue = leftSample;
    rightAudioOuputValue = rightSample;
    i2s_write_lr(leftSample * 0.98, rightSample * 0.98); // * 0.98 to avoid DAC distortion at extremes
  }

  void seti2sPins(int bck, int ws, int dout, int din) {
    Serial.println("seti2sPins() is not availible for the ESP8266 which has fixed i2s pins");
  } // ignored for ESP8266
#elif IS_ESP32()
  #include "driver/i2s.h"

  static const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number
  int i2sPinsOut [] = {16, 17, 18, 21}; // bck, ws, data_out, data_in defaults for eProject board, ESP32 or ESP32-S3 or ESP32-S2
  // there seems to be an issue on the S2 sharing bck (GPIO 16) with the MEMS microphone.

  /* ESP32 I2S pin allocation */
  static i2s_pin_config_t pin_config = { 
      .bck_io_num = i2sPinsOut[0],   // The bit clock connectiom, goes to pin 27 of ESP32
      .ws_io_num = i2sPinsOut[1],    // Word select, also known as word select or left right clock
      .data_out_num = i2sPinsOut[2], // Data out from the ESP32
      .data_in_num = i2sPinsOut[3]   // Data in to the ESP32
  };

  void seti2sPins(int bck, int ws, int dout, int din) {
    i2sPinsOut[0] = bck;
    i2sPinsOut[1] = ws;
    i2sPinsOut[2] = dout;
    i2sPinsOut[3] = din;
    pin_config = { 
      .bck_io_num = i2sPinsOut[0], 
      .ws_io_num = i2sPinsOut[1], 
      .data_out_num = i2sPinsOut[2],
      .data_in_num = i2sPinsOut[3]
    };
    Serial.println("i2s output pins set");
  }

  // I2S configuration structures
  static const int dmaBufferLength = 64;
  
  static const i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      // .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,       // high interrupt priority
      .dma_buf_count = 8,                             // buffers
      .dma_buf_len = dmaBufferLength,                 // samples per buffer
      .use_apll = 0,
      .tx_desc_auto_clear = true, 
      .fixed_mclk = -1    
  };

  void audioUpdate();

  /** Function for RTOS tasks to fill audio buffer */
  void audioCallback(void * paramRequiredButNotUsed) {
    for(;;) { // Looks ugly, but necesary. RTOS manages thread
      audioUpdate();
      yield();
    }
  }

  bool i2s_write_samples(int16_t leftSample, int16_t rightSample) {
    leftAudioOuputValue = leftSample;
    rightAudioOuputValue = rightSample;
    static size_t bytesWritten = 0;
    uint32_t value32Bit = (leftSample << 16) | (rightSample & 0xffff); // Combine both left and right channels
    i2s_write(i2s_num, &value32Bit, 4, &bytesWritten, portMAX_DELAY); 
    yield();
    if (bytesWritten > 0) {
        return true;
    } else return false;
  }

  TaskHandle_t audioCallback1Handle = NULL;
  TaskHandle_t audioCallback2Handle = NULL;

  /** Start the audio callback
   *  This function is typically called in setup() in the main file
   */
  void audioStart() {
    i2s_driver_install(i2s_num, &i2s_config, 0, NULL);        // ESP32 will allocated resources to run I2S
    i2s_set_pin(i2s_num, &pin_config);                        // Tell it the pins you will be using
    i2s_start(i2s_num); // not explicity necessary, called by install
    // RTOS callback
    xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer0", 2048, NULL, 2, &audioCallback1Handle, 0); // 1024 = memory, 1 = priorty, 0 = core
    xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer1", 2048, NULL, configMAX_PRIORITIES - 1, &audioCallback2Handle, 1);
    Serial.println("M16 is running");
  }
#endif


/** Return freq from a MIDI pitch 
* @pitch The MIDI pitch to be converted
*/
inline
float mtof(float midival) {
  midival = max(0.0f, min(127.0f, midival));
  float f = 0.0;
  if (midival) f = 8.1757989156 * pow(2.0, midival * 0.083333); // / 12.0);
  return f;
}

// inline
// float mtof(int midiNote) {
//   return 440.0 * pow(2.0, (midiNote - 69.0) / 12.0);
// }

/** Return a MIDI pitch from a frequency 
* @freq The frequency to be converted
*/
inline
float ftom(float freq) {
  return ( ( 12 * log(freq / 220.0) / log(2.0) ) + 57.01 );
}

/** Return closest scale pitch to a given MIDI pitch
* @pitch MIDI pitch number
* @pitchClassSet an int array of chromatic values, 0-11, of size 12 (padded with zeros as required)
* @key pitch class key, 0-11, where 0 = C root
*/
inline
int pitchQuantize(int pitch, int * pitchClassSet, int key) {
  for (int j=0; j<12; j++) {
    int pitchClass = pitch%12;
    bool adjust = true;
    for (int i=0; i < 12; i++) {
      if (pitchClass == pitchClassSet[i] + key) {
        adjust = false;
      }
    }
    if (adjust) {
      pitch -= 1;
    } else return pitch;
  }
  return pitch; // just in case?
}

// int pitchQuantize(int pitch, int * pitchClassSet, int key) {
//   int pitchClass = pitch%12;
//   int pitchClassSetIndex = 0;
//   for (int i=0; i < 12; i++) {
//     if (pitchClass == pitchClassSet[i] + key) {
//       pitchClassSetIndex = i;
//     }
//   }
//   int octave = pitch/12;
//   int pitchClassSetOctave = pitchClassSetIndex/12;
//   int pitchClassSetIndexMod = pitchClassSetIndex%12;
//   int pitchClassSetIndexModOctave = pitchClassSetIndexMod + pitchClassSetOctave*12;
//   return pitchClassSetIndexModOctave + octave*12;
// }

/** Return freq a chromatic interval away from base
* @freqVal The base frequency in Htz
* @interval The chromatic distance from the base in semitones, -12 to 12
*/
static float intervalRatios[] = {0.5, 0.53, 0.56, 0.595, 0.63, 0.665, 0.705, 0.75, 0.795, 0.84, 0.89, 0.945,
  1, 1.06, 1.12, 1.19, 1.26, 1.33, 1.41, 1.5, 1.59, 1.68, 1.78, 1.89, 2}; // equal tempered
// static float intervalRatios[] = {1, 1.067, 1.125, 1.2, 1.25, 1.33, 1.389, 1.5, 1.6, 1.67, 1.8, 1.875, 2}; // just

float intervalFreq(float freqVal, int interval) {
  float f = freqVal;
  if (interval >= -12 && interval <= 12) f = freqVal * intervalRatios[(interval + 12)];
  return f;
}

/** Return left amount for a pan position 0.0-1.0 */
float panLeft(float panVal) {
  return max(0.0, min(1.0, cos(6.219 * panVal * 0.25)));
}

/** Return right amount for a pan position 0.0-1.0 */
float panRight(float panVal) {
  return max(0.0, min(1.0, cos(6.219 * (panVal * 0.25 + 0.75))));
}

/** Return scaled floating point value 
* Arduino map() function for floats
*/
float floatMap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/** Return sigmond distributed value for value between 0.0-1.0 */
inline
float sigmoid(float inVal) { // 0.0 to 1.0
  if (inVal > 0.5) {
    return 0.5 + pow((inVal - 0.5)* 2, 4) * 0.5f;
  } else {
    return max(0.0, pow(inVal * 2, 0.25) * 0.5f);
  }
}

/** Return a partial increment toward target from current value
* @curr The curent value
* @target The desired final value
* @amt The percentage toward target (0.0 - 1.0)
*/
inline
float slew(float curr, float target, float amt) {
  if (curr == target) return target;
  float dist = target - curr;
  return curr + dist * amt;
}

// Rand from Mozzi library
static unsigned long randX=132456789, randY=362436069, randZ=521288629;

unsigned long xorshift96() { //period 2^96-1
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
int rand(int maxVal) {
  return (int) (((xorshift96() & 0xFFFF) * maxVal)>>16);
}

/** Approximate Gausian Random
* The values will tend to be near the middle of the range, midway between zero and maxVal
* @param maxVal The largest integer possible
* @tightness how many rand values (2+), greater numbers increasingly reduce standard deviation
*/
int gaussRandNumb(int maxVal, int tightness) {
  int sum = 0;
  for (int i=0; i<tightness; i++) {
    sum += rand(maxVal + 1);
  }
  return sum / tightness;
}

/** Approximate Gausian Random
* The values will tend to be near the middle of the range, midway between zero and maxVal
* @param maxVal The largest integer possible
*/
int gaussRand(int maxVal) {
  return gaussRandNumb(maxVal, 2);
}

/** Approximate Chaotic Random number generator
* The output values will between 0 and range
* Range of 1 provides 2 attractor oscillation, other value provide more diversity
* @param range The largest value possible
* Algorithm by Roger Luebeck  2000, 2017
*  https://chaos-equations.com/index.htm
*/
float prevChaosRandVal = 0.5;

float chaosRand(float range) {
  float chaosRandVal = range * sin(3.1459 * prevChaosRandVal);
  prevChaosRandVal = chaosRandVal;
  return chaosRandVal * 0.5 + range * 0.5;
}

// #endif #endif /* M16_H_ */
