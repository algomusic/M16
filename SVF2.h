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
      reset();
      setRes(0.2);
    }

    /** Reset all filter state to zero - call when reusing filter or to clear artifacts */
    void reset() {
      buf0 = 0.0f;
      buf1 = 0.0f;
      low = 0;
      band = 0;
      high = 0;
      notch = 0;
      allpassPrevIn = 0;
      allpassPrevOut = 0;
      simplePrev = 0;
      dcPrev = 0.0f;
      dcOut = 0.0f;
    }

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    inline
    void setRes(float resonance) {
      q = min(0.96, pow(max(0.0f, min(1.0f, resonance)), 0.6));
      fb = q + q * (1.0 - f);
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
    void setFreq(int freq_val) {
      freq = max(0, min((int)maxFreq, freq_val));
      f = min(0.96f, 2.0f * sin(3.1459f * freq * SAMPLE_RATE_INV));
    }

    /** Return the cutoff or centre frequency of the filter.*/
    inline
    int32_t getFreq() {
      return freq;
    }

    /** Return the f value.*/
    inline
    float getF() {
      return f;
    }

    /** Set the cutoff or corner frequency of the filter.
    * @param cutoff_val 0.0 - 1.0 which equates to 0 - 10k Hz (SAMPLE_RATE/2).
    * Sweeping the cutoff value linearly is mapped to a non-linear frequency sweep
    */
    inline
    void setCutoff(float cutoff_val) {
      f = min(0.96, pow(max(0.0f, min(1.0f, cutoff_val)), 2.2));
      freq = maxFreq * f;
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

      // Fast approximation avoiding sqrt - use linear crossfade instead
      // This is less "correct" but much faster and sounds similar
      int32_t lpfAmnt = 0;
      int32_t bpfAmnt = 0;
      int32_t hpfAmnt = 0;

      if (mix < 0.5f) {
          // LPF to BPF transition
          float lpfMix = 1.0f - mix * 2.0f;  // 1.0 at mix=0, 0.0 at mix=0.5
          float bpfMix = mix * 2.0f;          // 0.0 at mix=0, 1.0 at mix=0.5
          lpfAmnt = (int32_t)(low * lpfMix);
          bpfAmnt = (int32_t)(band * bpfMix);
      } else {
          // BPF to HPF transition
          float bpfMix = 1.0f - (mix - 0.5f) * 2.0f;  // 1.0 at mix=0.5, 0.0 at mix=1.0
          float hpfMix = (mix - 0.5f) * 2.0f;          // 0.0 at mix=0.5, 1.0 at mix=1.0
          bpfAmnt = (int32_t)(band * bpfMix);
          hpfAmnt = (int32_t)(high * hpfMix);
      }

      int32_t sum = lpfAmnt + bpfAmnt + hpfAmnt;
      if (sum > MAX_16) return MAX_16;
      if (sum < -MAX_16) return -MAX_16;
      return (int16_t)sum;
    }

    /** Calculate the next Allpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     *  Perhaps not technically a state variable filter, but...
     *  Includes DC blocking to prevent offset accumulation over time.
     */
    inline
    int16_t nextAllpass(int32_t input) {
      // y = x + x(t-1) - y(t-1)
      input = clip16(input);
      int32_t output = input + allpassPrevIn - allpassPrevOut;
      allpassPrevIn = input;
      allpassPrevOut = output;

      // Apply DC blocking filter to prevent accumulation: y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
      float outF = (float)output;
      dcOut = outF - dcPrev + 0.995f * dcOut;
      dcPrev = outF;
      // Flush denormal in DC blocker state
      dcOut = flushDenormal(dcOut);

      return max(-MAX_16, (int)min((int32_t)MAX_16, (int32_t)dcOut));
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
    // Threshold for flushing denormal floats (values below this become zero)
    static constexpr float DENORMAL_THRESHOLD = 1e-15f;

    int32_t low = 0;
    int32_t band = 0;
    int32_t high = 0;
    int32_t notch = 0;
    int32_t allpassPrevIn = 0;
    int32_t allpassPrevOut = 0;
    int32_t simplePrev = 0;
    int32_t maxFreq = SAMPLE_RATE * 0.195;
    int32_t freq = 0;
    float f = 1.0f;
    float q = 0.0f;
    float fb = 0.0f;
    float buf0 = 0.0f;
    float buf1 = 0.0f;
    // DC blocking state
    float dcPrev = 0.0f;
    float dcOut = 0.0f;

    /** Flush denormal floats to zero to prevent CPU slowdown */
    inline float flushDenormal(float value) {
      // Use absolute value comparison - faster than std::fpclassify
      return (value > DENORMAL_THRESHOLD || value < -DENORMAL_THRESHOLD) ? value : 0.0f;
    }

    void calcFilter(int32_t input) {
      float in = max(-1.0f, min(1.0f, (float)(input * MAX_16_INV)));
      buf0 = buf0 + f * (in - buf0 + fb * (buf0 - buf1));
      buf1 = buf1 + f * (buf0 - buf1);

      // Flush denormals to prevent CPU slowdown during silence/quiet passages
      buf0 = flushDenormal(buf0);
      buf1 = flushDenormal(buf1);

      low = buf1 * MAX_16; // LowPass
      high = (in - buf0) * MAX_16; // HighPass
      band = (buf0 - buf1) * MAX_16; // BandPass
      notch = (in - buf0 + buf1) * MAX_16; // Notch
    }
};

#endif /* SVF2_H_ */