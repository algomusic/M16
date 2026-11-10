/*
 * BBD.h
 *
 * A Bucket Brigade Delay emulation class.
 * Mimics the sonic character of analog BBD and tape delays.
 *
 * Features:
 * - Variable scan rate through fixed buffer (like BBD clock rate)
 * - Natural high-frequency rolloff at longer delays
 * - Soft saturation for analog warmth
 * - Sample-and-hold output between clock ticks
 * - Drop-in replacement for Del.h
 *
 * by Andrew R. Brown 2025
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef BBD_H_
#define BBD_H_

class BBD {

public:
  #if IS_ESP32() || IS_RP2040()
  std::atomic<bool> _bbdLock{false};
  #endif

private:
  static const uint16_t BUFFER_SIZE = 4096;
  static const uint16_t BUFFER_MASK = BUFFER_SIZE - 1;

  int16_t delayBuffer[BUFFER_SIZE];

  uint32_t phase = 0;
  uint32_t scanRate = 32768;        // Fixed-point 16.16: 65536 = 1.0, supports up to 3.0
  #if IS_ESP32() || IS_RP2040()
  std::atomic<uint32_t> _targetScanRate{32768};
  #else
  uint32_t _targetScanRate = 32768;
  #endif
  static const uint32_t SCAN_RATE_SLEW = 32; // Fixed-point units/sample (~42ms full-range slew)
  uint16_t bufferIndex = 0;

  int16_t delayLevel = 1024;
  #if IS_ESP32() || IS_RP2040()
  std::atomic<int16_t> _targetDelayLevel{1024};
  #else
  int16_t _targetDelayLevel = 1024;
  #endif
  #if IS_ESP32() || IS_RP2040()
  std::atomic<int16_t> delayMix{1024};
  std::atomic<uint32_t> delayMixGains{1024UL << 16};
  #else
  int16_t delayMix = 1024;
  uint32_t delayMixGains = 1024UL << 16;
  #endif
  int16_t feedbackLevel = 512;
  #if IS_ESP32() || IS_RP2040()
  std::atomic<int16_t> _targetFeedbackLevel{512};
  #else
  int16_t _targetFeedbackLevel = 512;
  #endif
  #if IS_ESP32() || IS_RP2040()
  std::atomic<bool> delayFeedback{false};
  #else
  bool delayFeedback = false;
  #endif
  static const int16_t LEVEL_SLEW = 4; // Units/sample for level/feedback slew (~6ms full range)

  int16_t prevOutValue = 0;
  int16_t holdValue = 0;
  int32_t smoothedOut = 0;  // Per-sample smoothed output
  int16_t lastResult = 0;   // Cached result for non-blocking dual-core
  uint16_t smoothCoeff = 8192;  // Smoothing coefficient (higher = more smoothing)
  int32_t inputAccum = 0;
  uint8_t inputCount = 0;
  #if IS_ESP32() || IS_RP2040()
  std::atomic<byte> filtered{1};
  #else
  byte filtered = 1;
  #endif

  // Base delay at scanRate 1.0: 4096 / 44100 * 1000 = 92.88ms
  static constexpr float BASE_DELAY_MS = (float)BUFFER_SIZE / 44100.0f * 1000.0f;

  /** Soft-knee compression for feedback-loop sums, matching Del.h. */
  inline int16_t softSaturate(int32_t x) {
    const int32_t knee = 24576; // 75% of positive full scale
    const int32_t range = 8192;
    const int32_t magnitude = x < 0 ? -x : x;
    if (magnitude <= knee) return (int16_t)x;

    const int32_t overKnee = magnitude - knee;
    const int32_t compressed = knee +
        (overKnee * range) / (overKnee + range);
    return (int16_t)(x < 0 ? -compressed : compressed);
  }

