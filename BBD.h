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
  uint32_t _targetScanRate = 32768; // Slew target set by setScanRate()/setTime()
  static const uint32_t SCAN_RATE_SLEW = 32; // Fixed-point units/sample (~42ms full-range slew)
  uint16_t bufferIndex = 0;

  int16_t delayLevel = 1024;
  int16_t _targetDelayLevel = 1024;
  int16_t feedbackLevel = 512;
  int16_t _targetFeedbackLevel = 512;
  bool delayFeedback = false;
  static const int16_t LEVEL_SLEW = 4; // Units/sample for level/feedback slew (~6ms full range)

  int16_t prevOutValue = 0;
  int16_t holdValue = 0;
  int32_t smoothedOut = 0;  // Per-sample smoothed output
  int16_t lastResult = 0;   // Cached result for non-blocking dual-core
  uint16_t smoothCoeff = 8192;  // Smoothing coefficient (higher = more smoothing)
  int32_t inputAccum = 0;
  uint8_t inputCount = 0;
  byte filtered = 1; 

  // Base delay at scanRate 1.0: 4096 / 44100 * 1000 = 92.88ms
  static constexpr float BASE_DELAY_MS = (float)BUFFER_SIZE / 44100.0f * 1000.0f;

  /** Soft saturation for analog warmth
   * Applies gentle compression above threshold
   */
  inline int16_t softSaturate(int32_t x) {
    // Threshold chosen so 4:1 compression above it keeps output below MAX_16
    // for any realistic feedback+input sum (up to ~64000):
    //   20000 + (64000-20000)/4 = 31000 < MAX_16 — hard guard never fires
    const int32_t threshold = 20000;
    if (x > threshold) {
      x = threshold + ((x - threshold) >> 2);
    } else if (x < -threshold) {
      x = -threshold + ((x + threshold) >> 2);
    }
    if (x > MAX_16) return MAX_16;
    if (x < MIN_16) return MIN_16;
    return x;
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
    _targetScanRate = (uint32_t)(rate * 65536.0f);
    if (_targetScanRate < 655) _targetScanRate = 655;
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
    _targetScanRate = (uint32_t)(rate * 65536.0f);
    if (_targetScanRate < 655) _targetScanRate = 655;
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
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    M16_ATOMIC_GUARD_BLOCKING(_bbdLock, {
      // Slew all hot parameters to avoid buffer-seam clicks on adjustment
      if (scanRate != _targetScanRate) {
        if (_targetScanRate > scanRate) {
          uint32_t d = _targetScanRate - scanRate;
          scanRate += (d < SCAN_RATE_SLEW) ? d : SCAN_RATE_SLEW;
        } else {
          uint32_t d = scanRate - _targetScanRate;
          scanRate -= (d < SCAN_RATE_SLEW) ? d : SCAN_RATE_SLEW;
        }
        uint32_t rfs = scanRate < 65536U ? scanRate : 65536U;
        smoothCoeff = (uint16_t)(2048U + ((rfs * 6144U) >> 16));
      }
      if (delayLevel != _targetDelayLevel) {
        int16_t d = _targetDelayLevel - delayLevel;
        delayLevel += (d > LEVEL_SLEW) ? LEVEL_SLEW : (d < -LEVEL_SLEW) ? -LEVEL_SLEW : d;
      }
      if (feedbackLevel != _targetFeedbackLevel) {
        int16_t d = _targetFeedbackLevel - feedbackLevel;
        feedbackLevel += (d > LEVEL_SLEW) ? LEVEL_SLEW : (d < -LEVEL_SLEW) ? -LEVEL_SLEW : d;
      }
      // Accumulate inputs for anti-aliasing at low scan rates
      inputAccum += inValue;
      inputCount++;
      uint32_t prevPhase = phase;
      phase += scanRate;
      // Calculate how many buffer positions we advanced (supports rate > 1.0)
      uint16_t prevPos = prevPhase >> 16;
      uint16_t currPos = phase >> 16;
      // Handle wrap-around
      if (phase >= (BUFFER_SIZE << 16)) {
        phase -= (BUFFER_SIZE << 16);
        currPos = (currPos >= BUFFER_SIZE) ? currPos - BUFFER_SIZE : currPos;
      }
      // Calculate steps (handling wrap-around)
      int16_t steps = currPos - prevPos;
      if (steps < 0) steps += BUFFER_SIZE;  // Wrapped around
      if (steps > 0) {
        // Read from buffer
        int32_t outValue = delayBuffer[bufferIndex];
        // Output low-pass filtering
        if (filtered > 0) {
          if (filtered == 1) {
            outValue = (outValue * 3 + prevOutValue) >> 2;
          } else if (filtered == 2) {
            outValue = (outValue + prevOutValue) >> 1;
          } else if (filtered == 3) {
            outValue = (outValue + prevOutValue * 3) >> 2;
          } else {
            outValue = (outValue + prevOutValue * 7) >> 3;
          }
          prevOutValue = outValue;
        }
        holdValue = (outValue * delayLevel) >> 10;
        // Average accumulated inputs
        int32_t writeValue;
        if (inputCount > 0) {
          writeValue = inputAccum / inputCount;
          inputAccum = 0;
          inputCount = 0;
        } else {
          writeValue = inValue;
        }
        // Apply feedback with slight gain reduction
        if (delayFeedback) {
          writeValue = writeValue + ((holdValue * feedbackLevel) >> 10);
          writeValue = (writeValue * 251) >> 8;
        }
        // Soft saturation and write to buffer
        writeValue = softSaturate(writeValue);
        delayBuffer[bufferIndex] = writeValue;
        // Advance buffer by number of steps (enables shorter delays at rate > 1.0)
        bufferIndex = (bufferIndex + steps) & BUFFER_MASK;
      }
      // Smooth output every sample to reduce staircase buzz
      // One-pole lowpass with rate-proportional smoothing (more at longer delays)
      smoothedOut += ((holdValue - smoothedOut) * smoothCoeff) >> 15;
      lastResult = (int16_t)smoothedOut;
    });
    #else
    // Slew all hot parameters to avoid buffer-seam clicks on adjustment
    if (scanRate != _targetScanRate) {
      if (_targetScanRate > scanRate) {
        uint32_t d = _targetScanRate - scanRate;
        scanRate += (d < SCAN_RATE_SLEW) ? d : SCAN_RATE_SLEW;
      } else {
        uint32_t d = scanRate - _targetScanRate;
        scanRate -= (d < SCAN_RATE_SLEW) ? d : SCAN_RATE_SLEW;
      }
      uint32_t rfs = scanRate < 65536U ? scanRate : 65536U;
      smoothCoeff = (uint16_t)(2048U + ((rfs * 6144U) >> 16));
    }
    if (delayLevel != _targetDelayLevel) {
      int16_t d = _targetDelayLevel - delayLevel;
      delayLevel += (d > LEVEL_SLEW) ? LEVEL_SLEW : (d < -LEVEL_SLEW) ? -LEVEL_SLEW : d;
    }
    if (feedbackLevel != _targetFeedbackLevel) {
      int16_t d = _targetFeedbackLevel - feedbackLevel;
      feedbackLevel += (d > LEVEL_SLEW) ? LEVEL_SLEW : (d < -LEVEL_SLEW) ? -LEVEL_SLEW : d;
    }
    // Accumulate inputs for anti-aliasing at low scan rates
    inputAccum += inValue;
    inputCount++;
    uint32_t prevPhase = phase;
    phase += scanRate;
    // Calculate how many buffer positions we advanced (supports rate > 1.0)
    uint16_t prevPos = prevPhase >> 16;
    uint16_t currPos = phase >> 16;
    // Handle wrap-around
    if (phase >= (BUFFER_SIZE << 16)) {
      phase -= (BUFFER_SIZE << 16);
      currPos = (currPos >= BUFFER_SIZE) ? currPos - BUFFER_SIZE : currPos;
    }
    // Calculate steps (handling wrap-around)
    int16_t steps = currPos - prevPos;
    if (steps < 0) steps += BUFFER_SIZE;  // Wrapped around
    if (steps > 0) {
      // Read from buffer
      int32_t outValue = delayBuffer[bufferIndex];
      // Output low-pass filtering
      if (filtered > 0) {
        if (filtered == 1) {
          outValue = (outValue * 3 + prevOutValue) >> 2;
        } else if (filtered == 2) {
          outValue = (outValue + prevOutValue) >> 1;
        } else if (filtered == 3) {
          outValue = (outValue + prevOutValue * 3) >> 2;
        } else {
          outValue = (outValue + prevOutValue * 7) >> 3;
        }
        prevOutValue = outValue;
      }
      holdValue = (outValue * delayLevel) >> 10;
      // Average accumulated inputs
      int32_t writeValue;
      if (inputCount > 0) {
        writeValue = inputAccum / inputCount;
        inputAccum = 0;
        inputCount = 0;
      } else {
        writeValue = inValue;
      }
      // Apply feedback with slight gain reduction
      if (delayFeedback) {
        writeValue = writeValue + ((holdValue * feedbackLevel) >> 10);
        writeValue = (writeValue * 251) >> 8;
      }
      // Soft saturation and write to buffer
      writeValue = softSaturate(writeValue);
      delayBuffer[bufferIndex] = writeValue;
      // Advance buffer by number of steps (enables shorter delays at rate > 1.0)
      bufferIndex = (bufferIndex + steps) & BUFFER_MASK;
    }
    // Smooth output every sample to reduce staircase buzz
    // One-pole lowpass with rate-proportional smoothing (more at longer delays)
    smoothedOut += ((holdValue - smoothedOut) * smoothCoeff) >> 15;
    lastResult = (int16_t)smoothedOut;
    #endif
    return lastResult;
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
