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
#define IS_RP2040() (defined(ARDUINO_ARCH_RP2040))
// IS_CAPABLE() groups platforms with sufficient CPU/memory for complex DSP (filters, reverb, etc.)
#define IS_CAPABLE() (IS_ESP32() || IS_RP2040())

// globals
int SAMPLE_RATE = 44100;
float SAMPLE_RATE_INV = 1.0f / SAMPLE_RATE;
#define MAX_16 32767
#define MIN_16 -32767
const float MAX_16_INV = 0.00003052;

// TABLE_SIZE can be overridden by defining it in your sketch BEFORE including M16.h
// Example: #define TABLE_SIZE 2048
#ifndef TABLE_SIZE
  #define TABLE_SIZE 4096 //4096; //8192; // 2048 // 4096 // 8192 // 16384 //32768 // 65536
#endif
#ifndef HALF_TABLE_SIZE
  #define HALF_TABLE_SIZE (TABLE_SIZE / 2)
#endif
#ifndef FULL_TABLE_SIZE
  #define FULL_TABLE_SIZE (TABLE_SIZE * 3) // accomodates low, mid, and high freq band limited waves
#endif

const int16_t _TABLE_SIZE = TABLE_SIZE;  // For backwards compatibility
const float TABLE_SIZE_INV = 1.0f / TABLE_SIZE;
const int16_t _HALF_TABLE_SIZE = HALF_TABLE_SIZE;
const int16_t _FULL_TABLE_SIZE = FULL_TABLE_SIZE;

int16_t prevWaveVal = 0;
int16_t leftAudioOuputValue = 0;
int16_t rightAudioOuputValue = 0;

// Global PSRAM availability flag - set once at startup
static bool g_psramAvailable = false;
static bool g_psramChecked = false;

inline bool isPSRAMAvailable() {
  if (!g_psramChecked) {
    #if IS_ESP32()
      g_psramAvailable = psramFound();
      if (g_psramAvailable) {
        Serial.print("PSRAM detected: ");
        Serial.print(ESP.getFreePsram() / 1024);
        Serial.println(" KB free");
      } else {
        Serial.println("No PSRAM detected");
      }
    #endif
    g_psramChecked = true;
  }
  return g_psramAvailable;
}

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
    i2s_write_lr(leftSample, rightSample);
  }

  void seti2sPins(int bck, int ws, int dout, int din) {
    Serial.println("seti2sPins() is not availible for the ESP8266 which has fixed i2s pins");
  } // ignored for ESP8266
