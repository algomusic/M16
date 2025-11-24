/*
 * SVF.h
 *
 * A State Variable Filter implementation
 *
 * by Andrew R. Brown 2021
 *
 * Based on the Mozzi audio library by Tim Barrass 2012 and on psuedo code from:
 * https://www.musicdsp.org/en/latest/Filters/23-state-variable.html
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef SVF_H_
#define SVF_H_

class SVF {

  public:
    /** Constructor */
    SVF() {
      // Initialize filter state to prevent garbage values
      low = 0;
      band = 0;
      high = 0;
      notch = 0;
      allpassPrevIn = 0;
      allpassPrevOut = 0;
      simplePrev = 0;
      setRes(0.2);
    }

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    inline
    void setRes(float resonance) { // An odd dip in level at around 70% resonance???
      resOffset = max(0.01f, min(0.84f, resonance));
      q = (1.0 - resOffset) * MAX_16;
      // q = sqrt(1.0 - atan(sqrt(resonance * MAX_16)) * 2.0 / 3.1459); // alternative
      scale = sqrt(max(0.1f, resOffset)) * MAX_16;
      resOffset = 1.2 - resOffset * 1.6;
      // scale = sqrt(q) * MAX_16; // alternative
    }

    /** Set the cutoff or centre frequency of the filter.
    * @param freq_val  40 Hz to ~21% of sample rate (safe range to prevent overflow).
    *                  At 44.1kHz: 40-9200 Hz, At 48kHz: 40-10000 Hz
    */
    inline
    void setFreq(int32_t freq_val) {
      // Calculate safe maximum frequency to prevent state variable overflow
      // Limits the 'f' coefficient to ~1.25 for numerical stability
      // This prevents the exponential accumulation that causes int32_t overflow
      int32_t safeMaxFreq = SAMPLE_RATE * 0.21;  // 21% of sample rate

      // Clamp frequency to safe range
      freq_val = max((int32_t)40, min(safeMaxFreq, freq_val));

      f = 2 * sin(3.1459 * freq_val * SAMPLE_RATE_INV);
    }

    /** Return the cutoff or centre frequency of the filter.*/
    inline
    float getFreq() {
      return f;
    }

    /** Set the cutoff or corner frequency of the filter.
    * @param cutoff_val 0.0 - 1.0 which maps to safe frequency range (40 Hz to 21% of sample rate).
    * Sweeping the cutoff value linearly is mapped to a non-linear frequency sweep
    */
    inline
    void setCutoff(float cutoff_val) {
      cutoff_val = max(0.0f, min(1.0f, cutoff_val));
      float cutoff_freq = 0;
      int32_t safeMaxFreq = SAMPLE_RATE * 0.21;  // Match setFreq() safety limit

      if (cutoff_val > 0.7) {
        cutoff_freq = pow(cutoff_val, 3) * safeMaxFreq;
      } else {
        cutoff_freq = pow(cutoff_val * 1.43, 2) * (safeMaxFreq * 0.38) + 40;
      }

      // Safety clamp
      cutoff_freq = max(40.0f, min((float)safeMaxFreq, cutoff_freq));
      f = 2 * sin(3.1459 * cutoff_freq * SAMPLE_RATE_INV);
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextLPF(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      low = clip16(low);
      return low; 
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t next(int input) {
      return nextLPF(input);
    }

    /** Retrieve the current Lowpass filter sample.
     *  Allows simultaneous use of LPF, HPF & BPF.
     *  Use nextXXX() for one of them at each sample to compute the next filter values.
     */
    inline
    int16_t currentLPF() {
      low = low;
      return low; 
    }

    /** Calculate the next Highpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextHPF(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, (int)high));
    }

    /** Retrieve the current Highpass filter sample.
     *  Allows simultaneous use of LPF, HPF & BPF. 
     *  Use nextXXX() for one of them at each sample to compute the next filter values.
     */
    inline
    int16_t currentHPF() {
      return max(-MAX_16, min(MAX_16, (int)high));
    }

    /** Calculate the next Bandpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextBPF(int input) {
      input = clip16(input);
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, (int)band));
    }

    /** Retrieve the current Bandpass filter sample.
     *  Allows simultaneous use of LPF, HPF & BPF. 
     *  Use nextXXX() for one of them at each sample to compute the next filter values.
     */
    inline
    int16_t currentBPF() {
      return max(-MAX_16, min(MAX_16, (int)band));
    }

    /** Calculate the next filter sample, given an input signal and a filter mix value.
     *  @input is an output from an oscillator or other audio element.
     *  @mix is the balance between low, band and high pass outputs, 0.0 - 1.0
     *  Mix 0 is LPF, Mix 0.5 is BPF and mix 1.0 is HPF, in between are combinations
     */
    inline
    int16_t nextFiltMix(int input, float mix) {
      input = clip16(input);
      calcFilter(input);
      int32_t lpfAmnt = 0;
      if (mix < 0.5) lpfAmnt = low * pow((1 - mix * 2), 0.5);
      int32_t bpfAmnt = 0;
      if (mix > 0.25 || mix < 0.75) {
        bpfAmnt = band * pow(1 - (abs(mix - 0.5) * 2), 0.5);
      }
      int32_t hpfAmnt = 0;
      if (mix > 0.5) hpfAmnt = clip16(high * pow((mix - 0.5) * 2, 0.5));
      return max(-MAX_16, min(MAX_16, (int)(lpfAmnt + bpfAmnt + hpfAmnt)));
    }

    /** Calculate the next Notch filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextNotch(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, (int)notch));
    }

  private:
    int32_t low, band, high, notch, allpassPrevIn, allpassPrevOut, simplePrev;
    int32_t q = MAX_16;
    int32_t scale = sqrt(1) * MAX_16;
    volatile float f = 1.0;
    int32_t centFreq = 10000;
    float resOffset;
    // maxFreq removed - now calculated dynamically in setFreq() based on sample rate

    void calcFilter(int32_t input) {
      input *= resOffset;
      low += f * band;
      // high = ((scale * input) >> 7) - low - ((q * band) >> 8);
      high = ((scale * input) >> 14) - low - ((q * band) >> 15);
      band += f * high;
      notch = high + low;

      // Prevent state variable overflow - critical for audio stability
      // Clamp to reasonable range to prevent int32_t overflow
      low = max((int32_t)-200000000, min((int32_t)200000000, low));
      band = max((int32_t)-200000000, min((int32_t)200000000, band));
      high = max((int32_t)-200000000, min((int32_t)200000000, high));
    }
};

#endif /* SVF_H_ */
