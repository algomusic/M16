/*
 * Verb.h
 *
 * A Freeverb-style reverb optimized for ESP32 and RP2040.
 * Uses parallel comb filters followed by series allpass filters.
 *
 * Based on the Freeverb algorithm by Jezar at Dreampoint.
 * Optimized for microcontrollers: reduced filter count, bit-shift math,
 * int16_t buffers in regular RAM (PSRAM too slow for audio-rate access).
 *
 * Standard quality: 4 parallel combs -> sum -> 2 series allpass -> output
 * High quality: 8 parallel combs -> sum -> 4 series allpass -> output (original Freeverb)
 *
 * by Andrew R. Brown 2025
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef VERB_H_
#define VERB_H_

// Maximum filter counts for high quality mode
#define VERB_MAX_COMBS 8
#define VERB_MAX_ALLPASS 4

class Verb {

public:
  /** Constructor */
  Verb() : initialized(false), highQuality(true), wetMix(512), dryMix(512),
           roomSize(952), damping(410), dampCoeff(871), stereoWidth(922), usePSRAM(true),
           numCombs(8), numAllpass(4) {  // damping 410 = 0.4, dampCoeff = 717 + ((1024-410)*307)>>10 = 871
    // Initialize buffer pointers to null
    for (int i = 0; i < VERB_MAX_COMBS; i++) combBuf[i] = nullptr;
    for (int i = 0; i < VERB_MAX_ALLPASS; i++) allpassBuf[i] = nullptr;
  }

  /** Destructor */
  ~Verb() {
    for (int i = 0; i < VERB_MAX_COMBS; i++) {
      if (combBuf[i]) { delete[] combBuf[i]; combBuf[i] = nullptr; }
    }
    for (int i = 0; i < VERB_MAX_ALLPASS; i++) {
      if (allpassBuf[i]) { delete[] allpassBuf[i]; allpassBuf[i] = nullptr; }
    }
  }

  /** Set quality mode - must be called BEFORE first reverb call
   *  @param high true for high quality (8 combs + 4 allpass, default)
   *              false for standard quality (4 combs + 2 allpass, lower CPU/memory)
   */
  void setHighQuality(bool high) {
    if (!initialized) {
      highQuality = high;
      numCombs = high ? 8 : 4;
      numAllpass = high ? 4 : 2;
    }
  }

  /** Initialize the reverb - call in setup() before audioStart() */
  void init() {
    if (initialized) return;

    // Freeverb-style delay times (in samples at 44100Hz)
    // Original Freeverb comb delays: 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
    combDelayBase[0] = 1116;  // ~25ms
    combDelayBase[1] = 1188;  // ~27ms
    combDelayBase[2] = 1277;  // ~29ms
    combDelayBase[3] = 1356;  // ~31ms
    combDelayBase[4] = 1422;  // ~32ms
    combDelayBase[5] = 1491;  // ~34ms
    combDelayBase[6] = 1557;  // ~35ms
    combDelayBase[7] = 1617;  // ~37ms

    // Original Freeverb allpass delays: 556, 441, 341, 225
    allpassDelayBase[0] = 556;   // ~12.6ms
    allpassDelayBase[1] = 441;   // ~10ms
    allpassDelayBase[2] = 341;   // ~7.7ms
    allpassDelayBase[3] = 225;   // ~5.1ms

    // Scale delays for actual sample rate
    float sampleRateScale = SAMPLE_RATE / 44100.0f;

    uint16_t maxCombDelay = 0;
    for (int i = 0; i < numCombs; i++) {
      combDelay[i] = (uint16_t)(combDelayBase[i] * sampleRateScale);
      if (combDelay[i] > maxCombDelay) maxCombDelay = combDelay[i];
    }

    uint16_t maxAllpassDelay = 0;
    for (int i = 0; i < numAllpass; i++) {
      allpassDelay[i] = (uint16_t)(allpassDelayBase[i] * sampleRateScale);
      if (allpassDelay[i] > maxAllpassDelay) maxAllpassDelay = allpassDelay[i];
    }

    // Find buffer sizes (round up to power of 2)
    combBufSize = 1;
    while (combBufSize <= maxCombDelay) combBufSize <<= 1;
    combBufMask = combBufSize - 1;

    allpassBufSize = 1;
    while (allpassBufSize <= maxAllpassDelay) allpassBufSize <<= 1;
    allpassBufMask = allpassBufSize - 1;

    // Allocate buffers
    #if IS_ESP32()
      if (usePSRAM && isPSRAMAvailable()) {
        Serial.println("Verb: Using PSRAM for buffers");
        for (int i = 0; i < numCombs; i++) {
          combBuf[i] = (int16_t*)ps_calloc(combBufSize, sizeof(int16_t));
          if (!combBuf[i]) {
            combBuf[i] = new int16_t[combBufSize]();
          }
        }
        for (int i = 0; i < numAllpass; i++) {
          allpassBuf[i] = (int16_t*)ps_calloc(allpassBufSize, sizeof(int16_t));
          if (!allpassBuf[i]) {
            allpassBuf[i] = new int16_t[allpassBufSize]();
          }
        }
      } else {
        Serial.println("Verb: Using regular RAM for buffers");
        for (int i = 0; i < numCombs; i++) {
          combBuf[i] = new int16_t[combBufSize]();
        }
        for (int i = 0; i < numAllpass; i++) {
          allpassBuf[i] = new int16_t[allpassBufSize]();
        }
      }
    #else
      // Non-ESP32: always use regular RAM
      for (int i = 0; i < numCombs; i++) {
        combBuf[i] = new int16_t[combBufSize]();
      }
      for (int i = 0; i < numAllpass; i++) {
        allpassBuf[i] = new int16_t[allpassBufSize]();
      }
    #endif

    // Initialize filter states
    for (int i = 0; i < numCombs; i++) {
      combFilterStore[i] = 0;
      combWritePos[i] = 0;
    }
    for (int i = 0; i < numAllpass; i++) {
      allpassWritePos[i] = 0;
    }

    initialized = true;
  }

  /** Pre-initialize buffers (optional)
   *  Call in setup() before audioStart() to avoid potential glitches
   *  on first reverb call. If not called, initialization happens
   *  automatically on first use.
   */
  void initVerbSafe() { init(); }

  /** Set PSRAM preference for buffer allocation (ESP32 only)
   *  Must be called BEFORE init() or initVerbSafe()
   *  @param use true to use PSRAM if available (default), false to use regular RAM
   *  Note: PSRAM may have higher latency which could affect audio quality
   */
  void setUsePSRAM(bool use) {
    if (!initialized) {
      usePSRAM = use;
    }
  }

  /** Set room size (reverb length/decay)
   *  @param size 0.0-1.0, affects feedback amount in comb filters
   */
  void setReverbLength(float size) {
    size = max(0.0f, min(1.0f, size));
    float scaledSize = pow(size, 0.2f);
    // Clamp to safe range to prevent runaway feedback
    scaledSize = max(0.5f, min(0.98f, scaledSize));
    roomSize = (int16_t)(scaledSize * 1024.0f);
  }

  /** Set room size - alias for setReverbLength */
  void setReverbSize(float size) { setReverbLength(size); }

  /** Set dampening (high frequency absorption)
   *  @param damp 0.0-1.0, higher = more HF dampening (darker sound)
   */
  void setDampening(float damp) {
    damp = max(0.0f, min(1.0f, damp));
    damping = (int16_t)(damp * 1024.0f);
    // Pre-calculate damping coefficient (0.7-1.0 range)
    dampCoeff = 717 + (((1024 - damping) * 307) >> 10);
  }

  /** Set wet/dry mix
   *  @param mix 0.0 = fully dry, 1.0 = fully wet
   *  Uses power curve to reduce wet at lower values while preserving higher range
   */
  void setMix(float mix) {
    mix = max(0.0f, min(1.0f, mix));
    wetMix = (int16_t)(mix * 1024.0f);
    dryMix = (int16_t)((1.0f - mix) * 1024.0f);
  }

  /** Set reverb mix - alias for setMix */
  void setReverbMix(float mix) { setMix(mix); }

  /** Set stereo width
   *  @param width 0.0 = mono, 1.0 = full stereo
   */
  void setWidth(float width) {
    width = max(0.0f, min(1.0f, width));
    stereoWidth = (int16_t)(width * 1024.0f);
  }

  /** Process mono input
   *  @param audioIn Input sample
   *  @return Processed sample with reverb
   */
  inline int16_t reverb(int32_t audioIn) {
    if (!initialized) init();

    audioIn = clip16(audioIn);

    // Process through comb filters in parallel and sum
    int32_t combSum = processCombFilters(audioIn);

    // Process through allpass filters in series
    int32_t wet = processAllpassFilters(combSum);

    // Mix dry and wet
    int32_t output = ((audioIn * dryMix) >> 10) + ((wet * wetMix) >> 10);
    return clip16(output);
  }

  /** Process stereo input
   *  @param audioInLeft Left input
   *  @param audioInRight Right input
   *  @param audioOutLeft Left output (reference)
   *  @param audioOutRight Right output (reference)
   */
  inline void reverbStereo(int32_t audioInLeft, int32_t audioInRight,
                           int32_t &audioOutLeft, int32_t &audioOutRight) {
    if (!initialized) init();

    audioInLeft = clip16(audioInLeft);
    audioInRight = clip16(audioInRight);

    // Mix to mono for reverb processing (like original Freeverb)
    int32_t monoIn = (audioInLeft + audioInRight) >> 1;

    // Process through comb filters
    int32_t combSum = processCombFilters(monoIn);

    // Process through allpass filters
    int32_t wet = processAllpassFilters(combSum);

    // Create stereo from mono reverb with width control
    int32_t wetL = wet;
    int32_t wetR = wet;

    // Add subtle variation for stereo width using different comb outputs
    if (stereoWidth > 0) {
      // Use stored comb filter values for stereo decorrelation
      int32_t stereoOffset = (combFilterStore[0] - combFilterStore[2]) >> 3;
      wetL += (stereoOffset * stereoWidth) >> 10;
      wetR -= (stereoOffset * stereoWidth) >> 10;
    }

    // Mix dry and wet
    audioOutLeft = clip16(((audioInLeft * dryMix) >> 10) + ((wetL * wetMix) >> 10));
    audioOutRight = clip16(((audioInRight * dryMix) >> 10) + ((wetR * wetMix) >> 10));
  }

  /** Check if initialized */
  bool isInitialized() { return initialized; }

  /** Check if high quality mode is enabled */
  bool isHighQuality() { return highQuality; }

