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
      setResonance(0.2);
    }

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    void setResonance(float resonance) {
      q = (1.0 - max(0.1f, min(0.98f, resonance))) * 255;
      // q = sqrt(1.0 - atan(sqrt(resonance * 255)) * 2.0 / 3.1459); // alternative
      scale = sqrt(resonance) * 255; //255;
      // scale = sqrt(q) * 255; // alternative
    }

    /** Set the centre or corner frequency of the filter.
    @param centre_freq 40 - 11k Hz (SAMPLE_RATE/4). */
    void setCentreFreq(int centre_freq) {
      centre_freq = max(40, min(SAMPLE_RATE/4, centre_freq));
      f = 2 * sin(3.1459 * centre_freq / SAMPLE_RATE);
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Needs to use int rather than uint16_t for some reason(?)
     */
    inline
    int nextLPF(int input) {
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, low)); // 65534, 32767
//      return low;
    }

    /** Calculate the next Highpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Needs to use int rather than uint16_t for some reason(?)
     */
    inline
    int nextHPF(int input) {
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, high));
    }

    /** Calculate the next Bandpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Needs to use int rather than uint16_t for some reason(?)
     */
    inline
    int nextBPF(int input) {
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, band));
//       return max(-32767, min(32767, band));
    }

    /** Calculate the next Notch filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Needs to use int rather than uint16_t for some reason(?)
     */
    inline
    int nextNotch(int input) {
      calcFilter(input);
      return max(-MAX_16, min(MAX_16, notch));
    }

    private:
      int low, band, high, notch;
//       float q = 1.0;
      int q = 255;
//       float scale;
      int scale = sqrt(1) * 255;
      volatile float f = SAMPLE_RATE / 4;

      void calcFilter(int input) {
        low += f * band;
        high = ((int)(scale * input * 2) >> 8) - low - ((int)(q * band) >> 8);
        band += f * high;
        notch = high + low;
      }

};

#endif /* SVF_H_ */
