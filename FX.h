/*
 * FX.h
 *
 * A class of various DSP effects
 *
 * by Andrew R. Brown 2021
 *
 * This file is part of the M16 audio library 
 * Inspired by the Mozzi audio library by Tim Barrass 2012
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef FX_H_
#define FX_H_

#include "Del.h"
#include "Osc.h"
#include "All.h"
#include "SVF.h"
#include "EMA.h"

class FX {

  public:
    /** Constructor. */
    FX() {}

    /** Wave Folding
    *  Fold clipped values
    *  Pass in a signal and an amount to multiply its value by
    * @param sample_in The next sample value
    * @param amount The degree of amplifcation which is then folded; 1.0 +
    */
    inline
    int16_t waveFold(int32_t sample_in, float amount) {
      if (amount <= 1) return sample_in;
      sample_in *= amount;
      while(abs(sample_in) > MAX_16) {
        if (sample_in > 0) sample_in = MAX_16 - (sample_in + MIN_16);
        if (sample_in <= 0) sample_in = MIN_16 - (sample_in + MAX_16);
      }
      return clip16(sample_in);
    }

    // clip16() in M16.h does hard clipping

    /* Soft Clipping default
    * Distort sound based on input level and depth amount
    * Pass in a signal and depth amount.
    * Output level can vary, so best to mix with original signal.
    * @param sample_in The next sample value
    * @param amount The degree of clipping to be applied - 1 to 400 is a reasonable range
    * From amount of 1.0 (neutral) upward to about 25.0
    */
    inline
    int16_t softClip(int32_t sample_in, float amount) {
      return softClipTube(sample_in, amount);
    }

    /** Soft Clipping (atan)
    * Distort sound based on input level and depth amount
    * Pass in a signal and depth amount.
    * Output level can vary, so best to mix with original signal.
    * @param sample_in The next sample value
    * @param amount The degree of clipping to be applied - 1 to 400 is a reasonable range
    * From amount of 1.0 (neutral) upward to about 2.0 for some compression to
    * about 3.0 for mild drive, to about 4 - 6 for noticiable overdrive, to 7 - 10 for distortion
    * Note: Uses atan() which is warm but can sound dull. See alternatives below.
    */
    inline
    int16_t softClipAtan(int32_t sample_in, float amount) {
      int32_t samp = 38000 * atan(amount * (sample_in * (float)MAX_16_INV)); // 20831
      return clip16(samp);
    }

    /** Cubic Soft Clipping
    * Faster and brighter than atan soft clip.
    * Uses polynomial x - x³/6.75 which has a sharper knee, preserving more harmonics.
    * @param sample_in The next sample value
    * @param amount The degree of clipping, 1.0 (neutral) to ~10.0 (heavy distortion)
    */
    inline
    int16_t softClipCubic(int32_t sample_in, float amount) {
      float x = amount * sample_in * (float)MAX_16_INV;
      float out;
      if (x > 1.5f) out = 1.0f;
      else if (x < -1.5f) out = -1.0f;
      else out = x - (x * x * x) * 0.148148f; // x - x³/6.75
      return clip16((int32_t)(out * MAX_16));
    }

    /** Fast Tanh Soft Clipping
    * Balanced tone between warm and bright using Pade approximant.
    * ~5x faster than real tanh, smooth curve with good harmonic balance.
    * @param sample_in The next sample value
    * @param amount The degree of clipping, 1.0 (neutral) to ~10.0 (heavy distortion)
    */
    inline
    int16_t softClipTanh(int32_t sample_in, float amount) {
      float x = amount * sample_in * (float)MAX_16_INV;
      float x2 = x * x;
      float out = x * (27.0f + x2) / (27.0f + 9.0f * x2);
      return clip16((int32_t)(out * MAX_16));
    }

    /** Tube-Style Asymmetric Saturation
    * Emulates tube amplifier characteristics with asymmetric clipping.
    * Adds even harmonics for warmth with presence, not dull.
    * @param sample_in The next sample value
    * @param amount The degree of saturation, 1.0 (mild) to ~10.0 (heavy)
    */
    inline
    int16_t softClipTube(int32_t sample_in, float amount) {
      float x = amount * sample_in * (float)MAX_16_INV;
      float out;
      if (x >= 0) {
        // Fast exp approximation: e^x ≈ (1 + x/8)^8 for small x, else clamp
        float ex = (x > 4.0f) ? 0.0f : fastExpNeg(x);
        out = 1.0f - ex;
      } else {
        float ex = (x < -4.0f) ? 0.0f : fastExpNeg(-x);
        out = ex - 1.0f;
      }
      return clip16((int32_t)(out * MAX_16));
    }

    /** Integer-only Soft Clipping (no floats)
    * Warm approximation of tube saturation using only integer math.
    * Uses rational function: out = x / (1 + |x|/threshold)
    * Optimized to avoid int64 division for better ESP32 performance.
    * @param sample_in The next sample value
    * @param amount The drive amount as integer, 1024 = unity, 2048 = 2x drive, etc.
    *               Useful range: 1024 (mild) to 10240 (heavy distortion)
    */
    inline
    int16_t softClipInt(int32_t sample_in, int32_t amount) {
      // Scale input by amount (amount is 10-bit fixed point, 1024 = 1.0)
      int32_t x = (sample_in * amount) >> 10;

      // Clamp input to prevent overflow
      if (x > 98304) x = 98304;         // 3x MAX_16
      else if (x < -98304) x = -98304;

      // Rational soft clip: out = x * threshold / (threshold + |x|)
      // Use threshold = 32768, scaled math to stay in 32-bit
      int32_t absX = (x >= 0) ? x : -x;

      // Scale down, compute, scale back up to avoid overflow
      // out = x * 32768 / (32768 + absX)
      // Rewrite as: out = x / (1 + absX/32768) = x / (1 + absX>>15)
      int32_t denom = 32768 + (absX >> 1);  // Approximate: threshold + absX/2
      int32_t out = (x << 14) / (denom >> 1);  // Scaled 32-bit division

      // Final clamp to valid 16-bit range
      if (out > MAX_16) out = MAX_16;
      else if (out < MIN_16) out = MIN_16;

      return (int16_t)out;
    }

    /** Foldback Soft Clipping
    * Folds the waveform back instead of clipping, creating bright harmonics.
    * More aggressive/synth-like character than traditional soft clip.
    * @param sample_in The next sample value
    * @param amount The degree of folding, 1.0 (neutral) to ~4.0 (heavy fold)
    */
    inline
    int16_t softClipFold(int32_t sample_in, float amount) {
      float x = amount * sample_in * (float)MAX_16_INV;
      // Single fold at ±1 threshold
      if (x > 1.0f) x = 2.0f - x;
      else if (x < -1.0f) x = -2.0f - x;
      // Clamp result in case of extreme values
      if (x > 1.0f) x = 1.0f;
      else if (x < -1.0f) x = -1.0f;
      return (int16_t)(x * MAX_16);
    }

    /** Overdrive
    * Distort sound based on input level and depth amount
    * Pass in a signal and depth amount.
    * Output level can vary, so best to mix with original signal.
    * @param sample_in The next sample value
    * @param amount The degree of clipping to be applied, from 1 to 4 is a reasonable range
    * Amounts less than 1.0 (neutral) will reduce the signal
    */
    inline
    int16_t overdrive(int32_t sample_in, float amount) {
      // filter input
      aveFilter.setFreq(10000); // htz approx 1/4 of sample rate
      sample_in = aveFilter.next(sample_in);
      amount *= 0.72; // scale so 1.0 is neutral
      // clipper
      sample_in = sample_in * amount;
      float sample = sample_in * MAX_16_INV * amount;
      float absSampIn = abs(sample);
      float clippedSampIn = (sample > 0) ? 1.0f : -1.0f;
      float clippedSampIn16 = (sample_in > 0) ? MAX_16 : MIN_16;
      float clipOut = 0;
      float thresh = 0.33; //10922; //MAX_16 * 0.33;
      
      if (absSampIn < thresh) {
        clipOut = 2.0f * sample;
      } else if (absSampIn < 2 * thresh) {
        clipOut = clippedSampIn * (3.0f - (2.0f - 3.0f * absSampIn) * (2.0f - 3.0f * absSampIn)) / 3.0f;
      } else {
        clipOut = clippedSampIn;
      }
      return clip16(clipOut * MAX_16);
    }

    /** Compressor with gain compensation
    *  @param sample is the input sample value
    *  @param threshold is the threshold value between 0.0 and 1.0
    *  @param ratio is the ratio value, > 1.0, typically 2 to 4
    */
    inline
    int16_t compression(int32_t sample, float threshold, float ratio) {
      int16_t thresh = threshold * MAX_16;
      float invRatio = 1.0f / ratio;
      float gainCompensationRatio = 1.0f + (1.0f - threshold * (1.0f + 1.0f * invRatio));
      if (sample >= thresh || sample <= -thresh) {
          int32_t compressed_sample;
          if (sample > 0) {
              compressed_sample = (int32_t)((sample - thresh) * invRatio + thresh);
              if (compressed_sample > MAX_16) compressed_sample = MAX_16;
          } else {
              compressed_sample = (int32_t)((sample + thresh) * invRatio - thresh);
              if (compressed_sample < MIN_16) compressed_sample = MIN_16;
          }
          return compressed_sample * gainCompensationRatio;
      }
      return sample * gainCompensationRatio;
    }    

    /** Update the wave shaping table
  	* @param TABLE_NAME is the name of the array. Filled with 16bit values.
    * @param tableSize the length in samples, typically a power of 2.
    * Size of TABLE_SIZE is suggested, smaller or larger bit sizes will reduce or increase resolution.
  	*/
    inline
    void setShapeTable(int16_t * TABLE_NAME, int tableSize) {
      if (shapeTable) { delete[] shapeTable; shapeTable = nullptr; }
      shapeTableSize = tableSize;
      shapeTable = new int16_t[shapeTableSize];
      memcpy(shapeTable, TABLE_NAME, shapeTableSize * sizeof(int16_t));
      waveShaperStepInc = 65537.0 / shapeTableSize;
      waveShaperStepIncInv = 1.0f / waveShaperStepInc;
    }

    /** Wave Shaper
    * Distorts wave input by wave shaping function
    * Shaping wave is, like osc wavetables, WAVE_TABLE wide from MIN_16 to MAX_16 values
    * @sample_in is the input sample value from the carrier wave
    * @amount is the degree of distortion, from 0.0 to 1.0
    */
    inline
    int16_t waveShaper(int16_t sample_in, float amount) {
      int index = sample_in;
      if (shapeTableSize > 0) index = min(shapeTableSize-1, (int)max(0.0f, (sample_in + MAX_16) * waveShaperStepIncInv));
      int16_t sampVal = shapeTable[index];
      if (amount >= 0 && amount < 1.0) sampVal = (sampVal * amount) + (sample_in * (1.0 - amount));
      return sampVal;
    }

    /** Create a dedicated soft clip wave shaper
    *  Distorts wave input by wave shaping function
    * @amount is the degree of distortion, from 1.0 being minimal to ~10.0 being a lot
    * It is not efficient to update this in real time,
    * if required, then manage wave shape function in your main code
    */
    inline
    void setShapeTableSoftClip(float amount) {
      if (shapeTable) { delete[] shapeTable; shapeTable = nullptr; }
      shapeTableSize = TABLE_SIZE;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      waveShaperStepInc = 65537.0 / shapeTableSize;
      waveShaperStepIncInv = 1.0f / waveShaperStepInc;
      
      for(int i=0; i<shapeTableSize; i++) {
        shapeTable[i] = 20813.0 * atan(amount * ((MIN_16 + i * waveShaperStepInc) * (float)MAX_16_INV));
      }
    }

    /** Create a dedicated s-wave wave shaper
    *  Distorts wave input by the wave shaping function
    * @amount is the degree of distortion, from 0.0 to 1.0
    * Smaller values for amount may require gain increase compensation
    * It is not efficient to update this in real time,
    * if required, then manage wave shape function in your main code
    */
    inline
    void setShapeTableSigmoidCurve(float amount) {
      if (shapeTable) { delete[] shapeTable; shapeTable = nullptr; } // remove any previous memory allocation
      shapeTableSize = TABLE_SIZE;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      waveShaperStepInc = 65537.0 / shapeTableSize;
      waveShaperStepIncInv = 1.0f / waveShaperStepInc;
      float tabInc = 1.0 / shapeTableSize * 2;
      for(int i=0; i<shapeTableSize * 0.5f; i++) {
        float sVal = pow(i * tabInc, amount);
        shapeTable[i] = sVal * MAX_16 - MAX_16;
        shapeTable[TABLE_SIZE - i] = MAX_16 - sVal * MAX_16;
      }
    }

    /** Create a randomly varied wave shaper
    *  Distorts wave input by the wave shaping function
    * @amount is the degree of distortion, from 0 to MAX_16;
    * Because random values are static and looped, the sound is grainy rather than noisy.
    */
    inline
    void setShapeTableJitter(float amount) {
      if (shapeTable) { delete[] shapeTable; shapeTable = nullptr; } // remove any previous memory allocation
      shapeTableSize = TABLE_SIZE;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      waveShaperStepInc = 65537.0 / shapeTableSize;
      waveShaperStepIncInv = 1.0f / waveShaperStepInc;
      for(int i=0; i<shapeTableSize; i++) {
        shapeTable[i] = waveShaperStepInc * i + audioRand(amount * 2) - amount;
      }
    }

     /** Karplus Strong fedback model
    *  @audioIn Pass in an oscillator or other signal
    *  @pluckFreq Feedback freq, usually equal to the freq of the incoming osc,
    *  or desired freq of pluck for signal (noise) bursts. In Hz.
    *  @depth The level of feedback. Controls the amount of effect or length of pluck tail. 0.0 - 1.0
    *  Use smaller depth for continuous feedback, and very large depth values for pluck string effect on impulses.
    */
    inline
    int16_t pluck(int16_t audioIn, float pluckFreq, float depth) {
      if (!pluckBufferEstablished) initPluckBuffer();
      // read
      //float read_index_fractional = SAMPLE_RATE / pluckFreq;
      int pluck_buffer_read_index = pluck_buffer_write_index - SAMPLE_RATE / pluckFreq + 1;
      if (pluck_buffer_read_index < 0) pluck_buffer_read_index += PLUCK_BUFFER_SIZE;
      int bufferRead = pluckBuffer[pluck_buffer_read_index] * depth;
      // update buffer
      int32_t output = audioIn + bufferRead;
      pluckBuffer[(int)pluck_buffer_write_index] = output; // divide?
      int32_t aveOut = (output + prevPluckOutput)>>1;
      prevPluckOutput = aveOut;
      // increment buffer phase
      pluck_buffer_write_index += 1;
      if (pluck_buffer_write_index > PLUCK_BUFFER_SIZE) pluck_buffer_write_index -= PLUCK_BUFFER_SIZE;
      // send output
      return aveOut;
    }

    /** A simple reverb using recursive delay lines.
    * Mono version that sums left and right channels
    * @audioIn A mono audio signal
    * Inspired by reverb example G08 in Pure Data.
    */
    inline
    int16_t reverb(int32_t audioIn) {
      // set up first time called
      if (!reverbInitiated) {
        initReverb(reverbSize);
      }
      processReverb(audioIn, audioIn);
      return clip16(((audioIn * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>12) + ((revP2 * reverbMix)>>12));
    }

    /** A simple 'spring' reverb using recursive delay lines.
    * Stereo version that takes two inputs (can be the same) and sets left and right channel outs
    * @audioInLeft A mono audio signal
    * @audioInRight A mono audio signal
    * @audioOutLeft A mono audio destination variable for the left channel
    * @audioOutRight A mono audio destination variable for the right channel
    * Inspired by reverb example G08 in Pure Data.
    */
    inline
    void reverbStereo(int32_t audioInLeft, int32_t audioInRight, int32_t &audioOutLeft, int32_t &audioOutRight) {
      // Thread-safe lazy initialization with mutex (ESP32 only)
      if (!reverbInitiated) {
        #if IS_ESP32()
          extern SemaphoreHandle_t audioInitMutex;
          if (audioInitMutex != NULL && xSemaphoreTake(audioInitMutex, portMAX_DELAY)) {
            // Double-check after acquiring mutex
            if (!reverbInitiated) {
              initReverb(reverbSize);
            }
            xSemaphoreGive(audioInitMutex);
          } else {
            initReverb(reverbSize);
          }
        #else
          initReverb(reverbSize);
        #endif
      }

      if (reverb2Initiated) {
        processReverb((audioInLeft + allpassRevOut)>>1, clip16(audioInRight + allpassRevOut)>>1);
      } else {
        processReverb(clip16(audioInLeft), clip16(audioInRight));
      }

      audioOutLeft = clip16(((audioInLeft * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>11));
      audioOutRight = clip16(((audioInRight * (1024 - reverbMix))>>10) + ((revP2 * reverbMix)>>11));
    }

    /** Half-rate stereo reverb with interpolation for reduced CPU usage.
    * Processes reverb every other sample and applies smoothing to reduce artifacts.
    * Saves ~40-50% CPU compared to full-rate reverb.
    * @audioInLeft A mono audio signal for left channel
    * @audioInRight A mono audio signal for right channel
    * @audioOutLeft A mono audio destination variable for the left channel
    * @audioOutRight A mono audio destination variable for the right channel
    */
    inline
    void reverbStereoInterp(int32_t audioInLeft, int32_t audioInRight, int32_t &audioOutLeft, int32_t &audioOutRight) {
      // Thread-safe lazy initialization with mutex (ESP32 only)
      if (!reverbInitiated) {
        #if IS_ESP32()
          extern SemaphoreHandle_t audioInitMutex;
          if (audioInitMutex != NULL && xSemaphoreTake(audioInitMutex, portMAX_DELAY)) {
            if (!reverbInitiated) {
              initReverb(reverbSize);
            }
            xSemaphoreGive(audioInitMutex);
          } else {
            initReverb(reverbSize);
          }
        #else
          initReverb(reverbSize);
        #endif
      }

      int32_t outL, outR;
      reverbInterpToggle = !reverbInterpToggle;

      if (reverbInterpToggle) {
        // Process reverb this sample
        if (reverb2Initiated) {
          processReverb((audioInLeft + allpassRevOut)>>1, clip16(audioInRight + allpassRevOut)>>1);
        } else {
          processReverb(clip16(audioInLeft), clip16(audioInRight));
        }
        outL = clip16(((audioInLeft * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>11));
        outR = clip16(((audioInRight * (1024 - reverbMix))>>10) + ((revP2 * reverbMix)>>11));
        reverbInterpPrevL = outL;
        reverbInterpPrevR = outR;
      } else {
        // Skip reverb processing, use previous output
        outL = reverbInterpPrevL;
        outR = reverbInterpPrevR;
      }

      // Smooth output with 1-pole lowpass to reduce half-rate artifacts
      reverbInterpSmoothL += (outL - reverbInterpSmoothL) >> 2;
      reverbInterpSmoothR += (outR - reverbInterpSmoothR) >> 2;
      audioOutLeft = reverbInterpSmoothL;
      audioOutRight = reverbInterpSmoothR;
    }

    /** Reset the interpolated reverb smoothing state.
    * Call when enabling reverb after it was disabled to avoid transients.
    * @seedL Initial value for left channel smoother
    * @seedR Initial value for right channel smoother
    */
    inline
    void resetReverbInterp(int32_t seedL = 0, int32_t seedR = 0) {
      reverbInterpSmoothL = seedL;
      reverbInterpSmoothR = seedR;
      reverbInterpPrevL = seedL;
      reverbInterpPrevR = seedR;
    }

    /** A simple 'Chamberlin' reverb using allpass filter preprocessor and recursive delay lines. */
    inline
    void reverbStereo2(int32_t audioInLeft, int32_t audioInRight, int32_t &audioOutLeft, int32_t &audioOutRight) {
      if (!reverb2Initiated) {
        allpass1.setDelayTime(49.6);
        allpass1.setFeedbackLevel(0.83);
        allpass2.setDelayTime(34.65);
        allpass2.setFeedbackLevel(0.79);
        reverb2Initiated = true;
      }
      int32_t summedMono = (audioInLeft + audioInRight) >> 1;
      allpassRevOut = allpass2.next(allpass1.next(summedMono));
      reverbStereo(audioInLeft , audioInRight, audioOutLeft, audioOutRight);
    }

    /** Set the reverb length
    * @rLen The amount of feedback that effects reverb decay time. Values from 0.0 to 1.0.
    */
    inline
    void setReverbLength(float rLen) {
      rLen = max(0.0f, min(1.0f, rLen));
      reverbFeedbackLevel = pow(rLen, 0.2f);
      // Pre-calculate integer coefficient for optimized path
      reverbFeedbackInt = (int16_t)(reverbFeedbackLevel * 1024.0f);
      // Update Del objects if using legacy path
      if (reverbInitiated && !useOptimizedReverb) {
        delay1.setLevel(reverbFeedbackLevel);
        delay2.setLevel(reverbFeedbackLevel);
        delay3.setLevel(reverbFeedbackLevel);
        delay4.setLevel(reverbFeedbackLevel);
      }
      allpass1.setFeedbackLevel(reverbFeedbackLevel * 0.95);
      allpass2.setFeedbackLevel(reverbFeedbackLevel * 0.9);
    }

    /** Return the reverb length. */
    float getReverbLength() {
      return reverbFeedbackLevel;
    }

    /** Set the reverb amount
    * @rMix The balance between dry and wet signal, as the amount of wet signal, from 0.0 to 1.0.
    */
    inline
    void setReverbMix(float rMix) {
      reverbMix = max(0, min(1024, (int)(rMix * 1024.0f)));
      // Serial.print("reverbMix ");Serial.println(reverbMix);
    }

    /** Return the reverb amount, 0.0 - 1.0 */
    float getReverbMix() {
      return reverbMix * 0.0009765625f;
    }

    /** Set dampening (high frequency absorption)
     *  @param damp 0.0-1.0, higher = more HF dampening (darker sound)
     */
    void setDampening(float damp) {
      damp = max(0.0f, min(1.0f, damp));
      // Pre-calculate damping coefficient (0.7-1.0 range)
      // Higher damp value = lower coefficient = more HF cut
      int16_t dampInt = (int16_t)(damp * 1024.0f);
      reverbDampCoeff = 717 + (((1024 - dampInt) * 307) >> 10);
    }

    /** Set the reverb memory size
    * @newSize A multiple of the default, >= 1.0
    * Larger sizes take up more memory
    */
    inline
    void setReverbSize(float newSize) {
      reverbSize = max(1.0f, newSize);
      initReverb(reverbSize);
    }

    /* This prevents PSRAM detection and memory allocation from happening in ISR context */
    inline
    void initReverbSafe() {
      if (!reverbInitiated) {
        // Use global PSRAM check instead of direct call
        if (isPSRAMAvailable()) {
          Serial.println("PSRAM available for reverb delays");
        } else {
          Serial.println("No PSRAM - using regular RAM for reverb");
        }
        initReverb(reverbSize);

        // Pre-initialize allpass filters to prevent lazy init in audio tasks
        allpass1.setDelayTime(49.6);
        allpass1.setFeedbackLevel(0.83);
        allpass2.setDelayTime(34.65);
        allpass2.setFeedbackLevel(0.79);
        // Force buffer allocation by calling next() once
        allpass1.next(0);
        allpass2.next(0);
        reverb2Initiated = true;

        Serial.println("Reverb and allpass filters pre-initialized");
      }
    }

    /** A mono chorus using a modulated delay line.
     * Based on Dattorro, Jon. 1997. Effect design, part 2: Delay line modulation and chorus.
     * Journal of the Audio Engineering Society 45(10), 764–788.
    * @audioIn A mono audio signal
    */
    inline
    int16_t chorus(int32_t audioIn) {
      // set up first time called
      if (!chorusInitiated) {
        initChorus();
      }
      float chorusLfoVal = chorusLfo.next() * MAX_16_INV;
      chorusDelay.setTime(chorusDelayTime + chorusLfoVal * chorusLfoWidth);
      int32_t delVal = chorusDelay.next(audioIn);
      int32_t inVal = (audioIn * (chorusMixInput))>>10;
      delVal = (delVal * chorusMixDelay)>>10;
      // Normalize output to prevent clipping
      int32_t sum = inVal + delVal;
      return clip16((sum * chorusMixNorm)>>10);
    }

    /** A stereo chorus using two modulated delay lines.
    * @audioInLeft An audio signal
    * @audioInRight An audio signal
    * @audioOutLeft A variable to receive audio output for the left channel
    * @audioOutLeft A variable to receive audio output for the right channel
    */
    inline
    void chorusStereo(int32_t audioInLeft, int32_t audioInRight, int32_t &audioOutLeft, int32_t &audioOutRight) {
      // set up first time called
      if (!chorusInitiated) {
        initChorus();
      }
      float chorusLfoVal = chorusLfo.next() * MAX_16_INV;
      chorusDelay.setTime(chorusDelayTime + chorusLfoVal * chorusLfoWidth);
      chorusDelay2.setTime(chorusDelayTime2 + chorusLfoVal * chorusLfoWidth);
      int32_t delVal = chorusDelay.next(audioInLeft);
      int32_t delVal2 = chorusDelay2.next(audioInRight);
      int32_t inVal = (audioInLeft * (chorusMixInput))>>10;
      int32_t inVal2 = (audioInRight * (chorusMixInput))>>10;
      delVal = (delVal * chorusMixDelay)>>10;
      delVal2 = (delVal2 * chorusMixDelay)>>10;
      // Normalize output to prevent clipping
      int32_t sumL = inVal + delVal;
      int32_t sumR = inVal2 + delVal2;
      audioOutLeft = clip16((sumL * chorusMixNorm)>>10);
      audioOutRight = clip16((sumR * chorusMixNorm)>>10);
    }

    /** Set the chorus effect depth
    * @depth amount of chorus to add to the input signal, 0.0 to 1.0
    */
    inline
    void setChorusDepth(float depth) {
      depth = pow(depth, 0.8) * 0.5;
      chorusMixInput = panLeft(depth) * 1024;
      chorusMixDelay = panRight(depth) * 1024;
      updateChorusMixNorm();
    }

    /** Set the chorus width
    * @depth pitch width of chorus LFO, 0.0 to 1.0
    */
    inline
    void setChorusWidth(float depth) {
      chorusLfoWidth = pow(max(0.0f, depth), 1.5) * 3.0d;
    }

    /** Set the chorus rate
    * @rate pitch frequency of chorus LFO, in hertz. Typically 0.01 to 3.0 
    */
    inline
    void setChorusRate(float rate) {
      chorusLfoRate = rate;
      chorusLfo.setFreq(chorusLfoRate);
    }

    /** Set the chorus feedback level
    * @val feedback level of the chorus delay line, from 0.0 to 1.0 
    */
    inline
    void setChorusFeedback(float val) {
      chorusFeedback = val;
      chorusDelay.setFeedback(true);
      chorusDelay.setFeedbackLevel(max(0.0f, min(1.0f, chorusFeedback)));
      chorusDelay2.setFeedback(true);
      chorusDelay2.setFeedbackLevel(max(0.0f, min(1.0f, chorusFeedback)));
    }

    /** Set the chorus delay time
    * @time delay time in ms, typically 20 to 40 ms
    */
    inline
    void setChorusDelayTime(float time) {
      chorusDelayTime = min(40.0f, max(0.0f, time));
      chorusDelayTime2 = min(40.0f, max(0.0f, time * 0.74f));
    }


  private:
    /** Fast approximation of e^(-x) for x >= 0
    * Uses (1 - x/n)^n approximation, accurate for 0 <= x <= 4
    */
    inline float fastExpNeg(float x) {
      // Approximate e^(-x) using (1 - x/8)^8
      float t = 1.0f - x * 0.125f;
      t = t * t; // ^2
      t = t * t; // ^4
      t = t * t; // ^8
      return max(0.0f, t);
    }

    const static int16_t PLUCK_BUFFER_SIZE = 1500; // lowest MIDI pitch is 24
    int * pluckBuffer; // [PLUCK_BUFFER_SIZE];
    float pluck_buffer_write_index = 0;
    int prevPluckOutput = 0;
    bool pluckBufferEstablished = false;
    bool reverbInitiated = false;
    float reverbFeedbackLevel = 0.93; // 0.0 to 1.0
    int reverbMix = 40; // 0 to 1024
    float reverbSize = 4.0; // >= 1, memory allocated to delay lengths
    int16_t reverbFeedbackInt = 950; // 0-1024, integer version of feedback level
    int16_t reverbDampCoeff = 904; // Pre-calculated dampening coefficient (0.3 default)
    int32_t revFilterStore1 = 0, revFilterStore2 = 0, revFilterStore3 = 0, revFilterStore4 = 0;
    int32_t revInputHPF_L = 0, revInputHPF_R = 0;  // Input highpass filter state

    // Optimized reverb delay buffers - power-of-2 sizes for fast modulo via bitwise AND
    // Buffer sizes chosen to accommodate delay times up to reverbSize=16
    static const int REV_BUF_BITS = 10;  // 1024 samples = ~23ms at 44.1kHz
    static const int REV_BUF_SIZE = 1 << REV_BUF_BITS;  // 1024
    static const int REV_BUF_MASK = REV_BUF_SIZE - 1;   // 0x3FF for fast wrap

    int16_t* revBuf1 = nullptr;
    int16_t* revBuf2 = nullptr;
    int16_t* revBuf3 = nullptr;
    int16_t* revBuf4 = nullptr;
    uint16_t revWritePos = 0;  // Single write position for all buffers
    uint16_t revDelay1 = 0, revDelay2 = 0, revDelay3 = 0, revDelay4 = 0;  // Delay offsets in samples

    // Legacy Del objects kept for API compatibility (setReverbSize with unusual sizes)
    Del delay1, delay2, delay3, delay4;
    bool useOptimizedReverb = false;  // Flag to use optimized path

    int32_t revD1, revD2, revD3, revD4, revP1, revP2, revP3, revP4, revP5, revP6, revM3, revM4, revM5, revM6;
    int16_t * shapeTable;
    int shapeTableSize = 0;
    float waveShaperStepInc = MAX_16 * 2.0 * TABLE_SIZE_INV;
    float waveShaperStepIncInv;
    bool chorusInitiated = false;
    int chorusDelayTime = 38;
    int chorusDelayTime2 = 28;
    float chorusLfoRate = 0.65; // hz
    float chorusLfoWidth = 0.5; // ms
    int chorusMixInput = 600; // 0 - 1024
    int chorusMixDelay = 800; // 0 - 1024
    int chorusMixNorm = 731; // normalization factor to prevent clipping (1024 * 1024 / (600 + 800))
    float chorusFeedback = 0.4; // 0.0 to 1.0
    int16_t * chorusLfoTable;
    Osc chorusLfo;
    Del chorusDelay, chorusDelay2;
    All allpass1, allpass2;
    bool reverb2Initiated = false;
    int32_t allpassRevOut = 0;
    // Half-rate reverb interpolation state
    bool reverbInterpToggle = false;
    int32_t reverbInterpPrevL = 0, reverbInterpPrevR = 0;
    int32_t reverbInterpSmoothL = 0, reverbInterpSmoothR = 0;
    int32_t prevSaturationOutput = 0;
    EMA aveFilter;

    void initPluckBuffer() {
      pluckBuffer = new int[PLUCK_BUFFER_SIZE]; // create a new array
      for(int i=0; i<PLUCK_BUFFER_SIZE; i++) {
        pluckBuffer[i] = 0;
      }
      pluckBufferEstablished = true;
    }

    /** Recalculate chorus normalization factor to prevent clipping */
    void updateChorusMixNorm() {
      int mixSum = chorusMixInput + chorusMixDelay;
      if (mixSum > 1024) {
        chorusMixNorm = (1024 * 1024) / mixSum; // scale down to unity gain
      } else {
        chorusMixNorm = 1024; // no scaling needed
      }
    }

    /** Set the reverb params */
    void initReverb() {
      initReverb(reverbSize);
    }

    /** Allocate optimized reverb buffers */
    void allocateReverbBuffers() {
      if (revBuf1) return;  // Already allocated

      #if IS_ESP32()
        if (isPSRAMAvailable()) {
          revBuf1 = (int16_t*)ps_calloc(REV_BUF_SIZE, sizeof(int16_t));
          revBuf2 = (int16_t*)ps_calloc(REV_BUF_SIZE, sizeof(int16_t));
          revBuf3 = (int16_t*)ps_calloc(REV_BUF_SIZE, sizeof(int16_t));
          revBuf4 = (int16_t*)ps_calloc(REV_BUF_SIZE, sizeof(int16_t));
        }
      #endif

      // Fallback to regular RAM if PSRAM not available or allocation failed
      if (!revBuf1) revBuf1 = new int16_t[REV_BUF_SIZE]();
      if (!revBuf2) revBuf2 = new int16_t[REV_BUF_SIZE]();
      if (!revBuf3) revBuf3 = new int16_t[REV_BUF_SIZE]();
      if (!revBuf4) revBuf4 = new int16_t[REV_BUF_SIZE]();

      revWritePos = 0;
    }

    /** Set the reverb params
    * @size Bigger sizes increase delay line times for better quality but use more memory (x2, x3, x4, etc.)
    */
    void initReverb(float size) {
      reverbSize = size;
      // Always sync integer coefficient with float level
      reverbFeedbackInt = (int16_t)(reverbFeedbackLevel * 1024.0f);

      // Calculate delay times in samples (1/8 of Pd values)
      float d1_ms = 7.5f * size;
      float d2_ms = 8.993f * size;
      float d3_ms = 10.844f * size;
      float d4_ms = 12.118f * size;

      uint16_t d1_samp = (uint16_t)(d1_ms * SAMPLE_RATE * 0.001f);
      uint16_t d2_samp = (uint16_t)(d2_ms * SAMPLE_RATE * 0.001f);
      uint16_t d3_samp = (uint16_t)(d3_ms * SAMPLE_RATE * 0.001f);
      uint16_t d4_samp = (uint16_t)(d4_ms * SAMPLE_RATE * 0.001f);

      // Check if delays fit in optimized buffers
      if (d1_samp < REV_BUF_SIZE && d2_samp < REV_BUF_SIZE &&
          d3_samp < REV_BUF_SIZE && d4_samp < REV_BUF_SIZE) {
        // Use optimized path
        allocateReverbBuffers();
        revDelay1 = d1_samp;
        revDelay2 = d2_samp;
        revDelay3 = d3_samp;
        revDelay4 = d4_samp;
        useOptimizedReverb = true;
      } else {
        // Fall back to Del-based reverb for large sizes
        delay1.setMaxDelayTime(8 * size); delay2.setMaxDelayTime(9 * size);
        delay3.setMaxDelayTime(11 * size); delay4.setMaxDelayTime(13 * size);
        delay1.setTime(d1_ms); delay1.setLevel(reverbFeedbackLevel); delay1.setFeedback(true);
        delay2.setTime(d2_ms); delay2.setLevel(reverbFeedbackLevel); delay2.setFeedback(true);
        delay3.setTime(d3_ms); delay3.setLevel(reverbFeedbackLevel); delay3.setFeedback(true);
        delay4.setTime(d4_ms); delay4.setLevel(reverbFeedbackLevel); delay4.setFeedback(true);
        useOptimizedReverb = false;
      }
      reverbInitiated = true;
    }

    /** Compute reverb - optimized version with inlined buffer operations */
    inline void processReverb(int16_t audioInLeft, int16_t audioInRight) {
      if (useOptimizedReverb) {
        // Fast path: inlined circular buffer operations with bitwise AND wrap
        uint16_t wp = revWritePos;

        // Input highpass filter to prevent low frequencies entering feedback loop
        // Simple one-pole HPF: y[n] = x[n] - lpf[n], where lpf[n] = lpf[n-1] + (x[n] - lpf[n-1]) * coeff
        // coeff = 64/1024 ≈ 0.0625 gives ~40Hz cutoff at 44.1kHz
        revInputHPF_L += (audioInLeft - revInputHPF_L + 8) >> 4;
        revInputHPF_R += (audioInRight - revInputHPF_R + 8) >> 4;
        int32_t inL = audioInLeft - revInputHPF_L;
        int32_t inR = audioInRight - revInputHPF_R;

        // Read from delay lines (no function call overhead, no filtering, no level scaling)
        int32_t d1 = revBuf1[(wp - revDelay1) & REV_BUF_MASK];
        int32_t d2 = revBuf2[(wp - revDelay2) & REV_BUF_MASK];
        int32_t d3 = revBuf3[(wp - revDelay3) & REV_BUF_MASK];
        int32_t d4 = revBuf4[(wp - revDelay4) & REV_BUF_MASK];

        // Apply feedback level (with rounding to reduce quantization noise)
        d1 = (d1 * reverbFeedbackInt + 512) >> 10;
        d2 = (d2 * reverbFeedbackInt + 512) >> 10;
        d3 = (d3 * reverbFeedbackInt + 512) >> 10;
        d4 = (d4 * reverbFeedbackInt + 512) >> 10;

        // Apply dampening lowpass filter (with rounding)
        revFilterStore1 += ((d1 - revFilterStore1) * reverbDampCoeff + 512) >> 10;
        revFilterStore2 += ((d2 - revFilterStore2) * reverbDampCoeff + 512) >> 10;
        revFilterStore3 += ((d3 - revFilterStore3) * reverbDampCoeff + 512) >> 10;
        revFilterStore4 += ((d4 - revFilterStore4) * reverbDampCoeff + 512) >> 10;

        // Use filtered values directly (dampening already reduces HF, input HPF handles LF)
        d1 = revFilterStore1;
        d2 = revFilterStore2;
        d3 = revFilterStore3;
        d4 = revFilterStore4;

        // Mixing matrix (Hadamard-style diffusion)
        revP1 = inL + d1;
        revP2 = inR + d2;
        int32_t p3 = revP1 + revP2;
        int32_t m3 = revP1 - revP2;
        int32_t p4 = d3 + d4;
        int32_t m4 = d3 - d4;

        // Write to delay lines (with rounding for cleaner output)
        int32_t w1 = (p3 + p4 + 1) >> 1;
        int32_t w2 = (m3 + m4 + 1) >> 1;
        int32_t w3 = (p3 - p4 + 1) >> 1;
        int32_t w4 = (m3 - m4 + 1) >> 1;

        // Soft limiting before hard clip for smoother saturation
        if (w1 > 24576) w1 = 24576 + ((w1 - 24576) >> 2);
        else if (w1 < -24576) w1 = -24576 + ((w1 + 24576) >> 2);
        if (w2 > 24576) w2 = 24576 + ((w2 - 24576) >> 2);
        else if (w2 < -24576) w2 = -24576 + ((w2 + 24576) >> 2);
        if (w3 > 24576) w3 = 24576 + ((w3 - 24576) >> 2);
        else if (w3 < -24576) w3 = -24576 + ((w3 + 24576) >> 2);
        if (w4 > 24576) w4 = 24576 + ((w4 - 24576) >> 2);
        else if (w4 < -24576) w4 = -24576 + ((w4 + 24576) >> 2);

        // Final clamp to 16-bit range
        revBuf1[wp] = (w1 > MAX_16) ? MAX_16 : ((w1 < MIN_16) ? MIN_16 : w1);
        revBuf2[wp] = (w2 > MAX_16) ? MAX_16 : ((w2 < MIN_16) ? MIN_16 : w2);
        revBuf3[wp] = (w3 > MAX_16) ? MAX_16 : ((w3 < MIN_16) ? MIN_16 : w3);
        revBuf4[wp] = (w4 > MAX_16) ? MAX_16 : ((w4 < MIN_16) ? MIN_16 : w4);

        // Increment write position with fast wrap
        revWritePos = (wp + 1) & REV_BUF_MASK;
      } else {
        // Legacy path using Del objects
        revD1 = delay1.read(); revD2 = delay2.read();
        revD3 = delay3.read(); revD4 = delay4.read();
        revP1 = audioInLeft + revD1; revP2 = audioInRight + revD2;
        int32_t p3 = revP1 + revP2; int32_t m3 = revP1 - revP2;
        int32_t p4 = revD3 + revD4; int32_t m4 = revD3 - revD4;
        delay1.write((p3 + p4) >> 1); delay2.write((m3 + m4) >> 1);
        delay3.write((p3 - p4) >> 1); delay4.write((m3 - m4) >> 1);
      }
    }

    void initChorus() {
      #if IS_ESP32()
        // Use cached global PSRAM availability
        if (isPSRAMAvailable() && ESP.getFreePsram() > FULL_TABLE_SIZE * sizeof(int16_t)) {
          chorusLfoTable = (int16_t *) ps_calloc(FULL_TABLE_SIZE, sizeof(int16_t)); // calloc fills array with zeros
          Serial.println("PSRAM is availible in chorus");
        } else {
          chorusLfoTable = new int16_t[FULL_TABLE_SIZE]; // create a new waveTable array
          Serial.println("PSRAM not available in chorus");
        }
      #else
        chorusLfoTable = new int16_t[FULL_TABLE_SIZE]; // create a new waveTable array
      #endif
      Osc::sinGen(chorusLfoTable); // fill the wavetable
      chorusLfo.setTable(chorusLfoTable);
      chorusLfo.setFreq(chorusLfoRate);
      chorusDelay.setMaxDelayTime(chorusDelayTime + 3);
      chorusDelay2.setMaxDelayTime(chorusDelayTime2 + 3);
      setChorusFeedback(chorusFeedback);
      chorusInitiated = true;
    }
};

#endif /* FX_H_ */
