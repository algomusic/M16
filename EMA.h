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
  EMA() {}

  /** Constructor with initial alpha
   * @param newAlpha Filter coefficient 0.0-1.0
   */
  EMA(float newAlpha) {
    newAlpha = max((int32_t)0, min((int32_t)1024, (int32_t)(newAlpha * 1024)));
    alpha_val = max(10.0f, (1024.0f - newAlpha));
  }

  /** Set resonance - no-op for compatibility with other M16 filters */
  inline void setRes(float resonance) {}

  /** Reset filter state to zero - useful for consistent attack transients */
  inline void reset() {
    outPrev = 0;
    inPrev = 0;
  }

  /** Set cutoff frequency in Hz
   * @param freq_val Frequency 40-10000 Hz
   */
  inline void setFreq(int32_t freq_val) {
    f = max((int32_t)40, min((int32_t)10000, freq_val));
    float cutVal = f * 0.0001;
    alpha_val = max((int32_t)10, (int32_t)((1.0f - pow((1.0f - cutVal), 0.3f)) * 1024));
  }

  /** @return Current cutoff frequency in Hz */
  inline float getFreq() {
    return f;
  }

  /** Set cutoff as normalized value
   * @param cutoff_val 0.0-1.0 maps to approx 40-10000 Hz
   */
  inline void setCutoff(float cutoff_val) {
    f = max(40.0f, min(10000.0f, cutoff_val * 10000));
    float cutVal = max(0.0f, min(1.0f, cutoff_val));
    alpha_val = max((int32_t)10, (int32_t)((1.0f - pow((1.0f - cutVal), 0.2f)) * (int32_t)1024));
  }

  /** Calculate next lowpass filter sample
   * @param input Audio sample
   * @return Filtered sample
   */
  inline int16_t nextLPF(int32_t input) {
    if (f <= 10000) {
      outPrev = ((input * alpha_val)>>10) + ((outPrev * (1024 - alpha_val))>>10);
    } else {
      outPrev = input;
    }
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
    outPrev = (((2048 - alpha_val) * (input - inPrev))>>11) + (((1024 - alpha_val) * outPrev)>>10);
    inPrev = input;
    return clip16(outPrev);
  }

private:
  // volatile: ensure cross-core visibility on dual-core ESP32 (no CPU overhead)
  volatile int32_t outPrev = 0;
  volatile int32_t inPrev = 0;
  volatile float f = 10000;
  int16_t alpha_val = 1024;
};

#endif /* EMA_H_ */
