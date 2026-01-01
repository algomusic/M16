/*
 * Bob.h
 *
 * A Moog Ladder 4-pole lowpass filter with soft-knee anti-aliasing.
 * Reduces aliasing by applying soft-knee limiting before the tanh nonlinearity,
 * maintaining character while reducing harmonics that cause aliasing.
 *
 * by Andrew R. Brown 2023
 * 
 * Inspired by Bob Moog
 * Ported from the moogLadder implementation by Nick Donaldson
 * which in turn is based on the DaisySP implemtation by Electrosmith LLC 
 * based on the version in SoundPipe by Paul Batchelor
 * all derrived from cSound implementations by Perry Cook, John ffitch, and Victor Lazzarini
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef BOB_H_
#define BOB_H_

#include <Arduino.h>
#include <math.h>

class Bob {
public:
  /** Constructor - initializes filter and tanh lookup table */
  Bob() {
    initTanhLUT();
    init(SAMPLE_RATE);
    setRes(0.2f);
  }

  /** Process one sample through the filter
   * @param samp Input audio sample, clipped to allow overdriven input
   * @return Filtered sample
   */
  inline int16_t next(int32_t samp) {
    const float input = (float)clip16(samp) * MAX_16_INV;

    // Local copies of state for faster access
    float z0_0 = z0_[0], z0_1 = z0_[1], z0_2 = z0_[2], z0_3 = z0_[3];
    float z1_0 = z1_[0], z1_1 = z1_[1], z1_2 = z1_[2], z1_3 = z1_[3];

    const float pbg_in = pbg_ * input;
    float ft3_sum;

    // First sub-sample (interp = 0, so inMix = input)
    {
      float u = input - (z1_3 - pbg_in) * KQ_;
      u = softTanh(u);

      float ft0 = u * a_ + z0_0 * b_ - z1_0;
      ft0 = ft0 * alpha_ + z1_0;
      z1_0 = ft0; z0_0 = u;

      float ft1 = ft0 * a_ + z0_1 * b_ - z1_1;
      ft1 = ft1 * alpha_ + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      float ft2 = ft1 * a_ + z0_2 * b_ - z1_2;
      ft2 = ft2 * alpha_ + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      float ft3 = ft2 * a_ + z0_3 * b_ - z1_3;
      ft3 = ft3 * alpha_ + z1_3;
      z1_3 = ft3; z0_3 = ft2;

      ft3_sum = ft3;
    }

    // Second sub-sample (interp = 0.5, so inMix = average of old and new input)
    {
      float inMix = (oldinput_ + input) * 0.5f;
      float u = inMix - (z1_3 - pbg_in) * KQ_;
      u = softTanh(u);

      float ft0 = u * a_ + z0_0 * b_ - z1_0;
      ft0 = ft0 * alpha_ + z1_0;
      z1_0 = ft0; z0_0 = u;

      float ft1 = ft0 * a_ + z0_1 * b_ - z1_1;
      ft1 = ft1 * alpha_ + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      float ft2 = ft1 * a_ + z0_2 * b_ - z1_2;
      ft2 = ft2 * alpha_ + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      float ft3 = ft2 * a_ + z0_3 * b_ - z1_3;
      ft3 = ft3 * alpha_ + z1_3;
      z1_3 = ft3; z0_3 = ft2;

      ft3_sum += ft3;
    }

    // Clamp state values to prevent runaway at high resonance
    // and check for NaN/infinity which would permanently break the filter
    auto clampState = [](float v) -> float {
      if (v != v || v > 1e6f || v < -1e6f) return 0.0f;  // NaN or overflow: reset
      return (v > 4.0f) ? 4.0f : ((v < -4.0f) ? -4.0f : v);
    };
    z0_0 = clampState(z0_0); z0_1 = clampState(z0_1);
    z0_2 = clampState(z0_2); z0_3 = clampState(z0_3);
    z1_0 = clampState(z1_0); z1_1 = clampState(z1_1);
    z1_2 = clampState(z1_2); z1_3 = clampState(z1_3);

    // Write state back
    z0_[0] = z0_0; z0_[1] = z0_1; z0_[2] = z0_2; z0_[3] = z0_3;
    z1_[0] = z1_0; z1_[1] = z1_1; z1_[2] = z1_2; z1_[3] = z1_3;
    oldinput_ = input;

    // Output scaling and clipping (ft3_sum * 0.5 * ampComp)
    int32_t outi = (int32_t)(ft3_sum * ampCompHalf_);
    if (outi > MAX_16) outi = MAX_16;
    else if (outi < MIN_16) outi = MIN_16;
    return (int16_t)outi;
  }

  /** Alias for next() - for API compatibility
   * @param samp Input audio sample
   * @return Filtered sample
   */
  inline int16_t nextLPF(int32_t samp) { return next(samp); }

  /** Set filter resonance
   * @param res Resonance 0.0-1.0
   */
  void setRes(float res) {
    if (res <= 0.0f) res = 0.0f;
    else if (res >= 1.0f) res = 1.0f;
    else res = sqrtf(res);
    K_ = 5.0f * res;
    KQ_ = K_ * Qadjust_;
  }

  /** Set cutoff frequency in Hz
   * @param freq Frequency in Hz (5-20000)
   */
  void setFreq(float freq) {
    // Reduce effect above 5kHz to limit aliasing
    if (freq > 5000.0f) {
      freq = 5000.0f + (freq - 5000.0f) * 0.4f;
    }
    freq = max(5.0f, min(20000.0f, freq));
    Fbase_ = freq;
    compute_coeffs(Fbase_);
    KQ_ = K_ * Qadjust_;
  }

  /** Set cutoff as normalized value with cubic mapping
   * @param cutoff_val 0.0-1.0
   */
  void setCutoff(float cutoff_val) {
    setFreq(20000.0f * cutoff_val * cutoff_val * cutoff_val);
  }

  /** @return Current cutoff frequency in Hz */
  float getFreq() const { return Fbase_; }

  /** Reset filter state to zero */
  void reset() {
    for (int i = 0; i < 4; i++) {
      z0_[i] = 0.0f;
      z1_[i] = 0.0f;
    }
    oldinput_ = 0.0f;
  }

