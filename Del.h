/*
 * Del.h
 *
 * An audio delay line class.
 *
 * by Andrew R. Brown 2022
 *
 * Based on the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef DEL_H_
#define DEL_H_

class Del {

private:
  #if IS_ESP32() || IS_RP2040()
  std::atomic<bool> _delLock{false};
  #endif
  int16_t * delayBuffer = nullptr;
  unsigned int writePos = 0;
  float delayTime_ms = 0.0f;
  #if IS_ESP32() || IS_RP2040()
  std::atomic<unsigned int> delayTime_samples{0};
  #else
  unsigned int delayTime_samples = 0;
  #endif
  #if IS_ESP32() || IS_RP2040()
  std::atomic<int16_t> delayLevel{1024};
  #else
  int16_t delayLevel = 1024;
  #endif
  float maxDelayTime_ms = 0;
  unsigned int delayBufferSize_samples = 0;
  #if IS_ESP32() || IS_RP2040()
  std::atomic<bool> delayFeedback{false};
  #else
  bool delayFeedback = false;
  #endif
  int16_t prevOutValue = 0;
  #if IS_ESP32() || IS_RP2040()
  std::atomic<byte> filtered{2};
  std::atomic<int16_t> feedbackLevel{512};
  std::atomic<int16_t> delayMix{1024};
  std::atomic<uint32_t> delayMixGains{1024UL << 16};
  #else
  byte filtered = 2;
  int16_t feedbackLevel = 512; // 0 to 1024
  int16_t delayMix = 1024;
  uint32_t delayMixGains = 1024UL << 16;
  #endif
  bool usePSRAM = false;

  /** Compress feedback-loop sums above a soft knee without changing normal
   * 16-bit signals. The bounded inputs keep every intermediate in int32_t.
   */
  static inline int16_t softClipFeedbackSum(int32_t sample) {
    const int32_t knee = 24576; // 75% of positive full scale
    const int32_t range = 8192;
    const int32_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude <= knee) return (int16_t)sample;

    const int32_t overKnee = magnitude - knee;
    const int32_t compressed = knee +
        (overKnee * range) / (overKnee + range);
    return (int16_t)(sample < 0 ? -compressed : compressed);
  }

  void releaseBuffer() {
    if (!delayBuffer) return;
    #if IS_ESP32()
      if (usePSRAM) free(delayBuffer);
      else delete[] delayBuffer;
    #else
      delete[] delayBuffer;
    #endif
    delayBuffer = nullptr;
    usePSRAM = false;
  }

