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
    void setRes(float resonance) {
      float resClamp = max(0.01f, min(0.84f, resonance));
      q = (int32_t)((1.0f - resClamp) * MAX_16);
      scale = (int32_t)(sqrt(max(0.1f, resClamp)) * MAX_16);
      // Convert resOffset to 15-bit fixed-point (range ~0.4 to 1.2)
      float resOffsetF = 1.2f - resClamp * 1.6f;
      resOffsetInt = (int32_t)(resOffsetF * 32768.0f);
    }

    /** Set the cutoff or centre frequency of the filter.
    * @param freq_val  40 Hz to ~21% of sample rate (safe range to prevent overflow).
    *                  At 44.1kHz: 40-9200 Hz, At 48kHz: 40-10000 Hz
    */
    inline
    void setFreq(int32_t freq_val) {
      // Calculate safe maximum frequency to prevent state variable overflow
      int32_t safeMaxFreq = SAMPLE_RATE * 0.21f;  // 21% of sample rate
      // Clamp frequency to safe range
      freq_val = max((int32_t)40, min(safeMaxFreq, freq_val));
      // Calculate f and store as 15-bit fixed-point
      float fFloat = 2.0f * sin(3.1415927f * freq_val * SAMPLE_RATE_INV);
      fInt = (int32_t)(fFloat * 32768.0f);
    }

    /** Return the cutoff or centre frequency of the filter.*/
    inline
    float getFreq() {
      return fInt * (1.0f / 32768.0f);
    }

    /** Set the cutoff or corner frequency of the filter.
    * @param cutoff_val 0.0 - 1.0 which maps to safe frequency range (40 Hz to 21% of sample rate).
    * Sweeping the cutoff value linearly is mapped to a non-linear frequency sweep
    */
    inline
    void setCutoff(float cutoff_val) {
      cutoff_val = max(0.0f, min(1.0f, cutoff_val));
      float cutoff_freq = 0;
      int32_t safeMaxFreq = SAMPLE_RATE * 0.21f;  // Match setFreq() safety limit

      if (cutoff_val > 0.7f) {
        cutoff_freq = cutoff_val * cutoff_val * cutoff_val * safeMaxFreq;
      } else {
        float cv = cutoff_val * 1.43f;
        cutoff_freq = cv * cv * (safeMaxFreq * 0.38f) + 40.0f;
      }

      // Safety clamp
      cutoff_freq = max(40.0f, min((float)safeMaxFreq, cutoff_freq));
      // Calculate f and store as 15-bit fixed-point
      float fFloat = 2.0f * sin(3.1415927f * cutoff_freq * SAMPLE_RATE_INV);
      fInt = (int32_t)(fFloat * 32768.0f);
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

      // Fast linear crossfade (avoids expensive pow/sqrt calls)
      int32_t lpfAmnt = 0;
      int32_t bpfAmnt = 0;
      int32_t hpfAmnt = 0;

      if (mix < 0.5f) {
          // LPF to BPF transition
          float lpfMix = 1.0f - mix * 2.0f;
          float bpfMix = mix * 2.0f;
          lpfAmnt = (int32_t)(low * lpfMix);
          bpfAmnt = (int32_t)(band * bpfMix);
      } else {
          // BPF to HPF transition
          float bpfMix = 1.0f - (mix - 0.5f) * 2.0f;
          float hpfMix = (mix - 0.5f) * 2.0f;
          bpfAmnt = (int32_t)(band * bpfMix);
          hpfAmnt = (int32_t)(high * hpfMix);
      }

      int32_t sum = lpfAmnt + bpfAmnt + hpfAmnt;
      if (sum > MAX_16) return MAX_16;
      if (sum < -MAX_16) return -MAX_16;
      return (int16_t)sum;
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
    int32_t scale = (int32_t)(sqrt(1.0f) * MAX_16);
    int32_t fInt = 32768;        // 15-bit fixed-point frequency coefficient (1.0 = 32768)
    int32_t resOffsetInt = 32768; // 15-bit fixed-point resonance offset (1.0 = 32768)

    /** Integer-only filter calculation for maximum performance.
     *  Uses 15-bit fixed-point for frequency and resonance coefficients.
     *  Mathematically equivalent to the original float version.
     */
    inline void calcFilter(int32_t input) {
      // Apply resonance offset (15-bit fixed-point multiply)
      input = (input * resOffsetInt) >> 15;

      // SVF difference equations using 15-bit fixed-point
      // low += f * band  ->  low += (fInt * band) >> 15
      low += (fInt * band) >> 15;

      // high = scaled_input - low - (q * band)
      high = ((scale * input) >> 14) - low - ((q * band) >> 15);

      // band += f * high  ->  band += (fInt * high) >> 15
      band += (fInt * high) >> 15;

      notch = high + low;

      // Prevent state variable overflow - tighter bounds for 32-bit safety
      // Using 2^24 (~16M) is sufficient headroom while staying well within int32 range
      const int32_t LIMIT = 16777216;
      if (low > LIMIT) low = LIMIT;
      else if (low < -LIMIT) low = -LIMIT;
      if (band > LIMIT) band = LIMIT;
      else if (band < -LIMIT) band = -LIMIT;
    }
};

#endif /* SVF_H_ */
