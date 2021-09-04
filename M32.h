/*
 * M32.h
 *
 * M32 is a 16-bit audio synthesis library for ESP32 microprocessors and I2S audio DACs/ADCs.
 *
 * by Andrew R. Brown 2021
 *
 * M32 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M32 audio library.
 *
 * M32 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

 #define SAMPLE_RATE 48000 // supports about 2x 2 osc voices at 48000, 3 x 3 osc voices at 22050
 #define MAX_16 32767
 #define MIN_16 -32767

#include "driver/i2s.h"
static const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number

/* ESP32 I2S configuration structures */
static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,       // high interrupt priority
    .dma_buf_count = 8,                             // 8 buffers
    .dma_buf_len = 64, //1024,                            // 1K per buffer, so 8K of buffer space
    .use_apll = false,
    .tx_desc_auto_clear= true,
    .fixed_mclk = -1
};

/* ESP32 I2S pin allocation */
static const i2s_pin_config_t pin_config = { // D1 mini, NodeMCU
    .bck_io_num = 25, //25,     // 25    // 8   // 6                     // The bit clock connectiom, goes to pin 27 of ESP32
    .ws_io_num = 27, //27,       //27    // 6   // 4                // Word select, also known as word select or left right clock
    .data_out_num = 12, //18,     //26   // 7   // 5            // Data out from the ESP32, connect to DIN on 38357A
    .data_in_num = I2S_PIN_NO_CHANGE                // we are not interested in I2S data into the ESP32
};

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
* The function must end with i2s_write_samples(leftVal, rightVal);
*/
void audioUpdate();

/** Function for RTOS tasks to fill audio buffer */
void audioCallback(void * paramRequiredButNotUsed) {
  for(;;) { // Looks ugly, but necesary. RTOS manages thread
    audioUpdate();
  }
}

/** Start the audio callback
 *  This function is typically called in setup() in the main file
 */
void audioStart() {
  i2s_driver_install(i2s_num, &i2s_config, 0, NULL);        // ESP32 will allocated resources to run I2S
  i2s_set_pin(i2s_num, &pin_config);                        // Tell it the pins you will be using
  i2s_start(i2s_num); // not explicity necessary, called by install
  // RTOS callback
  xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer0", 1024, NULL, configMAX_PRIORITIES - 1, NULL, 0); // 2048 = memory, 1 = priorty, 1 = core
  xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer1", 1024, NULL, configMAX_PRIORITIES - 1, NULL, 1);
}

/* Combine left and right samples and send out via I2S */
bool i2s_write_samples(int16_t leftSample, int16_t rightSample) {
  static size_t bytesWritten = 0;
  uint32_t value32Bit = ((uint32_t)leftSample/2 <<16) | ((uint32_t)rightSample/2 & 0xffff); // Output both left and right channels
  yield();
  i2s_write(i2s_num, &value32Bit, 4, &bytesWritten, portMAX_DELAY);
  if (bytesWritten > 0) {
      return true;
  } else return false;
}

/** Return freq from a MIDI pitch */
float mtof(float midival) {
    float f = 0.0;
    if(midival) f = 8.1757989156 * pow(2.0, midival/12.0);
    return f;
}

/** Return left amount for a pan position 0.0-1.0 */
float panLeft(float panVal) {
  return cos(6.219 * (panVal * 0.25 + 0.75));
}

/** Return right amount for a pan position 0.0-1.0 */
float panRight(float panVal) {
  return cos(6.219 * panVal * 0.25);
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
* Ranged random number generator, faster than Arduino's built-in random function,
* which is too slow for generating at audio rate with Mozzi.
* @param maxval the maximum signed int value of the range to be chosen from.
* Maxval-1 will be the largest value possibly returned by the function.
* @return a random int between 0 and maxval-1 inclusive.
*/
int rand(int maxval)
{
  return (int) (((xorshift96() & 0xFFFF) * maxval)>>16);
}
// #end /* M32_H_ */
