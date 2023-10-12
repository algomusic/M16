/*
 * Osc.h
 *
 * A wavetable oscillator class. Contains generators for common wavetables.
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
  /** Constructor.
	* Has no table specified - make sure to use setTable() after initialising
	*/
  Osc() {}

	/** Constructor.
	* @param TABLE_NAME the name of the array the Osc will be using.
  * Table is a int16_t array of TABLE_SIZE - values rabge from -16383 to 16383 (which seems like 15, not 16 bits???)
  * Use sinGen() or similar function in M16.h to fill the table in the setup() function before using
	*/
	Osc(const int16_t * TABLE_NAME):table(TABLE_NAME) {}

  /** Updates the phase according to the current frequency and returns the sample at the new phase position.
	* @return outSamp The next sample.
	*/
	inline
	int16_t next() {
    int32_t sampVal = readTable();
    sampVal = (sampVal + prevSampVal)>>1; // smooth
    incrementPhase();
    if (spread1 != 1) {
      sampVal = doSpread(sampVal);
    }
    prevSampVal = sampVal;
    return sampVal;
	}

  /** Returns the sample at for the oscillator phase at specified time in milliseconds.
  * Used for LFOs. Assumes the Osc started at time = 0;
	* @return outSamp The sample value at the calculated phase position - range MIN_16 to MAX_16.
	*/
	inline
	int16_t atTime(unsigned long ms) {
    unsigned long indexAtTime = ms * cycleLengthPerMS * TABLE_SIZE;
    int index = indexAtTime % TABLE_SIZE;
    int16_t outSamp = readTableIndex(index);
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
	void setTable(const int16_t * TABLE_NAME) {
		table = TABLE_NAME;
	}

	/** Set the phase of the Oscil. Phase ranges from 0.0 - 1.0 */
	inline
  void setPhase(float phase) {
		phase_fractional = phase;
    phase_fractional_s1 = phase;
    phase_fractional_s2 = phase;
	}

	/** Get the phase of the Oscil in fractional format. */
	inline
  float getPhase() {
		return phase_fractional;
	}

  /** Set the spread value of the Oscil.
  * @newVal A multiplyer of the base freq, from 0 to 1.0, values near zero are best for phasing effects
  */
	inline
  void setSpread(float newVal) {
    spread1 = 1.0f + newVal;
    spread2 = 1.0f - newVal * 0.5;
    setFreq(getFreq());
	}

  /** Set the spread value of the Oscil. Ranges > 0 */
	inline
  void setSpread(int val1, int val2) {
		spread1 = intervalRatios[val1 + 12]; //intervalFreq(frequency, val1);
    spread2 = intervalRatios[val2 + 12]; //intervalFreq(frequency, val2);
	}

  /** Get a blend of this Osc and another.
  * @param secondWaveTable - an wavetable array to morph with
  * @param morphAmount - The balance (mix) of the second wavetable, 0.0 - 1.0
  */
	inline
  int16_t nextMorph(int16_t * secondWaveTable, float morphAmount) {
    int intMorphAmount = max(0, min (1024, (int)(1024 * morphAmount)));
    int32_t sampVal = readTable();
    int32_t sampVal2 = secondWaveTable[(int)phase_fractional];
    if (morphAmount > 0) sampVal = (((sampVal2 * intMorphAmount) >> 10) +
      ((sampVal * (1024 - intMorphAmount)) >> 10));
    sampVal = (sampVal + prevSampVal)>>1; // smooth
    prevSampVal = sampVal;
    incrementPhase();
    if (spread1 != 1) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
	}

  /** Get a window transform between this Osc and another wavetable.
  * Inspired by the Window Transform Function by Dove Audio
  * @param secondWaveTable - an wavetable array to transform with
  * @param windowSize - The amount (mix) of the second wavetable to let through, 0.0 - 1.0
  * @param duel - Use a duel window that can increase harmonicity
  * @param duel - Invert the second wavefrom that can increase harmonicity
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
    if (duel) {
      if (phase_fractional < (quarterTable - portion14) || (phase_fractional > (quarterTable + portion14) &&
          phase_fractional < (threeQuarterTable - portion14)) || phase_fractional > (threeQuarterTable + portion14)) {
        sampVal = readTable();
        if (spread1 != 1) {
          sampVal = doSpread(sampVal);
        }
      } else {
        sampVal = secondWaveTable[(int)phase_fractional];
        if (invert) sampVal *= -1;
        if (spread1 != 1) {
          int32_t spreadSamp1 = secondWaveTable[(int)phase_fractional];
          sampVal = (sampVal + spreadSamp1)>>1;
          int32_t spreadSamp2 = secondWaveTable[(int)phase_fractional];
          sampVal = (sampVal + spreadSamp2)>>1;
          incrementSpreadPhase();
        }
      }
    } else {
      if (phase_fractional < (halfTable - portion12) || phase_fractional > (halfTable + portion12)) {
        sampVal = readTable();
        if (spread1 != 1) {
          sampVal = doSpread(sampVal);
        }
      } else {
        sampVal = secondWaveTable[(int)phase_fractional];
        if (invert) sampVal *= -1;
        if (spread1 != 1) {
          int32_t spreadSamp1 = secondWaveTable[(int)phase_fractional];
          sampVal = (sampVal + spreadSamp1)>>1;
          int32_t spreadSamp2 = secondWaveTable[(int)phase_fractional];
          sampVal = (sampVal + spreadSamp2)>>1;
          incrementSpreadPhase();
        }
      }
    }
    sampVal = (sampVal + prevSampVal)>>1; // smooth
    prevSampVal = sampVal;
    incrementPhase();
    return sampVal;
  }

  /** Phase Modulation (FM)
   * Pass in a second oscillator and multiply its value to change mod depth
   * @param modulator - The next sample from the modulating waveform
   * @param modIndex - The depth value to amplify the modulator by, from 0.0 to 1.0
   * ModIndex values between 0.0 - 1.0 are normally enough, higher values are possible
   * In Phase Mod, typically values 1/10th of FM ModIndex values provide equvalent change.
   */
  inline
  int16_t phMod(int32_t modulator, float modIndex) {
    modulator *= modIndex;
    int32_t sampVal = table[(int16_t)(phase_fractional + (modulator >> 4)) & (TABLE_SIZE - 1)];
  	incrementPhase();
    if (spread1 != 1) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** Ring Modulation
  *  Pass in a second oscillator and multiply its value to change mod depth
  *  Multiplying incomming oscillator amplitude between 0.5 - 2.0 is best.
  */
  inline
  int16_t ringMod(int16_t audioIn) {
    incrementPhase();
    int32_t currSamp = readTable();
    int16_t sampVal = (currSamp * audioIn)>>15;
    if (spread1 != 1) {
      sampVal = doSpread(sampVal);
    }
    return sampVal;
  }

  /** PhISM Shaker model
   * Designed for Osc being set to a noise wavetable.
   * @param thresh The amount of aparent particles. Typically 0.9 - 0.999
   * Envelope output and pass to one or more band pass filters or other resonator.
   */
  inline
  int16_t particle(float thresh) {
    int32_t noiseVal = readTable();
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
  int16_t feedback(int modIndex) {
  	int16_t y = table[(int)feedback_phase_fractional] >> 3;
  	int16_t s = readTableIndex(y);
  	int f = ((int32_t)modIndex * (int32_t)s) >> 16;
		phase_fractional += f + phase_increment_fractional;
		if (phase_fractional > TABLE_SIZE) {
			phase_fractional -= TABLE_SIZE;
		} else if (phase_fractional < 0) {
			phase_fractional += TABLE_SIZE;
		}
		feedback_phase_fractional += phase_increment_fractional;
		if (feedback_phase_fractional > TABLE_SIZE) {
			feedback_phase_fractional -= TABLE_SIZE;
		}
    int16_t out = table[(int16_t)phase_fractional & (TABLE_SIZE - 1)];
  	return out;
  }

  /** Glide toward the frequency of the oscillator in Hz.
  * @freq The desired final value
  * @amnt The percentage toward target (0.0 - 1.0)
  */
  inline
	void slewFreq(float freq, float amnt) {
    if (amnt == 0) {
      setFreq(freq);
		} else if (freq > 0 && amnt > 0 && amnt <= 1) {
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
		  phase_increment_fractional = freq / 440.0f * (float)TABLE_SIZE / (SAMPLE_RATE / 440.0f);
      if (pulseWidthOn) {
        phase_increment_fractional_w1 = phase_increment_fractional * 0.5 / pulseWidth;
        phase_increment_fractional_w2 = phase_increment_fractional * 0.5 / (1.0 - pulseWidth);
      }
      if (spread1 != 1) {
        phase_increment_fractional_s1 = phase_increment_fractional * spread1;
        phase_increment_fractional_s2 = phase_increment_fractional * spread2;
      } else {
        phase_increment_fractional_s1 = phase_increment_fractional;
        phase_increment_fractional_s2 = phase_increment_fractional;
      }
      cycleLengthPerMS = frequency * 0.001; /// 1000.0f;
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
		setFreq(mtof(min(127.0f, max(0.0f, midi_pitch * (1 + (rand(6) * 0.00001f))))));
    prevFrequency = frequency;
	}

  /** Return the pitch as a MIDI pitch 
   * @midiPitch The pitch, value 0 - 127
  */
	inline
	float getPitch(float midi_pitch) {
    return midiPitch;
  }

	/** Set a specific phase increment.
  * @phaseinc_fractional, value between 0.0 to 1.0
  */
	inline
	void setPhaseInc(float phaseinc_fractional) {
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
		isCrackle = val;
	}

  /** Set using crackle waveform flag.
  * @val Is true or false
  * @amnt Spareness of impulse in samples, from 1 to MAX_16
  */
	inline
	void setCrackle(bool val, int amnt) {
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
    float pwInv = 1.0f / pulseWidth;
    float halfPhaseInc = phase_increment_fractional * 0.5f;
    phase_increment_fractional_w1 = halfPhaseInc * pwInv;
    phase_increment_fractional_w2 = halfPhaseInc / (1.0f - pulseWidth);
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

  /** Generate a cosine wave
  * @theTable The the wavetable to be filled
  */
  static void cosGen(int16_t * theTable) {
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = (cos(2 * 3.1459 * i * TABLE_SIZE_INV) * MAX_16); //32767, 16383
    }
  }

  /** Generate a sine wave
  * @theTable The the wavetable to be filled
  */
  static void sinGen(int16_t * theTable) {
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = (sin(2 * 3.1459 * i * TABLE_SIZE_INV) * MAX_16); //32767, 16383
    }
  }

  /** Generate a triangle wave
  * @theTable The the wavetable to be filled
  */
  static void triGen(int16_t * theTable) {
    for (int i=0; i<TABLE_SIZE; i++) {
      if (i < HALF_TABLE_SIZE) {
        theTable[i] = MAX_16 - i * (MAX_16 * 2.0f * TABLE_SIZE_INV * 2.0f);
      } else theTable[i] = MIN_16 + (i - (float)HALF_TABLE_SIZE) * (MAX_16 * 2.0f * TABLE_SIZE_INV * 2.0f);
    }
  }

  /** Generate a square/pulse wave
  * @theTable The the wavetable to be filled
  * @duty The duty cycle, or pulse width, 0.0 - 1.0, 0.5 = sqr
  */
  static void pulseGen(int16_t * theTable, float duty) {
    for(int i=0; i<TABLE_SIZE; i++) {
      if (i < TABLE_SIZE * duty) {
        theTable[i] = MAX_16;
      } else theTable[i] = MIN_16;
    }
  }

  /** Generate a square wave
  * @theTable The the wavetable to be filled
  */
  static void sqrGen(int16_t * theTable) {
    pulseGen(theTable, 0.5);
  }

  /** Generate a sawtooth wave
  * @theTable The the wavetable to be filled
  */
  static void sawGen(int16_t * theTable) {
    for (int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = (MAX_16 - i * (MAX_16 * 2 * TABLE_SIZE_INV));
    }
  }

  /** Generate white noise
  * @theTable The the wavetable to be filled
  */
  static void noiseGen(int16_t * theTable) {
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = rand(MAX_16 * 2) - MAX_16;
    }
  }

  /** Generate grainly white noise, like a sample and hold wave
  * @theTable The the wavetable to be filled
  * @grainSize The number of samples each random value is held for
  */
  static void noiseGen(int16_t * theTable, int grainSize) {
    int grainCnt = 0;
    int randVal = rand(MAX_16 * 2) - MAX_16;
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = randVal;
      grainCnt++;
      if (grainCnt % grainSize == 0) randVal = rand(MAX_16 * 2) - MAX_16;
    }
  }

  /** Generate crackle noise
  * @theTable The the wavetable to be filled
  */
  static void crackleGen(int16_t * theTable) {
    // theTable[0] = MAX_16;
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = 0;
    }
    for(int i=0; i<2; i++) {
      theTable[(int)rand(TABLE_SIZE)] = MAX_16;
      theTable[(int)rand(TABLE_SIZE)] = MIN_16;
    }
  }

  /** Generate Browian noise
  * @theTable The the wavetable to be filled
  */
  static void brownNoiseGen(int16_t * theTable) {
    int val = 0;
    int deviation = MAX_16>>1;
    int halfDev = deviation>>1;
    for(int i=0; i<TABLE_SIZE; i++) {
      val += gaussRand(deviation) - halfDev;
      if (val > MAX_16) val = val - MAX_16;
      if (val < MIN_16) val = MIN_16 + abs(val) - MAX_16;
      theTable[i] = max(MIN_16, min(MAX_16, val));
    }
  }

  /** Generate pink noise
  * @theTable The the wavetable to be filled
  * Using Paul Kellet's refined method
  */
  static void pinkNoiseGen(int16_t * theTable) {
    float b0, b1, b2, b3, b4, b5, b6;
    for (int i=0; i<TABLE_SIZE; i++) {
      float white = (rand(5000) - 2500) * 0.001; // 20000, 10000
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

private:
  float phase_fractional = 0.0;
  float spread1 = 1.0;
  float spread2 = 1.0;
  float phase_fractional_s1 = 0.0;
  float phase_fractional_s2 = 0.0;
	float phase_increment_fractional = 18.75;
  float phase_increment_fractional_s1 = 18.75;
  float phase_increment_fractional_s2 = 18.75;
  float phase_increment_fractional_w1 = phase_increment_fractional;
  float phase_increment_fractional_w2 = phase_increment_fractional;
	const int16_t * table;
  int32_t prevSampVal = 0;
  bool isNoise = false;
  bool isCrackle = false;
  int crackleAmnt = MAX_16 * 0.5; //0; //MAX_16 * 0.5;
  float frequency = 440;
  float prevFrequency = 440;
  float pulseWidth = 0.5;
  bool pulseWidthOn = false;
  int16_t prevParticle, particleEnv, particleThreshold = 0.993; //MAX_16 * 0.993;
  float particleEnvReleaseRate = 0.92; // thresh and rate = number of apparent particles
  float feedback_phase_fractional = 0;
  float testVal = 1.3;
  float cycleLengthPerMS = frequency * 0.001f; // / 1000.0f;
  float midiPitch = 69;

  /** Increments the phase of the oscillator without returning a sample.*/
	inline
	void incrementPhase() {
    if (pulseWidthOn) {
      if (phase_fractional < HALF_TABLE_SIZE) {
        phase_fractional += phase_increment_fractional_w1;
      } else phase_fractional += phase_increment_fractional_w2;
    } else phase_fractional += phase_increment_fractional;
		if (phase_fractional > TABLE_SIZE) {
		  if (isNoise) {
        phase_fractional = rand(TABLE_SIZE);
		  } else if (isCrackle) {
        if (rand(MAX_16) > crackleAmnt) {
          phase_fractional = 1; //rand(TABLE_SIZE);
        } else phase_fractional = rand(TABLE_SIZE); //= 1;
      } else {
		    phase_fractional -= TABLE_SIZE;
        // randomness destabilises pitch at the expense of some CPU load
        phase_increment_fractional *= (1 + (rand(9) - 4) * 0.000001);
        if (pulseWidthOn) {
          phase_increment_fractional_w1 = phase_increment_fractional * 0.5 / pulseWidth;
          phase_increment_fractional_w2 = phase_increment_fractional * 0.5 / (1.0 - pulseWidth);
        }
		  }
		}
	}

  /** Increments the phase of spread reads of the oscillator
  * without returning a sample.*/
	inline
	void incrementSpreadPhase() {
		phase_fractional_s1 += phase_increment_fractional_s1;
    if (phase_fractional_s1 > TABLE_SIZE) phase_fractional_s1 -= TABLE_SIZE;
    phase_fractional_s2 += phase_increment_fractional_s2;
    if (phase_fractional_s2 > TABLE_SIZE) phase_fractional_s2 -= TABLE_SIZE;
  }

	/** Returns the current sample. */
	inline
	int16_t readTable() {
    return table[(int)(phase_fractional)];
	}

	/** Returns a particular sample. */
	inline
	int16_t readTableIndex(int ind) {
		return table[ind];
	}

  /** Returns a spread sample. */
	inline
	int16_t doSpread(int16_t sampVal) {
    int32_t spreadSamp1 = table[(int)phase_fractional_s1];
    sampVal = (sampVal + spreadSamp1)>>1;
    int32_t spreadSamp2 = table[(int)phase_fractional_s2];
    sampVal = (sampVal + spreadSamp2)>>1;
    incrementSpreadPhase();
    return sampVal;
	}
};


#endif /* OSC_H_ */
