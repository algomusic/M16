/*
 * Bob.h
 *
 * A Moog Ladder 4 pole lowpass filter implementation
 * This filter is about 4x more CPU intensive than the SVF
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

class Bob {

  public:
    /** Constructor. */
    Bob() {
      init(SAMPLE_RATE);
      setRes(0.2f);
    }

    int16_t next(int16_t samp) {
      float  input = samp * MAX_16_INV;
      float total = 0.0f;
      float interp = 0.0f;
      for (size_t os = 0; os < kInterpolation; os++)
      {
          float u = (interp * oldinput_ + (1.0f - interp) * input)
              - (z1_[3] - pbg_ * input) * K_ * Qadjust_;
          u = fast_tanh(u);
          float stage1 = LPF(u, 0);
          float stage2 = LPF(stage1, 1);
          float stage3 = LPF(stage2, 2);
          float stage4 = LPF(stage3, 3);
          total += stage4 * kInterpolationRecip;
          interp += kInterpolationRecip;
      }
      oldinput_ = input;
      return max(MIN_16, min(MAX_16, (int)(total * ampComp)));
    }

    void setRes(float res) {
      // maps resonance = 0->1 to K = 0 -> 5
      res = max(0.0, min(1.0, pow(res, 0.5)));
      K_ = 5.0f * res;
    }

    void setFreq(float freq) {    
      if (freq > 5000) {
        freq = 5000 + (freq - 5000) * 0.4;
      }
      freq = max(5.0f, min(20000.0f,freq));
      Fbase_ = freq;
      compute_coeffs(Fbase_);
    }

    float getFreq() {
      return Fbase_;
    }

  private:
    float PI_F = 3.1415927410125732421875f;
    static const uint8_t kInterpolation = 2;
    static constexpr float kInterpolationRecip = 1.0f / kInterpolation;
    static constexpr float kMaxResonance = 1.8f;

    float sample_rate_;
    float alpha_;
    float beta_[4] = {0.0, 0.0, 0.0, 0.0};
    float z0_[4] = {0.0, 0.0, 0.0, 0.0};
    float z1_[4] = {0.0, 0.0, 0.0, 0.0};
    float K_; // 0 - 4
    float Fbase_;
    float Qadjust_;
    float pbg_;
    float oldinput_;
    int32_t ampComp = MAX_16 * 1.4;

    static inline float fast_tanh(float x) {
      if (x > 3.0f) return 1.0f;
      if (x < -3.0f) return -1.0f;
      float x2 = x * x;
      return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    void init(float sample_rate) {
      sample_rate_ = sample_rate;
      alpha_       = 1.0f;
      K_           = 1.0f;
      Fbase_       = 1000.0f;
      Qadjust_     = 1.0f;
      pbg_         = 0.5f;
      oldinput_    = 0.f;

      setFreq(5000.f);
      setRes(0.2f);
    }

    inline
    float LPF(float s, int i) {
      float ft = s * (1.0f/1.3f) + (0.3f/1.3f) * z0_[i] - z1_[i];
      ft = ft * alpha_ + z1_[i];
      z1_[i] = ft;
      z0_[i] = s;
      return ft;
    }

    void compute_coeffs(float freq) {
      freq = max(5.0f, min(sample_rate_ * 0.425f,freq));
      float wc = freq * (float)(2.0f * PI_F / ((float)kInterpolation * sample_rate_));
      float wc2 = wc * wc;
      alpha_ = 0.9892f * wc - 0.4324f * wc2 + 0.1381f * wc * wc2 - 0.0202f * wc2 * wc2;
      Qadjust_ = 1.006f + 0.0536f * wc - 0.095f * wc2 - 0.05f * wc2 * wc2;
    }

};

#endif /* BOB_H_ */