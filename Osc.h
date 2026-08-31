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

/** A shareable, memory-owning oscillator wavetable.
 *
 * WaveTable keeps allocation details and raw pointers out of sketches. Generate
 * a waveform once, then pass the same object to any number of Osc instances.
 * The legacy pointer APIs remain available for advanced and existing code.
 */
class WaveTable {
public:
  WaveTable() = default;
  ~WaveTable() {
    if (_samples == nullptr) return;
    #if IS_ESP32()
      if (_usesPSRAM) free(_samples);
      else delete[] _samples;
    #else
      delete[] _samples;
    #endif
  }

  WaveTable(const WaveTable&) = delete;
  WaveTable& operator=(const WaveTable&) = delete;

  void cosGen();
  void sinGen();
  void triGen();
  void pulseGen(float duty);
  void sqrGen();
  void sawGen();
  void noiseGen();
  void noiseGen(int grainSize);
  void crackleGen();
  void brownNoiseGen();
  void pinkNoiseGen();

  bool isAllocated() const { return _samples != nullptr; }

private:
  int16_t* _samples = nullptr;
  bool _usesPSRAM = false;

  void allocate() {
    if (_samples != nullptr) return;
    #if IS_ESP32()
      _samples = psramAllocInt16(FULL_TABLE_SIZE, "wavetable");
      _usesPSRAM = (_samples != nullptr);
    #endif
    if (_samples == nullptr) {
      _samples = new int16_t[FULL_TABLE_SIZE];
      for (int i = 0; i < FULL_TABLE_SIZE; ++i) _samples[i] = 0;
      #if IS_ESP32()
        Serial.println("Wavetable allocated in regular RAM");
      #endif
    }
  }

  friend class Osc;
};

class Osc {

public:

  /** Constructor.
	* Has no table specified - make sure to use setTable() after initialising
	*/
  Osc() {
    // Give every oscillator a distinct deterministic noise stream without
    // consuming or locking the global audio PRNG. Users can override this via
    // setNoiseSeed() when a reproducible stream is required.
    noiseSalt = noiseHash((uint32_t)(uintptr_t)this ^ 0x9E3779B9u);
  }

  /** Updates the phase according to the current frequency and returns the sample at the new phase position.
	* @return outSamp The next sample.
	*/
	inline
	int16_t next() {
    int32_t sampVal;

    // Sample and hold: pick a random sample from the wavetable once per period
    if (isSandH) {
      #if IS_ESP32() || IS_RP2040()
      {
        uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
        int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
        if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;
        uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
        uint32_t newPhase = myPhase + cachedIncrement;
        // Detect period wrap: check if we crossed a TABLE_SIZE boundary
        if ((myPhase & ~TABLE_SIZE_FP_MASK) != (newPhase & ~TABLE_SIZE_FP_MASK)) {
          sandHValue = cachedBandPtr[audioRand(TABLE_SIZE)];
        }
        return sandHValue;
      }
      #endif
      // Non-atomic fallback
      phase_fractional += phase_increment_fractional;
      if (phase_fractional >= TABLE_SIZE_FP_CONST) {
        phase_fractional &= TABLE_SIZE_FP_MASK;
        sandHValue = bandPtr[audioRand(TABLE_SIZE)];
      }
      return sandHValue;
    }

    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      // Seqlock read: retry if setFreq() is mid-update (odd seq) or the seq changed
      // between our two loads — guarantees a consistent bandPtr+increment pair.
      uint32_t cachedIncrement;
      int16_t* cachedBandPtr;
      uint32_t _seqBefore, _seqAfter;
      do {
        _seqBefore = _freqSeq.load(std::memory_order_acquire);
        if (_seqBefore & 1) continue;  // write in progress — spin until stable
        cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
        cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
        _seqAfter = _freqSeq.load(std::memory_order_acquire);
      } while (_seqAfter != _seqBefore);

      // Safety check: if bandPtr is null or increment is zero, return silence
      if (cachedBandPtr == nullptr || cachedIncrement == 0) {
        return 0;
      }

      // Atomic fetch-and-add: each core gets a unique phase value
      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);

      // Apply deferred band change at a waveform zero crossing (same logic as phMod)
      {
        int16_t* pb = (int16_t*)__atomic_load_n((uintptr_t*)&_pendingBandPtr, __ATOMIC_RELAXED);
        if (pb != nullptr) {
          int rawIdx = (myPhase >> 16) & (TABLE_SIZE - 1);
          if (abs(cachedBandPtr[rawIdx]) < (MAX_16 >> 3)) {
            __atomic_store_n((uintptr_t*)&bandPtr, (uintptr_t)pb, __ATOMIC_RELAXED);
            __atomic_store_n((uintptr_t*)&_pendingBandPtr, (uintptr_t)nullptr, __ATOMIC_RELAXED);
            cachedBandPtr = pb;
          }
        }
      }

