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

// ESP8266 warning: SVF2 uses 64-bit math which is slow without hardware support
#if IS_ESP8266()
  #warning "SVF2.h uses 64-bit math and will be slow on ESP8266. Consider using SVF.h instead."
#endif

class SVF2 {

public:
  /** Constructor */
  SVF2() {
    reset();
    setRes(0.2);
  }

  /** Reset all filter state to zero */
  void reset() {
    buf0 = 0;
    buf1 = 0;
    low = 0;
    band = 0;
    high = 0;
    notch = 0;
    allpassPrevIn = 0;
    allpassPrevOut = 0;
    simplePrev = 0;
    dcPrev = 0;
    dcOut = 0;
  }

  /** Set filter resonance
   * @param resonance 0.01 to 1.0
   */
  inline void setRes(float resonance) {
    float qFloat = min(0.96f, pow(max(0.0f, min(1.0f, resonance)), 0.3f));
    qInt = (int32_t)(qFloat * 32768.0f);
    // Resonance-dependent gain compensation to prevent clipping at high Q
    // At res=0: gainComp=1.0, at res=1: gainComp≈0.6 (gentler curve)
    float gainCompF = 1.0f / (1.0f + qFloat * 0.7f);
    gainCompInt = (int32_t)(gainCompF * 32768.0f);
    updateFeedback();
  }

  /** @return Current resonance value */
  inline float getRes() {
    return qInt * (1.0f / 32768.0f);
  }

  /** Set cutoff frequency in Hz
   * @param freq_val 0 to ~10kHz
   */
  inline void setFreq(int freq_val) {
    freq = max(0, min((int)maxFreq, freq_val));
    float fFloat = min(0.96f, 2.0f * sin(3.1415927f * freq * SAMPLE_RATE_INV));
    fInt = (int32_t)(fFloat * 32768.0f);
    updateFeedback();
  }

  /** @return Current cutoff frequency in Hz */
  inline int32_t getFreq() {
    return freq;
  }

  /** @return Internal f coefficient */
  inline float getF() {
    return fInt * (1.0f / 32768.0f);
  }

  /** Set cutoff as normalized value with non-linear mapping
   * @param cutoff_val 0.0-1.0 maps to 0-10kHz
   * Use setFreq() for absolute Hz values.
   */
  inline void setNormalisedCutoff(float cutoff_val) {
    _normalisedCutoff = max(0.0f, min(1.0f, cutoff_val));
    // Minimum f of 0.001 (~20Hz) prevents filter from stalling at cutoff=0
    float fFloat = max(0.001f, min(0.96f, pow(_normalisedCutoff, 2.2f)));
    fInt = (int32_t)(fFloat * 32768.0f);
    freq = (int32_t)(maxFreq * fFloat);
    // fb = q + q * (1.0 + f)  ->  fbInt = qInt + (qInt * (32768 + fInt)) >> 15
    fbInt = qInt + ((int64_t)qInt * (32768 + fInt) >> 15);
  }

  /** Alias for setNormalisedCutoff for backwards compatibility */
  inline void setCutoff(float cutoff_val) { setNormalisedCutoff(cutoff_val); }

  /** @return Current normalised cutoff frequency (0.0-1.0) */
  inline float getNormalisedCutoff() {
    return _normalisedCutoff;
  }

  /** Alias for getNormalisedCutoff for backwards compatibility */
  inline float getCutoff() { return getNormalisedCutoff(); }

