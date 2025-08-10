/*
 * Bob.h
 *
 * A Moog Ladder 4 pole lowpass filter implementation
 * This filter is about 4x more CPU intensive than SVF.h
 * and about 3x more expensive that SFV2.h
 *
 * by Andrew R. Brown 2023
 *
 * Inspired by Bob Moog
 * Ported from the moogLadder implementation by Nick Donaldson
 * which in turn is based on the DaisySP implemtation by Electrosmith LLC 
 * based on the version in SoundPipe by Paul Batchelor
 * based on the non-linear digital implementation by Antti Huovilainen
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

/*
 Optimised Bob filter 2025
 - Keeps floating point internal state and coefficient math to preserve resonant character.
 - Precomputes/combines constants used per sample.
 - Inlines the LPF 4-stage processing and avoids per-sample function calls.
 - Minimises array indexing by copying z0_/z1_ to local variables for the hot path then writing them back.
 - Uses fast_tanh polynomial.
 - Removes pow() usage in hot path; pow() still used only in setRes (replaced with sqrtf to match original).
*/

class Bob {
public:
  Bob() {
    init(SAMPLE_RATE);
    setRes(0.2f);
  }

  inline int16_t next(int32_t samp) {
    // Convert to float -1..1 range using original scale
    const float input = (float)clip16(samp) * MAX_16_INV;

    // Local copies of state to reduce memory accesses
    float z0_0 = z0_[0], z0_1 = z0_[1], z0_2 = z0_[2], z0_3 = z0_[3];
    float z1_0 = z1_[0], z1_1 = z1_[1], z1_2 = z1_[2], z1_3 = z1_[3];

    // Interpolation: we always use small fixed kInterpolation (usually 2),
    // so precompute the step and unroll the loop for speed.
    const float interpStep = 1.0f / (float)kInterpolation;
    float interp = 0.0f;
    float total = 0.0f;

    // Precompute terms that are constant inside the oversampling loop:
    // pbg_ * input used in multiple places; combined K * Qadjust used widely.
    const float pbg_in = pbg_ * input;
    const float KQ = K_ * Qadjust_;  // precombine K and Qadjust (same numeric behaviour)
    // Precompute LPF coefficients used in each stage
    const float a = 1.0f / 1.3f;
    const float b = 0.3f / 1.3f;

    // Unrolled oversampling (kInterpolation is small - 1..4 typical). Unroll for kInterpolation==2 common case.
#if (kInterpolation == 2)
    {
      // first sub-sample
      float inMix = interp * oldinput_ + (1.0f - interp) * input;
      float u = inMix - (z1_3 - pbg_in) * KQ;
      u = fast_tanh_poly(u);

      // LPF stage 0
      float ft0 = u * a + z0_0 * b - z1_0;
      ft0 = ft0 * alpha_ + z1_0;
      z1_0 = ft0; z0_0 = u;

      // stage 1
      float ft1 = ft0 * a + z0_1 * b - z1_1;
      ft1 = ft1 * alpha_ + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      // stage 2
      float ft2 = ft1 * a + z0_2 * b - z1_2;
      ft2 = ft2 * alpha_ + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      // stage 3
      float ft3 = ft2 * a + z0_3 * b - z1_3;
      ft3 = ft3 * alpha_ + z1_3;
      z1_3 = ft3; z0_3 = ft2;

      total += ft3 * interpStep;
      interp += interpStep;
    }

    {
      // second sub-sample
      float inMix = interp * oldinput_ + (1.0f - interp) * input;
      float u = inMix - (z1_3 - pbg_in) * KQ;
      u = fast_tanh_poly(u);

      float ft0 = u * a + z0_0 * b - z1_0;
      ft0 = ft0 * alpha_ + z1_0;
      z1_0 = ft0; z0_0 = u;

      float ft1 = ft0 * a + z0_1 * b - z1_1;
      ft1 = ft1 * alpha_ + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      float ft2 = ft1 * a + z0_2 * b - z1_2;
      ft2 = ft2 * alpha_ + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      float ft3 = ft2 * a + z0_3 * b - z1_3;
      ft3 = ft3 * alpha_ + z1_3;
      z1_3 = ft3; z0_3 = ft2;

      total += ft3 * interpStep;
      // interp += interpStep; // not needed at end
    }
#else
    // Generic loop for other interpolation values
    for (size_t os = 0; os < kInterpolation; ++os) {
      float inMix = interp * oldinput_ + (1.0f - interp) * input;
      float u = inMix - (z1_3 - pbg_in) * KQ;
      u = fast_tanh_poly(u);

      float ft0 = u * a + z0_0 * b - z1_0;
      ft0 = ft0 * alpha_ + z1_0;
      z1_0 = ft0; z0_0 = u;

      float ft1 = ft0 * a + z0_1 * b - z1_1;
      ft1 = ft1 * alpha_ + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      float ft2 = ft1 * a + z0_2 * b - z1_2;
      ft2 = ft2 * alpha_ + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      float ft3 = ft2 * a + z0_3 * b - z1_3;
      ft3 = ft3 * alpha_ + z1_3;
      z1_3 = ft3; z0_3 = ft2;

      total += ft3 * interpStep;
      interp += interpStep;
    }
#endif

    // write local state back to the object's arrays
    z0_[0] = z0_0; z0_[1] = z0_1; z0_[2] = z0_2; z0_[3] = z0_3;
    z1_[0] = z1_0; z1_[1] = z1_1; z1_[2] = z1_2; z1_[3] = z1_3;

    oldinput_ = input;

    // final output scaling and clipping (same as original)
    float outf = total * ampComp;
    // Convert to integer with clipping
    int32_t outi = (int32_t)outf;
    if (outi > MAX_16) outi = MAX_16;
    else if (outi < MIN_16) outi = MIN_16;
    return (int16_t)outi;
  }

