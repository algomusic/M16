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
// from "Hardware_defines.h" in Mozzi
#define IS_ESP8266() (defined(ESP8266))
#define IS_ESP32() (defined(ESP32))

#if IS_ESP8266()
#include <I2S.h>
#elif IS_ESP32()
#include "driver/i2s.h"
static const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number
#endif
// ESP32 - GPIO 25 -> BCLK, GPIO 12 -> DIN, and GPIO 27 -> LRCLK (WS)
// ESP8266 I2S interface (D1 mini pins) BCLK->BCK (D8), I2SO->DOUT (RX), and LRCLK(WS)->LCK (D4) [SCK to GND on some boards]
// #if not defined (SAMPLE_RATE)
#define SAMPLE_RATE 48000 // ESP8266 supports about 2x 2 osc voices at 48000, 3 x 3 osc voices at 22050
#define MAX_16 32767
#define MIN_16 -32767

const uint16_t TABLE_SIZE = 2048; //4096;

uint16_t prevWaveVal = 0;

/** Define function signature here only
* audioUpdate function must be defined in your main file
* In it, calulate the next sample data values for left and right channels
* Typically using aOsc.next() or similar calls.
* The function must end with i2s_write_lr(leftVal, rightVal);
* Keep output vol to max 50% for mono MAX98357 which sums both channels!
*/
void audioUpdate();

#if IS_ESP8266()
/** Setup audio output callback for ESP8266*/
/*
void ICACHE_RAM_ATTR onTimerISR() { //Code needs to be in IRAM because its a ISR
  while (!(i2s_is_full())) { //Don’t block the ISR if the buffer is full
    audioUpdate();
  }
  timer1_write(1500);//Next callback in 2mS
}
*/
/** Start the audio callback
 *  This function is typically called in setup() in the main file
 */

void audioStart() {
  I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE, 16);
  // WiFi.forceSleepBegin(); // not necessary, but can't hurt
  /*
  delay(1);
  i2s_rxtx_begin(true, true); // Enable I2S RX only
  i2s_set_rate(SAMPLE_RATE);
  timer1_attachInterrupt(onTimerISR); //Attach our sampling ISR
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  timer1_write(1500);
  */
}
/* Combine left and right samples and send out via I2S */
bool i2s_write_samples(int16_t leftSample, int16_t rightSample) {
  I2S.write((int32_t)(leftSample + MAX_16));
  I2S.write((int32_t)(rightSample + MAX_16));
  /*
  uint32_t s = 0;
  s |= 0xffff & leftSample;
  s = rightSample & 0xffff;
  i2s_write_sample(s);
  */
  // i2s_write_sample((uint32_t)leftSample + MAX_16);
  // i2s_write_sample((uint32_t)rightSample + MAX_16);
}

#elif IS_ESP32()
/* ESP32 I2S configuration structures */
static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB), //I2S_COMM_FORMAT_STAND_I2S,
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
    .ws_io_num = 27, //27,      //27    // 6   // 4                // Word select, also known as word select or left right clock
    .data_out_num = 12, //12,     //26   // 7   // 5            // Data out from the ESP32, connect to DIN on 38357A
    .data_in_num = I2S_PIN_NO_CHANGE                // we are not interested in I2S data into the ESP32
};

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

/** Stop the audio callback */
void audioStop() {
  i2s_driver_uninstall(i2s_num); //stop & destroy i2s driver
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
#endif

/** Return freq from a MIDI pitch */
float mtof(float midival) {
    float f = 0.0;
    if(midival) f = 8.1757989156 * pow(2.0, midival/12.0);
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

/** Return sigmond distributed value for value between 0.0-1.0 */
float sigmoid(float inVal) { // 0.0 to 1.0
  if (inVal > 0.5) {
    return 0.5 + pow((inVal - 0.5)* 2, 4) / 2.0f;
  } else {
    return max(0.2, pow(inVal * 2, 0.25) / 2.0f);
  }
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