  /** Calculate next lowpass sample
   * @param input Audio sample, clipped to allow overdriven input
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

    // DC blocking filter (integer version)
    // dcOut = output - dcPrev + 0.995 * dcOut
    // Using 15-bit fixed-point: 0.995 * 32768 = 32604
    dcOut = output - dcPrev + ((dcOut * 32604) >> 15);
    dcPrev = output;

    // Clamp output
    if (dcOut > MAX_16) return MAX_16;
    if (dcOut < -MAX_16) return -MAX_16;
    return (int16_t)dcOut;
  }

  /** Calculate next notch filter sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextNotch(int32_t input) {
    input = clip16(input);
    calcFilter(input);
    int32_t n = notch;
    return max(-MAX_16, (int)min((int32_t)MAX_16, n));
  }

private:
  // volatile: ensure cross-core visibility on dual-core ESP32 (no CPU overhead)
  volatile int32_t low = 0;
  volatile int32_t band = 0;
  volatile int32_t high = 0;
  volatile int32_t notch = 0;
  int32_t allpassPrevIn = 0;
  int32_t allpassPrevOut = 0;
  int32_t simplePrev = 0;
  int32_t maxFreq = SAMPLE_RATE * 0.195f;
  int32_t freq = 0;
  float _normalisedCutoff = 0.0f;  // Stored normalized cutoff (0.0-1.0)

  // Fixed-point coefficients (15-bit, 32768 = 1.0)
  int32_t fInt = 32768;
  int32_t qInt = 0;
  int32_t fbInt = 0;
  int32_t gainCompInt = 32768;  // Resonance-dependent gain compensation

  // Filter state as 15-bit fixed-point (scaled by 32768)
  // volatile: ensure cross-core visibility on dual-core ESP32 (no CPU overhead)
  volatile int32_t buf0 = 0;
  volatile int32_t buf1 = 0;

  // DC blocker state (15-bit fixed-point)
  int32_t dcPrev = 0;
  int32_t dcOut = 0;

  /** Update feedback coefficient when f or q changes */
  inline void updateFeedback() {
    // fb = q + q * (1.0 - f)  ->  fbInt = qInt + (qInt * (32768 - fInt)) >> 15
    fbInt = qInt + ((int64_t)qInt * (32768 - fInt) >> 15);
  }

  /** Integer-only core filter calculation
   *  All arithmetic in 15-bit fixed-point (32768 = 1.0)
   *  Input expected in range -32767 to 32767, treated as -1.0 to 1.0
   */
  inline void calcFilter(int32_t input) {
    // Cache coefficients atomically to prevent race conditions with setFreq()/setRes()
    // This ensures we use a consistent set of coefficients for the entire sample
    int32_t cached_fInt, cached_fbInt, cached_gainCompInt;
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    cached_fInt = __atomic_load_n(&fInt, __ATOMIC_RELAXED);
    cached_fbInt = __atomic_load_n(&fbInt, __ATOMIC_RELAXED);
    cached_gainCompInt = __atomic_load_n(&gainCompInt, __ATOMIC_RELAXED);
    #else
    cached_fInt = fInt;
    cached_fbInt = fbInt;
    cached_gainCompInt = gainCompInt;
    #endif

    // Apply resonance-dependent gain compensation to prevent clipping at high Q
    int32_t in = ((int64_t)input * cached_gainCompInt) >> 15;

    // buf0 = buf0 + f * (in - buf0 + fb * (buf0 - buf1))
    int32_t diff01 = buf0 - buf1;
    int32_t fbTerm = ((int64_t)cached_fbInt * diff01) >> 15;
    int32_t innerSum = in - buf0 + fbTerm;
    buf0 += ((int64_t)cached_fInt * innerSum) >> 15;

    // buf1 = buf1 + f * (buf0 - buf1)
    buf1 += ((int64_t)cached_fInt * (buf0 - buf1)) >> 15;

    // Prevent fixed-point overflow (equivalent to denormal flush)
    // Clamp to reasonable range - increased headroom since input is gain-compensated
    const int32_t LIMIT = 32767 * 8;  // 8x headroom for resonance peaks
    if (buf0 > LIMIT) buf0 = LIMIT;
    else if (buf0 < -LIMIT) buf0 = -LIMIT;
    if (buf1 > LIMIT) buf1 = LIMIT;
    else if (buf1 < -LIMIT) buf1 = -LIMIT;

    // Calculate outputs (already in audio range)
    low = buf1;
    high = in - buf0;
    band = buf0 - buf1;
    notch = in - buf0 + buf1;
  }
};

#endif /* SVF2_H_ */
