/*
 * Osc.h
 *
 * A waveTable oscillator class. Contains generators for common wavetables.
 *
 * by Andrew R. Brown 2021
 *
 * Based on the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef OSC_H_
#define OSC_H_

class Osc {

public:
  // ============== Dual-core phase synchronization (RP2040) ==============
  #if IS_RP2040()
  static constexpr int MAX_REGISTERED_OSCS = 32;
  static Osc* registeredOscs[MAX_REGISTERED_OSCS];
  static int numRegisteredOscs;

  // Per-core phase storage for synchronized dual-core operation
  uint32_t storedPhase[2] = {0, 0};
  bool useStoredPhase[2] = {false, false};  // Per-core flag to prevent race conditions

  /** Register this oscillator for dual-core phase sync */
  void registerForDualCore() {
    if (numRegisteredOscs < MAX_REGISTERED_OSCS) {
      // Check if already registered
      for (int i = 0; i < numRegisteredOscs; i++) {
        if (registeredOscs[i] == this) return;
      }
      registeredOscs[numRegisteredOscs++] = this;
    }
  }

  /** Advance phases for all registered oscillators (call under mutex) */
  static void advanceAllPhases(int coreNum) {
    for (int i = 0; i < numRegisteredOscs; i++) {
      Osc* osc = registeredOscs[i];
      if (osc) {
        // Store current phase for this core, then advance
        osc->storedPhase[coreNum] = osc->phase_fractional;
        osc->phase_fractional += osc->phase_increment_fractional;
        osc->useStoredPhase[coreNum] = true;  // Only set flag for this core
      }
    }
  }

  /** Mark this core's oscillators as done with stored phase */
  static void clearStoredPhaseFlag(int coreNum) {
    for (int i = 0; i < numRegisteredOscs; i++) {
      if (registeredOscs[i]) {
        registeredOscs[i]->useStoredPhase[coreNum] = false;  // Only clear this core's flag
      }
    }
  }
  #endif
  // ========================================================================

  /** Constructor.
	* Has no table specified - make sure to use setTable() after initialising
	*/
  Osc() {
    #if IS_RP2040()
    registerForDualCore();
    #endif
  }

  /** Updates the phase according to the current frequency and returns the sample at the new phase position.
	* @return outSamp The next sample.
	*/
	inline
	int16_t next() {
    int32_t sampVal;

    // Fast path: atomic phase increment for thread-safe dual-core operation
    // Pulse width, noise, and crackle modes use the slower non-atomic path
    #if IS_RP2040()
    // Pico dual-core: use pre-acquired phase if available (set by advanceAllPhases)
    if (isDualCore && !pulseWidthOn && !isNoise && !isCrackle) {
      int coreNum = get_core_num();
      // Check this core's flag - prevents race where other core clears our flag
      if (useStoredPhase[coreNum]) {
        int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
        if (cachedBandPtr == nullptr) return 0;

        // Use the phase that was stored for this core (already advanced under mutex)
        uint32_t myPhase = storedPhase[coreNum];
        int idx = (myPhase >> 16) & (TABLE_SIZE - 1);
        sampVal = cachedBandPtr[idx];

        if (spreadActive) {
          sampVal = doSpreadAtomic(sampVal);
        }
        return sampVal;
      }
    }
    #endif

    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      // Cache volatile values atomically to prevent torn reads during setFreq()
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);

      // Safety check: if bandPtr is null or increment is zero, return silence
      if (cachedBandPtr == nullptr || cachedIncrement == 0) {
        return 0;
      }

      // Atomic fetch-and-add: each core gets a unique phase value
      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      int idx = (myPhase >> 16) & (TABLE_SIZE - 1);
      sampVal = cachedBandPtr[idx];

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif

    // Non-atomic path for pulse width, noise, crackle modes (or single-core platforms)
    int idx = phase_fractional >> 16;
    sampVal = bandPtr[idx];
    incrementPhase();
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Updates the phase and returns the next sample with interpolation.
   * Uses cubic interpolation for high band (>831Hz), linear for mid band (>208Hz),
   * smoothing for low band. Thresholds match band selection in setFreq().
   * Higher quality than next() at ~5-15% more CPU cost.
   * @return sampVal The next sample.
   */
  inline
  int16_t next2() {
    int32_t sampVal;
    uint32_t myPhase;
    int idx;

    // Fast path: atomic phase increment for thread-safe dual-core operation
    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      // Cache volatile values atomically to prevent torn reads during setFreq()
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
      float cachedFreq = frequency;  // Used for band selection decision

      // Safety check: if bandPtr is null or increment is zero, return silence
      if (cachedBandPtr == nullptr || cachedIncrement == 0) {
        return 0;
      }

      // Atomic fetch-and-add: each core gets a unique phase value
      myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      idx = (myPhase >> 16) & (TABLE_SIZE - 1);

      if (cachedFreq > 831.0f) {
        // Cubic interpolation for high frequencies (4-point Hermite)
        float t = (float)(myPhase & 0xFFFF) * (1.0f / 65536.0f);
        float t2 = t * t;
        float t3 = t2 * t;
        int16_t sm1 = cachedBandPtr[(idx - 1) & (TABLE_SIZE - 1)];
        int16_t s0 = cachedBandPtr[idx];
        int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
        int16_t s2 = cachedBandPtr[(idx + 2) & (TABLE_SIZE - 1)];
        float a0 = -0.5f * sm1 + 1.5f * s0 - 1.5f * s1 + 0.5f * s2;
        float a1 = sm1 - 2.5f * s0 + 2.0f * s1 - 0.5f * s2;
        float a2 = -0.5f * sm1 + 0.5f * s1;
        float a3 = s0;
        sampVal = (int32_t)(a0 * t3 + a1 * t2 + a2 * t + a3);
        if (sampVal > MAX_16) sampVal = MAX_16;
        else if (sampVal < MIN_16) sampVal = MIN_16;
      } else if (cachedFreq > 208.0f) {
        // Linear interpolation for mid frequencies
        int frac = (myPhase >> 6) & 0x3FF;
        int16_t s0 = cachedBandPtr[idx];
        int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
        sampVal = s0 + (((s1 - s0) * frac) >> 10);
      } else {
        // Low-frequency: direct lookup (smoothing handled at output stage)
        sampVal = cachedBandPtr[idx];
      }

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif

    // Non-atomic path for pulse width, noise, crackle modes (or single-core platforms)
    idx = (phase_fractional >> 16) & (TABLE_SIZE - 1);
    if (frequency > 831.0f) {
      float t = (float)(phase_fractional & 0xFFFF) * (1.0f / 65536.0f);
      float t2 = t * t;
      float t3 = t2 * t;
      int16_t sm1 = bandPtr[(idx - 1) & (TABLE_SIZE - 1)];
      int16_t s0 = bandPtr[idx];
      int16_t s1 = bandPtr[(idx + 1) & (TABLE_SIZE - 1)];
      int16_t s2 = bandPtr[(idx + 2) & (TABLE_SIZE - 1)];
      float a0 = -0.5f * sm1 + 1.5f * s0 - 1.5f * s1 + 0.5f * s2;
      float a1 = sm1 - 2.5f * s0 + 2.0f * s1 - 0.5f * s2;
      float a2 = -0.5f * sm1 + 0.5f * s1;
      float a3 = s0;
      sampVal = (int32_t)(a0 * t3 + a1 * t2 + a2 * t + a3);
      if (sampVal > MAX_16) sampVal = MAX_16;
      else if (sampVal < MIN_16) sampVal = MIN_16;
    } else if (frequency > 208.0f) {
      int frac = (phase_fractional >> 6) & 0x3FF;
      int16_t s0 = bandPtr[idx];
      int16_t s1 = bandPtr[(idx + 1) & (TABLE_SIZE - 1)];
      sampVal = s0 + (((s1 - s0) * frac) >> 10);
    } else {
      sampVal = bandPtr[idx];
      if (bandPtr == waveTable) {
        sampVal = (sampVal + prevSampVal) >> 1;
        prevSampVal = sampVal;
      }
    }
    incrementPhase();
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Returns the sample at a specified time in milliseconds.
  * Used for LFOs. Assumes the Osc started at time = 0;
	* @return outSamp The sample value at the calculated phase position - range MIN_16 to MAX_16.
	*/
	inline
	int16_t atTime(unsigned long ms) {
    unsigned long indexAtTime = ms * cycleLengthPerMS * TABLE_SIZE;
    int index = indexAtTime & (TABLE_SIZE - 1); //indexAtTime % TABLE_SIZE; assuming index is a power of 2
    int16_t outSamp = waveTable[index]; 
    return outSamp;
  }

  /** Returns the normalised oscillator value at specified time in milliseconds.
  * Used for LFOs. Assumes the Osc started at time = 0;
	* @return outVal The osc value at the calculated phase position normalised between 0.0 and 1.0.
	*/
	inline
	float atTimeNormal(unsigned long ms) {
    int16_t outSamp = atTime(ms);
    return max(0.0, outSamp * MAX_16_INV * 0.5 + 0.5);
  }

	/** Change the sound table which will be played by the Oscil.
	* @param TABLE_NAME is the name of the array. Must be the same size as the original table used when instantiated.
	*/
  inline
	void setTable(int16_t * TABLE_NAME) { // const
		waveTable = TABLE_NAME;
    // Update bandPtr based on current frequency
    if (frequency > 831) {
      bandPtr = waveTable + TABLE_SIZE * 2;
    } else if (frequency > 208) {
      bandPtr = waveTable + TABLE_SIZE;
    } else {
      bandPtr = waveTable;
    }
	}

	/** Set the phase of the Oscil. Phase ranges from 0.0 - 1.0 */
	inline
  void setPhase(float phase) {
    // Convert 0.0-1.0 to 16.16 fixed-point (0 to TABLE_SIZE << 16)
		phase_fractional = (uint32_t)(phase * TABLE_SIZE * 65536.0f);
    // Spread phases also 16.16
    phase_fractional_s1 = phase_fractional;
    phase_fractional_s2 = phase_fractional;
	}

	/** Get the phase of the Oscil in fractional format (0.0 - 1.0). */
	inline
  float getPhase() {
    // Convert 16.16 fixed-point back to 0.0-1.0
		return (float)phase_fractional / (TABLE_SIZE * 65536.0f);
	}

  /** Set the spread value of the Oscil.
  * @newVal A multiplyer of the base freq, from 0 to 1.0, values near zero are best for phasing effects
  */
	inline
  void setSpread(float newVal) {
    spread1 = 1.0f + newVal;
    spread2 = 1.0f - newVal * 0.5002;
    if (newVal > 0) {
      spreadActive = true;
    } else spreadActive = false;
    setFreq(getFreq());
	}

  /** Set the spread value of each detuned Oscilator instance. Ranges > 0 
   * @val1 The first spread value
   * @val2 The second spread value
   */
	inline
  void setSpread(int val1, int val2) {
		spread1 = intervalRatios[val1 + 12]; //intervalFreq(frequency, val1);
    spread2 = intervalRatios[val2 + 12]; //intervalFreq(frequency, val2);
	}

  /** Return the spread value of the Oscil. */
  float getSpread() {
    return spread1 - 1.0;
  }

  /** Return the current value of the Oscil. */
  int16_t getValue() {
    int idx = phase_fractional >> 16; // 16.16 fixed-point
    return bandPtr[idx];
  }

  /** Get a blend of this Osc and another.
  * @param secondWaveTable - an waveTable array to morph with
  * @param morphAmount - The balance (mix) of the second wavetable, 0.0 - 1.0
  */
	inline
  int16_t nextMorph(int16_t * secondWaveTable, float morphAmount) {
    int intMorphAmount = max(0, min (1024, (int)(1024 * morphAmount)));
    int idx = phase_fractional >> 16; // 16.16 fixed-point
    int32_t sampVal = waveTable[idx];
    int32_t sampVal2 = secondWaveTable[idx];
    if (morphAmount > 0) sampVal = (((sampVal2 * intMorphAmount) >> 10) +
      ((sampVal * (1024 - intMorphAmount)) >> 10));
    sampVal = (sampVal + prevSampVal)>>1; // smooth
    prevSampVal = sampVal;
    incrementPhase();
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Get a blend of this Osc and another, without incrementing the waveTable lookup.
  * @param secondWaveTable - a waveTable  array to morph with
  * @param morphAmount - The balance (mix) of the second waveTable , 0.0 - 1.0
  */
	inline
  int16_t currentMorph(int16_t * secondWaveTable, float morphAmount) {
    int intMorphAmount = max(0, min(1024, (int)(1024 * morphAmount)));
    int idx = phase_fractional >> 16; // 16.16 fixed-point
    int32_t sampVal = waveTable[idx];
    int32_t sampVal2 = secondWaveTable[idx];
    if (morphAmount > 0) sampVal = (((sampVal2 * intMorphAmount) >> 10) +
      ((sampVal * (1024 - intMorphAmount)) >> 10));
    prevSampVal = sampVal;
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Get a window transform between this Osc and another waveTable .
  * Inspired by the Window Transform Function by Dove Audio
  * @param secondWaveTable - an waveTable  array to transform with
  * @param windowSize - The amount (mix) of the second waveTable  to let through, 0.0 - 1.0
  * @param duel - Use a duel window that can increase harmonicity
  * @param invert - Invert the second wavefrom that can increase harmonicity
  */
	inline
  int16_t nextWTrans(int16_t * secondWaveTable, float windowSize, bool duel, bool invert) {
    // see https://dove-audio.com/wtf-module/
    int halfTable = HALF_TABLE_SIZE;
    int portion12 = halfTable * windowSize;
    int quarterTable = TABLE_SIZE * 0.25;
    int threeQuarterTable = quarterTable * 3;
    int portion14 = quarterTable * windowSize;
    int32_t sampVal = 0;
    int phaseIdx = phase_fractional >> 16; // 16.16: extract integer index for comparisons
    if (duel) {
      if (phaseIdx < (quarterTable - portion14) || (phaseIdx > (quarterTable + portion14) &&
          phaseIdx < (threeQuarterTable - portion14)) || phaseIdx > (threeQuarterTable + portion14)) {
        int idx = phaseIdx;
        if (frequency > 831) { // midi pitch ~80 // high
          sampVal = waveTable[idx + TABLE_SIZE + TABLE_SIZE];
        } else if (frequency > 208) { // midi pitch ~56 // mid
          sampVal = waveTable[idx + TABLE_SIZE];
        } else {
          sampVal = (waveTable[idx] + prevSampVal)>>1; // low
          prevSampVal = sampVal;
        }
        if (spreadActive) {
          sampVal = doSpread(sampVal);
        }
      } else {
        sampVal = secondWaveTable[phaseIdx];
        if (invert) sampVal *= -1;
        if (spreadActive) {
          int32_t spreadSamp1 = secondWaveTable[phaseIdx];
          sampVal = (sampVal + spreadSamp1)>>1;
          int32_t spreadSamp2 = secondWaveTable[phaseIdx];
          sampVal = (sampVal + spreadSamp2)>>1;
          incrementSpreadPhase();
        }
      }
    } else {
      if (phaseIdx < (halfTable - portion12) || phaseIdx > (halfTable + portion12)) {
        int idx = phaseIdx;
        if (frequency > 831) { // midi pitch ~80 // high
          sampVal = waveTable[idx + TABLE_SIZE + TABLE_SIZE];
        } else if (frequency > 208) { // midi pitch ~56 // mid
          sampVal = waveTable[idx + TABLE_SIZE];
        } else {
          sampVal = (waveTable[idx] + prevSampVal)>>1; // low
          prevSampVal = sampVal;
        }
        if (spreadActive) {
          sampVal = doSpread(sampVal);
        }
      } else {
        sampVal = secondWaveTable[phaseIdx];
        if (invert) sampVal *= -1;
        if (spreadActive) {
          int32_t spreadSamp1 = secondWaveTable[phaseIdx];
          sampVal = (sampVal + spreadSamp1)>>1;
          int32_t spreadSamp2 = secondWaveTable[phaseIdx];
          sampVal = (sampVal + spreadSamp2)>>1;
          incrementSpreadPhase();
        }
      }
    }
    sampVal = (sampVal + prevSampVal)>>1; // smooth joins
    prevSampVal = sampVal;
    incrementPhase();
    return sampVal;
  }

  /** Phase Modulation (FM)
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndex - Modulation depth, 0.0 to ~10.0 typical
   *   0.5-1.0: Subtle FM shimmer
   *   2.0-4.0: Classic FM tones
   *   5.0-10.0: Aggressive FM
   */
  inline int16_t phMod(int16_t modulator, float modIndex) {
    // Calculate phase offset in 16.16 format
    int32_t modOffset = (int32_t)((float)modulator * modIndex * 8.0f);

    // Anti-aliasing: reduce mod depth at high carrier frequencies
    modOffset = (modOffset * modDepthScale) >> 10;

    modOffset <<= 8; // Scale to 16.16 format

    // Add modulation offset to phase (both 16.16)
    uint32_t p = phase_fractional + modOffset;

    // Extract table index and fractional part for interpolation
    int idx = (p >> 16) & (TABLE_SIZE - 1);
    int frac = (p >> 6) & 0x3FF;  // 10-bit fractional (0-1023)

    // Atomic load of bandPtr for thread-safety with setFreq on dual-core systems
    #if IS_ESP32() || IS_RP2040()
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
    #else
      int16_t* cachedBandPtr = bandPtr;
    #endif

    // Linear interpolation between adjacent samples
    int16_t s0 = cachedBandPtr[idx];
    int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
    int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);

    incrementPhase();

    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Set cached mod index for use with single-argument phMod
   * @param modIndex - The depth value (0.0 - 10.0 typical)
   */
  inline void setModIndex(float modIndex) {
    cachedModIndexF = modIndex;
  }

  /** Phase Modulation using cached mod index
   * Call setModIndex() first, then use this in the audio loop
   */
  inline int16_t phMod(int16_t modulator) {
    return phMod(modulator, cachedModIndexF);
  }

  /** Phase Modulation with 2x oversampling (FM)
   * Higher quality anti-aliasing at ~2x CPU cost
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndex - Modulation depth, 0.0 to ~10.0 typical
   */
  inline int16_t phMod2(int16_t modulator, float modIndex) {
    // Calculate phase offset in 16.16 format
    int32_t modOffset = (int32_t)((float)modulator * modIndex * 8.0f);

    // Anti-aliasing: reduce mod depth at high carrier frequencies
    modOffset = (modOffset * modDepthScale) >> 10;
    modOffset <<= 8; // Scale to 16.16 format

    // Half phase increment for 2x oversampling
    uint32_t halfInc = phase_increment_fractional >> 1;

    // --- Sample 1: at current phase ---
    uint32_t p1 = phase_fractional + modOffset;
    int idx1 = (p1 >> 16) & (TABLE_SIZE - 1);
    int frac1 = (p1 >> 6) & 0x3FF;
    int16_t s0 = bandPtr[idx1];
    int16_t s1 = bandPtr[(idx1 + 1) & (TABLE_SIZE - 1)];
    int32_t samp1 = s0 + (((s1 - s0) * frac1) >> 10);

    // Advance phase by half increment
    phase_fractional += halfInc;
    phase_fractional &= TABLE_SIZE_FP_MASK;

    // --- Sample 2: at half-step advanced phase ---
    uint32_t p2 = phase_fractional + modOffset;
    int idx2 = (p2 >> 16) & (TABLE_SIZE - 1);
    int frac2 = (p2 >> 6) & 0x3FF;
    s0 = bandPtr[idx2];
    s1 = bandPtr[(idx2 + 1) & (TABLE_SIZE - 1)];
    int32_t samp2 = s0 + (((s1 - s0) * frac2) >> 10);

    // Advance phase by remaining half increment
    phase_fractional += halfInc;
    phase_fractional &= TABLE_SIZE_FP_MASK;

    // Average the two samples (simple 2-tap lowpass)
    int32_t sampVal = (samp1 + samp2) >> 1;

    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Phase Modulation with 2x oversampling using cached mod index */
  inline int16_t phMod2(int16_t modulator) {
    return phMod2(modulator, cachedModIndexF);
  }

  /** Ring Modulation
  *  Pass in a second oscillator and multiply its value to change mod depth
  *  Multiplying incomming oscillator amplitude between 0.5 - 2.0 is best.
  */
  inline
  int16_t ringMod(int audioIn) {
    incrementPhase();
    int idx = phase_fractional >> 16; // 16.16 fixed-point
    int32_t currSamp = waveTable[idx];
    int16_t sampVal = (currSamp * audioIn)>>15;
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** PhISM Shaker model
   * Designed for Osc being set to a noise waveTable .
   * @param thresh The amount of aparent particles. Ty cally 0.9 - 0.999
   * Envelope output and pass to one or more band pass filters or other resonator.
   */
  inline
  int16_t particle(float thresh) {
    int idx = phase_fractional >> 16; // 16.16 fixed-point
    int32_t noiseVal = waveTable[idx];
    if (noiseVal > (MAX_16 * thresh)) {
      particleEnv = noiseVal - (MAX_16 - noiseVal) - (MAX_16 - noiseVal);
    } else particleEnv *= particleEnvReleaseRate;
    incrementPhase();
    noiseVal = (prevParticle + noiseVal + noiseVal)/3;
    return (noiseVal * particleEnv) >> 16;
  }

  /** PhISM Shaker model
   * Designed for Osc being set to a noise wavetable.
   * Uses some private hard coded params.
   * Envelope output and pass to one or more band pass filters or other resonator.
   */
  inline
  int16_t particle() {
    return particle(particleThreshold);
  }

  /** Frequency Modulation Feedback
   * Designed for Osc being set to a sine waveform, but works with any waveform.
   * @modIndex amount of feedback applied, >=0 and useful < 100
   * Credit to description in The CMT (Roads 1996).
   */
  inline
  int16_t feedback(int32_t modIndex) {
    // Read feedback sample using 16.16 index
    int16_t y = waveTable[feedback_phase_fractional >> 16] >> 3;
    int16_t s = waveTable[y & (TABLE_SIZE - 1)];
    // Calculate feedback offset and convert to 16.16
    int32_t f_fp = ((modIndex * (int32_t)s) >> 16) << 16;
    // Update phase with feedback (signed arithmetic then fast wrap)
    phase_fractional = (uint32_t)((int32_t)phase_fractional + f_fp + (int32_t)phase_increment_fractional);
    phase_fractional &= TABLE_SIZE_FP_MASK; // Fast wrap
    // Increment feedback phase with fast wrap
    feedback_phase_fractional += phase_increment_fractional;
    feedback_phase_fractional &= TABLE_SIZE_FP_MASK;
    // Return sample at current phase
    int16_t out = waveTable[(phase_fractional >> 16) & (TABLE_SIZE - 1)];
    return out;
  }

  /** Glide toward the frequency of the oscillator in Hz.
  * @freq The desired final value
  * @amnt The percentage toward target (0.0 - 1.0)
  */
  inline
	void slewFreq(float freq, float amnt) {
    if (freq == frequency) return;
    if (amnt == 0) {
      setFreq(freq);
		} else if (freq >= 0 && amnt > 0 && amnt <= 1) {
      float tempFreq = frequency;
      setFreq(slew(frequency, freq, amnt));
      prevFrequency = tempFreq;
    }
  }

	/** Set the frequency of the oscillator.
   * @freq The desired frequency in Hz
  */
	inline
	void setFreq(float freq) {
		if (freq > 0) {
      frequency = freq;
      // 16.16 fixed-point: phase_inc = (freq * TABLE_SIZE / SAMPLE_RATE) * 65536
      uint32_t newIncrement = (uint32_t)(freq * TABLE_SIZE * 65536.0f / SAMPLE_RATE);

      // Calculate new band pointer before atomic update
      int16_t* newBandPtr = bandPtr;
      if (waveTable != nullptr) {
        if (freq > 831) {
          newBandPtr = waveTable + TABLE_SIZE * 2;  // high band
        } else if (freq > 208) {
          newBandPtr = waveTable + TABLE_SIZE;      // mid band
        } else {
          newBandPtr = waveTable;                   // low band
        }
      }

      // Atomic stores for thread-safety with next() on dual-core systems
      #if IS_ESP32() || IS_RP2040()
        // Update bandPtr first, then increment (next() reads increment first, then bandPtr)
        __atomic_store_n((uintptr_t*)&bandPtr, (uintptr_t)newBandPtr, __ATOMIC_RELEASE);
        __atomic_store_n(&phase_increment_fractional, newIncrement, __ATOMIC_RELEASE);
      #else
        bandPtr = newBandPtr;
        phase_increment_fractional = newIncrement;
      #endif

      if (pulseWidthOn) {
        // Calculate pulse width variants in 16.16
        uint32_t halfInc = newIncrement >> 1;
        phase_increment_fractional_w1 = (uint32_t)(halfInc / pulseWidth);
        phase_increment_fractional_w2 = (uint32_t)(halfInc / (1.0f - pulseWidth));
      }
      if (spreadActive) {
        // 16.16 spread increments
        phase_increment_fractional_s1 = (uint32_t)(newIncrement * spread1);
        phase_increment_fractional_s2 = (uint32_t)(newIncrement * spread2);
      } else {
        phase_increment_fractional_s1 = newIncrement;
        phase_increment_fractional_s2 = newIncrement;
      }
      cycleLengthPerMS = frequency * 0.001f;

      // Frequency-adaptive mod depth scaling for anti-aliasing in phMod
      // Below 1500Hz: full depth. Above: reduce proportionally to headroom to Nyquist.
      // This reduces FM sidebands that would alias at higher carrier frequencies.
      const float nyquist = SAMPLE_RATE * 0.5f;
      const float thresholdFreq = 1500.0f;
      if (freq <= thresholdFreq) {
        modDepthScale = 1024; // Full depth
      } else {
        // Scale down based on remaining headroom: (nyquist - freq) / (nyquist - threshold)
        float headroom = (nyquist - freq) / (nyquist - thresholdFreq);
        modDepthScale = (int32_t)(1024.0f * fmaxf(0.05f, fminf(1.0f, headroom)));
      }
    }
	}

	/** Return the frequency of the oscillator in Hz. */
	inline
	float getFreq() {
		return frequency;
	}

	/** Set the frequency via a MIDI pitch
  * @midiPitch The pitch, value 0 - 127
  */
	inline
	void setPitch(float midi_pitch) {
    midiPitch = midi_pitch;
		setFreq(mtof(min(127.0f, max(0.0f, midi_pitch * (1 + (audioRand(6) * 0.00001f))))));
    prevFrequency = frequency;
	}

  /** Return the pitch as a MIDI pitch 
   * @midiPitch The pitch, value 0 - 127
  */
	inline
	float getPitch() {
    return midiPitch;
  }

	/** Set a specific phase increment in 16.16 fixed-point format.
  * @phaseinc_fractional 16.16 fixed-point increment value
  * For reference: 65536 = 1 table index per sample
  */
	inline
	void setPhaseInc(uint32_t phaseinc_fractional) {
		phase_increment_fractional = phaseinc_fractional;
	}

	/** Set using noise waveform flag.
  * @val Is true or false
  */
	inline
	void setNoise(bool val) {
		isNoise = val;
	}

  /** Set using crackle waveform flag.
  * @val Is true or false
  */
	inline
	void setCrackle(bool val) {
    setNoise(true);
		isCrackle = val;
	}

  /** Set using crackle waveform flag.
  * @val Is true or false
  * @amnt Spareness of impulse in samples, from 1 to MAX_16
  */
	inline
	void setCrackle(bool val, int amnt) {
    setNoise(true);
		isCrackle = val;
    crackleAmnt = max(1, min(MAX_16, amnt));
	}

  /** Set using pulse width for the waveform
  * @width The cycle amount for the first half of the wave - 0.0 to 1.0
  */
	inline
	void setPulseWidth(float width) {
    pulseWidthOn = true;
    pulseWidth = max(0.05f, min(0.95f, width));
    // Calculate 16.16 pulse width increments
    uint32_t halfPhaseInc = phase_increment_fractional >> 1;
    phase_increment_fractional_w1 = (uint32_t)(halfPhaseInc / pulseWidth);
    phase_increment_fractional_w2 = (uint32_t)(halfPhaseInc / (1.0f - pulseWidth));
  }

  /** Set using pulse width for the waveform
  * @width The cycle amount for the first half of the wave - 0.0 to 1.0
  */
	inline
	float getPulseWidth() {
    return pulseWidth;
  }

  /** Below are helper methods for generating waveforms into existing arrays.
   * Call from class not instance. e.g. Osc::triGen(myWaveTableArray);
   * While it might be simpler to have each instance have its own wavetable,
   * it's more memory effcient for wavetables to be shared. So create them in the
   * main program file and reference them from instances of this class.
   */

  /** Generate a cosine wave for the provided wavetable
  * @theTable The the wavetable to be filled
  */
  static void cosGen(int16_t * theTable) {
    for(int i=0; i<TABLE_SIZE; i++) {
      int samp = (cos(2 * 3.14159 * i * TABLE_SIZE_INV) * MAX_16);
      theTable[i] = samp; // low
      theTable[i + TABLE_SIZE] = samp; // mid
      theTable[i + TABLE_SIZE * 2] = samp; // high
    }
  }

  /** Generate a sine wave for the provided wavetable
  * @theTable The the wavetable to be filled
  */
  static void sinGen(int16_t * theTable) {
    for(int i=0; i<TABLE_SIZE; i++) {
      int samp = (sin(2 * 3.14159 * i * TABLE_SIZE_INV) * MAX_16);
      theTable[i] = samp; // low
      theTable[i + TABLE_SIZE] = samp; // mid
      theTable[i + TABLE_SIZE * 2] = samp; // high
    }
  }

  /** Generate a sine wave for the oscillator */
  void sinGen() {
    allocateWavetable();
    for(int i=0; i<TABLE_SIZE; i++) {
      int samp = (sin(2 * 3.14159 * i * TABLE_SIZE_INV) * MAX_16);
      waveTable[i] = samp; // low
      waveTable[i + TABLE_SIZE] = samp; // mid
      waveTable[i + TABLE_SIZE * 2] = samp; // high
    }
  }

  /** Generate a triangle wave for the provided wavetable
  * @theTable The the wavetable to be filled
  */
  static void triGen(int16_t * theTable) {
    Osc::generateWave(theTable, 0, 48, 1); // low 
    Osc::generateWave(theTable, 1, 20, 1); // mid
    Osc::generateWave(theTable, 2, 12, 1); // high
    // for (int i=0; i<TABLE_SIZE; i++) {
    //   if (i < HALF_TABLE_SIZE) {
    //     int samp = MAX_16 - i * (MAX_16 * 2.0f * TABLE_SIZE_INV * 2.0f);
    //     theTable[i] = samp;
    //     theTable[i + TABLE_SIZE] = samp; // mid
    //     theTable[i + TABLE_SIZE * 2] = samp; // high
    //   } else {
    //     int samp = MIN_16 + (i - (float)HALF_TABLE_SIZE) * (MAX_16 * 2.0f * TABLE_SIZE_INV * 2.0f);
    //     theTable[i] = samp;
    //     theTable[i + TABLE_SIZE] = samp; // mid
    //     theTable[i + TABLE_SIZE * 2] = samp; // high
    //   }
    // }
  }

  /** Generate a triangle wave for the oscillator */
  void triGen() {
    allocateWavetable();
    Osc::generateWave(waveTable, 0, 48, 1); // low
    Osc::generateWave(waveTable, 1, 20, 1); // mid
    Osc::generateWave(waveTable, 2, 12, 1); // high
  }

  /** Generate a square/pulse wave for the provided wavetable
  * @theTable The the wavetable to be filled
  * @duty The duty cycle, or pulse width, 0.0 - 1.0, 0.5 = sqr
  */
  static void pulseGen(int16_t * theTable, float duty) {
    for(int i=0; i<TABLE_SIZE; i++) {
      if (i < TABLE_SIZE * duty) {
        theTable[i] = MAX_16;
        theTable[i + TABLE_SIZE] = MAX_16;
        theTable[i + TABLE_SIZE * 2] = MAX_16;
      } else {
        theTable[i] = MIN_16;
        theTable[i + TABLE_SIZE] = MIN_16;
        theTable[i + TABLE_SIZE * 2] = MIN_16;
      }
    }
  }

  /** Generate a square wave for the provided wavetable
  * @theTable The the wavetable to be filled
  */
  static void sqrGen(int16_t * theTable) {
    Osc::generateWave(theTable, 0, 56, 2); // low
    Osc::generateWave(theTable, 1, 28, 2); // mid
    Osc::generateWave(theTable, 2, 12, 2); // high
  }

  /** Generate a square wave for the local wavetable
  * @theTable The the wavetable to be filled
  */
  void sqrGen() {
    allocateWavetable();
    Osc::generateWave(waveTable, 0, 56, 2); // low
    Osc::generateWave(waveTable, 1, 28, 2); // mid
    Osc::generateWave(waveTable, 2, 12, 2); // high
  }

  /** Generate a sawtooth wave for the provided wavetable
  * @theTable The the wavetable to be filled
  */
  static void sawGen(int16_t * theTable) {
    Osc::generateWave(theTable, 0, 96, 3); // low 
    Osc::generateWave(theTable, 1, 34, 3); // mid
    Osc::generateWave(theTable, 2, 8, 3); // high
    // for (int i=0; i<TABLE_SIZE; i++) {
    //   theTable[i] = (MAX_16 - i * (MAX_16 * 2 * TABLE_SIZE_INV));
    // }
  }

  /** Generate a sawtooth wave for the oscillator 
   * in the local wavetable
  */
  void sawGen() { 
    allocateWavetable();
    Osc::generateWave(waveTable, 0, 96, 3); // low
    Osc::generateWave(waveTable, 1, 34, 3); // mid
    Osc::generateWave(waveTable, 2, 8, 3); // high
  }

  /** Generate white noise for the provided wavetable
  * @theTable The the wavetable to be filled
  */
  static void noiseGen(int16_t * theTable) {
    audioRandSeed(random(MAX_16));
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      theTable[i] = audioRand(MAX_16 * 2) - MAX_16;
    }
  }

  /** Generate white noise in the local wavetable */
  void noiseGen() {
    allocateWavetable();
    audioRandSeed(random(MAX_16));
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      waveTable [i] = audioRand(MAX_16 * 2) - MAX_16;
    }
  }

  /** Generate grainly white noise, like a sample and hold wave
  * @theTable The the wavetable to be filled
  * @grainSize The number of samples each random value is held for
  */
  static void noiseGen(int16_t * theTable, int grainSize) {
    int grainCnt = 0;
    int randVal = audioRand(MAX_16 * 2) - MAX_16;
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      theTable[i] = randVal;
      grainCnt++;
      if (grainCnt % grainSize == 0) randVal = audioRand(MAX_16 * 2) - MAX_16;
    }
  }

  /** Generate grainly white noise, like a sample and hold wave
  * @grainSize The number of samples each random value is held for
  */
  void noiseGen(int grainSize) {
    allocateWavetable();
    int grainCnt = 0;
    int randVal = audioRand(MAX_16 * 2) - MAX_16;
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      waveTable[i] = randVal;
      // prevSampVal = (randVal + prevSampVal)>>1;
      // waveTable[i] = prevSampVal;
      grainCnt++;
      if (grainCnt % grainSize == 0) randVal = audioRand(MAX_16 * 2) - MAX_16;
    }
  }

  /** Generate crackle noise
  * @theTable The wavetable to be filled
  */
  static void crackleGen(int16_t * theTable) {
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      theTable[i] = 0;
    }
    for(int i=0; i<6; i++) { 
      theTable[(int)audioRand(FULL_TABLE_SIZE)] = MAX_16;
      theTable[(int)audioRand(FULL_TABLE_SIZE)] = MIN_16;
    }
  }

  /** Generate crackle noise in the local wavetable */
  void crackleGen() {
    allocateWavetable();
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      waveTable [i] = 0;
    }
    for(int i=0; i<6; i++) { 
      waveTable[(int)audioRand(FULL_TABLE_SIZE)] = MAX_16;
      waveTable[(int)audioRand(FULL_TABLE_SIZE)] = MIN_16;
    }
  }

  /** Generate Browian noise
  * @theTable The the wavetable to be filled
  */
  static void brownNoiseGen(int16_t * theTable) {
    int val = 0;
    int deviation = MAX_16>>1;
    int halfDev = deviation>>1;
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      val += audioRandGauss(deviation, 2) - halfDev;
      if (val > MAX_16) val = val - MAX_16;
      if (val < MIN_16) val = MIN_16 + abs(val) - MAX_16;
      theTable[i] = max(MIN_16, min(MAX_16, (int)val));
    }
  }

  /** Generate Browian noise in the local wavetable */
  void brownNoiseGen() {
    allocateWavetable();
    int val = 0;
    int deviation = MAX_16>>1;
    int halfDev = deviation>>1;
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      val += audioRandGauss(deviation, 2) - halfDev;
      if (val > MAX_16) val = val - MAX_16;
      if (val < MIN_16) val = MIN_16 + abs(val) - MAX_16;
      waveTable [i] = max(MIN_16, min(MAX_16, (int)val));
    }
  }

  /** Generate pink noise
  * @theTable The the wavetable to be filled
  * Using Paul Kellet's refined method
  */
  static void pinkNoiseGen(int16_t * theTable) {
    float b0, b1, b2, b3, b4, b5, b6;
    for (int i=0; i<FULL_TABLE_SIZE; i++) {
      float white = (audioRand(5000) - 2500) * 0.001; // 20000, 10000
      b0 = 0.99886 * b0 + white * 0.0555179;
      b1 = 0.99332 * b1 + white * 0.0750759;
      b2 = 0.969 * b2 + white * 0.153852;
      b3 = 0.8665 * b3 + white * 0.3104856;
      b4 = 0.55 * b4 + white * 0.5329522;
      b5 = -0.7616 * b5 - white * 0.016898;
      float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
      pink *= 0.11;
      b6 = white * 0.115926;
      theTable[i] = max(MIN_16, min(MAX_16, (int)(pink * MAX_16)));
    }
  }

  /** Generate pink noise in the local wavetable
  * Using Paul Kellet's refined method
  */
  void pinkNoiseGen() {
    allocateWavetable();
    float b0, b1, b2, b3, b4, b5, b6;
    for (int i=0; i<FULL_TABLE_SIZE; i++) {
      float white = (audioRand(5000) - 2500) * 0.001; // 20000, 10000
      b0 = 0.99886 * b0 + white * 0.0555179;
      b1 = 0.99332 * b1 + white * 0.0750759;
      b2 = 0.969 * b2 + white * 0.153852;
      b3 = 0.8665 * b3 + white * 0.3104856;
      b4 = 0.55 * b4 + white * 0.5329522;
      b5 = -0.7616 * b5 - white * 0.016898;
      float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
      pink *= 0.11;
      b6 = white * 0.115926;
      waveTable [i] = max(MIN_16, min(MAX_16, (int)(pink * MAX_16)));
    }
  }

  /** Allocate memory for an external waveTable
   * @theTable The waveTable pointer
   * Be careful of the pointer to pointer arg requiring &NAME in the calling pointer
  */
  static void allocateWaveMemory(int16_t** theTable) {
    #if IS_ESP32()
      // Try PSRAM allocation with size checking
      *theTable = psramAllocInt16(FULL_TABLE_SIZE, "wavetable");
      if (!*theTable) {
        // Fallback to regular RAM
        *theTable = new int16_t[FULL_TABLE_SIZE];
        Serial.println("Wavetable allocated in regular RAM");
      }
    #else
      *theTable = new int16_t[FULL_TABLE_SIZE];
    #endif
  }

