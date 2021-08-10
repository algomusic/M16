/*
 * Oscil.h
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
    int16_t sampVal = readTable();
    int16_t outSamp = ((int32_t)sampVal + (int32_t)prevSampVal)/2; // smooth
    prevSampVal = outSamp;
    incrementPhase();
		return outSamp; // or return sampVal for no smoothing
	}

  /** Returns the sample at for the oscillator phase at specified time in milliseconds.
  * Used for LFOs. Assumes the Osc started at time = 0;
	* @return outSamp The sample value at the calulated phase position.
	*/
	inline
	int16_t atTime(unsigned long ms) {
    // float cycleLengthPerMS = frequency / 1000.0f;
    unsigned long indexAtTime = ms * cycleLengthPerMS * TABLE_SIZE;
    int index = indexAtTime % TABLE_SIZE;
    int16_t outSamp = readTableIndex(index);
    return outSamp;
  }

	/** Change the sound table which will be played by the Oscil.
	@param TABLE_NAME is the name of the array. Must be the same size as the original table used when instantiated.
	*/
  inline
	void setTable(const int16_t * TABLE_NAME) {
		table = TABLE_NAME;
	}

	/** Set the phase of the Oscil. phase ranges from 0 - TABLE_SIZE */
	inline
  void setPhase(float phase) {
		phase_fractional = phase;
	}

	/** Get the phase of the Oscil in fractional format. */
	inline
  float getPhase() {
		return phase_fractional;
	}

  /** Phase Modulation (FM)
   *  Pass in a second oscillator and multiply its value to change mod depth
   *  Multiply incomming oscillator Depth between 0.0 - 1.0 (plus if required).
   */
  inline
  int16_t phMod(int32_t phmod_proportion) {
  	incrementPhase();
    // >> 12 is just for M32 input, remove for M16 input - To DO - sort this out.
    return table[(int16_t)(phase_fractional + (phmod_proportion >> 12)) & (TABLE_SIZE - 1)];
  }

  /** Ring Modulation
  *  Pass in a second oscillator and multiply its value to change mod depth
  *  Multiplying incomming oscillator Depth between 0.5 - 2.0 is best.
  */
  inline
  int16_t ringMod(int32_t audioIn) {
    incrementPhase();
    int16_t currSamp = readTable();
    return (currSamp * audioIn)>>16;
  }

  /** PhISM Shaker model
   *  Designed for Osc being set to a noise wavetable.
   * Uses some private hard coded params.
   * Envelope output and pass to one or more band pass filters or other resonator.
   */
  inline
  int16_t particle() {
    int32_t noiseVal = readTable();
  	if (noiseVal > particleThreshold) {
  		particleEnv = noiseVal - (MAX_16 - noiseVal) - (MAX_16 - noiseVal);
  	} else particleEnv *= particleEnvReleaseRate;
  	incrementPhase();
  	noiseVal = (prevParticle + noiseVal + noiseVal)/3;
    return (noiseVal * particleEnv) >> 16;
  }

  /** Frequency Modulation Feedback
   * Designed for Osc being set to a sine waveform, but works with any.
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

	/** Set the frequency of the oscillator in Hz. */
	inline
	void setFreq(float freq) {
		if (freq > 0) {
      frequency = freq;
		  phase_increment_fractional = freq / 440.0 * (float)TABLE_SIZE / 109.25;
      cycleLengthPerMS = frequency / 1000.0f;
    }
	}

	/** Return the frequency of the oscillator in Hz. */
	inline
	float getFreq() {
		return frequency;
	}

	/** Set the frequency via a MIDI pitch value 0 - 127 */
	inline
	void setPitch(float midi_pitch) {
		setFreq(mtof(min(127.0f, max(0.0f,midi_pitch))));
	}

	/** Set a specific phase increment. */
	inline
	void setPhaseInc(float phaseinc_fractional) {
		phase_increment_fractional = phaseinc_fractional;
	}

	/** Set using noise waveform flag . */
	inline
	void setNoise(bool val) {
		isNoise = val;
	}

 /** Below are helper methods for generating waveforms into existing arrays.
 * Call from class not instance. e.g. Osc::triGen(myWaveTableArray);
 * While it might be simpler to have each instance have its own wavetable,
 * it's more memory effcient for wavetables to be shared. So create them in the
 * main program file and reference them from instances of this class.
 */

  /** Generate a cosine wave */
  static void cosGen(int16_t * theTable) {
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = (cos(2 * 3.1459 * i / TABLE_SIZE) * MAX_16); //32767, 16383
    }
  }

  static void sinGen(int16_t * theTable) {
    cosGen(theTable);
  //  for(int i=0; i<TABLE_SIZE; i++) {
  //    theTable[i] = max(0, min(65534, (int)(cos(2 * 3.1459 * i / TABLE_SIZE) * 32767 + 32767))) >> 1;
  //  }
  }

  /** Generate a triangle wave */
  static void triGen(int16_t * theTable) {
    int16_t waveData [TABLE_SIZE];
    for (int i=0; i<TABLE_SIZE; i++) {
      if (i < TABLE_SIZE / 2) {
        theTable[i] = MAX_16 - i * (MAX_16 * 2.0f / TABLE_SIZE * 2.0f);
      } else theTable[i] = MIN_16 + (i - TABLE_SIZE / 2.0f) * (MAX_16 * 2.0f / TABLE_SIZE * 2.0f);
    }
  }

  /** Generate a square/pulse wave */
  static void pulseGen(int16_t * theTable, float duty) { // 0.0 - 1.0, 0.5 = sqr
    int16_t waveData [TABLE_SIZE];
    for(int i=0; i<TABLE_SIZE; i++) {
      if (i < TABLE_SIZE * duty) {
        theTable[i] = MAX_16;
      } else theTable[i] = -MAX_16;
    }
  }

  static void sqrGen(int16_t * theTable) {
    pulseGen(theTable, 0.5);
  }

  /** Generate a sawtooth wave */
  static void sawGen(int16_t * theTable) {
    int waveData [TABLE_SIZE];
    for (int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = (MAX_16 - i * (MAX_16 * 2 / TABLE_SIZE));
    }
  }

  /** Generate white noise */
  static void noiseGen(int16_t * theTable) {
    int16_t waveData [TABLE_SIZE];
    for(int i=0; i<TABLE_SIZE; i++) {
      theTable[i] = random(MAX_16 * 2) - MAX_16;
    }
  }