public:
  /** Constructor.
	* Create but don't setup delay.
  * To use, setMaxDecayTime() must be called to initiate the audio buffer.
	*/
	Del() : delayBuffer(nullptr) {}; 

  /** Constructor.
	* Create and setup delay.
  * @param maxDelayTime The maximum delay time in milliseconds
  * @param msDur The initial delay time in milliseconds, up to maxDelayTime
  * @param level The delay feedback level, from 0.0 to 1.0 
  * @param feedback Multitap delay feedback on or off, true or false
	*/
	Del(unsigned int maxDelayTime, int msDur, float level, bool feedback) {
    setMaxDelayTime(maxDelayTime);
    setTime(msDur);
    setLevel(level);
    setFeedback(feedback);
  }

  /**
   * Set the maximum delay time in milliseconds
   * @param maxDelayTime The maximum delay time in milliseconds
   */
  void setMaxDelayTime(unsigned int maxDelayTime) {
    releaseBuffer();
    maxDelayTime_ms = max((unsigned int)0, maxDelayTime);

    const unsigned int MAX_SAFE_DELAY_MS = 4294967295U / SAMPLE_RATE;  // ~97,000ms at 44.1kHz
    if (maxDelayTime_ms > MAX_SAFE_DELAY_MS) {
      Serial.print("WARNING: Delay time ");
      Serial.print(maxDelayTime_ms);
      Serial.print("ms exceeds safe maximum ");
      Serial.print(MAX_SAFE_DELAY_MS);
      Serial.println("ms, capping to maximum");
      maxDelayTime_ms = MAX_SAFE_DELAY_MS;
    }

    delayBufferSize_samples = maxDelayTime_ms * SAMPLE_RATE * 0.001;

    #if IS_ESP32()
      // Try PSRAM allocation with size checking (silent for delay buffers)
      delayBuffer = psramAllocInt16(delayBufferSize_samples, nullptr);
      usePSRAM = delayBuffer != nullptr;
      if (!delayBuffer) {
        // Fallback to regular RAM — use nothrow so a failed alloc returns nullptr
        // instead of calling abort() and triggering SW_CPU_RESET on low-heap boards
        delayBuffer = new(std::nothrow) int16_t[delayBufferSize_samples];
        usePSRAM = false;
      }
    #else
      delayBuffer = new(std::nothrow) int16_t[delayBufferSize_samples];
      usePSRAM = false;
    #endif

    if (!delayBuffer) {
      Serial.println("ERROR: Del buffer allocation failed!");
      delayBufferSize_samples = 0;
      maxDelayTime_ms = 0;
      return;
    }
    empty();
  }

  /** Constructor.
	@param maxDelayTime The size of the delay buffer, in milliseconds.
	*/
	Del(unsigned int maxDelayTime) {
    setMaxDelayTime(maxDelayTime);
  }

  ~Del() {
    releaseBuffer();
  }

  /** Return the size of the delay buffer in ms */
  float getBufferSize() {
    return maxDelayTime_ms;
  }

  /** Return the delay length in samples */
  unsigned int getDelayLength() {
    #if IS_ESP32() || IS_RP2040()
    return delayTime_samples.load(std::memory_order_relaxed);
    #else
    return delayTime_samples;
    #endif
  }

  /** Return the size of the delay buffer in samples */
  unsigned int getBufferLength() {
    return delayBufferSize_samples;
  }

  /** Specify the delay duration in milliseconds.
   *  Automatically allocates buffer if not yet allocated or if too small.
   */
  void setTime(float msDur) {
    msDur = max(0.0f, msDur);
    // Auto-allocate buffer if needed (not allocated or too small)
    if (!delayBuffer || msDur > maxDelayTime_ms) {
      // Add 10% headroom to reduce reallocations from small time changes
      unsigned int newMaxTime = (unsigned int)(msDur * 1.1f) + 1;
      setMaxDelayTime(newMaxTime);
    }
    delayTime_ms = max(0.0f, min(maxDelayTime_ms > 0 ? maxDelayTime_ms - 1.0f : 0.0f, msDur));
    delayTime_samples = (unsigned int)(delayTime_ms * SAMPLE_RATE * 0.001f);
  }

  /** Return the delay duration in milliseconds */
  float getTime() {
    return delayTime_ms;
  }

  /** Set delay time via scan rate (BBD.h API compatibility)
   *  @param rate Scan rate 0.01-1.0, where 1.0 = ~93ms, 0.1 = ~930ms
   */
  void setScanRate(float rate) {
    const float BASE_DELAY_MS = 92.88f; // 4096 samples at 44100Hz
    rate = max(0.01f, min(1.0f, rate));
    setTime(BASE_DELAY_MS / rate);
  }

  /** Return current scan rate (BBD.h API compatibility) */
  float getScanRate() {
    const float BASE_DELAY_MS = 92.88f;
    if (delayTime_ms < BASE_DELAY_MS) return 1.0f;
    return max(0.01f, BASE_DELAY_MS / delayTime_ms);
  }

  /** Specify the delay feedback level, from 0.0 to 1.0 */
  void setLevel(float level) {
    const int16_t target = min((int32_t)1024,
        max((int32_t)0, (int32_t)(pow(level, 0.8) * 1024.0f)));
    #if IS_ESP32() || IS_RP2040()
    delayLevel.store(target, std::memory_order_relaxed);
    #else
    delayLevel = target;
    #endif
  }

  /** Return the delay level, from 0.0 to 1.0 */
  float getLevel() {
    #if IS_ESP32() || IS_RP2040()
    return delayLevel.load(std::memory_order_relaxed) * 0.0009765625f;
    #else
    return delayLevel * 0.0009765625f;
    #endif
  }

  /** Turn delay feedback on or off */
  void setFeedback(bool state) {
    #if IS_ESP32() || IS_RP2040()
    delayFeedback.store(state, std::memory_order_relaxed);
    #else
    delayFeedback = state;
    #endif
  }

  /** Specify the delay feedback level, from 0.0 to 1.0 */
  void setFeedbackLevel(float level) {
    setFeedback(true); // ensure feedback is on
    const int16_t target = min((int32_t)1024,
        max((int32_t)0, (int32_t)(pow(level, 0.8) * 1024.0f)));
    #if IS_ESP32() || IS_RP2040()
    feedbackLevel.store(target, std::memory_order_relaxed);
    #else
    feedbackLevel = target;
    #endif
  }

  /** Return the delay level, from 0.0 to 1.0 */
  float getFeedbackLevel() {
    #if IS_ESP32() || IS_RP2040()
    return feedbackLevel.load(std::memory_order_relaxed) * 0.0009765625f;
    #else
    return feedbackLevel * 0.0009765625f;
    #endif
  }

  /** Set a headroom-normalized equal-power dry/wet mix, matching BBD. */
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
    const uint32_t packedGains = ((uint32_t)wetGain << 16) | boundedDryGain;
    #if IS_ESP32() || IS_RP2040()
    delayMix.store(mixQ10, std::memory_order_relaxed);
    delayMixGains.store(packedGains, std::memory_order_relaxed);
    #else
    delayMix = mixQ10;
    delayMixGains = packedGains;
    #endif
  }

  /** Return the dry/wet mix from 0 (dry) to 1 (wet). */
  float getDelayMix() {
    #if IS_ESP32() || IS_RP2040()
    return delayMix.load(std::memory_order_relaxed) * 0.0009765625f;
    #else
    return delayMix * 0.0009765625f;
    #endif
  }

  /** Specify the degree of filtering of the delay signal, from 0 (none) to 4 (most dull) */
  void setFiltered(byte newVal) {
    #if IS_ESP32() || IS_RP2040()
    filtered.store(newVal, std::memory_order_relaxed);
    #else
    filtered = newVal;
    #endif
  }

  /** Return the delay filtering amount, matching BBD.h. */
  byte getFiltered() {
    #if IS_ESP32() || IS_RP2040()
    return filtered.load(std::memory_order_relaxed);
    #else
    return filtered;
    #endif
  }

  /** Fill the delay with silence */
  void empty() {
    if (!delayBuffer) return; 
    for(unsigned int i=0; i<delayBufferSize_samples; i++) {
      delayBuffer[i] = 0; // zero out the buffer
    }
    writePos = 0;
    prevOutValue = 0;
  }

  /** Input a value to the delay and retrieve the signal delayed by delayTime milliseconds.
	* @param inValue The signal input.
	*/
	inline
	int16_t next(int32_t inValue) {
    int16_t result = 0;
    M16_ATOMIC_GUARD_BLOCKING(_delLock, {
      int32_t outValue = 0;
      if (delayTime_samples > 0) {
        outValue = read();
        if (outValue > MAX_16) outValue = MAX_16;
        if (outValue < MIN_16) outValue = MIN_16;
      }
      if (delayFeedback) {
        inValue = inValue + ((outValue * feedbackLevel + 512)>>10);
      }
      if (inValue > MAX_16) inValue =  MAX_16;
      if (inValue < MIN_16) inValue = MIN_16;
      write(inValue);
      result = outValue;
    });
    return result;
  }

  /** Process separate dry and delay-send inputs, matching BBD's interface. */
  inline int16_t next(int32_t dryInput, int32_t delaySendInput) {
    int16_t result = 0;
    M16_ATOMIC_GUARD_BLOCKING(_delLock, {
      result = nextUnlocked(dryInput, delaySendInput);
    });
    return result;
  }

  /** Process one sample without acquiring the delay-state lock.
   * Use only when exactly one audio core owns this delay instance. Control-rate
   * parameter setters may still update naturally aligned target values.
   */
  inline int16_t nextUnlocked(int32_t inValue) {
    return nextUnlocked(inValue, inValue);
  }

  /** Process separate dry and delay-send inputs without acquiring the lock. */
  inline int16_t nextUnlocked(int32_t dryInput, int32_t delaySendInput) {
    int32_t outValue = 0;
    const unsigned int activeDelaySamples =
        #if IS_ESP32() || IS_RP2040()
        delayTime_samples.load(std::memory_order_relaxed);
        #else
        delayTime_samples;
        #endif
    if (activeDelaySamples > 0) {
      outValue = read();
      if (outValue > MAX_16) outValue = MAX_16;
      if (outValue < MIN_16) outValue = MIN_16;
    }
    const bool feedbackEnabled =
        #if IS_ESP32() || IS_RP2040()
        delayFeedback.load(std::memory_order_relaxed);
        #else
        delayFeedback;
        #endif
    // The incoming send is expected to be a 16-bit audio sample. Bound it
    // before summing so the feedback addition has ample int32_t headroom.
    delaySendInput = max((int32_t)MIN_16,
                         min((int32_t)MAX_16, delaySendInput));
    if (feedbackEnabled) {
      const int16_t feedbackAmount =
          #if IS_ESP32() || IS_RP2040()
          feedbackLevel.load(std::memory_order_relaxed);
          #else
          feedbackLevel;
          #endif
      const int32_t feedbackSample =
          (outValue * feedbackAmount + 512) >> 10;
      const int32_t feedbackSum = delaySendInput + feedbackSample;
      write(softClipFeedbackSum(feedbackSum));
    } else {
      write(delaySendInput);
    }

    const uint32_t gains =
        #if IS_ESP32() || IS_RP2040()
        delayMixGains.load(std::memory_order_relaxed);
        #else
        delayMixGains;
        #endif
    const int32_t dryGain = gains & 0xFFFF;
    const int32_t wetGain = gains >> 16;
    if (dryGain >= 1024) return clip16(dryInput);
    if (wetGain == 1024) return (int16_t)outValue;
    int32_t mixedDry = (int32_t)(((int64_t)dryInput * dryGain) >> 10);
    int32_t mixedWet = (int32_t)(((int64_t)outValue * wetGain) >> 10);
    mixedDry = max((int32_t)MIN_16, min((int32_t)MAX_16, mixedDry));
    mixedWet = max((int32_t)MIN_16 - mixedDry,
                   min((int32_t)MAX_16 - mixedDry, mixedWet));
    return (int16_t)(mixedDry + mixedWet);
  }

  /** Read the buffer at the delayTime without incrementing read/write index */
  inline
	int16_t read() {
    return read(0);
  }

  /** Read the buffer at the delayTime + an offset without incrementing read/write index
   * @param pos The delay offset in samples, can be positive or negative
  */
  inline
	int16_t read(int pos) {
    if (!delayBuffer) return 0;  
    int outValue = 0;
    const unsigned int activeDelaySamples =
        #if IS_ESP32() || IS_RP2040()
        delayTime_samples.load(std::memory_order_relaxed);
        #else
        delayTime_samples;
        #endif
    int readPos = writePos - activeDelaySamples + pos;
    if (readPos < 0) readPos += delayBufferSize_samples;
    if (readPos >= (int)delayBufferSize_samples) {
      readPos -= (int)delayBufferSize_samples;
    }
    outValue = min(MAX_16, max(MIN_16, (int)delayBuffer[readPos]));
    const byte filterAmount =
        #if IS_ESP32() || IS_RP2040()
        filtered.load(std::memory_order_relaxed);
        #else
        filtered;
        #endif
    if(filterAmount > 0) {
      if (filterAmount == 1) {
        outValue = (outValue + outValue + outValue + prevOutValue)>>2; // smooth
      } else if (filterAmount == 2) {
        outValue = (outValue + prevOutValue)>>1; // smooth
      } else if (filterAmount == 3) {
        outValue = (outValue + prevOutValue + prevOutValue + prevOutValue)>>2; // smooth
      } else outValue = (outValue + prevOutValue + prevOutValue + prevOutValue +
          prevOutValue + prevOutValue + prevOutValue + prevOutValue)>>3; // smooth
      prevOutValue = outValue;
    }
    const int16_t outputLevel =
        #if IS_ESP32() || IS_RP2040()
        delayLevel.load(std::memory_order_relaxed);
        #else
        delayLevel;
        #endif
    return (outValue * outputLevel)>>10;
  }

  /** Write to the buffer at pos without incrementing the write index
  * @param inValue The signal input (MIN_16 to MAX-16)
  * @param pos The buffer write position in samples (0 - buffer size)
  */
  inline
	void write(int inValue, int pos) {
    if (!delayBuffer) return;  
    if (delayBufferSize_samples == 0) return;  // Prevent divide by zero
    const int safePos = max(0, min((int)delayBufferSize_samples - 1, pos));
    delayBuffer[safePos] = min(MAX_16, max(MIN_16, inValue));
  }

  /** Write to the buffer and increment the write index
  * @param inValue The signal input (MIN_16 to MAX-16)
  */
  inline
	void write(int inValue) {
    if (!delayBuffer) return;  
    if (delayBufferSize_samples == 0) return;  // Prevent divide by zero
    delayBuffer[writePos] = min(MAX_16, max(MIN_16, inValue));
    writePos = (writePos + 1) % delayBufferSize_samples;
  }
};

#endif /* DEL_H_ */