private:
  bool initialized;
  bool highQuality;       // true = 8+4, false = 4+2
  bool usePSRAM;
  int16_t wetMix;         // 0-1024
  int16_t dryMix;         // 0-1024
  int16_t roomSize;       // 0-1024 (feedback amount)
  int16_t damping;        // 0-1024 (lowpass in feedback)
  int16_t dampCoeff;      // Pre-calculated damping coefficient
  int16_t stereoWidth;    // 0-1024
  uint8_t numCombs;       // 4 or 8
  uint8_t numAllpass;     // 2 or 4

  // Base delay times (at 44100Hz)
  uint16_t combDelayBase[VERB_MAX_COMBS];
  uint16_t allpassDelayBase[VERB_MAX_ALLPASS];

  // Comb filter state
  int16_t* combBuf[VERB_MAX_COMBS];
  uint16_t combBufSize;
  uint16_t combBufMask;
  uint16_t combDelay[VERB_MAX_COMBS];
  uint16_t combWritePos[VERB_MAX_COMBS];
  int32_t combFilterStore[VERB_MAX_COMBS];

  // Allpass filter state
  int16_t* allpassBuf[VERB_MAX_ALLPASS];
  uint16_t allpassBufSize;
  uint16_t allpassBufMask;
  uint16_t allpassDelay[VERB_MAX_ALLPASS];
  uint16_t allpassWritePos[VERB_MAX_ALLPASS];

  /** Process all comb filters in parallel - optimized version */
  inline int32_t processCombFilters(int32_t input) {
    int32_t sum = 0;

    // Attenuate input to leave headroom for feedback accumulation
    // Use multiply and shift for better precision (less truncation noise)
    // High quality: *0.1 (~102/1024), Standard: *0.2 (~205/1024)
    input = highQuality ? ((input * 102) >> 10) : ((input * 205) >> 10);

    // Cache frequently used values
    const uint16_t mask = combBufMask;
    const int16_t room = roomSize;
    const int16_t damp = dampCoeff;
    const int n = numCombs;

    for (int i = 0; i < n; i++) {
      int16_t* buf = combBuf[i];
      uint16_t wp = combWritePos[i];
      uint16_t rp = (wp - combDelay[i]) & mask;

      // Read from delay line
      int32_t output = buf[rp];

      // Apply damping lowpass filter with rounding
      int32_t store = combFilterStore[i];
      int32_t diff = ((output - store) * damp + 512) >> 10;  // +512 for rounding
      store += diff;
      combFilterStore[i] = store;

      // Write back with feedback (with rounding)
      int32_t toWrite = input + ((store * room + 512) >> 10);

      // Soft limiting to prevent harsh clipping in feedback loop
      if (toWrite > 24576) {
        toWrite = 24576 + ((toWrite - 24576) >> 2);
      } else if (toWrite < -24576) {
        toWrite = -24576 + ((toWrite + 24576) >> 2);
      }
      // Clamp to 16-bit range
      if (toWrite > MAX_16) toWrite = MAX_16;
      else if (toWrite < MIN_16) toWrite = MIN_16;
      buf[wp] = (int16_t)toWrite;

      // Advance write position
      combWritePos[i] = (wp + 1) & mask;

      // Accumulate output
      sum += output;
    }

    // Scale sum based on number of filters
    // High quality (8 filters): divide by 8 = >> 3
    // Standard (4 filters): divide by 4 = >> 2
    return highQuality ? (sum >> 3) : (sum >> 2);
  }

  /** Process allpass filters in series - optimized version */
  inline int32_t processAllpassFilters(int32_t input) {
    int32_t signal = input;
    const uint16_t mask = allpassBufMask;
    const int n = numAllpass;

    for (int i = 0; i < n; i++) {
      int16_t* buf = allpassBuf[i];
      uint16_t wp = allpassWritePos[i];
      uint16_t rp = (wp - allpassDelay[i]) & mask;

      // Read delayed sample
      int32_t delayed = buf[rp];

      // Allpass formula with g=0.5 (no multiply needed, just bit shift)
      int32_t toWrite = signal + (delayed >> 1);
      // Clamp to 16-bit range
      if (toWrite > MAX_16) toWrite = MAX_16;
      else if (toWrite < MIN_16) toWrite = MIN_16;
      buf[wp] = (int16_t)toWrite;

      signal = delayed - signal;

      // Advance write position
      allpassWritePos[i] = (wp + 1) & mask;
    }

    return signal;
  }
};

#endif /* VERB_H_ */
