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

#ifndef M16_H_
#define M16_H_

#include "Arduino.h"

// based on "Hardware_defines.h" in Mozzi
#define IS_ESP8266() (defined(ESP8266))
#define IS_ESP32() (defined(ESP32))
#define IS_ESP32S2() (defined(CONFIG_IDF_TARGET_ESP32S2))
#define IS_ESP32C3() (defined(CONFIG_IDF_TARGET_ESP32C3))
#define IS_RP2040() (defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2035) || defined(ARDUINO_ARCH_RP2350))
// IS_CAPABLE() groups platforms with sufficient CPU/memory for complex DSP (filters, reverb, etc.)
#define IS_CAPABLE() (IS_ESP32() || IS_RP2040())

/* Thread-safety helpers for explicitly enabled dual-core ESP32 rendering.
* ESP32 defaults to one dedicated audio task. After setIsDualCore(true),
* audioUpdate() runs on BOTH cores simultaneously and shared state modified in
* audioUpdate() needs protection or explicit voice partitioning.
* Only necessary when sample accurate triggering of a new audio event (note) 
* is required from within audioUpdate.
*
* Use M16_ATOMIC_GUARD for non-blocking protection of critical sections:
*
*   #include <atomic>
*   std::atomic<bool> myLock{false};
*
* Pseudo-structure:
*  void audioUpdate() {
*     // Unguarded: Both cores do DSP
*     int32_t mix = computeAllVoices();
*
*    // Guarded: Only one core advances sequencer
*     M16_ATOMIC_GUARD(seqLock, {
*        sharedCounter++;
*        triggerNextGrain();
*     });
*
*     // Unguarded: Both cores write samples
*     audioBlockWrite(mix, mix);
* }
*
* Note: The losing core SKIPS the guarded code (doesn't wait). Use this for
* operations that should only happen once per audio frame, like triggering
* grains or advancing sequencer positions.
*
* Pattern for event-driven actions (e.g., triggering a new grain when playback ends):
* The "handled" flag lets you take action OUTSIDE the lock only on the core that won.
*
*   bool handled = false;
*   M16_ATOMIC_GUARD(myLock, {
*       if (someCondition) {
*           doOncePerEvent();
*           handled = true;
*       }
*   });
*   if (handled) {
*       // Only runs on the winning core
*       doFollowUpWork();
*   }
*   // Code here runs on BOTH cores
*
* Common pitfalls:
* - 64-bit variables (like phase accumulators) require their own spinlock for
*   atomic read/write on 32-bit processors. See Samp.h for an example.
* - Variables accessed by both cores should be declared 'volatile' to ensure
*   visibility of changes across cores.
* - Non-ISR-safe functions (rand(), Serial.print()) should not be called in
*   audioUpdate(). Use audioRand() and buffer debug output for later.
*/

#if IS_ESP32() || IS_RP2040()
  #include <atomic>

  // Non-blocking atomic guard - executes code only if lock is acquired
  // If another core holds the lock, this core skips the code block
  #define M16_ATOMIC_GUARD(lock, code) \
    do { \
      bool _m16_expected = false; \
      if ((lock).compare_exchange_strong(_m16_expected, true, std::memory_order_acquire)) { \
        code; \
        (lock).store(false, std::memory_order_release); \
      } \
    } while(0)

  // Blocking atomic guard - spins until lock is acquired (use sparingly!)
  // WARNING: Can cause audio glitches if held too long
  #define M16_ATOMIC_GUARD_BLOCKING(lock, code) \
    do { \
      bool _m16_expected = false; \
      while (!(lock).compare_exchange_weak(_m16_expected, true, std::memory_order_acquire)) { \
        _m16_expected = false; \
      } \
      code; \
      (lock).store(false, std::memory_order_release); \
    } while(0)

#else
  // Single-core platforms (ESP8266) - no locking needed
  #define M16_ATOMIC_GUARD(lock, code) do { code; } while(0)
  #define M16_ATOMIC_GUARD_BLOCKING(lock, code) do { code; } while(0)
#endif


// globals
int SAMPLE_RATE = 44100;
float SAMPLE_RATE_INV = 1.0f / SAMPLE_RATE;
#define MAX_16 32767
#define MIN_16 -32767
const float MAX_16_INV = 0.00003052;
// ESP32 defaults to one dedicated audio task. This is deterministic for the
// ordinary shared/stateful DSP graph and leaves the other core for loop(), UI,
// MIDI, USB, and system work. Independent voice arrays can explicitly opt into
// block-partitioned dual-core rendering with setIsDualCore(true).
// Pico-family boards use the same conservative default. Polyphonic sketches
// explicitly opt into block jobs with setIsDualCore(true): Pico 2 uses an
// automatic doorbell worker, while Pico uses cooperative audioLoop() service.
#if IS_ESP32() || IS_RP2040()
bool isDualCore = false;
#else
bool isDualCore = true;
#endif

// ---- Global audio-frame clock --------------------------------------------
// Monotonic counter incremented once per output frame produced (advanced inside
// the per-frame write paths below: i2s_write_samples and the audioBlockWrite
// finaliser). Unlike micros(), it does NOT freeze while the audio task fills the
// DMA ring in a burst; unlike a per-next()-call counter it makes no assumption
// about how often a generator is ticked — it tracks real audio output. Time-based
// generators (e.g. Env) read it via audioFrameCount() so their slopes are smooth
// at audio rate and correctly timed whether advanced at control rate (loop()) or
// per sample (audioUpdate()). Wraps after ~27 h at 44.1 kHz; consumers must use
// unsigned deltas. On dual-core external I2S both cores advance the shared atomic
// for alternate frames, so the combined rate equals the output frame rate.
#if IS_RP2040()
#include "pico/multicore.h"
std::atomic<uint32_t> _m16AudioFrameCount{0};
static std::atomic<uint32_t> _m16RenderFrame[2]{{0}, {0}};
static std::atomic<bool> _m16RenderFrameActive[2]{{false}, {false}};

// During partitioned block rendering each core evaluates time-based generators
// against the same logical frame range. Outside that render context callers see
// the committed output-frame clock.
inline uint32_t audioFrameCount() {
  int core = get_core_num();
  if (_m16RenderFrameActive[core].load(std::memory_order_acquire)) {
    return _m16RenderFrame[core].load(std::memory_order_relaxed);
  }
  return _m16AudioFrameCount.load(std::memory_order_relaxed);
}
inline void m16AdvanceAudioFrame() {
  _m16AudioFrameCount.fetch_add(1, std::memory_order_relaxed);
}
inline void m16BeginRenderFrameContext(uint32_t frame) {
  int core = get_core_num();
  _m16RenderFrame[core].store(frame, std::memory_order_relaxed);
  _m16RenderFrameActive[core].store(true, std::memory_order_release);
}
inline void m16SetRenderFrame(uint32_t frame) {
  _m16RenderFrame[get_core_num()].store(frame, std::memory_order_relaxed);
}
inline void m16EndRenderFrameContext() {
  _m16RenderFrameActive[get_core_num()].store(false, std::memory_order_release);
}
#elif defined(ESP32) || defined(ESP_PLATFORM)
std::atomic<uint32_t> _m16AudioFrameCount{0};
inline uint32_t audioFrameCount() { return _m16AudioFrameCount.load(std::memory_order_relaxed); }
inline void m16AdvanceAudioFrame() { _m16AudioFrameCount.fetch_add(1, std::memory_order_relaxed); }
#else
volatile uint32_t _m16AudioFrameCount = 0;
inline uint32_t audioFrameCount() { return _m16AudioFrameCount; }
inline void m16AdvanceAudioFrame() { _m16AudioFrameCount++; }
#endif

// Sample reorder buffer (ESP32 dual-core external I2S only)
// When dual-core audio production writes to a shared I2S DMA, the order in which
// the two cores reach i2s_channel_write is non-deterministic — adjacent samples
// can be swapped, producing audible discontinuities under high-modulation FM.
// When enabled, inserts an MPSC ring buffer + single drainer task that restores
// deterministic sample order at the DAC. Only active when isDualCore=true and
// external I2S is in use; all other paths (ESP8266, RP2040, internal DAC,
// single-core mode) are unaffected. Costs ~220 B BSS, ~680 B flash on ESP32.
// Disabled by default: M16_ATOMIC_GUARD + output caching handles ordering
// correctly for shared-state FX, and the drainer task risks WDT starvation.
// Enable with `#define M16_REORDER_BUFFER_ENABLE 1` BEFORE `#include "M16.h"`.
#ifndef M16_REORDER_BUFFER_ENABLE
  #define M16_REORDER_BUFFER_ENABLE 0
#endif
#ifndef M16_REORDER_RING_SIZE
  #define M16_REORDER_RING_SIZE 16   // power-of-2; absorbs cross-core skew
#endif
#define M16_REORDER_RING_MASK (M16_REORDER_RING_SIZE - 1)

// Number of samples per dual-core block. Both cores fill their partial buffers
// independently; Core 0 combines and writes to DMA once per block.
// Synchronisation overhead: 2 FreeRTOS events / block = 1.4 % CPU at N=32.
// Define before #include "M16.h" to override. Power-of-2 recommended.
#ifndef M16_BLOCK_SIZE
  #define M16_BLOCK_SIZE 32
#endif

// Maximum time Core 0 waits for Core 1 at a block rendezvous, expressed as
// audio-block periods. Four blocks tolerates ordinary scheduler skew without
// allowing a stalled partition to drain the I2S DMA reserve. The calculated
// deadline is clamped to 1-10 ms for unusually small blocks/sample rates.
#ifndef M16_BLOCK_SYNC_TIMEOUT_BLOCKS
  #define M16_BLOCK_SYNC_TIMEOUT_BLOCKS 4
#endif

// A healthy, filled DMA ring makes block writes wait for approximately one
// block period. If many writes return almost immediately, output is underfilled
// and the maximum-priority audio task may never block long enough for IDLE0 to
// service the task watchdog. Force one scheduler tick of sleep after this many
// consecutive fast writes. Define 0 to disable the safeguard.
#ifndef M16_DMA_FAST_WRITE_YIELD_BLOCKS
  #define M16_DMA_FAST_WRITE_YIELD_BLOCKS 32
#endif

