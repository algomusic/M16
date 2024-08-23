/*
 * SVF2.h
 *
 * A State Variable Filter implementation
 * Higher quality but less efficient than SVF.h
 *
 * by Andrew R. Brown 2024
 *
 * Based on the Resonant Filter by Paul Kellett with SVF mods by Peter Schofhauzer from:
 * https://www.musicdsp.org/en/latest/Filters/29-resonant-filter.html
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef SVF2_H_
#define SVF2_H_

class SVF2 {

  public:
    /** Constructor */
    SVF2() {
      setRes(0.2);
    }

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    inline
    void setRes(float resonance) {
      q = pow(max(0.0f, min(1.0f, resonance)), 0.4);
      fb = q + q * (1.0 + f);
    }

    /** Return the resonance or q of the filter.*/
    inline
    float getRes() {
      return q;
    }

    /** Set the cutoff or centre frequency of the filter.
    * @param freq_val  0 - 10k Hz (~SAMPLE_RATE/4).
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
    * @param cutoff_val 0.0 - 1.0 which equates to 0 - 10k Hz (SAMPLE_RATE/2).
    * Sweeping the cutoff value linearly is mapped to a non-linear frequency sweep
    */
    inline
    void setCutoff(float cutoff_val) {
      f = pow(max(0.0f, min(1.0f, cutoff_val)), 2.2);
      fb = q + q * (1.0 + f);
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextLPF(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      return clip16(low); 
    }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t next(int32_t input) {
      return nextLPF(input);
    }

    /** Retrieve the current Lowpass filter sample.
     *  Allows simultaneous use of LPF, HPF & BPF.
     *  Use nextXXX() for one of them at each sample to compute the next filter values.
     */
    inline
    int16_t currentLPF() {
      return clip16(low); 
    }

    /** Calculate the next Highpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextHPF(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      return clip16(high);
    }

    /** Retrieve the current Highpass filter sample.
     *  Allows simultaneous use of LPF, HPF & BPF.
     *  Use nextXXX() for one of them at each sample to compute the next filter values.
     */
    inline
    int16_t currentHPF() {
      return clip16(high);
    }

    /** Calculate the next Bandpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextBPF(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      return clip16(band);
    }

    /** Retrieve the current Bandpass filter sample.
     *  Allows simultaneous use of LPF, HPF & BPF.
     *  Use nextXXX() for one of them at each sample to compute the next filter values.
     */
    inline
    int16_t nextBPF() {
      return clip16(band);
    }

    /** Calculate the next filter sample, for a mix between low, band and high pass filters.
     *  @input is an output from an oscillator or other audio element.
     *  @mix is the balance between low, band and high pass outputs, 0.0 - 1.0
     *  Mix 0 is LPF, Mix 0.5 is BPF and Mix 1.0 is HPF, inbetween are combinations
     */
    inline
    int16_t nextFiltMix(int32_t input, float mix) {
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
      return max(-MAX_16, min(MAX_16, lpfAmnt + bpfAmnt + hpfAmnt));
    }

    /** Calculate the next Allpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Perhaps not technically a state variable filter, but...
     */
    inline
    int16_t nextAllpass(int32_t input) {
      // y = x + x(t-1) - y(t-1)
      input = clip16(input);
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
      input = clip16(input);
      calcFilter(input);
      return max(-MAX_16, (int)min((int32_t)MAX_16, notch));
    }

  private:
    int32_t low, band, high, notch, allpassPrevIn, allpassPrevOut, simplePrev;
    int32_t maxFreq = SAMPLE_RATE * 0.195;
    float f = 1.0;
    float q = 0.0;
    float fb = 0.0;
    float buf0 = 0.0;
    float buf1 = 0.0;

    void calcFilter(int32_t input) {
      float in =  max(-1.0f, min(1.0f, input * MAX_16_INV));
      buf0 = buf0 + f * (in - buf0 + fb * (buf0 - buf1));
      buf1 = buf1 + f * (buf0 - buf1);
      low = buf1 * MAX_16; // LowPass
      high = (in - buf0) * MAX_16; // HighPass
      band = (buf0 - buf1) * MAX_16; // BandPass
      notch = (in - buf0 + buf1) * MAX_16; // Notch
    }
};

#endif /* SVF2_H_ */