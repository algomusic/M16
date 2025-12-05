/*
 * SVF2.h
 *
 * A State Variable Filter implementation.
 * Higher quality but less efficient than SVF.h.
 * Provides LPF, HPF, BPF, Notch and Allpass outputs.
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

  /** Reset all filter state to zero */
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

  /** Set filter resonance
   * @param resonance 0.01 to 1.0
   */
  inline void setRes(float resonance) {
    q = min(0.96, pow(max(0.0f, min(1.0f, resonance)), 0.6));
    fb = q + q * (1.0 - f);
  }

  /** @return Current resonance value */
  inline float getRes() {
    return q;
  }

  /** Set cutoff frequency in Hz
   * @param freq_val 0 to ~10kHz
   */
  inline void setFreq(int freq_val) {
    freq = max(0, min((int)maxFreq, freq_val));
    f = min(0.96f, 2.0f * sin(3.1459f * freq * SAMPLE_RATE_INV));
  }

  /** @return Current cutoff frequency in Hz */
  inline int32_t getFreq() {
    return freq;
  }

  /** @return Internal f coefficient */
  inline float getF() {
    return f;
  }

  /** Set cutoff as normalized value with non-linear mapping
   * @param cutoff_val 0.0-1.0 maps to 0-10kHz
   */
  inline void setCutoff(float cutoff_val) {
    f = min(0.96, pow(max(0.0f, min(1.0f, cutoff_val)), 2.2));
    freq = maxFreq * f;
    fb = q + q * (1.0 + f);
  }

  /** Calculate next lowpass sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextLPF(int32_t input) {
    input = clip16(input);
    calcFilter(input);
    return clip16(low);
  }

  /** Calculate next filter sample (alias for nextLPF)
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t next(int32_t input) {
    return nextLPF(input);
  }

  /** @return Current lowpass output without advancing filter */
  inline int16_t currentLPF() {
    return clip16(low);
  }

  /** Calculate next highpass sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextHPF(int32_t input) {
    input = clip16(input);
    calcFilter(input);
    return clip16(high);
  }

  /** @return Current highpass output without advancing filter */
  inline int16_t currentHPF() {
    return clip16(high);
  }

  /** Calculate next bandpass sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextBPF(int32_t input) {
    input = clip16(input);
    calcFilter(input);
    return clip16(band);
  }

  /** @return Current bandpass output without advancing filter */
  inline int16_t nextBPF() {
    return clip16(band);
  }

  /** Calculate next sample with crossfade between LPF, BPF, HPF
   * @param input Audio sample
   * @param mix 0.0=LPF, 0.5=BPF, 1.0=HPF
   * @return Filtered sample
   */
  inline int16_t nextFiltMix(int32_t input, float mix) {
    input = clip16(input);
    calcFilter(input);

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

  /** Calculate next allpass sample with DC blocking
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextAllpass(int32_t input) {
    input = clip16(input);
    int32_t output = input + allpassPrevIn - allpassPrevOut;
    allpassPrevIn = input;
    allpassPrevOut = output;

    // DC blocking filter
    float outF = (float)output;
    dcOut = outF - dcPrev + 0.995f * dcOut;
    dcPrev = outF;
    dcOut = flushDenormal(dcOut);

    return max(-MAX_16, (int)min((int32_t)MAX_16, (int32_t)dcOut));
  }

  /** Calculate next notch filter sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextNotch(int32_t input) {
    input = clip16(input);
    calcFilter(input);
    return max(-MAX_16, (int)min((int32_t)MAX_16, notch));
  }

private:
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
  float dcPrev = 0.0f;
  float dcOut = 0.0f;

  /** Flush denormal floats to zero to prevent CPU slowdown */
  inline float flushDenormal(float value) {
    return (value > DENORMAL_THRESHOLD || value < -DENORMAL_THRESHOLD) ? value : 0.0f;
  }

  /** Core filter calculation */
  void calcFilter(int32_t input) {
    float in = max(-1.0f, min(1.0f, (float)(input * MAX_16_INV)));
    buf0 = buf0 + f * (in - buf0 + fb * (buf0 - buf1));
    buf1 = buf1 + f * (buf0 - buf1);

    buf0 = flushDenormal(buf0);
    buf1 = flushDenormal(buf1);

    low = buf1 * MAX_16;
    high = (in - buf0) * MAX_16;
    band = (buf0 - buf1) * MAX_16;
    notch = (in - buf0 + buf1) * MAX_16;
  }
};

#endif /* SVF2_H_ */
