/*
 * EMA.h
 *
 * A simple but efficient low pass filter based on the exponential moving average.
 * Single-pole IIR filter with minimal CPU overhead.
 *
 * by Andrew R. Brown 2025
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef EMA_H_
#define EMA_H_

class EMA {

public:
  /** Default constructor */
  EMA() = default;

  /** Constructor with initial alpha
   * @param newAlpha EMA coefficient 0.0-1.0: low values smooth more, 1.0 is bypass
   */
  explicit EMA(float newAlpha) {
    setCoefficient((int32_t)(clamp01(newAlpha) * 1024.0f));
  }

  /** Set resonance - no-op for compatibility with other M16 filters */
  inline void setRes(float resonance) { (void)resonance; }

  /** Reset filter state to zero - useful for consistent attack transients */
  inline void reset() {
    outPrev = 0;
    inPrev = 0;
  }

  /** Set cutoff frequency in Hz
   * @param freqVal Frequency 40-10000 Hz
   */
  inline void setFreq(int32_t freq_val) {
    int32_t clampedFreq = max((int32_t)40, min((int32_t)10000, freq_val));
    if (coefficientSource == SOURCE_FREQ && f == clampedFreq) return;
    f = clampedFreq;
    float cutVal = f * 0.0001;
    alpha_val.store(
        (int16_t)max((int32_t)10,
                     (int32_t)((1.0f - pow((1.0f - cutVal), 0.3f)) * 1024)),
        std::memory_order_relaxed);
    coefficientSource = SOURCE_FREQ;
  }

  /** @return Current cutoff frequency in Hz */
  inline float getFreq() const {
    return f;
  }

  /** Set cutoff as normalized value
   * @param cutoffVal 0.0-1.0 maps to approx 40-10000 Hz
   */
  inline void setCutoff(float cutoff_val) {
    float cutVal = clamp01(cutoff_val);
    if (coefficientSource == SOURCE_CUTOFF && cutVal == normalizedCutoff) return;
    normalizedCutoff = cutVal;
    f = max(40.0f, cutVal * 10000.0f);
    alpha_val.store(
        (int16_t)max((int32_t)10,
                     (int32_t)((1.0f - pow((1.0f - cutVal), 0.2f)) * 1024.0f)),
        std::memory_order_relaxed);
    coefficientSource = SOURCE_CUTOFF;
  }

  /** Return cutoff as normalized value */
  inline float getCutoff() const {
    return normalizedCutoff;
  }

  /** Convert a normalized cutoff to EMA's integer coefficient.
   * Intended for building a lookup table during setup(), not for audio-rate use.
   */
  static inline int16_t coefficientForCutoff(float cutoff_val) {
    float cutVal = clamp01(cutoff_val);
    return (int16_t)max((int32_t)10,
        min((int32_t)1024,
            (int32_t)((1.0f - pow((1.0f - cutVal), 0.2f)) * 1024.0f)));
  }

  /** Set a precomputed filter coefficient without pow().
   * @param coefficient 10-1024, normally from coefficientForCutoff()
   */
  inline void setCoefficient(int32_t coefficient) {
    int16_t clampedCoefficient = (int16_t)max((int32_t)10,
                                              min((int32_t)1024, coefficient));
    if (coefficientSource == SOURCE_DIRECT &&
        alpha_val.load(std::memory_order_relaxed) == clampedCoefficient) return;
    alpha_val.store(clampedCoefficient, std::memory_order_relaxed);
    coefficientSource = SOURCE_DIRECT;
  }

  /** @return Active fixed-point coefficient in the range 10-1024. */
  inline int16_t getCoefficient() const {
    return alpha_val.load(std::memory_order_relaxed);
  }

  /** Calculate next lowpass filter sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextLPF(int32_t input) {
    // One-pole recurrence. alpha_val == 1024 naturally produces bypass.
    int16_t coefficient = alpha_val.load(std::memory_order_relaxed);
    outPrev += ((input - outPrev) * coefficient) >> 10;
    return outPrev;
  }

  /** Calculate next filter sample (alias for nextLPF)
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t next(int32_t input) {
    return nextLPF(input);
  }

  /** Calculate next highpass filter sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextHPF(int32_t input) {
    int16_t coefficient = alpha_val.load(std::memory_order_relaxed);
    outPrev = (((2048 - coefficient) * (input - inPrev)) >> 11) +
              (((1024 - coefficient) * outPrev) >> 10);
    inPrev = input;
    return clip16(outPrev);
  }

private:
  static inline float clamp01(float value) {
    return max(0.0f, min(1.0f, value));
  }

  enum : uint8_t {
    SOURCE_DEFAULT,
    SOURCE_FREQ,
    SOURCE_CUTOFF,
    SOURCE_DIRECT
  };

  // Filter history belongs to one audio stream/core. The coefficient is atomic
  // so control code on another core may safely call the coefficient setters.
  int32_t outPrev = 0;
  int32_t inPrev = 0;
  float f = 10000.0f;
  float normalizedCutoff = 1.0f;
  std::atomic<int16_t> alpha_val{1024};
  uint8_t coefficientSource = SOURCE_DEFAULT;
};

#endif /* EMA_H_ */
