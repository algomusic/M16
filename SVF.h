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
      setRes(0.2);
    }

    /** Reset filter state to zero - useful for consistent attack transients */
    inline void reset() {
      low = 0;
      band = 0;
      high = 0;
    }

    /** Set how resonant the filter will be.
    * 0.01 > res < 1.0
    */
    inline
    void setRes(float resonance) {
      // Minimum 0.3 to avoid transient overshoot at low resonance
      float resClamp = max(0.3f, min(0.84f, resonance));
      q = (int32_t)((1.0f - resClamp) * MAX_16);
      scale = (int32_t)(sqrt(resClamp) * MAX_16);
      // Linear curve with floor - more attenuation at low res to prevent transient overshoot
      // Range: 0.8 at res=0.3 down to 0.3 floor at high res
      float resOffsetF = fmaxf(0.3f, 1.04f - resClamp * 0.8f);
      resOffsetInt = (int32_t)(resOffsetF * 32768.0f);
      // Resonance-dependent output gain - less boost at low res, more at high res
      // Range: ~0.7 at res=0.2 up to ~1.2 at res=0.84
      float gainCompF = 0.55f + resClamp * 0.75f;
      gainCompInt = (int32_t)(gainCompF * 32768.0f);
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

    /** Set the cutoff or corner frequency of the filter using normalised value.
    * @param cutoff_val 0.0 - 1.0 which maps to safe frequency range (40 Hz to 21% of sample rate).
    * Sweeping the cutoff value linearly is mapped to a non-linear frequency sweep.
    * Use setFreq() for absolute Hz values.
    */
    inline
    void setNormalisedCutoff(float cutoff_val) {
      _normalisedCutoff = max(0.0f, min(1.0f, cutoff_val));
      float cutoff_freq = 0;
      int32_t safeMaxFreq = SAMPLE_RATE * 0.21f;  // Match setFreq() safety limit

      if (_normalisedCutoff > 0.7f) {
        cutoff_freq = _normalisedCutoff * _normalisedCutoff * _normalisedCutoff * safeMaxFreq;
      } else {
        float cv = _normalisedCutoff * 1.43f;
        cutoff_freq = cv * cv * (safeMaxFreq * 0.38f) + 40.0f;
      }

      // Safety clamp
      cutoff_freq = max(40.0f, min((float)safeMaxFreq, cutoff_freq));
      // Calculate f and store as 15-bit fixed-point
      float fFloat = 2.0f * sin(3.1415927f * cutoff_freq * SAMPLE_RATE_INV);
      fInt = (int32_t)(fFloat * 32768.0f);
    }

    /** Alias for setNormalisedCutoff for backwards compatibility */
    inline void setCutoff(float cutoff_val) { setNormalisedCutoff(cutoff_val); }

    /** @return Current normalised cutoff frequency (0.0-1.0) */
    inline
    float getNormalisedCutoff() {
      return _normalisedCutoff;
    }

    /** Alias for getNormalisedCutoff for backwards compatibility */
    inline float getCutoff() { return getNormalisedCutoff(); }

    /** Calculate the next Lowpass filter sample, given an input signal.
     *  Input is an output from an oscillator or other audio element.
     */
    inline
    int16_t nextLPF(int32_t input) {
      input = clip16(input);
      calcFilter(input);
      return clip16((int32_t)(((int64_t)low * gainCompInt) >> 15));
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
      return clip16((int32_t)(((int64_t)high * gainCompInt) >> 15));
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
      return clip16((int32_t)(((int64_t)band * gainCompInt) >> 15));
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

      int32_t sum = (int32_t)(((int64_t)(lpfAmnt + bpfAmnt + hpfAmnt) * gainCompInt) >> 15);
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
      int32_t notch = high + low;  // Compute on demand
      return clip16((int32_t)(((int64_t)notch * gainCompInt) >> 15));
    }

  private:
    int32_t low = 0, band = 0, high = 0;
    int32_t q = MAX_16;
    int32_t scale = (int32_t)(sqrt(1.0f) * MAX_16);
    int32_t fInt = 32768;        // 15-bit fixed-point frequency coefficient (1.0 = 32768)
    int32_t resOffsetInt = 32768; // 15-bit fixed-point resonance offset (1.0 = 32768)
    int32_t gainCompInt = 32768;  // Resonance-dependent output gain compensation
    float _normalisedCutoff = 1.0f;  // Stored normalized cutoff (0.0-1.0), default fully open

    /** Integer-only filter calculation for maximum performance.
     *  Uses 15-bit fixed-point for frequency and resonance coefficients.
     *  Uses 64-bit intermediates to prevent overflow with larger state limits.
     */
    inline void calcFilter(int32_t input) {
      // Apply resonance offset (15-bit fixed-point multiply)
      input = (input * resOffsetInt) >> 15;

      // SVF difference equations using 15-bit fixed-point
      // Use 64-bit intermediates to prevent overflow with larger LIMIT
      low += (int32_t)(((int64_t)fInt * band) >> 15);

      // high = scaled_input - low - (q * band)
      high = ((scale * input) >> 14) - low - (int32_t)(((int64_t)q * band) >> 15);

      // band += f * high
      band += (int32_t)(((int64_t)fInt * high) >> 15);

      // Prevent state variable overflow - only clamp accumulated states (low, band)
      // high is recalculated each sample so doesn't need clamping
      const int32_t LIMIT = 2000000;
      if (low > LIMIT) low = LIMIT;
      else if (low < -LIMIT) low = -LIMIT;
      if (band > LIMIT) band = LIMIT;
      else if (band < -LIMIT) band = -LIMIT;
    }
};

#endif /* SVF_H_ */