// Core 1 normally blocks while waiting for a reusable partition slot. If it is
// consistently a little slower than Core 0, however, every slot may already be
// free and the maximum-priority producer can run forever without allowing
// IDLE1, loop(), or TinyUSB to execute. Force one tick of sleep after this many
// consecutive producer blocks that received no natural semaphore pacing.
#ifndef M16_PRODUCER_UNPACED_YIELD_BLOCKS
  #define M16_PRODUCER_UNPACED_YIELD_BLOCKS 32
#endif

// Original ESP32 internal DAC is only 8-bit. TPDF dither can make quiet
// delay/reverb tails less steppy, but it adds a fixed one-DAC-LSB noise floor
// after all gain/effects, which can read as crackle on low-level repeats.
#ifndef M16_INTERNAL_DAC_DITHER_ENABLE
  #define M16_INTERNAL_DAC_DITHER_ENABLE 1
#endif

// Set to 1 to use DAC_CHANNEL_MODE_SIMUL: both DAC channels update simultaneously
// from a mono mix of L+R. Eliminates inter-channel coupling crackle at the cost
// of stereo output. Recommended for internal DAC use. Default 0 (ALTER) for
// backward compatibility.
#ifndef M16_INTERNAL_DAC_SIMUL
  #define M16_INTERNAL_DAC_SIMUL 1
#endif

// Noise gate threshold for internal DAC output (16-bit scale, 0 = disabled).
// Asymmetric envelope follower silences the output when the signal drops to this
// level, preventing 8-bit quantisation noise from becoming audible in quiet tails.
// Default 3000 (~2.5 DAC LSBs). Override before #include "M16.h" to tune or disable.
#ifndef M16_INTERNAL_DAC_GATE_THRESHOLD
  #define M16_INTERNAL_DAC_GATE_THRESHOLD 3000
#endif

/** Specify the use of one or two cores for audio processing.
* ESP32 defaults to false (one dedicated audio core). Set true only when
* audioUpdate() partitions independent voice state with audioPartitionOffset()
* and audioPartitionStride(). Pico-family boards use the same dedicated-core
* default. Pico 2 adds Core 0 automatically through bounded doorbell jobs;
* Pico adds it cooperatively through audioLoop().
* @dualCore True to use both cores, false for dedicated single-audio-core mode
* Call this function before audioStart() in setUp() to set the desired number of cores used for audio.
* ESP32 uses FreeRTOS tasks; Pico 2 uses an interrupt-driven block worker.
* Retaining audioLoop() in Pico-family sketches provides Pico compatibility.
*/
void setIsDualCore(bool dualCore) { 
  isDualCore = dualCore;
}

// TABLE_SIZE can be overridden by defining it in your sketch BEFORE including M16.h
// Example: #define TABLE_SIZE 2048
#ifndef TABLE_SIZE
  #if IS_ESP8266()
    #define TABLE_SIZE 1024  // Smaller default for ESP8266 due to limited RAM (~50KB heap)
  #else
    #define TABLE_SIZE 4096  // 2048 // 4096 // 8192 // 16384 // 32768 // 65536
  #endif
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
static size_t g_psramTotal = 0;

/** Check if PSRAM is available and functional
 *  Performs comprehensive diagnostics on first call
 *  @return true if PSRAM is available and usable
 */
inline bool isPSRAMAvailable() {
  if (!g_psramChecked) {
    #if IS_ESP32()
      bool hwDetected = psramFound();
      size_t freePsram = ESP.getFreePsram();
      g_psramTotal = freePsram;

      // PSRAM is only truly available if both detected AND has free space
      g_psramAvailable = hwDetected && (freePsram > 0);

      Serial.println("--- M16 PSRAM Diagnostics ---");
      Serial.print("  Hardware detected: ");
      Serial.println(hwDetected ? "Yes" : "No");
      Serial.print("  Free PSRAM: ");
      Serial.print(freePsram / 1024);
      Serial.println(" KB");

      if (hwDetected && freePsram == 0) {
        Serial.println("  WARNING: PSRAM detected but not usable!");
        Serial.println("  Check Arduino IDE board settings:");
        Serial.println("  - PSRAM type (QSPI vs OPI) must match your chip");
        Serial.println("  - Ensure psramInit() is called before M16 allocations");
      } else if (g_psramAvailable) {
        Serial.println("  Status: PSRAM ready for use");
      } else {
        Serial.println("  Status: No PSRAM available");
      }
      Serial.println("-----------------------------");
    #endif
    g_psramChecked = true;
  }
  return g_psramAvailable;
}

/** Get current free PSRAM in bytes
 *  @return Free PSRAM bytes, or 0 if not available
 */
inline size_t getFreePSRAM() {
  #if IS_ESP32()
    return ESP.getFreePsram();
  #else
    return 0;
  #endif
}

/** Safely allocate memory from PSRAM with size checking
 *  @param size Number of bytes to allocate
 *  @param description Name/description for debug output (can be nullptr to suppress)
 *  @return Pointer to allocated memory, or nullptr if failed
 */
inline void* psramAllocSafe(size_t size, const char* description = nullptr) {
  #if IS_ESP32()
    if (!isPSRAMAvailable()) {
      if (description) {
        Serial.print("PSRAM alloc failed (unavailable): ");
        Serial.println(description);
      }
      return nullptr;
    }

    size_t available = ESP.getFreePsram();
    // Require 10% headroom to avoid fragmentation issues
    size_t required = size + (size / 10);

    if (available < required) {
      if (description) {
        Serial.print("PSRAM alloc failed (insufficient): ");
        Serial.print(description);
        Serial.print(" needs ");
        Serial.print(size / 1024);
        Serial.print("KB, only ");
        Serial.print(available / 1024);
        Serial.println("KB free");
      }
      return nullptr;
    }

    void* ptr = ps_calloc(size, 1);
    if (!ptr) {
      if (description) {
        Serial.print("PSRAM alloc failed (ps_calloc): ");
        Serial.println(description);
      }
      return nullptr;
    }

    if (description) {
      Serial.print("PSRAM allocated: ");
      Serial.print(description);
      Serial.print(" (");
      Serial.print(size / 1024);
      Serial.print("KB, ");
      Serial.print(ESP.getFreePsram() / 1024);
      Serial.println("KB remaining)");
    }
    return ptr;
  #else
    return nullptr;
  #endif
}

/** Safely allocate int16_t array from PSRAM with size checking
 *  @param count Number of int16_t elements to allocate
 *  @param description Name/description for debug output (can be nullptr to suppress)
 *  @return Pointer to allocated array, or nullptr if failed
 */
inline int16_t* psramAllocInt16(size_t count, const char* description = nullptr) {
  return (int16_t*)psramAllocSafe(count * sizeof(int16_t), description);
}

// Forward declaration — clip16 is defined at global scope after the platform blocks.
// Needed so platform-specific functions (e.g. audioBlockWrite) can call it.
int32_t clip16(int input);

// Master processing hook shared by every block-output platform. In partitioned
// mode the finalizer invokes it exactly once after combining both partial mixes.
typedef void (*AudioPostProcessCallback)(int32_t& left, int32_t& right);
static AudioPostProcessCallback _audioPostProcessCallback = nullptr;