private:
  float phase_fractional = 0.0;
	float phase_increment_fractional = 18.75;
	const int16_t * table;
  int16_t prevSampVal = 0;
  bool isNoise = false;
  float frequency = 440;;
  int16_t prevParticle, particleEnv, particleThreshold = MAX_16 * 0.993;
  float particleEnvReleaseRate = 0.92; // thresh and rate = number of apparent particles
  float feedback_phase_fractional = 0;
  float testVal = 1.3;
  float cycleLengthPerMS = frequency / 1000.0f;

  /** Increments the phase of the oscillator without returning a sample.*/
	inline
	void incrementPhase() {
		phase_fractional += phase_increment_fractional;
		if (phase_fractional > TABLE_SIZE) {
		  if (isNoise) {
        phase_fractional = random(TABLE_SIZE);
		  } else {
		    phase_fractional -= TABLE_SIZE;
        // randomness destabilises pitch at the expense of some CPU load
        phase_increment_fractional *= (1 + (rand(11) - 5) * 0.000001);
		  }
		}
	}

	/** Returns the current sample. */
	inline
	int16_t readTable() {
		return table[(int)phase_fractional];
	}

	/** Returns a particular sample. */
	inline
	int16_t readTableIndex(int ind) {
		return table[ind];
	}

};


#endif /* OSC_H_ */
