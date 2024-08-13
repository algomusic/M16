/*
 * Ave.h
 *
 * A crude but efficient low pass filter based on the moving average of the last 2 samples.
 *
 * by Andrew R. Brown 2024
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef AVE_H_
#define AVE_H_

class Ave {

  public:
    /** Constructor */
    Ave() {}

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    inline
    void setRes(float resonance) {
      // no resonance implemented
      // API call here for compatibility with other filters
    }

    /** Set the cutoff or centre frequency of the filter.
    * @param freq_val  40 - 10k Hz (SAMPLE_RATE/4).
    */
    inline
    void setFreq(int32_t freq_val) {
      // very approximate mapping of freq_val to f
      f = max(40, min(10000, freq_val));
      float cutVal = f * 0.0001;
      cutLevel = pow((1.0f - cutVal), 6.0f) * 70;
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
      cutLevel = pow((1.0f - cutVal), 4.5f) * 70;
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextLPF(int32_t input) {
      // no input clipping implemented - take care.
      simplePrev = (input + simplePrev * cutLevel) / (1 + cutLevel);
      return simplePrev;
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t next(int32_t input) {
      return nextLPF(input);
    }

  private:
    int32_t simplePrev = 0; // previous sample value for averaging
    volatile float f = 10000; // approx cutoff frequency in Hz
    int16_t cutLevel = 0; // 0 - 10

};

#endif /* AVE_H_ */