public:
  /** Default constructor */
  BBD() {
    empty();
  }

  /** Constructor for Del.h compatibility
   * @param maxDelayTime Ignored - included for API compatibility
   */
  BBD(unsigned int maxDelayTime) {
    empty();
    float requestedMs = maxDelayTime;
    if (requestedMs > BASE_DELAY_MS) {
      setTime(requestedMs * 0.5f);
    }
    scanRate = _targetScanRate;
    uint32_t rfs = scanRate < 65536U ? scanRate : 65536U;
    smoothCoeff = (uint16_t)(2048U + ((rfs * 6144U) >> 16));
  }

  /** Constructor with full parameters
   * @param maxDelayTime Ignored - included for API compatibility
   * @param msDur Initial delay time in milliseconds
   * @param level Output level 0.0-1.0
   * @param feedback Enable feedback
   */
  BBD(unsigned int maxDelayTime, int msDur, float level, bool feedback) {
    empty();
    setTime(msDur);
    setLevel(level);
    setFeedback(feedback);
    // Apply all targets immediately at construction (no audio playing yet)
    scanRate = _targetScanRate;
    delayLevel = _targetDelayLevel;
    uint32_t rfs = scanRate < 65536U ? scanRate : 65536U;
    smoothCoeff = (uint16_t)(2048U + ((rfs * 6144U) >> 16));
  }

  ~BBD() {}

  /** No-op for BBD - included for Del.h API compatibility */
  void setMaxDelayTime(unsigned int maxDelayTime) {}

  /** @return Maximum delay time in ms (at minimum scan rate) */
  float getBufferSize() {
    return BASE_DELAY_MS / 0.01f;
  }

  /** @return Current effective delay length in samples */
  unsigned int getDelayLength() {
    float rate = scanRate / 65536.0f;
    if (rate < 0.01f) rate = 0.01f;
    return (unsigned int)(BUFFER_SIZE / rate);
  }

  /** @return Buffer size in samples */
  unsigned int getBufferLength() {
    return BUFFER_SIZE;
  }

  /** Set delay time - longer delays produce darker sound
   * @param msDur Delay time in milliseconds (~31ms to ~9000ms)
   */
  void setTime(float msDur) {
    float minDelay = BASE_DELAY_MS / 3.0f;  // ~31ms minimum
    msDur = max(minDelay, msDur);
    float rate = BASE_DELAY_MS / msDur;
    rate = max(0.01f, min(3.0f, rate));  // Allow up to 3x scan rate for short delays
    _targetScanRate = max((uint32_t)655, (uint32_t)(rate * 65536.0f));
  }

  /** @return Current delay time in milliseconds */
  float getTime() {
    float rate = scanRate / 65536.0f;
    if (rate < 0.01f) rate = 0.01f;
    return BASE_DELAY_MS / rate;
  }

  /** Set scan rate directly (BBD clock rate equivalent)
   * Higher = shorter delay, brighter. Lower = longer delay, darker.
   * @param rate Scan rate 0.01 to 3.0
   */
  void setScanRate(float rate) {
    rate = max(0.01f, min(3.0f, rate));  // Allow up to 3x for short delays
    _targetScanRate = max((uint32_t)655, (uint32_t)(rate * 65536.0f));
  }

  /** @return Current scan rate (0.01 to 1.0) */
  float getScanRate() {
    return _targetScanRate / 65536.0f;
  }

  /** @param level Output level 0.0 to 1.0 */
  void setLevel(float level) {
    _targetDelayLevel = min((int32_t)1024, max((int32_t)0, (int32_t)(pow(level, 0.8) * 1024.0f)));
  }

  /** @return Output level (0.0 to 1.0) */
  float getLevel() {
    return _targetDelayLevel * 0.0009765625f;
  }

  /** Set the delay dry/wet mix using a headroom-normalized equal-power curve.
   * @param mix 0.0 is fully dry, 1.0 is fully wet. Defaults to 1.0 so
   * existing code continues to receive only the delayed signal from next().
   * The gain ratio follows an equal-power curve while the coefficient sum
   * stays at unity, preventing correlated dry/wet peaks from overloading.
   */
  void setDelayMix(float mix) {
    const float mixPosition = fmaxf(0.0f, fminf(1.0f, mix));
    const int16_t mixQ10 = (int16_t)(mixPosition * 1024.0f + 0.5f);
    const float angle = mixPosition * 1.57079632679f;
    const float dryPowerGain = cosf(angle);
    const float wetPowerGain = sinf(angle);
    const float gainSum = dryPowerGain + wetPowerGain;
    const uint16_t dryGain = gainSum > 0.0f
        ? (uint16_t)(dryPowerGain * (1024.0f / gainSum) + 0.5f)
        : 1024;
    const uint16_t boundedDryGain = min((uint16_t)1024, dryGain);
    const uint16_t wetGain = 1024 - boundedDryGain;
    delayMix = mixQ10;
    delayMixGains = ((uint32_t)wetGain << 16) | boundedDryGain;
  }

  /** @return Delay dry/wet mix, from 0.0 to 1.0. */
  float getDelayMix() {
    return delayMix * 0.0009765625f;
  }

  /** @param state true to enable feedback */
  void setFeedback(bool state) {
    delayFeedback = state;
  }

  /** Set feedback level (also enables feedback)
   * @param level Feedback level 0.0 to 1.0
   */
  void setFeedbackLevel(float level) {
    setFeedback(true);
    _targetFeedbackLevel = min((int32_t)1024, max((int32_t)0, (int32_t)(pow(level, 0.8) * 1024.0f)));
  }

  /** @return Feedback level (0.0 to 1.0) */
  float getFeedbackLevel() {
    return _targetFeedbackLevel * 0.0009765625f;
  }

  /** @param newVal Filter amount 0=none, 4=darkest */
  void setFiltered(byte newVal) {
    filtered = newVal;
  }

  /** @return Current filter setting */
  byte getFiltered() {
    return filtered;
  }

  /** Clear the delay buffer */
  void empty() {
    for (int i = 0; i < BUFFER_SIZE; i++) {
      delayBuffer[i] = 0;
    }
    phase = 0;
    bufferIndex = 0;
    holdValue = 0;
    smoothedOut = 0;
    prevOutValue = 0;
    inputAccum = 0;
    inputCount = 0;
    lastResult = 0;
    scanRate = _targetScanRate; // flush any pending slews
    delayLevel = _targetDelayLevel;
    feedbackLevel = _targetFeedbackLevel;
    uint32_t rfs = scanRate < 65536U ? scanRate : 65536U;
    smoothCoeff = (uint16_t)(2048U + ((rfs * 6144U) >> 16));
  }

  /** Process one sample through the BBD
   * @param inValue Input sample
   * @return Delayed output sample
   */
  inline int16_t next(int32_t inValue) {
    #if IS_ESP32() || IS_RP2040()
    int16_t output = 0;
    M16_ATOMIC_GUARD_BLOCKING(_bbdLock, { output = nextUnlocked(inValue); });
    return output;
    #else
    return nextUnlocked(inValue);
    #endif
  }

  /** Process separate dry and delay-send inputs. The dry signal is mixed
   * unchanged while delaySendValue is accumulated into the BBD buffer.
   */
  inline int16_t next(int32_t dryValue, int32_t delaySendValue) {
    #if IS_ESP32() || IS_RP2040()
    int16_t output = 0;
    M16_ATOMIC_GUARD_BLOCKING(_bbdLock, {
      output = nextUnlocked(dryValue, delaySendValue);
    });
    return output;
    #else
    return nextUnlocked(dryValue, delaySendValue);
    #endif
  }

  /** Process one sample without acquiring the BBD state lock.
   * Use only when exactly one audio core owns and advances this instance, such
   * as a master delay inside the post-combine callback. Parameter targets may
   * still be changed at control rate; targets use atomic publication on dual-core targets and each render call
   * snapshots them once before processing.
   * @param inValue Input sample
   * @return Delayed output sample
   */
  inline int16_t nextUnlocked(int32_t inValue) {
    return nextUnlocked(inValue, inValue);
  }

  /** Process separate dry and delay-send inputs without acquiring the BBD
   * state lock. Use only when one audio core owns this instance.
   */
  inline int16_t nextUnlocked(int32_t dryInput, int32_t delaySendInput) {
    const uint32_t render__targetScanRate = _targetScanRate;
    const int16_t render__targetDelayLevel = _targetDelayLevel;
    const uint32_t renderDelayMixGains = delayMixGains;
    const int32_t renderDryGain = renderDelayMixGains & 0xFFFF;
    const int32_t renderWetGain = renderDelayMixGains >> 16;
    const int16_t render__targetFeedbackLevel = _targetFeedbackLevel;
    const bool render_delayFeedback = delayFeedback;
    const byte render_filtered = filtered;
    // Slew all hot parameters to avoid buffer-seam clicks on adjustment.
    if (scanRate != render__targetScanRate) {
      if (render__targetScanRate > scanRate) {
        uint32_t d = render__targetScanRate - scanRate;
        scanRate += (d < SCAN_RATE_SLEW) ? d : SCAN_RATE_SLEW;
      } else {
        uint32_t d = scanRate - render__targetScanRate;
        scanRate -= (d < SCAN_RATE_SLEW) ? d : SCAN_RATE_SLEW;
      }
      uint32_t rfs = scanRate < 65536U ? scanRate : 65536U;
      smoothCoeff = (uint16_t)(2048U + ((rfs * 6144U) >> 16));
    }
    if (delayLevel != render__targetDelayLevel) {
      int16_t d = render__targetDelayLevel - delayLevel;
      delayLevel += (d > LEVEL_SLEW) ? LEVEL_SLEW : (d < -LEVEL_SLEW) ? -LEVEL_SLEW : d;
    }
    if (feedbackLevel != render__targetFeedbackLevel) {
      int16_t d = render__targetFeedbackLevel - feedbackLevel;
      feedbackLevel += (d > LEVEL_SLEW) ? LEVEL_SLEW : (d < -LEVEL_SLEW) ? -LEVEL_SLEW : d;
    }

    // Accumulate inputs for anti-aliasing at low scan rates.
    inputAccum += delaySendInput;
    inputCount++;
    uint32_t prevPhase = phase;
    phase += scanRate;
    uint16_t prevPos = prevPhase >> 16;
    uint16_t currPos = phase >> 16;
    if (phase >= (BUFFER_SIZE << 16)) {
      phase -= (BUFFER_SIZE << 16);
      currPos = (currPos >= BUFFER_SIZE) ? currPos - BUFFER_SIZE : currPos;
    }
    int16_t steps = currPos - prevPos;
    if (steps < 0) steps += BUFFER_SIZE;

    if (steps > 0) {
      int32_t outValue = delayBuffer[bufferIndex];
      if (render_filtered > 0) {
        if (render_filtered == 1) {
          outValue = (outValue * 3 + prevOutValue) >> 2;
        } else if (render_filtered == 2) {
          outValue = (outValue + prevOutValue) >> 1;
        } else if (render_filtered == 3) {
          outValue = (outValue + prevOutValue * 3) >> 2;
        } else {
          outValue = (outValue + prevOutValue * 7) >> 3;
        }
        prevOutValue = outValue;
      }
      holdValue = (outValue * delayLevel) >> 10;

      int32_t writeValue;
      if (inputCount > 0) {
        // At short delay times a BBD step can consume exactly one input
        // sample. Avoid the variable integer divide in that common case.
        writeValue = inputCount == 1
            ? inputAccum : inputAccum / inputCount;
        inputAccum = 0;
        inputCount = 0;
      } else {
        writeValue = delaySendInput;
      }
      if (render_delayFeedback) {
        writeValue += (holdValue * feedbackLevel) >> 10;
      }
      delayBuffer[bufferIndex] = softSaturate(writeValue);
      bufferIndex = (bufferIndex + steps) & BUFFER_MASK;
    }

    smoothedOut += ((holdValue - smoothedOut) * smoothCoeff) >> 15;
    lastResult = (int16_t)smoothedOut;
    if (renderDryGain >= 1024) return clip16(dryInput);
    if (renderDryGain == 0) return lastResult;
    // Keep the direct input linear. If the equal-power blend exceeds the
    // output range, reduce only the wet contribution; saturating the combined
    // signal would add BBD-style harmonics to the initial dry tone as well.
    int32_t dryValue = (int32_t)(((int64_t)dryInput * renderDryGain) >> 10);
    int32_t wetValue = (int32_t)(((int64_t)lastResult * renderWetGain) >> 10);
    dryValue = max((int32_t)MIN_16, min((int32_t)MAX_16, dryValue));
    const int32_t wetMinimum = (int32_t)MIN_16 - dryValue;
    const int32_t wetMaximum = (int32_t)MAX_16 - dryValue;
    wetValue = max(wetMinimum, min(wetMaximum, wetValue));
    return (int16_t)(dryValue + wetValue);
  }

  /** @return Current hold value */
  inline int16_t read() {
    return holdValue;
  }

  /** Read with offset - for Del.h compatibility
   * @param pos Ignored for BBD
   * @return Current hold value
   */
  inline int16_t read(int pos) {
    return holdValue;
  }

  /** Write to delay - for Del.h compatibility
   * @param inValue Input sample
   */
  inline void write(int inValue) {
    next(inValue);
  }
};

#endif /* BBD_H_ */