private:
  // 16.16 fixed-point constants (compile-time for efficiency)
  static constexpr uint32_t TABLE_SIZE_FP_CONST = TABLE_SIZE << 16;
  static constexpr uint32_t HALF_TABLE_SIZE_FP = HALF_TABLE_SIZE << 16;
  static constexpr uint32_t TABLE_SIZE_FP_MASK = TABLE_SIZE_FP_CONST - 1; // For fast wrapping

  // 16.16 fixed-point: upper 16 bits = table index, lower 16 bits = fractional
  uint32_t phase_fractional = 0;
  uint32_t phase_increment_fractional = 1228800; // ~440Hz default: (440 * TABLE_SIZE << 16) / SAMPLE_RATE
  uint32_t phase_increment_fractional_w1 = 1228800; // pulse width variant 1
  uint32_t phase_increment_fractional_w2 = 1228800; // pulse width variant 2
  // Spread variables (16.16 fixed-point for phase, float for ratios)
  float spread1 = 1.0f;
  float spread2 = 1.0f;
  bool spreadActive = false;
  uint32_t phase_fractional_s1 = 0;
  uint32_t phase_fractional_s2 = 0;
  uint32_t phase_increment_fractional_s1 = 1228800; // ~440Hz default
  uint32_t phase_increment_fractional_s2 = 1228800;
  int16_t * waveTable = nullptr;
  int16_t * bandPtr = nullptr;  // Pre-computed pointer to current frequency band
  bool allocated = false;
  int32_t prevSampVal = 0;
  bool isNoise = false;
  bool isCrackle = false;
  int crackleAmnt = MAX_16 * 0.5; //MAX_16 * 0.5;
  float frequency = 440;
  float prevFrequency = 440;
  float pulseWidth = 0.5;
  bool pulseWidthOn = false;
  int16_t prevParticle, particleEnv, particleThreshold = 0.993; //MAX_16 * 0.993;
  float particleEnvReleaseRate = 0.92; // thresh and rate = number of apparent particles
  uint32_t feedback_phase_fractional = 0; // 16.16 fixed-point
  float cachedModIndexF = 1.0f; // Cached mod index for single-arg phMod
  int32_t modDepthScale = 1024;  // Frequency-adaptive mod depth (0-1024, 1024 = full)
  float testVal = 1.3;
  float cycleLengthPerMS = frequency * 0.001f; // / 1000.0f;
  float midiPitch = 69;
  bool usePSRAM = false;

  /** Fill the waveTable with silence */
  void empty(int16_t * theTable) {
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      theTable[i] = 0; // zero out the wavetable
    }
  }

  /** Allocate memory for the waveTable */
  void allocateWavetable() {
    if (!allocated) {
      #if IS_ESP32()
        // Try PSRAM allocation with size checking (silent - no debug output for internal tables)
        waveTable = psramAllocInt16(FULL_TABLE_SIZE, nullptr);
        usePSRAM = (waveTable != nullptr);
        if (!waveTable) {
          // Fallback to regular RAM
          waveTable = new int16_t[FULL_TABLE_SIZE];
          empty(waveTable);
        }
      #else
        waveTable = new int16_t[FULL_TABLE_SIZE];
        empty(waveTable);
      #endif
      // Initialize bandPtr for default frequency (440Hz = mid band)
      bandPtr = waveTable + TABLE_SIZE;
      allocated = true;
    }
  }

  /* Bandlimited waveTable generator 
  * @segment Which 3rd of the full table to use, low (0), mid (1), high (2)
  */
  static void generateWave(int16_t * theTable, int segment, int overtones, int waveType) {
    float angularFreq = (2.0f * PI) / (float)TABLE_SIZE;
    float maxAmp = 1.0;
    float maxValue = -1;
    float minValue = 1;
    float * tempTable = new float[TABLE_SIZE];

    for (int i=0; i<TABLE_SIZE; i++) {
      tempTable[i] = 0;
    }
    for (int i=0; i<TABLE_SIZE; i++) {
      if (waveType == 1) { // triangle
        for(int m=0; m<overtones; m+=2) { // low 48, mid 20, high 12
          float nextOvertone = (maxAmp/((m+1)*(m+1)) * sin((angularFreq*(m+1))*i)); //triangle formula
          if (m%4 == 0) nextOvertone *= -1;
          tempTable[i] = (tempTable[i] + nextOvertone); 
        }
      }
      if (waveType == 2) { // square
        for(int m=0; m<overtones; m+=2) {  // <56 = 56, 28, >80 = 12
          float nextOvertone = (maxAmp/((m+1)) * sin((angularFreq*(m+1))*i)); //square wave formula
          tempTable[i] = (tempTable[i] + nextOvertone); 
        }
      }
      if (waveType == 3) { // sawtooth
        for(int m=0; m<overtones; m++) { // low 84, mid 34, high 8
          tempTable[i] = (tempTable[i] + ((maxAmp/(m+1) * sin((angularFreq*(m+1))*i)))); //sawtooth formula
        }
      }
      if (tempTable[i] > maxValue) { //checks highest value
        maxValue = tempTable[i];
      }
      if (tempTable[i] < minValue) { //checks lowest value
          minValue = tempTable[i];
      }
    }
    // normalise
    int segOffset = TABLE_SIZE * segment;
    for (int i=0; i<TABLE_SIZE; i++) {
      theTable[i + segOffset] = floatMap(tempTable[i], minValue, maxValue, -1, 1) * (MAX_16 * 2 - MAX_16);
    }
    delete[] tempTable;
  }

  /** Increments the phase of the oscillator without returning a sample. */
  inline void incrementPhase() {
      // Increment phase (pulse width uses different increments per half-cycle)
      if (pulseWidthOn) {
          if (phase_fractional < HALF_TABLE_SIZE_FP) {
              phase_fractional += phase_increment_fractional_w1;
          } else {
              phase_fractional += phase_increment_fractional_w2;
          }
      } else {
          // Use atomic load for thread-safety with setFreq on dual-core systems
          #if IS_ESP32() || IS_RP2040()
            uint32_t inc = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
            phase_fractional += inc;
          #else
            phase_fractional += phase_increment_fractional;
          #endif
      }

      // Fast path: normal wrapping using bitwise AND (TABLE_SIZE must be power of 2)
      if (!isNoise && !isCrackle) {
          phase_fractional &= TABLE_SIZE_FP_MASK; // Fast wrap: equivalent to modulo
          return;
      }

      // Noise / crackle modes (slower path, less common)
      if (phase_fractional >= TABLE_SIZE_FP_CONST) {
          if (isNoise) {
              phase_fractional = audioRand(TABLE_SIZE) << 16;
          } else { // crackle
              if (audioRand(0x8000) > crackleAmnt) {
                  phase_fractional = 1 << 16;
              } else {
                  phase_fractional = audioRand(TABLE_SIZE) << 16;
              }
          }
      }
  }

  /** Increments the phase of spread reads of the oscillator
  * without returning a sample.*/
	inline
	void incrementSpreadPhase() {
		phase_fractional_s1 += phase_increment_fractional_s1;
    phase_fractional_s1 &= TABLE_SIZE_FP_MASK; // Fast wrap
    phase_fractional_s2 += phase_increment_fractional_s2;
    phase_fractional_s2 &= TABLE_SIZE_FP_MASK; // Fast wrap
  }

  /** Returns a spread sample. */
	inline
	int16_t doSpread(int32_t sampVal) {
    int32_t spreadSamp1 = waveTable[phase_fractional_s1 >> 16]; // 16.16 fixed-point
    int32_t spreadSamp2 = waveTable[phase_fractional_s2 >> 16];
    sampVal = clip16((sampVal + ((spreadSamp1 * 500)>>10) + ((spreadSamp2 * 500)>>10))>>1);
    incrementSpreadPhase();
    return sampVal;
	}

  #if IS_ESP32() || IS_RP2040()
  /** Returns a spread sample using atomic phase updates for thread-safety. */
  inline
  int16_t doSpreadAtomic(int32_t sampVal) {
    // Atomic fetch-and-add for spread phases
    uint32_t myPhaseS1 = __atomic_fetch_add(&phase_fractional_s1, phase_increment_fractional_s1, __ATOMIC_RELAXED);
    uint32_t myPhaseS2 = __atomic_fetch_add(&phase_fractional_s2, phase_increment_fractional_s2, __ATOMIC_RELAXED);
    int32_t spreadSamp1 = waveTable[(myPhaseS1 >> 16) & (TABLE_SIZE - 1)];
    int32_t spreadSamp2 = waveTable[(myPhaseS2 >> 16) & (TABLE_SIZE - 1)];
    sampVal = clip16((sampVal + ((spreadSamp1 * 500)>>10) + ((spreadSamp2 * 500)>>10))>>1);
    return sampVal;
  }
  #endif
};

// Static member definitions for RP2040 dual-core phase sync
#if IS_RP2040()
Osc* Osc::registeredOscs[Osc::MAX_REGISTERED_OSCS] = {nullptr};
int Osc::numRegisteredOscs = 0;

// Wrapper functions for callbacks (can't use static member functions directly as function pointers easily)
void _oscAdvanceAllPhases(int coreNum) { Osc::advanceAllPhases(coreNum); }
void _oscClearStoredPhaseFlag(int coreNum) { Osc::clearStoredPhaseFlag(coreNum); }

// Auto-register callbacks when Osc.h is included
namespace {
  struct OscPhaseSyncRegistrar {
    OscPhaseSyncRegistrar() {
      registerPhaseSyncCallbacks(_oscAdvanceAllPhases, _oscClearStoredPhaseFlag);
    }
  };
  static OscPhaseSyncRegistrar _oscPhaseSyncRegistrar;
}
#endif

#endif /* OSC_H_ */