      int idx = (myPhase >> 16) & (TABLE_SIZE - 1);
      sampVal = cachedBandPtr[idx];

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }

    // Atomic path for noise and crackle oscillators
    if ((isNoise || isCrackle) && !pulseWidthOn) {
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);

      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;

      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);

      int idx;
      if (isCrackle) {
        // Crackle: normal index, sparse impulses in table
        idx = (myPhase >> 16) & (TABLE_SIZE - 1);
      } else {
        // Strong stateless avalanche hash: decorrelates adjacent phases while
        // preserving the efficiency of one masked wavetable lookup.
        idx = noiseTableIndex(myPhase);
      }
      sampVal = cachedBandPtr[idx];

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif

    // Non-atomic fallback for pulse width mode (or single-core platforms)
    int idx = (isNoise && !isCrackle)
                ? noiseTableIndex(phase_fractional)
                : (phase_fractional >> 16);
    sampVal = bandPtr[idx];
    incrementPhase();
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Return the next sample without an atomic phase advance.
   * Use only when exactly one audio core owns this oscillator's render state,
   * such as a voice assigned with audioPartitionOffset()/Stride(). Control-rate
   * setFreq()/setPitch() changes remain safe: the frequency seqlock still gives
   * this method a consistent wavetable-band pointer and phase increment.
   */
  inline int16_t nextUnlocked() {
    int32_t sampVal;

    #if IS_ESP32() || IS_RP2040()
    uint32_t cachedIncrement;
    int16_t* cachedBandPtr;
    uint32_t seqBefore, seqAfter;
    do {
      seqBefore = _freqSeq.load(std::memory_order_acquire);
      if (seqBefore & 1u) continue;
      cachedIncrement = __atomic_load_n(&phase_increment_fractional,
                                         __ATOMIC_RELAXED);
      cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr,
                                                __ATOMIC_RELAXED);
      seqAfter = _freqSeq.load(std::memory_order_acquire);
    } while (seqAfter != seqBefore);
    #else
    uint32_t cachedIncrement = phase_increment_fractional;
    int16_t* cachedBandPtr = bandPtr;
    #endif

    if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;

    if (isSandH) {
      uint32_t oldPhase = phase_fractional;
      uint32_t newPhase = oldPhase + cachedIncrement;
      phase_fractional = newPhase & TABLE_SIZE_FP_MASK;
      if ((oldPhase & ~TABLE_SIZE_FP_MASK) !=
          (newPhase & ~TABLE_SIZE_FP_MASK)) {
        sandHValue = cachedBandPtr[audioRand(TABLE_SIZE)];
      }
      return sandHValue;
    }

    // Pulse-width and crackle modes have specialised phase/wrap behaviour.
    if (pulseWidthOn || isCrackle) {
      int idx = (phase_fractional >> 16) & (TABLE_SIZE - 1);
      sampVal = cachedBandPtr[idx];
      incrementPhase();
      if (spreadActive) sampVal = doSpread(sampVal);
      return sampVal;
    }

    uint32_t myPhase = phase_fractional;
    phase_fractional = (myPhase + cachedIncrement) & TABLE_SIZE_FP_MASK;

    // Preserve next()'s click-reducing deferred band switch without making the
    // exclusively owned phase atomic.
    #if IS_ESP32() || IS_RP2040()
    int16_t* pending = (int16_t*)__atomic_load_n(
        (uintptr_t*)&_pendingBandPtr, __ATOMIC_RELAXED);
    if (pending != nullptr) {
      int rawIdx = (myPhase >> 16) & (TABLE_SIZE - 1);
      if (abs(cachedBandPtr[rawIdx]) < (MAX_16 >> 3)) {
        __atomic_store_n((uintptr_t*)&bandPtr, (uintptr_t)pending,
                         __ATOMIC_RELAXED);
        __atomic_store_n((uintptr_t*)&_pendingBandPtr, (uintptr_t)nullptr,
                         __ATOMIC_RELAXED);
        cachedBandPtr = pending;
      }
    }
    #endif

    int idx = isNoise ? noiseTableIndex(myPhase)
                      : ((myPhase >> 16) & (TABLE_SIZE - 1));
    sampVal = cachedBandPtr[idx];
    if (spreadActive) sampVal = doSpread(sampVal);
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
    int idx;

    // Fast path: atomic phase increment for thread-safe dual-core operation
    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      uint32_t myPhase;
      // Seqlock read: same protocol as next() — guarantees consistent bandPtr+increment pair.
      uint32_t cachedIncrement;
      int16_t* cachedBandPtr;
      uint32_t _seqBefore, _seqAfter;
      do {
        _seqBefore = _freqSeq.load(std::memory_order_acquire);
        if (_seqBefore & 1) continue;
        cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
        cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
        _seqAfter = _freqSeq.load(std::memory_order_acquire);
      } while (_seqAfter != _seqBefore);
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

  /** Use a shared WaveTable without exposing its internal memory pointer. */
  inline void setTable(WaveTable& table) {
    table.allocate();
    setTable(table._samples);
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
    // Refresh only the spread increments. Calling setFreq(getFreq()) here used
    // to force this indirectly, but would defeat setFreq()'s idempotent path.
    #if IS_ESP32() || IS_RP2040()
    uint32_t currentIncrement =
        __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
    #else
    uint32_t currentIncrement = phase_increment_fractional;
    #endif
    if (spreadActive) {
      phase_increment_fractional_s1 = (uint32_t)(currentIncrement * spread1);
      phase_increment_fractional_s2 = (uint32_t)(currentIncrement * spread2);
    } else {
      phase_increment_fractional_s1 = currentIncrement;
      phase_increment_fractional_s2 = currentIncrement;
    }
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
    if (morphAmount <= 0) return next(); // identical to next() when not morphing
    int intMorphAmount = max(0, min (1024, (int)(1024 * morphAmount)));
    int32_t sampVal;
    #if IS_ESP32() || IS_RP2040()
    {
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;
      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      int idx = (myPhase >> 16) & (TABLE_SIZE - 1);
      int bandOffset = (int)(cachedBandPtr - waveTable);
      int32_t sampVal1 = cachedBandPtr[idx];
      int32_t sampVal2;
      if (isSandH && isNoise) {
        uint32_t newPhase = myPhase + cachedIncrement;
        if ((myPhase & ~TABLE_SIZE_FP_MASK) != (newPhase & ~TABLE_SIZE_FP_MASK)) {
          sandHValue = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
        }
        sampVal2 = sandHValue;
      } else if (isNoise) {
        sampVal2 = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
      } else {
        sampVal2 = secondWaveTable[bandOffset + idx];
      }
      sampVal = (((sampVal2 * intMorphAmount) >> 10) +
        ((sampVal1 * (1024 - intMorphAmount)) >> 10));
      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif
    // Non-atomic fallback for single-core platforms
    int idx = (phase_fractional >> 16) & (TABLE_SIZE - 1);
    int bandOffset = (int)(bandPtr - waveTable);
    int32_t sampVal1 = bandPtr[idx];
    int32_t sampVal2;
    if (isSandH && isNoise) {
      phase_fractional += phase_increment_fractional;
      if (phase_fractional >= TABLE_SIZE_FP_CONST) {
        phase_fractional &= TABLE_SIZE_FP_MASK;
        sandHValue = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
      }
      sampVal2 = sandHValue;
    } else if (isNoise) {
      sampVal2 = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
    } else {
      sampVal2 = secondWaveTable[bandOffset + idx];
    }
    sampVal = (((sampVal2 * intMorphAmount) >> 10) +
      ((sampVal1 * (1024 - intMorphAmount)) >> 10));
    if (!(isSandH && isNoise)) incrementPhase();
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Morph toward a shared WaveTable. */
  inline int16_t nextMorph(const WaveTable& secondWaveTable, float morphAmount) {
    if (!secondWaveTable.isAllocated()) return next();
    return nextMorph(secondWaveTable._samples, morphAmount);
  }

  /** Get a blend of this Osc and another, without incrementing the waveTable lookup.
  * @param secondWaveTable - a waveTable  array to morph with
  * @param morphAmount - The balance (mix) of the second waveTable , 0.0 - 1.0
  */
	inline
  int16_t currentMorph(int16_t * secondWaveTable, float morphAmount) {
    if (morphAmount <= 0) {
      // Read current sample without morphing
      #if IS_ESP32() || IS_RP2040()
        int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
        if (cachedBandPtr == nullptr) return 0;
        uint32_t myPhase = __atomic_load_n(&phase_fractional, __ATOMIC_RELAXED);
        int idx = (myPhase >> 16) & (TABLE_SIZE - 1);
        prevSampVal = cachedBandPtr[idx];
      #else
        int idx = (phase_fractional >> 16) & (TABLE_SIZE - 1);
        prevSampVal = bandPtr[idx];
      #endif
      return prevSampVal;
    }
    int intMorphAmount = max(0, min(1024, (int)(1024 * morphAmount)));
    int32_t sampVal;
    #if IS_ESP32() || IS_RP2040()
    {
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
      if (cachedBandPtr == nullptr) return 0;
      uint32_t myPhase = __atomic_load_n(&phase_fractional, __ATOMIC_RELAXED);
      int idx = (myPhase >> 16) & (TABLE_SIZE - 1);
      int bandOffset = (int)(cachedBandPtr - waveTable);
      int32_t sampVal1 = cachedBandPtr[idx];
      int32_t sampVal2 = secondWaveTable[bandOffset + idx];
      sampVal = (((sampVal2 * intMorphAmount) >> 10) +
        ((sampVal1 * (1024 - intMorphAmount)) >> 10));
      prevSampVal = sampVal;
      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif
    int idx = (phase_fractional >> 16) & (TABLE_SIZE - 1);
    int bandOffset = (int)(bandPtr - waveTable);
    int32_t sampVal1 = bandPtr[idx];
    int32_t sampVal2 = secondWaveTable[bandOffset + idx];
    sampVal = (((sampVal2 * intMorphAmount) >> 10) +
      ((sampVal1 * (1024 - intMorphAmount)) >> 10));
    prevSampVal = sampVal;
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Read the current phase morphed toward a shared WaveTable. */
  inline int16_t currentMorph(const WaveTable& secondWaveTable, float morphAmount) {
    if (!secondWaveTable.isAllocated()) return getValue();
    return currentMorph(secondWaveTable._samples, morphAmount);
  }

  /** Get a window transform between this Osc and another waveTable .
  * Inspired by the Window Transform Function by Dove Audio
  * @param secondWaveTable - an waveTable array to transform with
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

  /** Apply a window transform using a shared WaveTable. */
  inline int16_t nextWTrans(const WaveTable& secondWaveTable, float windowSize,
                           bool duel, bool invert) {
    if (!secondWaveTable.isAllocated()) return next();
    return nextWTrans(secondWaveTable._samples, windowSize, duel, invert);
  }

  /** Phase Modulation (FM)
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndex - Modulation depth, 0.0 to ~10.0 typical
   *   0.5-1.0: Subtle FM shimmer
   *   2.0-4.0: Classic FM tones
   *   5.0-10.0: Aggressive FM
   */
  inline int16_t phMod(int16_t modulator, float modIndex) {
    // Anti-aliasing: clamp modIndex to depth_max = 9000 / (freq * cmRatio)
    float dMax = _cachedDepthMax;
    if (modIndex > dMax) modIndex = dMax;

    // Calculate phase offset in 16.16 format
    int32_t modOffset = (int32_t)((float)modulator * modIndex * 8.0f);
    modOffset <<= 8; // Scale to 16.16 format

    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      // Atomic path: each core gets a unique phase value (matches next() pattern)
      // ACQUIRE on increment synchronizes-with setFreq()'s RELEASE store.
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_ACQUIRE);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);

      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;

      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);

      // Apply deferred band change at a zero crossing of the raw (unmodulated) phase.
      // All band variants share the same zero crossings, so the switch is seamless:
      // cachedBandPtr[rawIdx] ≈ 0 → modOffset ≈ 0 → no discontinuity in carrier output.
      {
        int16_t* pb = (int16_t*)__atomic_load_n((uintptr_t*)&_pendingBandPtr, __ATOMIC_RELAXED);
        if (pb != nullptr) {
          int rawIdx = (myPhase >> 16) & (TABLE_SIZE - 1);
          if (abs(cachedBandPtr[rawIdx]) < (MAX_16 >> 3)) {
            __atomic_store_n((uintptr_t*)&bandPtr, (uintptr_t)pb, __ATOMIC_RELAXED);
            __atomic_store_n((uintptr_t*)&_pendingBandPtr, (uintptr_t)nullptr, __ATOMIC_RELAXED);
            cachedBandPtr = pb;
          }
        }
      }

      // Add modulation offset to this core's phase
      uint32_t p = myPhase + modOffset;

      int idx = (p >> 16) & (TABLE_SIZE - 1);
      int frac = (p >> 6) & 0x3FF;

      int16_t s0 = cachedBandPtr[idx];
      int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
      int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif

    // Non-atomic fallback for pulse width, noise, crackle, or single-core platforms
    uint32_t p = phase_fractional + modOffset;

    int idx = (p >> 16) & (TABLE_SIZE - 1);
    int frac = (p >> 6) & 0x3FF;  // 10-bit fractional (0-1023)

    int16_t* cachedBandPtr = bandPtr;

    int16_t s0 = cachedBandPtr[idx];
    int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
    int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);

    incrementPhase();

    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Phase Modulation using pre-scaled integer mod index (avoids per-sample float math)
   *
   * Equivalent to phMod() but replaces two per-sample float multiplies with one
   * integer multiply + shift. Pre-compute the scaled value at parameter-change time
   * (e.g., in loop() or at note trigger), then pass it here in audioUpdate().
   *
   * Pre-scaling formula:
   *   int32_t modIndexScaled = (int32_t)(modIndex * 2048.0f);
   *   // where 2048 = 8.0 * 256.0 (the internal scaling factors)
   *
   * Example usage:
   *   // At parameter-change time (loop, envelope update, note trigger):
   *   float modIndex = fmDepth * envelopeFollow;  // 0.0 to ~10.0
   *   int32_t scaledMod = (int32_t)(modIndex * 2048.0f);
   *
   *   // In audioUpdate() (called at sample rate):
   *   int16_t sample = osc.phModInt(fmOsc.next(), scaledMod);
   *
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndexScaled - Pre-scaled mod index: (int32_t)(modIndex * 2048.0f)
   */
  inline int16_t phModInt(int16_t modulator, int32_t modIndexScaled) {
    // Anti-aliasing: clamp pre-scaled modIndex to depth_max * 2048
    int32_t dMaxScaled = _cachedDepthMaxScaled;
    if (modIndexScaled > dMaxScaled) modIndexScaled = dMaxScaled;

    // modOffset = modulator * modIndex * 8.0f, pre-scaled by 256
    int32_t modOffset = ((int32_t)modulator * modIndexScaled) >> 8;
    modOffset <<= 8; // Scale to 16.16 format

    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      // ACQUIRE on increment synchronizes-with setFreq()'s RELEASE store.
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_ACQUIRE);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);

      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;

      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      uint32_t p = myPhase + modOffset;

      int idx = (p >> 16) & (TABLE_SIZE - 1);
      int frac = (p >> 6) & 0x3FF;

      int16_t s0 = cachedBandPtr[idx];
      int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
      int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif

    uint32_t p = phase_fractional + modOffset;
    int idx = (p >> 16) & (TABLE_SIZE - 1);
    int frac = (p >> 6) & 0x3FF;

    int16_t* cachedBandPtr = bandPtr;
    int16_t s0 = cachedBandPtr[idx];
    int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
    int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);

    incrementPhase();

    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Integer phase modulation without an atomic carrier-phase advance.
   * Use only for an oscillator exclusively owned by the calling audio core.
   */
  inline int16_t phModIntUnlocked(int16_t modulator,
                                  int32_t modIndexScaled) {
    int32_t dMaxScaled = _cachedDepthMaxScaled;
    if (modIndexScaled > dMaxScaled) modIndexScaled = dMaxScaled;

    int32_t modOffset = ((int32_t)modulator * modIndexScaled) >> 8;
    modOffset <<= 8;

    #if IS_ESP32() || IS_RP2040()
    uint32_t cachedIncrement;
    int16_t* cachedBandPtr;
    uint32_t seqBefore, seqAfter;
    do {
      seqBefore = _freqSeq.load(std::memory_order_acquire);
      if (seqBefore & 1u) continue;
      cachedIncrement = __atomic_load_n(&phase_increment_fractional,
                                         __ATOMIC_RELAXED);
      cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr,
                                                __ATOMIC_RELAXED);
      seqAfter = _freqSeq.load(std::memory_order_acquire);
    } while (seqAfter != seqBefore);
    #else
    uint32_t cachedIncrement = phase_increment_fractional;
    int16_t* cachedBandPtr = bandPtr;
    #endif

    if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;
    if (pulseWidthOn || isNoise || isCrackle) {
      uint32_t p = phase_fractional + modOffset;
      int idx = (p >> 16) & (TABLE_SIZE - 1);
      int frac = (p >> 6) & 0x3FF;
      int16_t s0 = cachedBandPtr[idx];
      int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
      int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);
      incrementPhase();
      if (spreadActive) sampVal = doSpread(sampVal);
      return sampVal;
    }

    uint32_t myPhase = phase_fractional;
    phase_fractional = (myPhase + cachedIncrement) & TABLE_SIZE_FP_MASK;
    uint32_t p = myPhase + modOffset;
    int idx = (p >> 16) & (TABLE_SIZE - 1);
    int frac = (p >> 6) & 0x3FF;
    int16_t s0 = cachedBandPtr[idx];
    int16_t s1 = cachedBandPtr[(idx + 1) & (TABLE_SIZE - 1)];
    int32_t sampVal = s0 + (((s1 - s0) * frac) >> 10);
    if (spreadActive) sampVal = doSpread(sampVal);
    return sampVal;
  }

  /** Phase Modulation with wavetable morphing (FM + Morph)
   * Combines phMod and nextMorph in a single phase advance.
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndex - Modulation depth, 0.0 to ~10.0 typical
   * @param secondWaveTable - A wavetable array to morph with
   * @param morphAmount - The balance (mix) of the second wavetable, 0.0 - 1.0
   */
  inline int16_t phModMorph(int16_t modulator, float modIndex, int16_t * secondWaveTable, float morphAmount) {
    if (morphAmount <= 0) return phMod(modulator, modIndex);
    int intMorphAmount = max(0, min(1024, (int)(1024 * morphAmount)));

    // Anti-aliasing: clamp modIndex to depth_max = 9000 / (freq * cmRatio)
    float dMax = _cachedDepthMax;
    if (modIndex > dMax) modIndex = dMax;

    int32_t modOffset = (int32_t)((float)modulator * modIndex * 8.0f);
    modOffset <<= 8;

    #if IS_ESP32() || IS_RP2040()
    {
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;
      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      uint32_t p = myPhase + modOffset;
      int idx = (p >> 16) & (TABLE_SIZE - 1);
      int bandOffset = (int)(cachedBandPtr - waveTable);
      int32_t sampVal1 = cachedBandPtr[idx];
      int32_t sampVal2;
      if (isSandH && isNoise) {
        uint32_t newPhase = myPhase + cachedIncrement;
        if ((myPhase & ~TABLE_SIZE_FP_MASK) != (newPhase & ~TABLE_SIZE_FP_MASK)) {
          sandHValue = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
        }
        sampVal2 = sandHValue;
      } else if (isNoise) {
        sampVal2 = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
      } else {
        sampVal2 = secondWaveTable[bandOffset + idx];
      }
      if (intMorphAmount >= 1024) {
        if (spreadActive) sampVal2 = doSpreadAtomic(sampVal2);
        return sampVal2;
      }
      int32_t sampVal = (((sampVal2 * intMorphAmount) >> 10) +
        ((sampVal1 * (1024 - intMorphAmount)) >> 10));
      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif
    // Non-atomic fallback
    uint32_t p = phase_fractional + modOffset;
    int idx = (p >> 16) & (TABLE_SIZE - 1);
    int bandOffset = (int)(bandPtr - waveTable);
    int32_t sampVal1 = bandPtr[idx];
    int32_t sampVal2;
    if (isSandH && isNoise) {
      phase_fractional += phase_increment_fractional;
      if (phase_fractional >= TABLE_SIZE_FP_CONST) {
        phase_fractional &= TABLE_SIZE_FP_MASK;
        sandHValue = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
      }
      sampVal2 = sandHValue;
    } else if (isNoise) {
      sampVal2 = secondWaveTable[bandOffset + audioRand(TABLE_SIZE)];
    } else {
      sampVal2 = secondWaveTable[bandOffset + idx];
    }
    int32_t sampVal = (((sampVal2 * intMorphAmount) >> 10) +
      ((sampVal1 * (1024 - intMorphAmount)) >> 10));
    if (!(isSandH && isNoise)) incrementPhase();
    if (spreadActive) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Phase modulation and morphing using a shared WaveTable. */
  inline int16_t phModMorph(int16_t modulator, float modIndex,
                           const WaveTable& secondWaveTable, float morphAmount) {
    if (!secondWaveTable.isAllocated()) return phMod(modulator, modIndex);
    return phModMorph(modulator, modIndex, secondWaveTable._samples, morphAmount);
  }

  /** Phase Modulation with window transform (FM + WTrans)
   * Combines phMod and nextWTrans in a single phase advance.
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndex - Modulation depth, 0.0 to ~10.0 typical
   * @param secondWaveTable - A wavetable array for the window transform
   * @param windowSize - Window size 0.0 - 1.0
   * @param duel - Use dual windows (quarter-cycle spacing)
   * @param invert - Invert the second wavetable in window regions
   */
  inline int16_t phModWTrans(int16_t modulator, float modIndex, int16_t * secondWaveTable, float windowSize, bool duel, bool invert) {
    // Anti-aliasing: clamp modIndex to depth_max = 9000 / (freq * cmRatio)
    float dMax = _cachedDepthMax;
    if (modIndex > dMax) modIndex = dMax;

    int32_t modOffset = (int32_t)((float)modulator * modIndex * 8.0f);
    modOffset <<= 8;

    // Advance phase and compute modulated phase position
    #if IS_ESP32() || IS_RP2040()
    {
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;
      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      uint32_t p = myPhase + modOffset;
      int phaseIdx = (p >> 16) & (TABLE_SIZE - 1);

      int halfTable = HALF_TABLE_SIZE;
      int portion12 = halfTable * windowSize;
      int quarterTable = TABLE_SIZE * 0.25;
      int threeQuarterTable = quarterTable * 3;
      int portion14 = quarterTable * windowSize;
      int32_t sampVal = 0;

      if (duel) {
        if (phaseIdx < (quarterTable - portion14) || (phaseIdx > (quarterTable + portion14) &&
            phaseIdx < (threeQuarterTable - portion14)) || phaseIdx > (threeQuarterTable + portion14)) {
          sampVal = cachedBandPtr[phaseIdx];
        } else {
          sampVal = secondWaveTable[phaseIdx];
          if (invert) sampVal *= -1;
        }
      } else {
        if (phaseIdx < (halfTable - portion12) || phaseIdx > (halfTable + portion12)) {
          sampVal = cachedBandPtr[phaseIdx];
        } else {
          sampVal = secondWaveTable[phaseIdx];
          if (invert) sampVal *= -1;
        }
      }
      sampVal = (sampVal + prevSampVal) >> 1;
      prevSampVal = sampVal;
      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif
    // Non-atomic fallback
    uint32_t p = phase_fractional + modOffset;
    int phaseIdx = (p >> 16) & (TABLE_SIZE - 1);

    int halfTable = HALF_TABLE_SIZE;
    int portion12 = halfTable * windowSize;
    int quarterTable = TABLE_SIZE * 0.25;
    int threeQuarterTable = quarterTable * 3;
    int portion14 = quarterTable * windowSize;
    int32_t sampVal = 0;

    if (duel) {
      if (phaseIdx < (quarterTable - portion14) || (phaseIdx > (quarterTable + portion14) &&
          phaseIdx < (threeQuarterTable - portion14)) || phaseIdx > (threeQuarterTable + portion14)) {
        int idx = phaseIdx;
        if (frequency > 831) {
          sampVal = waveTable[idx + TABLE_SIZE + TABLE_SIZE];
        } else if (frequency > 208) {
          sampVal = waveTable[idx + TABLE_SIZE];
        } else {
          sampVal = (waveTable[idx] + prevSampVal) >> 1;
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
          sampVal = (sampVal + spreadSamp1) >> 1;
          int32_t spreadSamp2 = secondWaveTable[phaseIdx];
          sampVal = (sampVal + spreadSamp2) >> 1;
          incrementSpreadPhase();
        }
      }
    } else {
      if (phaseIdx < (halfTable - portion12) || phaseIdx > (halfTable + portion12)) {
        int idx = phaseIdx;
        if (frequency > 831) {
          sampVal = waveTable[idx + TABLE_SIZE + TABLE_SIZE];
        } else if (frequency > 208) {
          sampVal = waveTable[idx + TABLE_SIZE];
        } else {
          sampVal = (waveTable[idx] + prevSampVal) >> 1;
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
          sampVal = (sampVal + spreadSamp1) >> 1;
          int32_t spreadSamp2 = secondWaveTable[phaseIdx];
          sampVal = (sampVal + spreadSamp2) >> 1;
          incrementSpreadPhase();
        }
      }
    }
    sampVal = (sampVal + prevSampVal) >> 1;
    prevSampVal = sampVal;
    incrementPhase();
    return sampVal;
  }

  /** Phase modulation and window transform using a shared WaveTable. */
  inline int16_t phModWTrans(int16_t modulator, float modIndex,
                            const WaveTable& secondWaveTable, float windowSize,
                            bool duel, bool invert) {
    if (!secondWaveTable.isAllocated()) return phMod(modulator, modIndex);
    return phModWTrans(modulator, modIndex, secondWaveTable._samples,
                       windowSize, duel, invert);
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

  /** Phase Modulation with atomically paired modulator advance (dual-core safe)
   *
   * Mirrors phModInt(Osc& modOsc, int32_t) — use when modIndex is a runtime float
   * rather than a pre-scaled integer. On dual-core platforms, ensures modOsc.next()
   * and the carrier phase advance are treated as an atomic pair.
   *
   * @param modOsc    - The modulator oscillator (advanced atomically with carrier)
   * @param modIndex  - Modulation depth, 0.0 to ~10.0 typical
   */
  inline int16_t phMod(Osc& modOsc, float modIndex) {
    // If C:M ratio wasn't set explicitly, derive depth_max from the modulator's
    // own frequency: ratio = modOsc.freq / carrier.freq, so the cap simplifies to
    // depth_max = 9000 / modOsc.freq. Pre-clamp here; the inner phMod still runs
    // its own clamp against the cached cap (using ratio=1 default), so the final
    // modIndex is the tighter of the two — strictly safer for anti-aliasing.
    if (!_cmRatioSet) {
      float modFreq = modOsc.getFreq();
      if (modFreq < 1.0f) modFreq = 1.0f;
      float dMax = 9000.0f / modFreq;
      if (modIndex > dMax) modIndex = dMax;
    }
    #if IS_ESP32() || IS_RP2040()
    int16_t result;
    M16_ATOMIC_GUARD_BLOCKING(_pairLock, {
      result = phMod(modOsc.next(), modIndex);
    });
    return result;
    #else
    return phMod(modOsc.next(), modIndex);
    #endif
  }

  /** Phase Modulation with atomically paired modulator advance (dual-core safe)
   *
   * Use this overload instead of phModInt(int16_t, int32_t) when both the carrier
   * and modulator oscillators are shared between dual-core audio callbacks. It
   * guarantees that fmOsc.next() and the carrier phase advance happen as an atomic
   * pair — preventing the interleaving race where one core picks up the wrong
   * modulator sample relative to its carrier phase, causing FM discontinuities.
   *
   * Lock hold time is ~2 atomic fetch_add operations, so spinning is safe.
   *
   * Usage:
   *   // Instead of: carrier.phModInt(modOsc.next(), scaledIdx)
   *   carrier.phModInt(modOsc, scaledIdx)
   *
   * @param modOsc         - The modulator oscillator (advanced atomically with carrier)
   * @param modIndexScaled - Pre-scaled mod index: (int32_t)(modIndex * 2048.0f)
   */
  inline int16_t phModInt(Osc& modOsc, int32_t modIndexScaled) {
    // Auto-derive depth cap from modulator freq when C:M ratio wasn't explicitly set.
    // See phMod(Osc&, float) for rationale.
    if (!_cmRatioSet) {
      float modFreq = modOsc.getFreq();
      if (modFreq < 1.0f) modFreq = 1.0f;
      int32_t dMaxScaled = (int32_t)((9000.0f / modFreq) * 2048.0f);
      if (modIndexScaled > dMaxScaled) modIndexScaled = dMaxScaled;
    }
    #if IS_ESP32() || IS_RP2040()
    int16_t result;
    M16_ATOMIC_GUARD_BLOCKING(_pairLock, {
      result = phModInt(modOsc.next(), modIndexScaled);
    });
    return result;
    #else
    return phModInt(modOsc.next(), modIndexScaled);
    #endif
  }

  /** Paired integer phase modulation for an exclusively owned voice pair.
   * Unlike phModInt(Osc&, ...), this performs no pair lock or atomic phase
   * fetch-add. Both carrier and modulator must belong to this audio core.
   */
  inline int16_t phModIntUnlocked(Osc& modOsc, int32_t modIndexScaled) {
    if (!_cmRatioSet) {
      float modFreq = modOsc.getFreq();
      if (modFreq < 1.0f) modFreq = 1.0f;
      int32_t dMaxScaled = (int32_t)((9000.0f / modFreq) * 2048.0f);
      if (modIndexScaled > dMaxScaled) modIndexScaled = dMaxScaled;
    }
    return phModIntUnlocked(modOsc.nextUnlocked(), modIndexScaled);
  }

  /** Phase Modulation with 2x oversampling (FM)
   * Higher quality anti-aliasing at ~2x CPU cost
   * @param modulator - The next sample from the modulating waveform (int16_t)
   * @param modIndex - Modulation depth, 0.0 to ~10.0 typical
   */
  inline int16_t phMod2(int16_t modulator, float modIndex) {
    // Anti-aliasing: clamp modIndex to depth_max = 9000 / (freq * cmRatio)
    float dMax = _cachedDepthMax;
    if (modIndex > dMax) modIndex = dMax;

    // Calculate phase offset in 16.16 format
    int32_t modOffset = (int32_t)((float)modulator * modIndex * 8.0f);
    modOffset <<= 8; // Scale to 16.16 format

    #if IS_ESP32() || IS_RP2040()
    if (!pulseWidthOn && !isNoise && !isCrackle) {
      // Atomic path: advance full increment atomically, compute two lookups from it
      uint32_t cachedIncrement = __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* cachedBandPtr = (int16_t*)__atomic_load_n((uintptr_t*)&bandPtr, __ATOMIC_RELAXED);

      if (cachedBandPtr == nullptr || cachedIncrement == 0) return 0;

      uint32_t myPhase = __atomic_fetch_add(&phase_fractional, cachedIncrement, __ATOMIC_RELAXED);
      uint32_t halfInc = cachedIncrement >> 1;

      // --- Sample 1: at start of this core's phase slice ---
      uint32_t p1 = myPhase + modOffset;
      int idx1 = (p1 >> 16) & (TABLE_SIZE - 1);
      int frac1 = (p1 >> 6) & 0x3FF;
      int16_t s0 = cachedBandPtr[idx1];
      int16_t s1 = cachedBandPtr[(idx1 + 1) & (TABLE_SIZE - 1)];
      int32_t samp1 = s0 + (((s1 - s0) * frac1) >> 10);

      // --- Sample 2: at half-step within this core's phase slice ---
      uint32_t p2 = (myPhase + halfInc) + modOffset;
      int idx2 = (p2 >> 16) & (TABLE_SIZE - 1);
      int frac2 = (p2 >> 6) & 0x3FF;
      s0 = cachedBandPtr[idx2];
      s1 = cachedBandPtr[(idx2 + 1) & (TABLE_SIZE - 1)];
      int32_t samp2 = s0 + (((s1 - s0) * frac2) >> 10);

      int32_t sampVal = (samp1 + samp2) >> 1;

      if (spreadActive) {
        sampVal = doSpreadAtomic(sampVal);
      }
      return sampVal;
    }
    #endif

    // Non-atomic fallback for pulse width, noise, crackle, or single-core platforms
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

      // Most control loops may present the same settled value repeatedly. If
      // both published parts already describe this exact frequency, avoid all
      // derived arithmetic and, on dual-core targets, avoid an unnecessary
      // seqlock generation that can briefly make the audio reader retry.
      #if IS_ESP32() || IS_RP2040()
      uint32_t currentIncrement =
          __atomic_load_n(&phase_increment_fractional, __ATOMIC_RELAXED);
      int16_t* currentBandPtr = (int16_t*)__atomic_load_n(
          (uintptr_t*)&bandPtr, __ATOMIC_RELAXED);
      int16_t* pendingBandPtr = (int16_t*)__atomic_load_n(
          (uintptr_t*)&_pendingBandPtr, __ATOMIC_RELAXED);
      #else
      uint32_t currentIncrement = phase_increment_fractional;
      int16_t* currentBandPtr = bandPtr;
      int16_t* pendingBandPtr = _pendingBandPtr;
      #endif
      bool bandAlreadyRequested = newBandPtr == currentBandPtr ||
                                  newBandPtr == pendingBandPtr;
      if (freq == frequency && newIncrement == currentIncrement &&
          bandAlreadyRequested) {
        return;
      }

      frequency = freq;

      // Seqlock write: mark odd (write in progress), store increment, mark even (stable).
      // next()/phMod() read the seqlock before and after loading the pair and retry if
      // the count changed — guaranteeing they always see a consistent increment.
      // Band changes are deferred to the oscillator's next zero crossing (applied in
      // next()/phMod()) to prevent waveform discontinuities in FM cascade chains.
      #if IS_ESP32() || IS_RP2040()
        {
          if (newBandPtr != currentBandPtr) {
            if (currentBandPtr == nullptr) {
              // Initial setup before audio starts: apply immediately
              __atomic_store_n((uintptr_t*)&bandPtr, (uintptr_t)newBandPtr, __ATOMIC_RELAXED);
            } else {
              // Runtime change: defer to next zero crossing to avoid click
              __atomic_store_n((uintptr_t*)&_pendingBandPtr, (uintptr_t)newBandPtr, __ATOMIC_RELAXED);
            }
          }
        }
        uint32_t _seq = _freqSeq.load(std::memory_order_relaxed);
        _freqSeq.store(_seq + 1, std::memory_order_release);  // odd = write in progress
        __atomic_store_n(&phase_increment_fractional, newIncrement, __ATOMIC_RELAXED);
        _freqSeq.store(_seq + 2, std::memory_order_release);  // even = stable
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

      // Anti-aliasing depth cap for phMod variants: depth_max = 9000 / (freq * cmRatio).
      // Each phMod clamps the caller's modIndex against this cap so FM sidebands stay
      // bounded as the carrier rises and/or the C:M ratio widens.
      // Skipped when disableAntiAlias() has been called (feedback FM, intentional aliasing).
      if (!_antiAliasDisabled) {
        float dMax = 9000.0f / (freq * _cmRatio);
        _cachedDepthMax = dMax;
        _cachedDepthMaxScaled = (int32_t)(dMax * 2048.0f);
      }
    }
	}

	/** Return the frequency of the oscillator in Hz. */
	inline
	float getFreq() {
		return frequency;
	}

  /** Set the carrier-to-modulator ratio used by the anti-aliasing depth cap.
   *
   * The cap applied inside every phMod variant is:
   *     depth_max = 9000 / (freq * ratio)
   *
   * Calling this marks the ratio as explicitly set, so the Osc& overloads
   * (phMod(Osc&, ...), phModInt(Osc&, ...)) will use this value rather than
   * auto-deriving from the modulator oscillator's frequency.
   *
   * @param ratio - C:M ratio (positive). Default before this is called is 1.0.
   */
  inline void setCMRatio(float ratio) {
    if (ratio > 0.0f) {
      _cmRatio = ratio;
      _cmRatioSet = true;
      if (!_antiAliasDisabled) {
        float dMax = 9000.0f / (frequency * ratio);
        _cachedDepthMax = dMax;
        _cachedDepthMaxScaled = (int32_t)(dMax * 2048.0f);
      }
    }
  }

  /** Return the currently configured C:M ratio (1.0 if never set). */
  inline float getCMRatio() const { return _cmRatio; }

  /** Disable the anti-aliasing modIndex cap for this oscillator.
   *
   * By default, every phMod variant clamps modIndex to depth_max = 9000/(freq*cmRatio)
   * to keep FM sidebands below Nyquist. Call this when that cap is undesirable:
   * feedback FM (self-modulation for noise), intentional aliasing, or bit-crush effects.
   *
   * After calling this, setFreq() and setCMRatio() will no longer update the depth cap.
   * The cap is set to 9999, effectively unlimited.
   */
  inline void disableAntiAlias() {
    _antiAliasDisabled = true;
    _cachedDepthMax = 9999.0f;
    _cachedDepthMaxScaled = (int32_t)(9999.0f * 2048.0f);
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

  /** Set the per-oscillator salt used by stateless noise indexing.
   * Equal seeds produce equal lookup sequences; different seeds decorrelate
   * oscillators that share the same noise wavetable.
   */
  inline void setNoiseSeed(uint32_t seed) {
    noiseSalt = noiseHash(seed ? seed : 0x9E3779B9u);
  }

  /** Set sample and hold mode.
  * When true, a random sample from the wavetable is selected once per period
  * and held for the duration, creating a sample-and-hold effect at the oscillator frequency.
  * @val Is true or false
  */
	inline
	void setSandH(bool val) {
		isSandH = val;
	}

  /** Get the current sample and hold value.
  * @return The held sample value
  */
	inline
	int16_t getSandHValue() {
		return sandHValue;
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

  /** Generate a band-limited pulse wave for the provided wavetable.
  * Each frequency band uses a progressively lower harmonic limit, matching
  * sqrGen(). The DC component is removed so duty changes do not shift the
  * output baseline.
  * @theTable The the wavetable to be filled
  * @duty The duty cycle, or pulse width, 0.0 - 1.0, 0.5 = sqr
  */
  static void pulseGen(int16_t * theTable, float duty) {
    duty = max(0.0f, min(1.0f, duty));
    if (duty <= 0.0f || duty >= 1.0f) {
      for (int i = 0; i < FULL_TABLE_SIZE; i++) theTable[i] = 0;
      return;
    }
    Osc::generatePulseWave(theTable, 0, 56, duty); // low
    Osc::generatePulseWave(theTable, 1, 28, duty); // mid
    Osc::generatePulseWave(theTable, 2, 12, duty); // high
  }

  /** Generate a pulse wave in this oscillator's internal wavetable.
   * @param duty Pulse width from 0.0 to 1.0
   */
  void pulseGen(float duty) {
    allocateWavetable();
    Osc::pulseGen(waveTable, duty);
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
    removeNoiseMean(theTable);
  }

  /** Generate white noise in the local wavetable */
  void noiseGen() {
    allocateWavetable();
    audioRandSeed(random(MAX_16));
    for(int i=0; i<FULL_TABLE_SIZE; i++) {
      waveTable [i] = audioRand(MAX_16 * 2) - MAX_16;
    }
    removeNoiseMean(waveTable);
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
    removeNoiseMean(theTable);
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
    removeNoiseMean(waveTable);
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
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
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
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
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
  /** Fast 32-bit avalanche hash for stateless noise lookup. */
  static inline uint32_t noiseHash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
  }

  inline uint32_t noiseTableIndex(uint32_t phase) const {
    return noiseHash(phase + noiseSalt) & (TABLE_SIZE - 1);
  }

  /** Remove DC independently from all three wavetable bands. */
  static void removeNoiseMean(int16_t* table) {
    if (table == nullptr) return;
    for (int segment = 0; segment < 3; segment++) {
      int offset = segment * TABLE_SIZE;
      int64_t sum = 0;
      for (int i = 0; i < TABLE_SIZE; i++) sum += table[offset + i];
      int32_t mean = (int32_t)(sum / TABLE_SIZE);
      for (int i = 0; i < TABLE_SIZE; i++) {
        table[offset + i] = (int16_t)clip16((int32_t)table[offset + i] - mean);
      }
    }
  }

  // Spinlock for paired modulator+carrier advance (dual-core only).
  // Used by phModInt(Osc& modOsc, ...) to prevent cross-core phase mismatches.
  #if IS_ESP32() || IS_RP2040()
  std::atomic<bool> _pairLock{false};
  // Seqlock for bandPtr+increment pair — ensures next() always reads a consistent
  // pair even when setFreq() is updating both from the other core.
  // Even value = stable; odd value = write in progress (reader must retry).
  std::atomic<uint32_t> _freqSeq{0};
  #endif

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
  int16_t * _pendingBandPtr = nullptr; // Deferred band change; applied at next zero crossing
  bool allocated = false;
  int32_t prevSampVal = 0;
  bool isNoise = false;
  uint32_t noiseSalt = 0x9E3779B9u;
  bool isCrackle = false;
  bool isSandH = false;
  int16_t sandHValue = 0;
  int crackleAmnt = MAX_16 * 0.5; //MAX_16 * 0.5;
  float frequency = 440;
  float prevFrequency = 440;
  float pulseWidth = 0.5;
  bool pulseWidthOn = false;
  int16_t prevParticle, particleEnv, particleThreshold = 0.993; //MAX_16 * 0.993;
  float particleEnvReleaseRate = 0.92; // thresh and rate = number of apparent particles
  uint32_t feedback_phase_fractional = 0; // 16.16 fixed-point
  float cachedModIndexF = 1.0f; // Cached mod index for single-arg phMod
  // Anti-aliasing depth cap: depth_max = 9000 / (freq * cmRatio).
  // _cmRatioSet=false → Osc& overloads auto-derive ratio from modulator freq.
  // _antiAliasDisabled=true → cap frozen at 9999; setFreq/setCMRatio won't touch it.
  float _cmRatio = 1.0f;
  bool _cmRatioSet = false;
  bool _antiAliasDisabled = false;
  volatile float _cachedDepthMax = 9000.0f / 440.0f;          // for float modIndex (phMod, phMod2, phModMorph, phModWTrans)
  volatile int32_t _cachedDepthMaxScaled = (int32_t)((9000.0f / 440.0f) * 2048.0f); // for phModInt's pre-scaled int32
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
    const float angularFreq = (2.0f * PI) / (float)TABLE_SIZE;
    float * tempTable = new float[TABLE_SIZE];

    for (int i=0; i<TABLE_SIZE; i++) {
      tempTable[i] = 0;
    }

    // Generate one complete harmonic at a time. Each harmonic needs only its
    // initial angle and phase step evaluated trigonometrically; all remaining
    // samples use the inexpensive oscillator recurrence in addSineHarmonic().
    if (waveType == 1) { // triangle
      for (int m=0; m<overtones; m+=2) {
        int harmonic = m + 1;
        float amplitude = 1.0f / (harmonic * harmonic);
        if (m % 4 == 2) amplitude *= -1.0f;
        float initialPhase = harmonic * PI * 0.5f;
        addSineHarmonic(tempTable, amplitude,
                        angularFreq * harmonic, initialPhase);
      }
    } else if (waveType == 2) { // square
      for (int m=0; m<overtones; m+=2) {
        int harmonic = m + 1;
        addSineHarmonic(tempTable, 1.0f / harmonic,
                        angularFreq * harmonic, 0.0f);
      }
    } else if (waveType == 3) { // sawtooth
      for (int m=0; m<overtones; m++) {
        int harmonic = m + 1;
        addSineHarmonic(tempTable, 1.0f / harmonic,
                        angularFreq * harmonic, 0.0f);
      }
    }

    float maxValue = tempTable[0];
    float minValue = tempTable[0];
    for (int i=1; i<TABLE_SIZE; i++) {
      if (tempTable[i] > maxValue) maxValue = tempTable[i];
      if (tempTable[i] < minValue) minValue = tempTable[i];
    }

    // normalise
    int segOffset = TABLE_SIZE * segment;
    float range = maxValue - minValue;
    if (range <= 0.000001f) {
      for (int i=0; i<TABLE_SIZE; i++) theTable[i + segOffset] = 0;
    } else {
      for (int i=0; i<TABLE_SIZE; i++) {
        float normalized = ((tempTable[i] - minValue) / range) * 2.0f - 1.0f;
        theTable[i + segOffset] = (int16_t)clip16(
            (int32_t)lroundf(normalized * MAX_16));
      }
    }
    delete[] tempTable;
  }

  /** Add one sine harmonic using a complex-rotation recurrence.
   * Renormalising the phasor periodically bounds floating-point amplitude drift
   * without returning to a trigonometric call for every output sample.
   */
  static void addSineHarmonic(float* table, float amplitude,
                              float phaseStep, float initialPhase) {
    float sine = sinf(initialPhase);
    float cosine = cosf(initialPhase);
    float stepSine = sinf(phaseStep);
    float stepCosine = cosf(phaseStep);

    for (int i=0; i<TABLE_SIZE; i++) {
      table[i] += amplitude * sine;

      float nextSine = sine * stepCosine + cosine * stepSine;
      float nextCosine = cosine * stepCosine - sine * stepSine;
      sine = nextSine;
      cosine = nextCosine;

      if ((i & 255) == 255) {
        float magnitudeSquared = sine * sine + cosine * cosine;
        if (magnitudeSquared > 0.0f) {
          float inverseMagnitude = 1.0f / sqrtf(magnitudeSquared);
          sine *= inverseMagnitude;
          cosine *= inverseMagnitude;
        }
      }
    }
  }

  /** Generate one anti-aliased pulse-wave band by additive synthesis.
   *
   * A non-50% pulse contains both odd and even harmonics. The phase-shifted
   * cosine form places the positive portion at the start of the table, matching
   * the former hard-edged generator. The theoretical DC term (2*duty-1) is
   * deliberately omitted, then residual numerical mean is removed before
   * peak normalization.
   */
  static void generatePulseWave(int16_t* theTable, int segment,
                                int harmonicLimit, float duty) {
    float* tempTable = new float[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; i++) tempTable[i] = 0.0f;

    const float angularFreq = (2.0f * PI) / (float)TABLE_SIZE;
    for (int harmonic = 1; harmonic <= harmonicLimit; harmonic++) {
      float coefficient = sinf(PI * harmonic * duty) / harmonic;
      // cos(x) = sin(x + PI/2), allowing reuse of the sine recurrence.
      float initialPhase = PI * 0.5f - harmonic * PI * duty;
      addSineHarmonic(tempTable, coefficient,
                      angularFreq * harmonic, initialPhase);
    }

    double sum = 0.0;
    for (int i = 0; i < TABLE_SIZE; i++) sum += tempTable[i];
    float mean = (float)(sum / TABLE_SIZE);
    float peak = 0.0f;
    for (int i = 0; i < TABLE_SIZE; i++) {
      tempTable[i] -= mean;
      peak = max(peak, fabsf(tempTable[i]));
    }

    int offset = segment * TABLE_SIZE;
    if (peak <= 0.000001f) {
      for (int i = 0; i < TABLE_SIZE; i++) theTable[offset + i] = 0;
    } else {
      float scale = MAX_16 / peak;
      for (int i = 0; i < TABLE_SIZE; i++) {
        theTable[offset + i] = (int16_t)clip16(
            (int32_t)lroundf(tempTable[i] * scale));
      }
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

      // Noise uses stateless hashed lookup, so it can use the normal cheap
      // phase wrap. Only crackle retains randomized wrap behavior.
      if (!isCrackle) {
          phase_fractional &= TABLE_SIZE_FP_MASK; // Fast wrap: equivalent to modulo
          return;
      }

      // Noise / crackle modes (slower path, less common)
      if (phase_fractional >= TABLE_SIZE_FP_CONST) {
          if (audioRand(0x8000) > crackleAmnt) {
              phase_fractional = 1 << 16;
          } else {
              phase_fractional = audioRand(TABLE_SIZE) << 16;
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

inline void WaveTable::cosGen() {
  allocate();
  Osc::cosGen(_samples);
}

inline void WaveTable::sinGen() {
  allocate();
  Osc::sinGen(_samples);
}

inline void WaveTable::triGen() {
  allocate();
  Osc::triGen(_samples);
}

inline void WaveTable::pulseGen(float duty) {
  allocate();
  Osc::pulseGen(_samples, duty);
}

inline void WaveTable::sqrGen() {
  allocate();
  Osc::sqrGen(_samples);
}

inline void WaveTable::sawGen() {
  allocate();
  Osc::sawGen(_samples);
}

inline void WaveTable::noiseGen() {
  allocate();
  Osc::noiseGen(_samples);
}

inline void WaveTable::noiseGen(int grainSize) {
  allocate();
  Osc::noiseGen(_samples, grainSize);
}

inline void WaveTable::crackleGen() {
  allocate();
  Osc::crackleGen(_samples);
}

inline void WaveTable::brownNoiseGen() {
  allocate();
  Osc::brownNoiseGen(_samples);
}

inline void WaveTable::pinkNoiseGen() {
  allocate();
  Osc::pinkNoiseGen(_samples);
}

#endif /* OSC_H_ */
