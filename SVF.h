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

//template <int8_t FILTER_TYPE> class SVF {
class SVF {

  public:
    /** Constructor. */
    SVF() {
      setRes(0.2);
    }

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    inline
    void setRes(float resonance) {
      resOffset = max(0.2f, min(0.98f, resonance));
      q = (1.0 - resOffset) * 255;
      // q = sqrt(1.0 - atan(sqrt(resonance * 255)) * 2.0 / 3.1459); // alternative
      scale = sqrt(max(0.1f, resOffset)) * 255;
      resOffset = 1.2 - resOffset * 1.6;
      // scale = sqrt(q) * 255; // alternative
    }

    /** Set the cutoff or centre frequency of the filter.
    * @param freq_val  40 - 10k Hz (SAMPLE_RATE/4).
    */
    inline
    void setFreq(int32_t freq_val) {
      f = 2 * sin(3.1459 * max(0, (int)min(maxFreq, freq_val)) * SAMPLE_RATE_INV);
    }

    /** Return the cutoff or centre frequency of the filter.*/
    inline
    int16_t getFreq() {
      return f;
    }

    /** Set the cutoff or corner frequency of the filter.
    * @param cutoff_val 0.0 - 1.0 which equates to 40 - 10k Hz (SAMPLE_RATE/4).
    * Sweeping the cutoff value linearly is mapped to a non-linear frequency sweep
    */
    inline
    void setCutoff(float cutoff_val) {
      cutoff_val = max(0.0f, min(1.0f, cutoff_val));
      float cutoff_freq = 0;
      if (cutoff_val > 0.7) {
        cutoff_freq = pow(cutoff_val, 3) * SAMPLE_RATE * 0.2222; ///4.5;
      } else cutoff_freq = pow(cutoff_val * 1.43, 2) * 3500 + 40;
      f = 2 * sin(3.1459 * cutoff_freq * SAMPLE_RATE_INV);
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextLPF(int32_t input) {
      calcFilter(input);
      low = max((int32_t)-MAX_16, min((int32_t)MAX_16, low));
      return low; 
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
      calcFilter(input);
      return max(-MAX_16, (int)min((int32_t)MAX_16, high));
    }

    /** Calculate the next Bandpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextBPF(int32_t input) {
      calcFilter(input);
      return max(-MAX_16, (int)min((int32_t)MAX_16, band));
    }

    /** Calculate the next filter sample, given an input signal and a filter mix value.
     *  @input is an output from an oscillator or other audio element.
     *  @mix is the balance between low, band and high pass outputs, 0.0 - 1.0
     *  Mix 0 is LPF, Mix 0.5 is BPF and mix 1.0 is HPF, in between are combinations
     */
    inline
    int16_t nextFiltMix(int32_t input, float mix) {
      calcFilter(input);
      int lpfAmnt = 0;
      if (mix < 0.5) lpfAmnt = low * (1 - mix * 2);
      int bpfAmnt = 0;
      if (mix > 0.25 || mix < 0.75) {
        if (mix < 0.5) {
          lpfAmnt = band * mix * 2;
        } else lpfAmnt = band * (2 - mix * 2);
      }
      int hpfAmnt = 0;
      if (mix > 0.5) hpfAmnt = high * (mix - 0.5) * 2;
      return max(-MAX_16, min(MAX_16, lpfAmnt + bpfAmnt + hpfAmnt));
    }

    /** Calculate the next Allpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Perhaps not technically a state variable filter, but...
     */
    inline
    int16_t nextAllpass(int32_t input) {
      // y = x + x(t-1) - y(t-1)
      int32_t output = input + allpassPrevIn - allpassPrevOut;
      allpassPrevIn = input;
      allpassPrevOut = output;
      return max(-MAX_16, (int)min((int32_t)MAX_16, output));
    }

    /** Calculate the next Notch filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextNotch(int32_t input) {
      calcFilter(input);
      return max(-MAX_16, (int)min((int32_t)MAX_16, notch));
    }

    /** Calculate the next averaged filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Perhaps not technically a state variable filter, but useful for low power CPUs like ESP8266
     */
    inline
    int16_t simpleLPF(int32_t input) {
      simplePrev = (input + simplePrev) >> 1;
      return simplePrev;
    }

  private:
    int32_t low, band, high, notch, allpassPrevIn, allpassPrevOut, simplePrev;
    int32_t q = 255;
    int32_t scale = sqrt(1) * 255;
    volatile float f = SAMPLE_RATE * 0.25;
    int32_t centFreq = 10000;
    float resOffset;
    int32_t maxFreq = SAMPLE_RATE * 0.2222;

    void calcFilter(int32_t input) {
      input *= resOffset;
      low += f * band;
      high = ((scale * input) >> 7) - low - ((q * band) >> 8);
      band += f * high;
      notch = high + low;
    }

};

#endif /* SVF_H_ */