#elif IS_ESP32()
  // i2s
  #include <driver/i2s_std.h>
  // #include <Arduino.h>

  static const i2s_port_t i2s_num = I2S_NUM_0;
  int i2sPinsOut [] = {16, 17, 18, 21}; // bck, ws, dout, din
  // int i2sPinsOut [] = {38, 39, 40, 41}; // bck, ws, dout, din

  i2s_chan_handle_t tx_handle = NULL;
  i2s_chan_handle_t rx_handle = NULL;


  // Configuration macros/constants
  // #define SAMPLE_RATE         44100       // defined above
  #define DMA_BUFFERS         8
  #define DMA_BUFFER_LENGTH   64

  // Channel (I2S port) config
  i2s_chan_config_t chan_cfg = {
      .id = I2S_NUM_0,
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = DMA_BUFFERS,
      .dma_frame_num = DMA_BUFFER_LENGTH,
      .auto_clear = true,
  };

  // Standard mode config
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)i2sPinsOut[0],
          .ws   = (gpio_num_t)i2sPinsOut[1],
          .dout = (gpio_num_t)i2sPinsOut[2],
          .din  = (gpio_num_t)i2sPinsOut[3]
      }
  };

  void seti2sPins(int bck, int ws, int dout, int din) {
      i2sPinsOut[0] = bck;
      i2sPinsOut[1] = ws;
      i2sPinsOut[2] = dout;
      i2sPinsOut[3] = din;
      std_cfg.gpio_cfg.bclk = (gpio_num_t)i2sPinsOut[0];
      std_cfg.gpio_cfg.ws   = (gpio_num_t)i2sPinsOut[1];
      std_cfg.gpio_cfg.dout = (gpio_num_t)i2sPinsOut[2];
      std_cfg.gpio_cfg.din  = (gpio_num_t)i2sPinsOut[3];
      Serial.println("i2s output pins set");
  }

  void audioUpdate(); // forward

  void audioCallback(void* param) {
      for (;;) {
          audioUpdate();
          yield();
      }
  }

  bool i2s_write_samples(int16_t leftSample, int16_t rightSample) {
      uint8_t sampleBuffer[4];
      sampleBuffer[0] = rightSample & 0xFF;
      sampleBuffer[1] = (rightSample >> 8) & 0xFF;
      sampleBuffer[2] = leftSample & 0xFF;
      sampleBuffer[3] = (leftSample >> 8) & 0xFF;

      size_t bytesWritten = 0;
      esp_err_t err = i2s_channel_write(tx_handle, sampleBuffer, 4, &bytesWritten, portMAX_DELAY);
      yield();
      return (err == ESP_OK && bytesWritten == 4);
  }

  // These handles can now be used for vTask things
  TaskHandle_t audioCallback1Handle = NULL;
  TaskHandle_t audioCallback2Handle = NULL;

  // Mutex for thread-safe initialization
  static SemaphoreHandle_t audioInitMutex = NULL;

  void audioStart() {
      // Create mutex for initialization protection
      if (audioInitMutex == NULL) {
          audioInitMutex = xSemaphoreCreateMutex();
      }

      // Create the channels
      i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle); // both TX and RX

      // Configure the channel(s) in standard Philips I2S mode
      i2s_channel_init_std_mode(tx_handle, &std_cfg);
      i2s_channel_init_std_mode(rx_handle, &std_cfg);

      // Enable channel(s)
      i2s_channel_enable(tx_handle);
      i2s_channel_enable(rx_handle);

      // Dual-core audio tasks with INCREASED stack size for complex DSP
      // Stack increased from 2048 to 8192 bytes to prevent overflow
      xTaskCreatePinnedToCore(
          audioCallback,
          "FillAudioBuffer0",
          8192,  // Increased from 2048
          NULL,
          configMAX_PRIORITIES - 1,
          &audioCallback1Handle,
          0
      );
      xTaskCreatePinnedToCore(
          audioCallback,
          "FillAudioBuffer1",
          8192,  // Increased from 2048
          NULL,
          2,
          &audioCallback2Handle,
          1
      );
      Serial.println("M16 is running (dual-core mode)");
  }
  /*
  // ESP32 Arduino Core V2
  #include "driver/i2s.h"

  static const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number
  int i2sPinsOut [] = {16, 17, 18, 21}; // bck, ws, data_out, data_in defaults for eProject board, ESP32 or ESP32-S3 or ESP32-S2
  // there seems to be an issue on the S2 sharing bck (GPIO 16) with the MEMS microphone.

 // ESP32 I2S pin allocation
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

  // Function for RTOS tasks to fill audio buffer 
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

  // Start the audio callback
  //  This function is typically called in setup() in the main file
  void audioStart() {
    i2s_driver_install(i2s_num, &i2s_config, 0, NULL);        // ESP32 will allocated resources to run I2S
    i2s_set_pin(i2s_num, &pin_config);                        // Tell it the pins you will be using
    i2s_start(i2s_num); // not explicity necessary, called by install // 
    // RTOS callback
    xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer0", 2048, NULL, configMAX_PRIORITIES - 1, &audioCallback1Handle, 0); // 1024 = memory, 1 = priorty, 0 = core
    xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer1", 2048, NULL, 2, &audioCallback2Handle, 1); // move core 1 to 0 priority to enable smoother calulculations, such as for allpass filters
    Serial.println("M16 is running");
  }
  */
#elif IS_RP2040()
  // Raspberry Pi Pico / Pico 2 (RP2040/RP2350)
  // Core 1 dedicated to audio for maximum performance
  #include <I2S.h>

  // Default I2S pins - BCLK on GPIO 16, WS is implicitly BCLK+1 (GPIO 17), DOUT on GPIO 18
  static int picoI2sPins[] = {16, 18}; // {BCLK, DOUT}

  // I2S instance
  I2S i2s(OUTPUT);

  // Flag to signal core 1 to start
  static volatile bool picoAudioRunning = false;

  void seti2sPins(int bck, int ws, int dout, int din) {
    picoI2sPins[0] = bck;
    picoI2sPins[1] = dout;
    if (ws != bck + 1) {
      Serial.print("Warning: Pico WS is always BCLK+1 (GPIO ");
      Serial.print(bck + 1);
      Serial.print("), ignoring ws=");
      Serial.println(ws);
    }
    Serial.println("i2s pins set for Pico");
  }

  void audioUpdate(); // forward declaration

  void i2s_write_samples(int16_t leftSample, int16_t rightSample) {
    leftAudioOuputValue = leftSample;
    rightAudioOuputValue = rightSample;
  }

  // Core 1: Dedicated audio generation - optimized tight loop
  void __attribute__((weak)) setup1() {
    while (!picoAudioRunning) {
      tight_loop_contents();
    }

    // Tight audio loop - no function call overhead for callback
    while (true) {
      audioUpdate();
      int32_t sample32 = ((int32_t)leftAudioOuputValue << 16) |
                         ((uint16_t)rightAudioOuputValue);
      i2s.write(sample32);  // Blocking write paced by I2S hardware
    }
  }

  void __attribute__((weak)) loop1() {
    // Empty - audio loop runs in setup1()
  }

  void audioStart() {
    // Configure I2S with optimized buffer settings
    i2s.setBCLK(picoI2sPins[0]);
    i2s.setDOUT(picoI2sPins[1]);
    i2s.setBitsPerSample(32);
    i2s.setBuffers(8, 64);  // Larger buffers for complex DSP headroom (8*64=512 samples ~11.6ms)

    if (!i2s.begin(SAMPLE_RATE)) {
      Serial.println("I2S init failed!");
      while(1);
    }

    // Signal core 1 to start
    picoAudioRunning = true;

    Serial.println("M16 is running (Pico)");
  }
