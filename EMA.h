/*
 * EMA.h
 *
 * A simple but efficient low pass filter based on the exponential moving average (EMA) of recent samples.
 * This is an IIR filter with a single pole.
 *
 * by Andrew R. Brown 2025
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef EMA_H_
#define EMA_H_

class EMA {

  public:
    /** Constructor */
    EMA() {}

    /** Constructor 
     * @param newAlpha 0.0 - 1.0
    */
    EMA(float newAlpha) { 
      newAlpha = max((int32_t)0, min((int32_t)1024, (int32_t)(newAlpha * 1024)));
      alpha_val = max(10.0f, (1024.0f - newAlpha));
    }

    /** Set how resonant the filter will be. */
    inline
    void setRes(float resonance) {
      // no resonance implemented
      // API call here for compatibility with other M16 filters
    }

    /** Set the approx cutoff amount of the filter.
    * @param freq_val  40 - 10k Hz (SAMPLE_RATE/4).
    */
    inline
    void setFreq(int32_t freq_val) {
      // very approximate mapping of freq_val to f
      f = max((int32_t)40, min((int32_t)10000, freq_val));
      float cutVal = f * 0.0001;
      alpha_val = max((int32_t)10, (int32_t)((1.0f - pow((1.0f - cutVal), 0.3f)) * 1024));
    }

    /** Return the cutoff or centre frequency of the filter.*/
    inline
    float getFreq() {
      return f;
    }

    /** Set the cutoff or corner frequency of the filter.
    * @param cutoff_val 0.0 - 1.0 which equates to approx 40 - 10k Hz (~SAMPLE_RATE/4).
    * Sweeping cutoff will be stepped.
    */
    inline
    void setCutoff(float cutoff_val) {
      f = max(40.0f, min(10000.0f, cutoff_val * 10000)); // approximate mapping
      float cutVal = max(0.0f, min(1.0f, cutoff_val));
      alpha_val = max((int32_t)10, (int32_t)((1.0f - pow((1.0f - cutVal), 0.2f)) * (int32_t)1024));
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextLPF(int32_t input) {
      // no input clipping implemented - take care.
      // y[i]=α⋅x[i]+(1−α)⋅y[i−1]
      if (f <= 10000) {
        outPrev = ((input * alpha_val)>>10) + ((outPrev * (1024 - alpha_val))>>10);
      } else outPrev = input;
      return outPrev;
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t next(int32_t input) {
      return nextLPF(input);
    }

    /** Calculate the next Highpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextHPF(int32_t input) {
      // y[i] = 1/2(2 - α) . (x[i] - x[i-1]) + (1−α) ⋅ y[i−1]
      outPrev = (((2048 - alpha_val) * (input - inPrev))>>11) + (((1024 - alpha_val) * outPrev)>>10);
      inPrev = input;
      return clip16(outPrev);
    }

  private:
    int32_t outPrev = 0; // previous sample output value
    int32_t inPrev = 0; // previous sample input value for HPF
    volatile float f = 10000; // approx cutoff frequency in Hz
    int16_t alpha_val = 1024; // 0 - 1024

};

#endif /* EMA_H_ */