  /** For compatability with SVF filter code. */
  inline int16_t nextLPF(int32_t samp) { return next(samp); }

  void setRes(float res) {
    // maps resonance = 0->1 to K = 0 -> 5
    if (res <= 0.0f) res = 0.0f;
    else if (res >= 1.0f) res = 1.0f;
    else res = sqrtf(res);             // exactly as original
    K_ = 5.0f * res;
    // update precomputed combined value
    KQ_ = K_ * Qadjust_;
  }

  void setFreq(float freq) {
    if (freq > 5000.0f) {
      freq = 5000.0f + (freq - 5000.0f) * 0.4f;
    }
    freq = max(5.0f, min(20000.0f, freq));
    Fbase_ = freq;
    compute_coeffs(Fbase_);
    // update precomputed combined K * Qadjust
    KQ_ = K_ * Qadjust_;
  }

  void setCutoff(float cutoff_val) {
    setFreq(20000.0f * cutoff_val * cutoff_val * cutoff_val); 
  }

  float getFreq() const { return Fbase_; }

private:
  static const uint8_t kInterpolation = 2;
  float alpha_;
  float z0_[4] = {0,0,0,0};
  float z1_[4] = {0,0,0,0};
  float K_;        // 0 - 5 
  float Qadjust_;  // computed in compute_coeffs
  float KQ_;       // precomputed K_ * Qadjust_ for inner loop
  float pbg_;
  float oldinput_;
  float Fbase_;
  int32_t ampComp = (int32_t)(MAX_16 * 1.4f);

  static inline float fast_tanh_poly(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

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

  void compute_coeffs(float freq) {
    // run only when freq set
    freq = max(5.0f, min((float)SAMPLE_RATE * 0.425f, freq));
    float wc = freq * (2.0f * 3.1415927410125732f / ((float)kInterpolation * SAMPLE_RATE));
    float wc2 = wc * wc;
    alpha_ = 0.9892f * wc - 0.4324f * wc2 + 0.1381f * wc * wc2 - 0.0202f * wc2 * wc2;
    Qadjust_ = 1.006f + 0.0536f * wc - 0.095f * wc2 - 0.05f * wc2 * wc2;
    // update combined multiplier used in inner loop
    KQ_ = K_ * Qadjust_;
  }
};

#endif /* BOB_H_ */