inline void setAudioPostProcessCallback(AudioPostProcessCallback callback) {
  _audioPostProcessCallback = callback;
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

  void _i2s_write_samples_platform(int16_t leftSample, int16_t rightSample) {
    leftAudioOuputValue = leftSample;
    rightAudioOuputValue = rightSample;
    i2s_write_lr(leftSample, rightSample);
    m16AdvanceAudioFrame(); // one output frame produced
  }

  void seti2sPins(int bck, int ws, int dout, int din) {
    Serial.println("seti2sPins() is not availible for the ESP8266 which has fixed i2s pins");
  } // ignored for ESP8266

  /** No-op on ESP8266 - included for API compatibility with Pico dual-core mode */
  inline void audioLoop() {}

  // Block-split API stubs — ESP8266 is always single-core.
  // Sketches using audioPartitionOffset/Stride/audioBlockWrite compile unchanged.
  inline int  audioPartitionOffset()  { return 0; }
  inline int  audioPartitionStride()  { return 1; }
  inline bool audioPartitionIsActive() { return false; }
  inline bool audioIsFinalizerCore()  { return true; }
  bool audioBlockWrite(int32_t L, int32_t R);

  [[deprecated("Use audioBlockWrite(left, right); direct legacy I2S writes are single-core only")]]
  void i2s_write_samples(int16_t leftSample, int16_t rightSample) {
    audioBlockWrite(leftSample, rightSample);
  }

  inline bool audioBlockWrite(int32_t L, int32_t R) {
    if (_audioPostProcessCallback != nullptr) {
      _audioPostProcessCallback(L, R);
    }
    _i2s_write_samples_platform((int16_t)clip16(L), (int16_t)clip16(R));
    return true;
  }
#elif IS_ESP32()
  // i2s
  #include <driver/i2s_std.h>

  // Internal DAC support (ESP32 and ESP32-S2 only)
  // SOC_DAC_SUPPORTED is defined by ESP-IDF for chips with internal DAC
  #if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
    #include <driver/dac_continuous.h>
  #endif

  bool _useInternalDAC = false;

  /** Enable internal DAC output instead of external I2S DAC.
   *  Call before audioStart() in setup().
   *  Only available on ESP32 (GPIO25/26) and ESP32-S2 (GPIO17/18).
   *  Output is 8-bit (reduced from 16-bit internally).
   *  On chips without internal DAC (S3, C3, etc.) this call is ignored.
   *  Note: 8-bit resolution may produce audible quantization noise with
   *  effects that have wide dynamic range (e.g. reverb). For best quality,
   *  use an external I2S DAC.
   */
  void useInternalDAC() {
    #if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
      _useInternalDAC = true;
      Serial.println("Internal DAC mode enabled (8-bit output)");
    #else
      Serial.println("Warning: This chip has no internal DAC, using external I2S DAC");
    #endif
  }

  static const i2s_port_t i2s_num = I2S_NUM_0;
  int i2sPinsOut [] = {16, 17, 18, 21}; // bck, ws, dout, din

  i2s_chan_handle_t tx_handle = NULL;
  i2s_chan_handle_t rx_handle = NULL;

  // Internal DAC handle and buffering
  #if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
    dac_continuous_handle_t _dac_handle = NULL;

    // Buffered output for internal DAC
    // 256 bytes = 128 stereo frames per flush
    #define DAC_ACCUM_SIZE 256
    #define DAC_DESC_NUM 8        // DMA descriptors (8 × 256 = ~23ms buffer at 44.1kHz)
    static uint8_t _dacAccum[DAC_ACCUM_SIZE];
    static size_t _dacAccumPos = 0;
    static uint32_t _ditherState = 22695477UL;  // LCG state for TPDF dither
    static int32_t  _gateLevel   = 0;           // envelope follower for noise gate
  #endif

  // Configuration macros/constants
  // DMA_BUFFERS × DMA_BUFFER_LENGTH frames at SAMPLE_RATE sets total ring depth.
  // Default 4 × 512 = 46 ms total ring depth at 44.1 kHz. Some of that ring is
  // normally occupied, so it must not be treated as a permissible render stall.
  // Sketches can override these values before including M16.h when they prefer
  // more stall tolerance or lower latency.
  #ifndef DMA_BUFFERS
    #define DMA_BUFFERS       4
  #endif
  #ifndef DMA_BUFFER_LENGTH
    #define DMA_BUFFER_LENGTH 512
  #endif

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
  static volatile bool _audioTasksRunning = false;

  void audioCallback(void* param) {
      while (!_audioTasksRunning) {
          vTaskDelay(1);
      }
      for (;;) {
          audioUpdate();
          // No yield() here — i2s_channel_write() and dac_continuous_write()
          // block on FreeRTOS semaphores when DMA is full, naturally yielding
          // to all lower-priority tasks including loop() and IDLE (watchdog).
          // Per-sample yield() adds ~2μs overhead (4-9% CPU) for no benefit,
          // which can push heavy DSP (reverb) past the sample deadline.
      }
  }

  // Output smoothing state
  static int32_t _prevOutL = 0;
  static int32_t _prevOutR = 0;

#if M16_REORDER_BUFFER_ENABLE
  // ---- Sample reorder buffer (MPSC) ----
  // Producers (audio tasks on cores 0 & 1) deposit completed samples into ring
  // slots indexed by a monotonically-increasing seq#. A single drainer task
  // reads slots in seq order and feeds i2s_channel_write(). The seq# is also
  // the per-slot ready flag: 0 = empty/consumed, non-zero = filled with that
  // seq#. uint32_t is used because Xtensa LX6/LX7 lack native lock-free 64-bit
  // atomics — counter wraps after 2^32 ≈ 13.5 hours at 88 kHz claim rate;
  // acceptable for testing, address with separate ready flag in a follow-up if
  // long-run stability requires it.
  struct ReorderSlot {
    int16_t l;
    int16_t r;
    std::atomic<uint32_t> ready;  // 0 = empty/consumed; otherwise = seq#
  };

  static ReorderSlot _reorderRing[M16_REORDER_RING_SIZE];
  static std::atomic<uint32_t> _reorderNextSlotToProduce{1};  // 1-indexed
  static uint32_t _reorderNextSlotToConsume = 1;              // drainer-only
  static TaskHandle_t _reorderDrainerHandle = NULL;
  static volatile bool _reorderActive = false;                // gates runtime branch
  static SemaphoreHandle_t _reorderSlotSem    = NULL;  // counting sem; tokens = free ring slots
  static SemaphoreHandle_t _reorderSlotFilled = NULL;  // counting sem; tokens = slots awaiting drain
  // Per-core pre-claimed seq# (0 = none). Set inside Samp spinlock to atomically
  // tie phase order to reorder sequence, eliminating the lock-release/seq-claim race.
  static uint32_t _preClaimedSeq[2];

  // Drainer task: reads ring in seq order, writes one stereo frame per
  // i2s_channel_write call. The blocking write paces the loop at SAMPLE_RATE.
  static void reorderDrainerTask(void* /*unused*/) {
    // Batch buffer: accumulate M16_BLOCK_SIZE frames before each DMA write.
    // Per-sample writes (4 bytes) return instantly when DMA has space, so the
    // drainer never blocks and IDLE0 is starved when audio renders faster than
    // real-time (e.g. half-rate reverb). Batching makes each write large enough
    // to reliably block on DMA-full, giving IDLE0 time to reset the watchdog.
    static uint8_t batchBuf[M16_BLOCK_SIZE * 4];
    static int batchPos = 0;
    for (;;) {
      // Block until a producer signals a filled slot — allows IDLE0 to run,
      // preventing WDT starvation on CPU 0 when the ring is empty.
      xSemaphoreTake(_reorderSlotFilled, portMAX_DELAY);

      uint32_t idx = (_reorderNextSlotToConsume & M16_REORDER_RING_MASK);

      // Wait for the next-expected slot to be marked ready.
      // Acquire pairs with producer's release-store of seq#.
      // In split-core mode this spin is bounded: the semaphore was already given,
      // so the store is imminent. In practice this loop body runs 0–1 times.
      while (_reorderRing[idx].ready.load(std::memory_order_acquire) != _reorderNextSlotToConsume) {
        taskYIELD();
      }

      int16_t l = _reorderRing[idx].l;
      int16_t r = _reorderRing[idx].r;

      // Mark consumed. Release pairs with producer's acquire on next wraparound.
      _reorderRing[idx].ready.store(0, std::memory_order_release);
      _reorderNextSlotToConsume++;
      xSemaphoreGive(_reorderSlotSem);  // signal free slot — unblocks waiting producer

      // Byte order matches legacy i2s_write_samples external-I2S path.
      batchBuf[batchPos * 4 + 0] = r & 0xFF;
      batchBuf[batchPos * 4 + 1] = (r >> 8) & 0xFF;
      batchBuf[batchPos * 4 + 2] = l & 0xFF;
      batchBuf[batchPos * 4 + 3] = (l >> 8) & 0xFF;
      batchPos++;

      if (batchPos >= M16_BLOCK_SIZE) {
        batchPos = 0;
        size_t bytesWritten = 0;
        i2s_channel_write(tx_handle, batchBuf, sizeof(batchBuf), &bytesWritten, portMAX_DELAY);
      }
    }
  }
  // Called from Samp::next()/nextStereo() while SAMP_LOCK is held.
  // Pre-claims the next reorder sequence number so it is assigned in phase
  // order (matching the just-advanced phase), not in the non-deterministic
  // order in which cores reach i2s_write_samples() after releasing the lock.
  // Must be followed by i2s_write_samples() on the same core; no other call
  // to m16_claimReorderSeq() may intervene on this core before that write.
  inline void m16_claimReorderSeq() {
    if (!_reorderActive) return;
    xSemaphoreTake(_reorderSlotSem, portMAX_DELAY);
    _preClaimedSeq[xPortGetCoreID()] =
        _reorderNextSlotToProduce.fetch_add(1, std::memory_order_relaxed);
  }
#endif // M16_REORDER_BUFFER_ENABLE

  bool _i2s_write_samples_direct_external(int16_t leftSample, int16_t rightSample) {
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

  // ---- Block-based dual-core voice partitioning ----
  // Core 0 owns even-indexed voices, Core 1 owns odd-indexed voices.
  // Each core accumulates N=M16_BLOCK_SIZE samples into its partial buffer;
  // Core 0 combines both and writes one DMA burst per block.
  static volatile bool _blockSplitActive = false;
  // Two slots let Core 1 render block N+1 while Core 0 combines, processes,
  // and writes block N. Sequence tags prevent stale task notifications from
  // being mistaken for the readiness of a reused slot.
  static int32_t _blockPartialL[2][2][M16_BLOCK_SIZE] __attribute__((aligned(32)));
  static int32_t _blockPartialR[2][2][M16_BLOCK_SIZE] __attribute__((aligned(32)));
  static int _blockPos[2];
  static uint32_t _blockSequence[2] = {0, 0};
  static std::atomic<uint32_t> _blockReadySequence[2]{{0}, {0}};
  static std::atomic<uint32_t> _blockConsumedSequence[2]{{0}, {0}};
  static volatile uint32_t _audioBlockSyncTimeouts = 0;
  static volatile uint32_t _audioBlockConsecutiveSyncTimeouts = 0;
  static volatile uint32_t _audioBlockMaxConsecutiveSyncTimeouts = 0;
  static volatile uint32_t _audioBlockLateProducerRecoveries = 0;
  static volatile uint32_t _audioProducerStarvationYields = 0;
  static volatile uint32_t _audioDmaWriteTimeouts = 0;
  static volatile uint32_t _audioDmaStarvationYields = 0;
  static uint16_t _audioConsecutiveFastDmaWrites = 0;
  static uint16_t _audioConsecutiveUnpacedProducerBlocks = 0;
  static bool _audioProducerBlockWasPaced = false;

  // The global post-process hook runs here on Core 0 after both ESP32 voice
  // partitions have been combined.

  inline uint32_t audioBlockSyncTimeoutCount() {
    return _audioBlockSyncTimeouts;
  }

  inline uint32_t audioBlockConsecutiveSyncTimeoutCount() {
    return _audioBlockConsecutiveSyncTimeouts;
  }

  inline uint32_t audioBlockMaxConsecutiveSyncTimeoutCount() {
    return _audioBlockMaxConsecutiveSyncTimeouts;
  }

  /** Number of times Core 1 found that Core 0 had already advanced beyond the
   * exact slot generation it expected. Such overload is recovered rather than
   * becoming a permanent wait for a sequence value that cannot recur. */
  inline uint32_t audioBlockLateProducerRecoveryCount() {
    return _audioBlockLateProducerRecoveries;
  }

  /** Number of times an unpaced maximum-priority Core 1 producer was briefly
   * blocked so IDLE1, loop(), and USB tasks could run. */
  inline uint32_t audioProducerStarvationYieldCount() {
    return _audioProducerStarvationYields;
  }

  inline uint32_t audioDmaWriteTimeoutCount() {
    return _audioDmaWriteTimeouts;
  }

  inline uint32_t audioDmaStarvationYieldCount() {
    return _audioDmaStarvationYields;
  }

  // Platform writer used by audioBlockWrite().  The public i2s_write_samples()
  // API below is retained only as a single-core compatibility shim.
  bool _i2s_write_samples_platform(int16_t leftSample, int16_t rightSample) {
    // One output frame produced per call on every path below (external direct,
    // internal DAC, reorder-enqueue). On dual-core external both cores call this
    // for alternate frames, so the shared atomic advances at the frame rate.
    m16AdvanceAudioFrame();
    #if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
      if (_useInternalDAC) {
#if M16_INTERNAL_DAC_GATE_THRESHOLD > 0
        {
          // Asymmetric envelope follower: fast attack (>> 4 ≈ 0.4ms) opens on transients,
          // slow release (>> 12 ≈ 93ms) holds through brief gaps. Gates output to zero
          // when level drops below the quantisation noise floor of the 8-bit DAC.
          int32_t absVal = abs((int32_t)leftSample);
          _gateLevel += absVal > _gateLevel ? (absVal - _gateLevel) >> 4
                                            : (absVal - _gateLevel) >> 10; // 12
          if (_gateLevel < M16_INTERNAL_DAC_GATE_THRESHOLD) { leftSample = 0; rightSample = 0; }
        }
#endif
        // Convert 16-bit signed to 8-bit unsigned for internal DAC.
        // Apply TPDF dithering to any non-trivial signal so delay/reverb tails fade smoothly
        // rather than hard-clipping to silence at the ±1 LSB boundary (which causes rhythmic clicks).
        // Gate only at ±32 (1/8 DAC step) — below that the signal is truly inaudible.
#if M16_INTERNAL_DAC_SIMUL
        {
          int32_t mono = ((int32_t)leftSample + (int32_t)rightSample) >> 1;
          if (M16_INTERNAL_DAC_DITHER_ENABLE && (mono > 32 || mono < -32)) {
            _ditherState = _ditherState * 1664525UL + 1013904223UL;
            int32_t d = (int32_t)((int8_t)(_ditherState >> 24)) + (int32_t)((int8_t)(_ditherState >> 16));
            int32_t val = mono + 32768 + d;
            if (val < 0) val = 0; else if (val > 65535) val = 65535;
            _dacAccum[_dacAccumPos++] = (uint8_t)(val >> 8);
          } else {
            _dacAccum[_dacAccumPos++] = (uint8_t)((mono + 32768) >> 8);
          }
        }
#else
        if (M16_INTERNAL_DAC_DITHER_ENABLE &&
            (leftSample > 32 || leftSample < -32 || rightSample > 32 || rightSample < -32)) {
          _ditherState = _ditherState * 1664525UL + 1013904223UL;
          int32_t dL = (int32_t)((int8_t)(_ditherState >> 24)) + (int32_t)((int8_t)(_ditherState >> 16));
          int32_t dR = (int32_t)((int8_t)(_ditherState >> 8))  + (int32_t)((int8_t)(_ditherState >> 0));
          int32_t valL = (int32_t)leftSample  + 32768 + dL;
          int32_t valR = (int32_t)rightSample + 32768 + dR;
          if (valL < 0) valL = 0; else if (valL > 65535) valL = 65535;
          if (valR < 0) valR = 0; else if (valR > 65535) valR = 65535;
          _dacAccum[_dacAccumPos++] = (uint8_t)(valL >> 8);
          _dacAccum[_dacAccumPos++] = (uint8_t)(valR >> 8);
        } else {
          _dacAccum[_dacAccumPos++] = (uint8_t)((leftSample  + 32768) >> 8);
          _dacAccum[_dacAccumPos++] = (uint8_t)((rightSample + 32768) >> 8);
        }
#endif

        // Flush when buffer is full
        if (_dacAccumPos >= DAC_ACCUM_SIZE) {
          size_t bytesLoaded = 0;
          size_t remaining = DAC_ACCUM_SIZE;
          uint8_t* ptr = _dacAccum;
          // Retry on partial writes to prevent dropped samples at DMA seams
          while (remaining > 0) {
            dac_continuous_write(_dac_handle, ptr, remaining, &bytesLoaded, portMAX_DELAY);
            ptr += bytesLoaded;
            remaining -= bytesLoaded;
          }
          _dacAccumPos = 0;
        }
        return true;
      }
    #endif

      // External I2S DAC path (16-bit)
#if M16_REORDER_BUFFER_ENABLE
      // When the drainer is active (dual-core external I2S path), enqueue to
      // the ring instead of writing direct. The drainer feeds DMA in seq order.
      if (_reorderActive) {
        // If a seq# was pre-claimed inside Samp's spinlock, use it directly
        // (semaphore already taken). Otherwise claim normally.
        uint32_t mySeq;
        int _core = xPortGetCoreID();
        if (_preClaimedSeq[_core]) {
          mySeq = _preClaimedSeq[_core];
          _preClaimedSeq[_core] = 0;
        } else {
          // Take BEFORE fetch_add — keeps in_flight ≤ RING_SIZE, guaranteeing
          // the slot we claim (seq & MASK) cannot still hold unexpired data.
          // Genuine block allows IDLE0 to run, preventing WDT starvation on CPU 0.
          xSemaphoreTake(_reorderSlotSem, portMAX_DELAY);
          mySeq = _reorderNextSlotToProduce.fetch_add(1, std::memory_order_relaxed);
        }
        uint32_t idx = (mySeq & M16_REORDER_RING_MASK);

        _reorderRing[idx].l = leftSample;
        _reorderRing[idx].r = rightSample;

        // Publish: data writes happen-before the seq# becomes visible.
        _reorderRing[idx].ready.store(mySeq, std::memory_order_release);
        xSemaphoreGive(_reorderSlotFilled);  // wake drainer
        return true;
      }
#endif

      return _i2s_write_samples_direct_external(leftSample, rightSample);
  }

  /**
   * Legacy compatibility wrapper.
   *
   * Direct per-frame writes from two ESP32 audio tasks are intentionally no
   * longer supported.  Migrate sketches to audioBlockWrite(); in dedicated
   * single-core mode this wrapper forwards there so old sketches continue to
   * run while producing the same block-scheduled output.
   */
  bool audioBlockWrite(int32_t L, int32_t R);

  [[deprecated("Use audioBlockWrite(left, right); direct legacy I2S writes are single-core only")]]
  bool i2s_write_samples(int16_t leftSample, int16_t rightSample) {
    if (_blockSplitActive) {
      return false;
    }
    return audioBlockWrite(leftSample, rightSample);
  }

  // These handles can now be used for vTask things
  TaskHandle_t audioCallback1Handle = NULL;  // Core 0 audio task
  TaskHandle_t audioCallback2Handle = NULL;  // Core 1 audio task

  // ---- Block-based dual-core voice partitioning helpers ----

  // Starting index for this core's voice partition.
  //   Block-split active → core 0: 0 (even voices), core 1: 1 (odd voices).
  //   Single-core or inactive → 0 (full range, step 1 = all voices).
  inline int audioPartitionOffset() {
    if (!_blockSplitActive) return 0;
    return xPortGetCoreID();
  }

  // Loop step for iterating this core's voices.
  //   Block-split active → 2 (interleaved even/odd).
  //   Single-core or inactive → 1 (contiguous, all voices).
  inline int audioPartitionStride() {
    return _blockSplitActive ? 2 : 1;
  }

  /** True only when both ESP32 audio tasks were created successfully and the
   * block-split pipeline is actually running. Sketches may use this to provide
   * a safe all-voices fallback when dual-core startup is unavailable. */
  inline bool audioPartitionIsActive() {
    return _blockSplitActive;
  }

  // Returns true only on Core 0 (the finaliser) in block-split mode, or always
  // in single-core mode. Use to guard shared-state effects (reverb, global
  // filters) that should run once per sample. In block-split mode these effects
  // process only Core 0's voice partition; full-mix post-processing is handled
  // inside audioBlockWrite after combining.
  inline bool audioIsFinalizerCore() {
    if (!_blockSplitActive) return true;
    return xPortGetCoreID() == 0;
  }

  // Block-based audio write.
  // Accumulates samples into a per-core partial buffer of M16_BLOCK_SIZE entries.
  // This creates "breathing room" for the OS and prevents watchdog resets.
  bool audioBlockWrite(int32_t leftSample, int32_t rightSample) {
    #if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
      // The internal DAC already owns a single buffered audio task and does not
      // create an external-I2S TX channel. Preserve the portable block-writer API
      // by forwarding each frame to its dedicated DAC accumulation path.
      if (_useInternalDAC) {
        if (_audioPostProcessCallback != nullptr) {
          _audioPostProcessCallback(leftSample, rightSample);
        }
        return _i2s_write_samples_platform((int16_t)clip16(leftSample),
                                            (int16_t)clip16(rightSample));
      }
    #endif

    int coreId = xPortGetCoreID();
    
    // Safety: if we are in dual-core block-split mode but running on Core 1,
    // we use the existing partition sync logic.
    if (_blockSplitActive && coreId == 1) {
      uint32_t sequence = _blockSequence[1];
      int slot = sequence & 1;
      int pos = _blockPos[1];
      // Claim the reusable slot once, at the start of a block. Checking on
      // every sample made a producer that had been lapped repeatedly account
      // the same recovery and continue rendering obsolete blocks at maximum
      // task priority.
      if (pos == 0 && sequence >= 2) {
        _audioProducerBlockWasPaced = false;
        uint32_t expectedConsumed = sequence - 1;
        uint32_t consumed =
            _blockConsumedSequence[slot].load(std::memory_order_acquire);
        bool producerWasPaced = false;
        // Wrap-safe monotonic comparison: wait only while Core 0 is behind.
        // Requiring equality deadlocks if repeated timeout recovery lets Core 0
        // pass this generation before Core 1 observes it.
        while ((int32_t)(consumed - expectedConsumed) < 0) {
          producerWasPaced = true;
          ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
          consumed =
              _blockConsumedSequence[slot].load(std::memory_order_acquire);
        }
        if (consumed != expectedConsumed) {
          // Core 0 has already discarded one or more missing partitions and
          // released this slot for a newer generation. Skip the obsolete DSP
          // work instead of trying to render every lost block. consumed+1 is
          // the next generation that legitimately owns this same slot.
          sequence = consumed + 1;
          _blockSequence[1] = sequence;
          slot = sequence & 1;
          _audioBlockLateProducerRecoveries++;
          producerWasPaced = true;
        }
        if (producerWasPaced) {
          _audioProducerBlockWasPaced = true;
          _audioConsecutiveUnpacedProducerBlocks = 0;
        }
      }
      _blockPartialL[slot][1][pos] = leftSample;
      _blockPartialR[slot][1][pos] = rightSample;
      pos++;
      _blockPos[1] = pos;
      if (pos < M16_BLOCK_SIZE) return true;
      _blockPos[1] = 0;
      _blockReadySequence[slot].store(sequence + 1, std::memory_order_release);
      _blockSequence[1] = sequence + 1;
      xTaskNotifyGive(audioCallback1Handle); // Wake Core 0
      #if M16_PRODUCER_UNPACED_YIELD_BLOCKS > 0
        if (!_audioProducerBlockWasPaced) {
          if (++_audioConsecutiveUnpacedProducerBlocks >=
              M16_PRODUCER_UNPACED_YIELD_BLOCKS) {
            _audioConsecutiveUnpacedProducerBlocks = 0;
            _audioProducerStarvationYields++;
            vTaskDelay(1);
          }
        }
      #endif
      return true;
    }

    // Standard buffering path (Single-core OR Core 0 in dual-core).
    // One output frame per call here. The Core 1 block-split branch above does
    // NOT advance the clock — it produces the same frames Core 0 finalises, so
    // counting it too would double the rate. audioBlockWrite never falls through
    // to i2s_write_samples on ESP32, so the two advance sites never overlap.
    m16AdvanceAudioFrame();
    uint32_t sequence = _blockSequence[0];
    int slot = sequence & 1;
    int pos = _blockPos[0];
    _blockPartialL[slot][0][pos] = leftSample;
    _blockPartialR[slot][0][pos] = rightSample;
    pos++;
    _blockPos[0] = pos;

    if (pos < M16_BLOCK_SIZE) {
      return true;
    }

    // Block is full — Process/Write
    _blockPos[0] = 0;

    bool core1Ready = _blockSplitActive;
    if (_blockSplitActive) {
      // Never let a failed/stalled producer drain the I2S DMA reserve. Core 1
      // normally finishes at almost the same time as Core 0, so two block
      // periods allow scheduler jitter without turning a missed partition into
      // a long output stall. Sequence tags reject late/stale notifications.
      uint32_t syncTimeoutUs = (uint32_t)(((uint64_t)M16_BLOCK_SIZE *
                                (uint64_t)M16_BLOCK_SYNC_TIMEOUT_BLOCKS *
                                1000000ULL) / (uint32_t)SAMPLE_RATE);
      if (syncTimeoutUs < 1000U) syncTimeoutUs = 1000U;
      if (syncTimeoutUs > 10000U) syncTimeoutUs = 10000U;
      uint32_t waitStartUs = micros();
      uint32_t expectedReady = sequence + 1;
      while (_blockReadySequence[slot].load(std::memory_order_acquire) != expectedReady &&
             (uint32_t)(micros() - waitStartUs) < syncTimeoutUs) {
        // One scheduler tick is the longest sleep appropriate in an audio
        // rendezvous. Re-check the microsecond deadline after every wake-up.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
      }
      core1Ready = _blockReadySequence[slot].load(std::memory_order_acquire) == expectedReady;
      if (!core1Ready) {
        _audioBlockSyncTimeouts++;
        uint32_t consecutive = ++_audioBlockConsecutiveSyncTimeouts;
        if (consecutive > _audioBlockMaxConsecutiveSyncTimeouts) {
          _audioBlockMaxConsecutiveSyncTimeouts = consecutive;
        }
      } else {
        _audioBlockConsecutiveSyncTimeouts = 0;
      }
    }

    // Combine and/or Format for DMA
    static uint8_t _blockBuf[M16_BLOCK_SIZE * 4];
    for (int i = 0; i < M16_BLOCK_SIZE; i++) {
      int32_t outL, outR;
      if (_blockSplitActive && core1Ready) {
        outL = _blockPartialL[slot][0][i] + _blockPartialL[slot][1][i];
        outR = _blockPartialR[slot][0][i] + _blockPartialR[slot][1][i];
      } else {
        outL = _blockPartialL[slot][0][i];
        outR = _blockPartialR[slot][0][i];
      }
      if (_audioPostProcessCallback != nullptr) {
        _audioPostProcessCallback(outL, outR);
      }
      outL = clip16(outL);
      outR = clip16(outR);
      _blockBuf[i*4 + 0] = (uint8_t)(outR & 0xFF);
      _blockBuf[i*4 + 1] = (uint8_t)((outR >> 8) & 0xFF);
      _blockBuf[i*4 + 2] = (uint8_t)(outL & 0xFF);
      _blockBuf[i*4 + 3] = (uint8_t)((outL >> 8) & 0xFF);
    }

    size_t bytesWritten = 0;
    uint32_t dmaWriteStartUs = micros();
    esp_err_t writeResult = i2s_channel_write(tx_handle, _blockBuf, sizeof(_blockBuf),
                                              &bytesWritten, 100);
    uint32_t dmaWriteElapsedUs = (uint32_t)(micros() - dmaWriteStartUs);
    bool result = (writeResult == ESP_OK && bytesWritten == sizeof(_blockBuf));
    if (!result) _audioDmaWriteTimeouts++;

    bool yieldForIdle = false;
    #if M16_DMA_FAST_WRITE_YIELD_BLOCKS > 0
      // Less than one quarter of a block period means DMA accepted the block
      // without providing meaningful pacing. A streak is used so normal startup
      // ring filling and isolated quick writes do not add latency.
      uint32_t blockPeriodUs = (uint32_t)(((uint64_t)M16_BLOCK_SIZE * 1000000ULL) /
                                          (uint32_t)SAMPLE_RATE);
      uint32_t fastWriteUs = blockPeriodUs >> 2;
      if (fastWriteUs < 50U) fastWriteUs = 50U;
      if (dmaWriteElapsedUs < fastWriteUs) {
        if (_audioConsecutiveFastDmaWrites < M16_DMA_FAST_WRITE_YIELD_BLOCKS) {
          _audioConsecutiveFastDmaWrites++;
        }
        if (_audioConsecutiveFastDmaWrites >= M16_DMA_FAST_WRITE_YIELD_BLOCKS) {
          _audioConsecutiveFastDmaWrites = 0;
          _audioDmaStarvationYields++;
          yieldForIdle = true;
        }
      } else {
        _audioConsecutiveFastDmaWrites = 0;
      }
    #endif
    
    if (_blockSplitActive) {
      _blockConsumedSequence[slot].store(sequence + 1, std::memory_order_release);
      _blockSequence[0] = sequence + 1;
      xTaskNotifyGive(audioCallback2Handle); // Release this slot for reuse
    }
    // taskYIELD()/yield() is insufficient here: this maximum-priority task can
    // immediately win scheduling again. A one-tick block guarantees IDLE0 and
    // the task watchdog get a scheduling opportunity. Core 1 is released first.
    if (yieldForIdle) vTaskDelay(1);
    return result;
  }

  // Mutex for thread-safe initialization (not static - needs external linkage for FX.h)
  SemaphoreHandle_t audioInitMutex = NULL;

  void audioStart() {
      // Create mutex for initialization protection
      if (audioInitMutex == NULL) {
          audioInitMutex = xSemaphoreCreateMutex();
      }

    #if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
      if (_useInternalDAC) {
        // Internal DAC initialization via dac_continuous driver
        dac_continuous_config_t dac_cfg = {
            .chan_mask = DAC_CHANNEL_MASK_ALL,
            .desc_num = DAC_DESC_NUM,
            .buf_size = DAC_ACCUM_SIZE,
            .freq_hz = (uint32_t)SAMPLE_RATE * (M16_INTERNAL_DAC_SIMUL ? 1 : 2),
            .offset = 0,
            .clk_src = DAC_DIGI_CLK_SRC_APLL,
            .chan_mode = M16_INTERNAL_DAC_SIMUL ? DAC_CHANNEL_MODE_SIMUL : DAC_CHANNEL_MODE_ALTER,
        };
        ESP_ERROR_CHECK(dac_continuous_new_channels(&dac_cfg, &_dac_handle));
        ESP_ERROR_CHECK(dac_continuous_enable(_dac_handle));

        if (ESP.getChipCores() > 1) {
          // Dual-core (ESP32): dedicated audio task on core 0, loop() on core 1.
          // No starvation since they're on separate cores.
          // Single task only — dual tasks cause buffer interleaving discontinuities.
          xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer0", 8192, NULL,
              configMAX_PRIORITIES - 1, &audioCallback1Handle, 0);
          _audioTasksRunning = true;
          Serial.println("M16 is running (internal DAC, 8-bit output, core 0)");
        } else {
          // Single-core (ESP32-S2): audio task at moderate priority.
          // dac_continuous_write() blocks on a FreeRTOS semaphore when DMA is full,
          // yielding CPU to loop() naturally. No audioLoop() needed.
          // Priority 2 (just above Arduino loopTask at 1) ensures audio gets
          // priority but loop() runs during every DMA write block (~3ms per buffer).
          xTaskCreatePinnedToCore(audioCallback, "FillAudioBuffer0", 8192, NULL,
              2, &audioCallback1Handle, 0);
          _audioTasksRunning = true;
          Serial.println("M16 is running (internal DAC, 8-bit output, single-core)");
        }
        return;
      }
    #endif

      // External I2S DAC initialization.
      // Only allocate RX channel when an input pin is actually wired (-1 means no input).
      bool useRx = (i2sPinsOut[3] != -1);
      i2s_new_channel(&chan_cfg, &tx_handle, useRx ? &rx_handle : NULL);
      i2s_channel_init_std_mode(tx_handle, &std_cfg);
      if (useRx) {
        i2s_channel_init_std_mode(rx_handle, &std_cfg);
        i2s_channel_enable(rx_handle);
      }
      i2s_channel_enable(tx_handle);

      _audioTasksRunning = false;
      _blockSplitActive  = false;
      _blockPos[0] = 0;
      _blockPos[1] = 0;
      _blockSequence[0] = 0;
      _blockSequence[1] = 0;
      _blockReadySequence[0].store(0, std::memory_order_relaxed);
      _blockReadySequence[1].store(0, std::memory_order_relaxed);
      _blockConsumedSequence[0].store(0, std::memory_order_relaxed);
      _blockConsumedSequence[1].store(0, std::memory_order_relaxed);
      _audioBlockLateProducerRecoveries = 0;
      _audioProducerStarvationYields = 0;
      _audioConsecutiveFastDmaWrites = 0;
      _audioConsecutiveUnpacedProducerBlocks = 0;
      _audioProducerBlockWasPaced = false;

      audioCallback1Handle = NULL;
      audioCallback2Handle = NULL;

      // Core 0 audio task — required for both single- and dual-core output.
      BaseType_t core0TaskResult = xTaskCreatePinnedToCore(
          audioCallback,
          "FillAudioBuffer0",
          16384,
          NULL,
          configMAX_PRIORITIES - 2,
          &audioCallback1Handle,
          0
      );

      bool dualTasksActive = false;
      BaseType_t core1TaskResult = pdFAIL;
      if (core0TaskResult == pdPASS && isDualCore && ESP.getChipCores() > 1) {
        core1TaskResult = xTaskCreatePinnedToCore(
            audioCallback,
            "FillAudioBuffer1",
            16384,
            NULL,
            configMAX_PRIORITIES - 2,
            &audioCallback2Handle,
            1
        );
        dualTasksActive = (core1TaskResult == pdPASS && audioCallback2Handle != NULL);
      }

      if (core0TaskResult != pdPASS || audioCallback1Handle == NULL) {
        Serial.printf("M16 ERROR: Core 0 audio task creation failed (result=%ld, free heap=%u)\n",
                      (long)core0TaskResult, (unsigned)ESP.getFreeHeap());
        _audioTasksRunning = false;
        return;
      }
      if (isDualCore && !dualTasksActive) {
        Serial.printf("M16 WARNING: Core 1 audio task unavailable (result=%ld, chip cores=%u, free heap=%u); using single-core fallback\n",
                      (long)core1TaskResult, (unsigned)ESP.getChipCores(),
                      (unsigned)ESP.getFreeHeap());
      }

#if M16_REORDER_BUFFER_ENABLE
      // Legacy reorder buffer: ensures deterministic sample order for sketches
      // that call i2s_write_samples() from both cores (e.g. Beat Machine).
      // audioBlockWrite() sketches bypass the ring entirely — the drainer
      // stays idle (blocking on _reorderSlotFilled) and does not interfere.
      if (dualTasksActive) {
        for (int i = 0; i < M16_REORDER_RING_SIZE; i++) {
          _reorderRing[i].ready.store(0, std::memory_order_relaxed);
        }
        _reorderNextSlotToProduce.store(1, std::memory_order_relaxed);
        _reorderNextSlotToConsume = 1;
        _reorderSlotSem    = xSemaphoreCreateCounting(M16_REORDER_RING_SIZE, M16_REORDER_RING_SIZE);
        _reorderSlotFilled = xSemaphoreCreateCounting(M16_REORDER_RING_SIZE, 0);
        BaseType_t drainerOk = xTaskCreatePinnedToCore(
            reorderDrainerTask,
            "M16Drainer",
            8192,
            NULL,
            configMAX_PRIORITIES - 1,
            &_reorderDrainerHandle,
            0
        );
        if (drainerOk == pdPASS) {
          _reorderActive = true;
          Serial.println("M16 reorder buffer active (legacy dual-core i2s_write_samples ordering)");
        } else {
          Serial.println("M16 WARNING: reorder buffer drainer task creation FAILED — direct-write fallback");
        }
      }
#endif

      if (dualTasksActive) {
        _blockSplitActive = true;
        _audioTasksRunning = true;
        Serial.println("M16 is running (dual-core, block-split N=" + String(M16_BLOCK_SIZE) + ")");
      } else {
        _audioTasksRunning = true;
        Serial.println("M16 is running (single-core audio task mode)");
      }
  }

  /** No-op on ESP32 — FreeRTOS tasks handle audio for both external I2S
   *  and internal DAC modes. Included for API compatibility with RP2040.
   */
  inline void audioLoop() {}
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
  // Dual-core audio support. RP2350 uses a dedicated interrupt-driven block
  // worker on Core 0; RP2040 retains the cooperative audioLoop() worker.
  #include <I2S.h>
  #include "pico/multicore.h"
  #include "pico/mutex.h"

  // Enable separate 8KB stack for Core 1 (recommended for audio processing)
  // This prevents stack collisions between cores during heavy DSP work
  bool core1_separate_stack = true;

  // Default I2S pins - BCLK on GPIO 16, WS is implicitly BCLK+1 (GPIO 17), DOUT on GPIO 18, DIN on GPIO 19
  static int picoI2sPins[] = {16, 18, 19}; // {BCLK, DOUT, DIN}

  // I2S instance for output
  I2S i2sOut(OUTPUT);
  I2S i2sIn(INPUT);    // Audio input (microphone)

  // Mutex for thread-safe initialization (mirrors ESP32's audioInitMutex)
  static mutex_t picoAudioInitMutex;
  static bool picoMutexInitialized = false;

  // Flags for core synchronization
  static volatile bool picoAudioRunning = false;
  static volatile bool picoInputEnabled = false;

  // Input buffer for microphone data (shared with Mic.h)
  static const int PICO_INPUT_BUF_SIZE = 256;
  static int32_t picoInputBuf[PICO_INPUT_BUF_SIZE];
  static volatile int picoInputBufIndex = 0;
  static volatile int picoInputSamplesRead = 0;

  void seti2sPins(int bck, int ws, int dout, int din) {
    picoI2sPins[0] = bck;
    picoI2sPins[1] = dout;
    picoI2sPins[2] = din;
    if (ws != bck + 1) {
      Serial.print("Warning: Pico WS is always BCLK+1 (GPIO ");
      Serial.print(bck + 1);
      Serial.print("), ignoring ws=");
      Serial.println(ws);
    }
    Serial.print("i2s pins set for Pico: BCLK=");
    Serial.print(bck);
    Serial.print(" WS=");
    Serial.print(bck + 1);
    Serial.print(" DOUT=");
    Serial.print(dout);
    Serial.print(" DIN=");
    Serial.println(din);
  }

  void audioUpdate(); // forward declaration

  // Output smoothing state for RP2040 (consistency with ESP32)
  static int32_t _prevOutL = 0;
  static int32_t _prevOutR = 0;

  // Legacy direct writes are serialized, but partitioned block mode has a
  // single I2S writer on Core 1 and does not use this mutex.
  auto_init_mutex(picoI2SMutex);  // Separate mutex for I2S writes

  /** Write audio samples - direct I2S write with mutex serialization */
  void _i2s_write_samples_platform(int16_t leftSample, int16_t rightSample) {
    int32_t outL = leftSample;
    int32_t outR = rightSample;

    leftAudioOuputValue = outL;
    rightAudioOuputValue = outR;

    uint32_t sample32 = ((uint32_t)(uint16_t)outL << 16) |
                        (uint16_t)outR;

    if (isDualCore) {
      // Serialize I2S writes with mutex - samples go directly to I2S in order
      mutex_enter_blocking(&picoI2SMutex);
      i2sOut.write((int32_t)sample32);
      mutex_exit(&picoI2SMutex);
    } else {
      i2sOut.write((int32_t)sample32);
    }
    m16AdvanceAudioFrame(); // one output frame produced (both cores, serialized)
  }

  static bool _picoBlockSplitActive = false;
  bool audioBlockWrite(int32_t L, int32_t R);

  [[deprecated("Use audioBlockWrite(left, right); direct legacy I2S writes are single-core only")]]
  bool i2s_write_samples(int16_t leftSample, int16_t rightSample) {
    if (_picoBlockSplitActive) {
      return false;
    }
    return audioBlockWrite(leftSample, rightSample);
  }

  // ---- Block-partitioned rendering ---------------------------------------
  // Core 1 is the permanent coordinator and sole I2S writer. Two block slots
  // allow Core 0 to render the next even-voice partition while Core 1 finalizes
  // the current block. On RP2350 a spare hardware doorbell invokes the bounded
  // Core-0 worker automatically. RP2040 has no doorbells, so audioLoop() claims
  // the same jobs cooperatively. An unclaimed job always falls back to Core 1.
  enum PicoBlockJobState : uint8_t {
    PICO_BLOCK_IDLE = 0,
    PICO_BLOCK_POSTED,
    PICO_BLOCK_CLAIMED,
    PICO_BLOCK_READY,
    PICO_BLOCK_FALLBACK
  };

  static constexpr uint8_t PICO_BLOCK_SLOTS = 2;
  static std::atomic<uint8_t> _picoBlockJobState[PICO_BLOCK_SLOTS];
  static std::atomic<uint32_t> _picoBlockJobStartFrame[PICO_BLOCK_SLOTS];
  static std::atomic<uint32_t> _picoBlockFallbacks{0};
  static std::atomic<uint32_t> _picoBlockWorkerClaims{0};
  static std::atomic<uint32_t> _picoBlockWriteErrors{0};
  static int32_t _picoBlockPartialL[PICO_BLOCK_SLOTS][2][M16_BLOCK_SIZE] __attribute__((aligned(32)));
  static int32_t _picoBlockPartialR[PICO_BLOCK_SLOTS][2][M16_BLOCK_SIZE] __attribute__((aligned(32)));
  static uint16_t _picoBlockWritePos[2] = {0, 0};
  static uint8_t _picoBlockWriteSlot[2] = {0, 0};
  static int8_t _picoPartitionOverride[2] = {-1, -1};

  #ifdef PICO_RP2350
  static int _picoBlockWorkerDoorbell = -1;
  static bool _picoBlockWorkerInterruptActive = false;
  #endif

  inline int audioPartitionOffset() {
    if (!_picoBlockSplitActive) return 0;
    int core = get_core_num();
    int8_t overridePartition = _picoPartitionOverride[core];
    return overridePartition >= 0 ? overridePartition : core;
  }

  inline int audioPartitionStride() {
    return _picoBlockSplitActive ? 2 : 1;
  }

  inline bool audioPartitionIsActive() {
    return _picoBlockSplitActive;
  }

  inline bool audioIsFinalizerCore() {
    return !_picoBlockSplitActive || get_core_num() == 1;
  }

  inline uint32_t picoAudioBlockFallbackCount() {
    return _picoBlockFallbacks.load(std::memory_order_relaxed);
  }

  inline uint32_t picoAudioBlockWorkerClaimCount() {
    return _picoBlockWorkerClaims.load(std::memory_order_relaxed);
  }

  inline uint32_t picoAudioBlockWriteErrorCount() {
    return _picoBlockWriteErrors.load(std::memory_order_relaxed);
  }

  inline bool picoAudioBlockWorkerIsAutomatic() {
    #ifdef PICO_RP2350
    return _picoBlockWorkerInterruptActive;
    #else
    return false;
    #endif
  }

  inline bool audioBlockWrite(int32_t L, int32_t R) {
    if (!_picoBlockSplitActive) {
      if (_audioPostProcessCallback != nullptr) {
        _audioPostProcessCallback(L, R);
      }
      _i2s_write_samples_platform((int16_t)clip16(L), (int16_t)clip16(R));
      return true;
    }

    int core = get_core_num();
    int partition = audioPartitionOffset();
    uint16_t pos = _picoBlockWritePos[core];
    if (partition < 0 || partition > 1 || pos >= M16_BLOCK_SIZE) {
      _picoBlockWriteErrors.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    uint8_t slot = _picoBlockWriteSlot[core];
    _picoBlockPartialL[slot][partition][pos] = L;
    _picoBlockPartialR[slot][partition][pos] = R;
    _picoBlockWritePos[core] = pos + 1;
    return true;
  }

  inline void picoRenderBlockPartition(int partition, uint8_t slot,
                                       uint32_t startFrame) {
    int core = get_core_num();
    _picoPartitionOverride[core] = partition;
    _picoBlockWriteSlot[core] = slot;
    _picoBlockWritePos[core] = 0;
    for (int i = 0; i < M16_BLOCK_SIZE; i++) {
      _picoBlockPartialL[slot][partition][i] = 0;
      _picoBlockPartialR[slot][partition][i] = 0;
    }

    m16BeginRenderFrameContext(startFrame);
    for (int i = 0; i < M16_BLOCK_SIZE; i++) {
      m16SetRenderFrame(startFrame + (uint32_t)i);
      audioUpdate();
    }
    m16EndRenderFrameContext();
    _picoPartitionOverride[core] = -1;

    if (_picoBlockWritePos[core] != M16_BLOCK_SIZE) {
      _picoBlockWriteErrors.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline void picoFinalizeBlock(uint8_t slot) {
    for (int i = 0; i < M16_BLOCK_SIZE; i++) {
      int64_t combinedL = (int64_t)_picoBlockPartialL[slot][0][i] +
                          _picoBlockPartialL[slot][1][i];
      int64_t combinedR = (int64_t)_picoBlockPartialR[slot][0][i] +
                          _picoBlockPartialR[slot][1][i];
      int32_t outL = combinedL > INT32_MAX ? INT32_MAX :
                     combinedL < INT32_MIN ? INT32_MIN : (int32_t)combinedL;
      int32_t outR = combinedR > INT32_MAX ? INT32_MAX :
                     combinedR < INT32_MIN ? INT32_MIN : (int32_t)combinedR;
      if (_audioPostProcessCallback != nullptr) {
        _audioPostProcessCallback(outL, outR);
      }
      outL = clip16(outL);
      outR = clip16(outR);
      leftAudioOuputValue = outL;
      rightAudioOuputValue = outR;
      uint32_t sample32 = ((uint32_t)(uint16_t)outL << 16) |
                          (uint16_t)outR;
      i2sOut.write((int32_t)sample32);
      m16AdvanceAudioFrame();
    }
  }

  inline bool picoClaimAndRenderBlock(uint8_t slot) {
    uint8_t expected = PICO_BLOCK_POSTED;
    if (!_picoBlockJobState[slot].compare_exchange_strong(
            expected, PICO_BLOCK_CLAIMED,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      return false;
    }
    _picoBlockWorkerClaims.fetch_add(1, std::memory_order_relaxed);
    uint32_t startFrame =
        _picoBlockJobStartFrame[slot].load(std::memory_order_relaxed);
    picoRenderBlockPartition(0, slot, startFrame);
    _picoBlockJobState[slot].store(PICO_BLOCK_READY,
                                   std::memory_order_release);
    return true;
  }

  #ifdef PICO_RP2350
  static void __no_inline_not_in_flash_func(picoBlockWorkerIrq)() {
    if (_picoBlockWorkerDoorbell < 0 ||
        !multicore_doorbell_is_set_current_core(_picoBlockWorkerDoorbell)) {
      return;
    }
    multicore_doorbell_clear_current_core(_picoBlockWorkerDoorbell);
    // At most two bounded jobs can be outstanding. Normally only one is.
    for (uint8_t slot = 0; slot < PICO_BLOCK_SLOTS; ++slot) {
      picoClaimAndRenderBlock(slot);
    }
  }
  #endif

  inline void picoPostBlock(uint8_t slot, uint32_t startFrame) {
    _picoBlockJobStartFrame[slot].store(startFrame,
                                        std::memory_order_relaxed);
    _picoBlockJobState[slot].store(PICO_BLOCK_POSTED,
                                   std::memory_order_release);
    #ifdef PICO_RP2350
    if (_picoBlockWorkerInterruptActive) {
      multicore_doorbell_set_other_core(_picoBlockWorkerDoorbell);
    }
    #endif
  }

  inline void picoFinishBlock(uint8_t slot) {
    uint32_t startFrame =
        _picoBlockJobStartFrame[slot].load(std::memory_order_relaxed);

    // Core 0 has the duration of Core 1's odd-voice render to claim the job.
    picoRenderBlockPartition(1, slot, startFrame);

    uint8_t expected = PICO_BLOCK_POSTED;
    if (_picoBlockJobState[slot].compare_exchange_strong(
            expected, PICO_BLOCK_FALLBACK,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      _picoBlockFallbacks.fetch_add(1, std::memory_order_relaxed);
      picoRenderBlockPartition(0, slot, startFrame);
    } else {
      // Once Core 0 has claimed a job it completes the whole bounded block
      // synchronously inside audioLoop(), so duplicate fallback is forbidden.
      while (_picoBlockJobState[slot].load(std::memory_order_acquire) != PICO_BLOCK_READY) {
        tight_loop_contents();
      }
    }
  }

  inline void picoRenderPipelinedBlocks() {
    uint8_t slot = 0;
    uint32_t startFrame = audioFrameCount();
    picoPostBlock(slot, startFrame);

    while (true) {
      picoFinishBlock(slot);

      // Queue the alternate slot before finalizing this one. On Pico 2 the
      // Core-0 doorbell worker overlaps that render with post-processing/I2S.
      uint8_t nextSlot = slot ^ 1u;
      uint32_t nextFrame = startFrame + M16_BLOCK_SIZE;
      picoPostBlock(nextSlot, nextFrame);
      picoFinalizeBlock(slot);
      _picoBlockJobState[slot].store(PICO_BLOCK_IDLE,
                                     std::memory_order_release);
      slot = nextSlot;
      startFrame = nextFrame;
    }
  }

  /** Core 1 audio callback */
  void picoAudioCallback() {
    while (!picoAudioRunning) {
      tight_loop_contents();
    }

    if (_picoBlockSplitActive) {
      picoRenderPipelinedBlocks();
    } else {
      // Single-core: simple tight loop
      while (true) {
        audioUpdate();
      }
    }
  }

  /** Core 0 audio processing - call from loop() in dual-core mode */
  inline void audioLoop() {
    if (!_picoBlockSplitActive || !picoAudioRunning) return;

    #ifdef PICO_RP2350
    if (_picoBlockWorkerInterruptActive) return;
    #endif
    for (uint8_t slot = 0; slot < PICO_BLOCK_SLOTS; ++slot) {
      if (picoClaimAndRenderBlock(slot)) return;
    }
  }

  void audioStart() {
    // Initialize mutex for thread-safe initialization protection
    if (!picoMutexInitialized) {
      mutex_init(&picoAudioInitMutex);
      picoMutexInitialized = true;
    }

    // Configure I2S OUTPUT with optimized buffer settings
    i2sOut.setBCLK(picoI2sPins[0]);
    i2sOut.setDOUT(picoI2sPins[1]);
    i2sOut.setBitsPerSample(32);
    i2sOut.setBuffers(8, 128);  // Larger buffers for complex DSP headroom (8*64=512 samples ~11.6ms)

    if (!i2sOut.begin(SAMPLE_RATE)) {
      Serial.println("I2S output init failed!");
      while(1);
    }

    _picoBlockSplitActive = isDualCore;
    for (uint8_t slot = 0; slot < PICO_BLOCK_SLOTS; ++slot) {
      _picoBlockJobState[slot].store(PICO_BLOCK_IDLE,
                                     std::memory_order_relaxed);
      _picoBlockJobStartFrame[slot].store(0, std::memory_order_relaxed);
    }
    _picoBlockFallbacks.store(0, std::memory_order_relaxed);
    _picoBlockWorkerClaims.store(0, std::memory_order_relaxed);
    _picoBlockWriteErrors.store(0, std::memory_order_relaxed);

    #ifdef PICO_RP2350
    if (_picoBlockSplitActive) {
      // The Arduino core reserves one doorbell for multicore coordination.
      // Claim a different doorbell on Core 0 and share its IRQ safely.
      _picoBlockWorkerDoorbell = multicore_doorbell_claim_unused(0b01, false);
      if (_picoBlockWorkerDoorbell >= 0) {
        uint32_t workerIrq =
            multicore_doorbell_irq_num(_picoBlockWorkerDoorbell);
        irq_add_shared_handler(workerIrq, picoBlockWorkerIrq, 129);
        irq_set_enabled(workerIrq, true);
        _picoBlockWorkerInterruptActive = true;
      }
    }
    #endif

    // Signal that I2S is ready
    picoAudioRunning = true;

    // Launch Core 1 for dedicated audio processing
    multicore_launch_core1(picoAudioCallback);

    if (_picoBlockSplitActive) {
      Serial.println("M16 is running (Pico block-partitioned mode)");
      Serial.println("  Core 1: coordinator, odd voices, post-process, I2S");
      #ifdef PICO_RP2350
      if (_picoBlockWorkerInterruptActive) {
        Serial.println("  Core 0: automatic doorbell block worker (double buffered)");
      } else {
        Serial.println("  Core 0: audioLoop() block worker (no free doorbell)");
      }
      #else
      Serial.println("  Core 0: audioLoop() services even-voice block jobs");
      #endif
      Serial.println("  Missing Core 0 jobs fall back safely to Core 1");
    } else {
      Serial.println("M16 is running (Pico single-core mode)");
      Serial.println("  Core 1: dedicated audio");
      Serial.println("  Core 0: free for UI in loop()");
    }
  }

  /** Start audio input for microphone/line-in on Pico
   *  Call this after audioStart() if you need audio input.
   *  Uses a separate I2S instance to avoid conflicts with audio output.
   *  Input uses BCLK+4 to avoid pin conflicts (e.g., GPIO 20 if output uses 16).
   */
  void audioInputStart() {
    // Use separate BCLK for input (4 pins higher than output BCLK)
    // This avoids any potential conflicts between input and output clocks
    int inputBclk = picoI2sPins[0] + 4;  // e.g., GPIO 20 if output uses 16

    i2sIn.setBCLK(inputBclk);
    i2sIn.setDIN(picoI2sPins[2]);
    i2sIn.setBitsPerSample(32);
    i2sIn.setBuffers(4, 64);  // Smaller buffers OK for input

    if (!i2sIn.begin(SAMPLE_RATE)) {
      Serial.println("I2S input init failed!");
      return;
    }

    picoInputEnabled = true;

    Serial.print("M16 audio input enabled (Pico) - BCLK=");
    Serial.print(inputBclk);
    Serial.print(" WS=");
    Serial.print(inputBclk + 1);
    Serial.print(" DIN=");
    Serial.println(picoI2sPins[2]);
  }

  /** Set custom input BCLK pin (call before audioInputStart)
   *  By default, input uses output BCLK + 4.
   *  Use this if you need a specific pin configuration.
   */
  void setInputBclk(int bclk) {
    // Store in unused slot or add new variable if needed
    // For now, we'll add a simple global
    static int customInputBclk = -1;
    customInputBclk = bclk;
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

// Pre-computed MIDI note to frequency table (notes 0-127)
// mtof(n) = 8.1757989156 * 2^(n/12)
static const float _mtofTable[128] = {
  8.176f, 8.662f, 9.177f, 9.723f, 10.301f, 10.913f, 11.562f, 12.250f,
  12.978f, 13.750f, 14.568f, 15.434f, 16.352f, 17.324f, 18.354f, 19.445f,
  20.602f, 21.827f, 23.125f, 24.500f, 25.957f, 27.500f, 29.135f, 30.868f,
  32.703f, 34.648f, 36.708f, 38.891f, 41.203f, 43.654f, 46.249f, 48.999f,
  51.913f, 55.000f, 58.270f, 61.735f, 65.406f, 69.296f, 73.416f, 77.782f,
  82.407f, 87.307f, 92.499f, 97.999f, 103.826f, 110.000f, 116.541f, 123.471f,
  130.813f, 138.591f, 146.832f, 155.563f, 164.814f, 174.614f, 184.997f, 195.998f,
  207.652f, 220.000f, 233.082f, 246.942f, 261.626f, 277.183f, 293.665f, 311.127f,
  329.628f, 349.228f, 369.994f, 391.995f, 415.305f, 440.000f, 466.164f, 493.883f,
  523.251f, 554.365f, 587.330f, 622.254f, 659.255f, 698.456f, 739.989f, 783.991f,
  830.609f, 880.000f, 932.328f, 987.767f, 1046.502f, 1108.731f, 1174.659f, 1244.508f,
  1318.510f, 1396.913f, 1479.978f, 1567.982f, 1661.219f, 1760.000f, 1864.655f, 1975.533f,
  2093.005f, 2217.461f, 2349.318f, 2489.016f, 2637.020f, 2793.826f, 2959.955f, 3135.963f,
  3322.438f, 3520.000f, 3729.310f, 3951.066f, 4186.009f, 4434.922f, 4698.636f, 4978.032f,
  5274.041f, 5587.652f, 5919.911f, 6271.927f, 6644.875f, 7040.000f, 7458.620f, 7902.133f,
  8372.018f, 8869.844f, 9397.273f, 9956.063f, 10548.08f, 11175.30f, 11839.82f, 12543.85f
};

/** Return freq from a MIDI pitch (fast lookup version)
* @pitch The MIDI pitch to be converted (in-range integer for fastest, out-of-range and float supported)
*/
inline
float mtof(float midival) {
  if (midival < 0.0f || midival > 127.0f) return 440.0 * pow(2.0, (midival - 69.0) / 12.0);

  int idx = (int)midival;
  float frac = midival - idx;

  // Fast path for integer MIDI notes (no interpolation needed)
  if (frac < 0.001f) return _mtofTable[idx];

  // Linear interpolation for fractional notes
  return _mtofTable[idx] + (_mtofTable[idx + 1] - _mtofTable[idx]) * frac;
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
    if (pitchClassSet[i] < 0) continue; // sentinel: unused slot
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

// overload to use int scale values; size defaults to 12 for backward compat
inline
int pitchQuantize(int pitch, int * pitchClassSet, int key, int size = 12) {
  int8_t pc[12];
  memset(pc, -1, sizeof(pc)); // -1 sentinel = unused slot
  int n = size < 12 ? size : 12;
  for (int i = 0; i < n; i++) {
    pc[i] = (int8_t)pitchClassSet[i];
  }
  return pitchQuantize(pitch, pc, key);
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

// Pre-computed pan lookup table (17 entries for 0.0-1.0 in 1/16 steps)
// Values are cos/sin constant-power panning: L=cos(pan*π/2), R=sin(pan*π/2)
// At center (0.5), both L and R = 0.707 for equal power
static const float _panTableL[17] = {
  1.000f, 0.995f, 0.981f, 0.957f, 0.924f, 0.882f, 0.831f, 0.773f, 0.707f,
  0.634f, 0.556f, 0.471f, 0.383f, 0.290f, 0.195f, 0.098f, 0.000f
};
static const float _panTableR[17] = {
  0.000f, 0.098f, 0.195f, 0.290f, 0.383f, 0.471f, 0.556f, 0.634f, 0.707f,
  0.773f, 0.831f, 0.882f, 0.924f, 0.957f, 0.981f, 0.995f, 1.000f
};

/** Return left amount for a pan position 0.0-1.0 (fast lookup version) */
inline float panLeft(float panVal) {
  if (panVal <= 0.0f) return 1.0f;
  if (panVal >= 1.0f) return 0.0f;
  // Linear interpolation in lookup table
  float idx = panVal * 16.0f;
  int i = (int)idx;
  float frac = idx - i;
  return _panTableL[i] + (_panTableL[i + 1] - _panTableL[i]) * frac;
}

/** Return right amount for a pan position 0.0-1.0 (fast lookup version) */
inline float panRight(float panVal) {
  if (panVal <= 0.0f) return 0.0f;
  if (panVal >= 1.0f) return 1.0f;
  // Linear interpolation in lookup table
  float idx = panVal * 16.0f;
  int i = (int)idx;
  float frac = idx - i;
  return _panTableR[i] + (_panTableR[i + 1] - _panTableR[i]) * frac;
}

/** Return scaled floating point value 
* Arduino map() function for floats
*/
float floatMap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/** Return sigmoid distributed value for value between 0.0-1.0 */
inline
float sigmoid(float x) {
  if (x <= 0.0) return 0.0;
  if (x >= 1.0) return 1.0;
  return max(0.0, min(1.0, 1.0 / (1.0 + exp(-10.0 * (x - 0.5)))));
}

/** Return the invoice sigmoid distributed value for value between 0.0-1.0 
 * Useful for flattening the centre of linear distributions/values.
*/
float inverseSigmoid(float x) { // 0.0 to 1.0
  if (x <= 0.0) return 0.0;
  if (x >= 1.0) return 1.0;
  return max(0.0, min(1.0, 0.5 - log(1.0 / x - 1.0) / 10.0));
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
  * Per-core state arrays prevent dual-core race conditions that could
  * corrupt the PRNG to the all-zero absorbing state.
  * */
  #if IS_ESP32()
    #define _M16_PRNG_CORES 2
    #define _M16_CORE_ID() xPortGetCoreID()
  #elif IS_RP2040()
    #define _M16_PRNG_CORES 2
    #define _M16_CORE_ID() get_core_num()
  #else
    #define _M16_PRNG_CORES 1
    #define _M16_CORE_ID() 0
  #endif

  static uint32_t _prng_s0[_M16_PRNG_CORES] = {0x9E3779B9, 0x12345678};
  static uint32_t _prng_s1[_M16_PRNG_CORES] = {0x243F6A88, 0xFEDCBA98};
  static uint32_t _prng_s2[_M16_PRNG_CORES] = {0xB7E15162, 0xABCDEF01};
  static uint32_t _prng_s3[_M16_PRNG_CORES] = {0xC0DEC0DE, 0x87654321};

  // Rotate left helper
  inline uint32_t rotl(const uint32_t x, int k) {
      return (x << k) | (x >> (32 - k));
  }

  // Core generator: xoshiro128** (per-core state, no locking needed)
  inline uint32_t audioRand32() {
      int c = _M16_CORE_ID();
      const uint32_t result = rotl(_prng_s1[c] * 5, 7) * 9;

      const uint32_t t = _prng_s1[c] << 9;
      _prng_s2[c] ^= _prng_s0[c];
      _prng_s3[c] ^= _prng_s1[c];
      _prng_s1[c] ^= _prng_s2[c];
      _prng_s0[c] ^= _prng_s3[c];

      _prng_s2[c] ^= t;
      _prng_s3[c] = rotl(_prng_s3[c], 11);

      return result;
  }

  // Uniform int in [0, maxVal)
  inline int audioRand(int32_t maxVal) {
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
  // Seeds both cores' PRNG state with different sequences
  inline void audioRandSeed(uint32_t seed) {
      if (seed == 0) {
          uint32_t t = (uint32_t)micros();
          seed = t ^ 0xA5A5A5A5UL;
      }
      auto splitmix32 = [](uint32_t &x) {
          uint32_t z = (x += 0x9E3779B9UL);
          z = (z ^ (z >> 16)) * 0x85EBCA6BUL;
          z = (z ^ (z >> 13)) * 0xC2B2AE35UL;
          return z ^ (z >> 16);
      };

      for (int c = 0; c < _M16_PRNG_CORES; c++) {
          _prng_s0[c] = splitmix32(seed);
          _prng_s1[c] = splitmix32(seed);
          _prng_s2[c] = splitmix32(seed);
          _prng_s3[c] = splitmix32(seed);
      }
  }

// /* M16_H_ */
#endif /* M16_H_ */