#endif


 /** change the default samplerate 
 * Typical rates for DACs are 96000, 88200, 48000, 44100, 32000, 16000, 8000
 * Put this prior to audioStart() and prior to any Osc.setPitch calls
 */
  void setSampleRate(int newRate) {
    SAMPLE_RATE = newRate;
    SAMPLE_RATE_INV = 1.0f / SAMPLE_RATE;
    #if IS_ESP32()
      if (tx_handle == NULL) {
        // Before audioStart() - update the config structure
        std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(newRate);
        Serial.printf("Sample rate configured to %d Hz (pre-start)\n", newRate);
      } else {
        // After audioStart() - dynamically reconfigure the running I2S
        i2s_channel_disable(tx_handle);
        i2s_std_clk_config_t new_clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(newRate);
        esp_err_t err = i2s_channel_reconfig_std_clock(tx_handle, &new_clk_cfg);
        if (err == ESP_OK) {
            Serial.printf("Sample rate updated to %d Hz (running)\n", newRate);
        } else {
            Serial.printf("Failed to update sample rate: %d\n", err);
        }
        i2s_channel_enable(tx_handle);
      }
    #elif IS_RP2040()
      // Pico: sample rate must be set before audioStart()
      Serial.print("Sample rate set to ");
      Serial.print(newRate);
      Serial.println(" Hz (call before audioStart)");
    #endif
  }

/** Return freq from a MIDI pitch 
* @pitch The MIDI pitch to be converted
*/
inline
float mtof(float midival) {
  midival = max(0.0f, midival);
  float f = 0.0;
  if (midival) f = 8.1757989156 * pow(2.0, midival * 0.083333); // / 12.0);
  return f;
}

/** Return a MIDI pitch from a frequency 
* @freq The frequency to be converted
*/
inline
float ftom(float freq) {
  return ( ( 12 * log(freq / 220.0) / log(2.0) ) + 57.01 );
}

/** Convert beats per minute to milliseconds per beat */
inline
float bpmToMs(float bpm) {
  return 60000.0f / bpm;
}

/** Return closest scale pitch to a given MIDI pitch
* @pitch MIDI pitch number
* @pitchClassSet an int array of chromatic values, 0-11, of size 12 (padded with zeros as required)
* @key pitch class key, 0-11, where 0 = C root
*/
inline
int pitchQuantize(int pitch, int8_t * pitchClassSet, int key) {
  // Build quick lookup table of allowed pitch classes
  bool allowed[12] = {false};
  for (int i = 0; i < 12; i++) {
    int pc = (pitchClassSet[i] + key) % 12;
    if (pc < 0) pc += 12;  // keep in range
    allowed[pc] = true;
  }
  int baseClass = (pitch % 12 + 12) % 12;
  if (allowed[baseClass]) return pitch;

  // Search outward up to 12 semitones
  for (int dist = 1; dist < 12; dist++) {
    int upClass   = (baseClass + dist) % 12;
    int downClass = (baseClass - dist + 12) % 12;
    if (allowed[downClass]) return pitch - dist;
    if (allowed[upClass])   return pitch + dist;
  }

  return pitch; // just in case?
}

// overload to use int scale values
inline
int pitchQuantize(int pitch, int * pitchClassSet, int key) {
  int8_t pc [12]; // empty
  for (int i=0; i<12; i++) {
    pc[i] = pitchClassSet[i];
  }
  int returnValue = pitchQuantize(pitch, pc, key);
  return returnValue;
}

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
  if (panVal == 0) return 1;
  if (panVal == 1) return 0;
  return max(0.0, min(1.0, cos(6.291 * panVal * 0.25)));
}