private:
  static const uint8_t kInterpolation = 2;
  static const int LUT_SIZE = 1024;
  static constexpr float LUT_RANGE = 4.0f;
  static constexpr float LUT_SCALE = (LUT_SIZE - 1) / (2.0f * LUT_RANGE);  // Precomputed: 127.875

  float tanhLUT_[LUT_SIZE];
  float alpha_;
  float z0_[4] = {0,0,0,0};
  float z1_[4] = {0,0,0,0};
  float K_;
  float Qadjust_;
  float KQ_;
  float pbg_;
  float oldinput_;
  float Fbase_;
  int32_t ampComp = (int32_t)(MAX_16 * 1.4f);
  float ampCompHalf_ = ampComp * 0.5f;  // Precomputed for output scaling
  static constexpr float a_ = 1.0f / 1.3f;   // 0.769230769f - filter coefficient
  static constexpr float b_ = 0.3f / 1.3f;   // 0.230769231f - filter coefficient
  static constexpr float softKnee_ = 1.5f;   // Soft-knee threshold for tanh saturation

  /** Initialize tanh lookup table */
  void initTanhLUT() {
    for (int i = 0; i < LUT_SIZE; i++) {
      float x = ((float)i / (LUT_SIZE - 1)) * 2.0f * LUT_RANGE - LUT_RANGE;
      tanhLUT_[i] = tanhf(x);
    }
  }

  /** Lookup tanh with linear interpolation */
  inline float tanhLookup(float x) {
    if (x >= LUT_RANGE) return 1.0f;
    if (x <= -LUT_RANGE) return -1.0f;
    float indexF = (x + LUT_RANGE) * LUT_SCALE;  // Multiplication instead of division
    int idx = (int)indexF;
    float frac = indexF - idx;
    if (idx < 0) idx = 0;
    if (idx >= LUT_SIZE - 1) idx = LUT_SIZE - 2;
    return tanhLUT_[idx] + (tanhLUT_[idx + 1] - tanhLUT_[idx]) * frac;
  }

  /** Fast inverse square root approximation (Quake-style)
   * Good enough for soft-knee compression where precision isn't critical
   */
  inline float fastSqrt(float x) {
    // Fast inverse sqrt then invert - avoids division
    union { float f; uint32_t i; } conv = {x};
    conv.i = 0x5f3759df - (conv.i >> 1);
    float y = conv.f;
    y = y * (1.5f - (0.5f * x * y * y));  // One Newton iteration
    return x * y;  // x * (1/sqrt(x)) = sqrt(x)
  }

  /** Soft-knee tanh - reduces aliasing by gentler saturation
   * Applies soft compression before tanh to reduce harmonic generation.
   */
  inline float softTanh(float x) {
    float ax = fabsf(x);
    if (ax <= softKnee_) {
      return tanhLookup(x);
    }
    // Above knee: compress input to reduce harmonic generation
    float excess = ax - softKnee_;
    float compressed = softKnee_ + fastSqrt(excess * 0.5f);
    return (x >= 0.0f) ? tanhLookup(compressed) : tanhLookup(-compressed);
  }

  /** Initialize filter parameters */
  void init(float sample_rate) {
    (void)sample_rate;
    alpha_ = 1.0f;
    K_ = 1.0f;
    Qadjust_ = 1.0f;
    KQ_ = K_ * Qadjust_;
    pbg_ = 0.5f;
    oldinput_ = 0.0f;
    setFreq(5000.f);
    setRes(0.2f);
  }

  /** Compute filter coefficients for given frequency */
  void compute_coeffs(float freq) {
    freq = max(5.0f, min((float)SAMPLE_RATE * 0.425f, freq));
    float wc = freq * (2.0f * 3.1415927410125732f / ((float)kInterpolation * SAMPLE_RATE));
    float wc2 = wc * wc;
    alpha_ = 0.9892f * wc - 0.4324f * wc2 + 0.1381f * wc * wc2 - 0.0202f * wc2 * wc2;
    Qadjust_ = 1.006f + 0.0536f * wc - 0.095f * wc2 - 0.05f * wc2 * wc2;
    KQ_ = K_ * Qadjust_;
  }
};

#endif /* BOB_H_ */