/** Return right amount for a pan position 0.0-1.0 */
float panRight(float panVal) {
  if (panVal == 0) return 0;
  if (panVal == 1) return 1;
  return max(0.0, min(1.0, cos(6.291 * (panVal * 0.25 + 0.75))));
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

/** Return cosine value based on step between -1.0 to 1.0 */
inline
float cosr(int step, int maxSteps = 16, float pulseDivision = 8) {
  return cos(((step % maxSteps) / pulseDivision) * 3.1459);
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

/** Constrain values to a 16bit range
* @input The value to be clipped
*/
int32_t clip16(int input) {
  if (abs(input) > MAX_16) {
    input = max(-MAX_16, min(MAX_16, input));
  }
  return input;
}

/** Clipping
*  Clip values outside max/min range
* @in_val Pass in a value to be clipped
* @min_val The minimum value to clip to
* @max_val The maximum value to clip to
*/
inline
int16_t clip(float in_val, float min_val, float max_val) {
  if (in_val > max_val) in_val = max_val;
  if (in_val < min_val) in_val = min_val;
  return in_val;
}

// Rand from Mozzi library
// static unsigned long randX=132456789, randY=362436069, randZ=521288629;
static unsigned long randX=random() * 1000000000, randY=random() * 1000000000, randZ=random() * 1000000000; // randomise seed

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
int rand(int32_t maxVal) {
  return (int) (((xorshift96() & 0xFFFF) * maxVal)>>16);
  // use MSB for increased randomness?, seems less evenly distributed
  // return (int)(((xorshift96() >> 8) * maxVal) >> 24); 
  // return (int)(xorshift96() % maxVal); // slighlty less event
}

// Gaussian approx for fixed tightness=3 (common case)
// Much faster: unrolled, no loop overhead
inline int gaussRand3(int maxVal) {
    return (rand(maxVal + 1) + rand(maxVal + 1) + rand(maxVal + 1)) / 3;
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
  return gaussRandNumb(maxVal, 3);
}

/** Approximate Chaotic Random number generator
* The output values will between 0 and range
* Range of 1 provides 2 attractor oscillation, other value provide more diversity
* Is very slow because of floating point calcs!
* @param range The largest value possible
* Algorithm by Roger Luebeck  2000, 2017
*  https://chaos-equations.com/index.htm
*/
float prevChaosRandVal = random(); //0.5;

float chaosRand(float range) {
  float chaosRandVal = range * sin(3.1459 * prevChaosRandVal);
  prevChaosRandVal = chaosRandVal;
  return chaosRandVal * 0.5 + range * 0.5;
}

  /**  === ISR-safe Xoshiro128** PRNG ===
  * Good low-bit randomness for audio applications
  * */
  static uint32_t s0 = 0x9E3779B9;
  static uint32_t s1 = 0x243F6A88;
  static uint32_t s2 = 0xB7E15162;
  static uint32_t s3 = 0xC0DEC0DE;

  // Rotate left helper
  inline uint32_t rotl(const uint32_t x, int k) {
      return (x << k) | (x >> (32 - k));
  }

  // Core generator: xoshiro128**
  inline uint32_t audioRand32() {
      const uint32_t result = rotl(s1 * 5, 7) * 9; // strong low-bit mix

      const uint32_t t = s1 << 9;
      s2 ^= s0;
      s3 ^= s1;
      s1 ^= s2;
      s0 ^= s3;

      s2 ^= t;
      s3 = rotl(s3, 11);

      return result;
  }

  // Uniform int in [0, maxVal)
  inline int audioRand(int32_t maxVal) {
      // Use top 24 bits for better low-value uniformity
      // return (int)((audioRand32() >> 8) * (uint32_t)maxVal >> 24);
    if (maxVal <= 0) return 0;
    return (int)(((uint64_t)(audioRand32() >> 8) * (uint64_t)maxVal) >> 24);
  }

  // Approx Gaussian
  inline int audioRandGauss(int maxVal, int tightness) {
      int sum = 0;
      for (int i = 0; i < tightness; i++) {
          sum += audioRand(maxVal + 1);
      }
      return sum / tightness;
  }

  // Portable seed function (no hardware RNG)
  inline void audioRandSeed(uint32_t seed) {
      if (seed == 0) {
          // Use micros() + a simple LCG scramble for variety
          uint32_t t = (uint32_t)micros();
          seed = t ^ 0xA5A5A5A5UL;
      }
      // SplitMix32 seeding — ensures all states are non-zero
      auto splitmix32 = [](uint32_t &x) {
          uint32_t z = (x += 0x9E3779B9UL);
          z = (z ^ (z >> 16)) * 0x85EBCA6BUL;
          z = (z ^ (z >> 13)) * 0xC2B2AE35UL;
          return z ^ (z >> 16);
      };

      s0 = splitmix32(seed);
      s1 = splitmix32(seed);
      s2 = splitmix32(seed);
      s3 = splitmix32(seed);
  }

// /* M16_H_ */
