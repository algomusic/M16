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
#if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
#include <atomic>
#endif

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
    // Dual-core protection: only one core processes the filter at a time.
    // If the other core holds the lock, return the previous output immediately.
    // This avoids spin-waiting and produces a 1-sample hold — inaudible at 44.1kHz.
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    if (_threadSafe) {
      bool expected = false;
      if (!_bobLock.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
        return prevOutput_;
      }
    }
    #endif

    const float input = (float)clip16(samp) * MAX_16_INV;

    // Cache coefficients atomically to prevent race conditions with setFreq()/setRes()
    // This ensures we use a consistent set of coefficients for the entire sample
    float cached_alpha, cached_KQ, cached_pbg;
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    uint32_t tmp;
    tmp = __atomic_load_n((uint32_t*)&alpha_, __ATOMIC_RELAXED);
    cached_alpha = *(float*)&tmp;
    tmp = __atomic_load_n((uint32_t*)&KQ_, __ATOMIC_RELAXED);
    cached_KQ = *(float*)&tmp;
    tmp = __atomic_load_n((uint32_t*)&pbg_, __ATOMIC_RELAXED);
    cached_pbg = *(float*)&tmp;
    #else
    cached_alpha = alpha_;
    cached_KQ = KQ_;
    cached_pbg = pbg_;
    #endif

    // Local copies of state for faster access
    float z0_0 = z0_[0], z0_1 = z0_[1], z0_2 = z0_[2], z0_3 = z0_[3];
    float z1_0 = z1_[0], z1_1 = z1_[1], z1_2 = z1_[2], z1_3 = z1_[3];

    const float pbg_in = cached_pbg * input;
    float ft3_sum;

    // First sub-sample (interp = 0, so inMix = input)
    {
      float u = input - (z1_3 - pbg_in) * cached_KQ;
      u = softTanh(u);

      float ft0 = u * a_ + z0_0 * b_ - z1_0;
      ft0 = ft0 * cached_alpha + z1_0;
      z1_0 = ft0; z0_0 = u;

      float ft1 = ft0 * a_ + z0_1 * b_ - z1_1;
      ft1 = ft1 * cached_alpha + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      float ft2 = ft1 * a_ + z0_2 * b_ - z1_2;
      ft2 = ft2 * cached_alpha + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      float ft3 = ft2 * a_ + z0_3 * b_ - z1_3;
      ft3 = ft3 * cached_alpha + z1_3;
      z1_3 = ft3; z0_3 = ft2;

      ft3_sum = ft3;
    }

    // Second sub-sample (interp = 0.5, so inMix = average of old and new input)
    {
      float inMix = (oldinput_ + input) * 0.5f;
      float u = inMix - (z1_3 - pbg_in) * cached_KQ;
      u = softTanh(u);

      float ft0 = u * a_ + z0_0 * b_ - z1_0;
      ft0 = ft0 * cached_alpha + z1_0;
      z1_0 = ft0; z0_0 = u;

      float ft1 = ft0 * a_ + z0_1 * b_ - z1_1;
      ft1 = ft1 * cached_alpha + z1_1;
      z1_1 = ft1; z0_1 = ft0;

      float ft2 = ft1 * a_ + z0_2 * b_ - z1_2;
      ft2 = ft2 * cached_alpha + z1_2;
      z1_2 = ft2; z0_2 = ft1;

      float ft3 = ft2 * a_ + z0_3 * b_ - z1_3;
      ft3 = ft3 * cached_alpha + z1_3;
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
    prevOutput_ = (int16_t)outi;

    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    if (_threadSafe) _bobLock.store(false, std::memory_order_release);
    #endif

    return prevOutput_;
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

  /** Keyboard tracking: 0 disables tracking; +/-1 follows/inverts octaves.
   * MIDI note 69 (440 Hz) leaves the base cutoff unchanged. Pass an integer
   * MIDI note or a floating-point frequency in Hz. Call on the control thread.
   */
  void setPitchTrackingAmount(float amount) { if (setTrackingAmount(amount)) applyPitchTracking(); }
  void setPitchTracking(int pitch) { if (setTrackingPitch(pitch)) applyPitchTracking(); }
  void setPitchTracking(float frequency) { if (setTrackingFrequency(frequency)) applyPitchTracking(); }
  void setPitchTracking(double frequency) { setPitchTracking((float)frequency); }
  float getPitchTrackingAmount() const { return _trackingAmount; }
  float getPitchTrackingMultiplier() const { return _trackingMultiplier; }
  float getPitchTrackedFrequency() const { return trackedFrequency(); }
  // Advanced modulation graphs can consume the tracked value without changing
  // coefficients here. Re-enable automatic application for ordinary use.
  void setPitchTrackingAutoApply(bool enabled) { _pitchTrackingAutoApply = enabled; if (enabled) applyPitchTracking(); }
  void setPitchTrackingBaseFrequency(float frequency) {
    if (!isfinite(frequency) || frequency < 0.0f) return;
    setTrackingBase(frequency);
    _pitchTrackingUsesNormalised = false;
    applyPitchTracking();
  }

  /** Set cutoff frequency in Hz
   * @param freq Frequency in Hz (5-20000)
   */
  void setFreq(float freq) {
    if (!_pitchTrackingApplying) {
      _pitchTrackingUsesNormalised = false;
      setTrackingBase(max(0.0f, freq));
      if (_pitchTrackingAutoApply) freq = trackedFrequency();
    }
    // Reduce effect above 5kHz to limit aliasing
    if (freq > 5000.0f) {
      freq = 5000.0f + (freq - 5000.0f) * 0.4f;
    }
    freq = max(5.0f, min(20000.0f, freq));
    // Ignore changes smaller than 8 steps on a 15-bit frequency scale. This
    // prevents control-rate modulation from needlessly rebuilding coefficients.
    const float minimumCutoffChange =
        ((float)SAMPLE_RATE * 0.425f) * (8.0f / 32768.0f);
    if (fabsf(freq - Fbase_) < minimumCutoffChange) return;
    Fbase_ = freq;
    compute_coeffs(Fbase_);
    KQ_ = K_ * Qadjust_;
  }

  /** Set cutoff as normalized value with cubic mapping
   * @param cutoff_val 0.0-1.0
   */
  void setNormalisedCutoff(float cutoff_val) {
    _normalisedCutoff = max(0.0f, min(1.0f, cutoff_val));
    const float base = 20000.0f * _normalisedCutoff * _normalisedCutoff * _normalisedCutoff;
    setFreq(_pitchTrackingApplying && _pitchTrackingAutoApply
        ? trackedFrequency() : base);
    if (!_pitchTrackingApplying) _pitchTrackingUsesNormalised = true;
  }

  /** Alias for setNormalisedCutoff for backwards compatibility */
  inline void setCutoff(float cutoff_val) { setNormalisedCutoff(cutoff_val); }

  /** @return Current normalised cutoff frequency (0.0-1.0) */
  float getNormalisedCutoff() const { return _normalisedCutoff; }

  /** Alias for getNormalisedCutoff for backwards compatibility */
  inline float getCutoff() const { return getNormalisedCutoff(); }

  /** Enable cross-core protection for next(). Enabled by default.
   * Set false only when one fixed audio partition owns this Bob instance.
   * Parameter setters remain safe because their coefficients are read atomically.
   */
  void setThreadSafe(bool enable) { _threadSafe = enable; }

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
  // Pitch tracking is control-thread state, local to this filter.
  float _trackingBase = 0.0f;
  float _trackingAmount = 0.0f;
  float _trackingMidiPitch = 69.0f;
  float _trackingMultiplier = 1.0f;
  void setTrackingBase(float hz) {
    if (isfinite(hz) && hz >= 0.0f) _trackingBase = hz;
  }
  bool setTrackingAmount(float value) {
    if (!isfinite(value)) return false;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    if (_trackingAmount == value) return false;
    _trackingAmount = value;
    updateTrackingMultiplier();
    return true;
  }
  bool setTrackingPitch(int pitch) {
    if (pitch < 0) pitch = 0;
    if (pitch > 127) pitch = 127;
    if (_trackingMidiPitch == (float)pitch) return false;
    _trackingMidiPitch = (float)pitch;
    updateTrackingMultiplier();
    return true;
  }
  bool setTrackingFrequency(float hz) {
    if (!isfinite(hz) || hz <= 0.0f) return false;
    float pitch = 69.0f + 12.0f * (log2f(hz) - log2f(440.0f));
    if (_trackingMidiPitch == pitch) return false;
    _trackingMidiPitch = pitch;
    updateTrackingMultiplier();
    return true;
  }
  float trackedFrequency() const {
    float result = _trackingBase * _trackingMultiplier;
    return isfinite(result) ? result : 3.402823466e38f;
  }
  void updateTrackingMultiplier() {
    float exponent = (_trackingMidiPitch - 69.0f) * _trackingAmount * (1.0f / 12.0f);
    if (exponent > 100.0f) exponent = 100.0f;
    if (exponent < -100.0f) exponent = -100.0f;
    _trackingMultiplier = _trackingAmount == 0.0f ? 1.0f : exp2f(exponent);
  }
  bool _pitchTrackingAutoApply = true;
  bool _pitchTrackingApplying = false;
  bool _pitchTrackingUsesNormalised = false;
  void applyPitchTracking() {
    if (!_pitchTrackingAutoApply) return;
    _pitchTrackingApplying = true;
    if (_pitchTrackingUsesNormalised) setNormalisedCutoff(_normalisedCutoff);
    else setFreq(trackedFrequency());
    _pitchTrackingApplying = false;
  }

  #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  std::atomic<bool> _bobLock{false};
  #endif
  bool _threadSafe = true;
  int16_t prevOutput_ = 0;

  static const uint8_t kInterpolation = 2;
  static const int LUT_SIZE = 1024;
  static constexpr float LUT_RANGE = 4.0f;
  static constexpr float LUT_SCALE = (LUT_SIZE - 1) / (2.0f * LUT_RANGE);  // Precomputed: 127.875

  float alpha_;
  float z0_[4] = {0,0,0,0};
  float z1_[4] = {0,0,0,0};
  float K_;
  float Qadjust_;
  float KQ_;
  float pbg_;
  float oldinput_;
  float Fbase_ = 0.0f;
  float _normalisedCutoff = 0.0f;  // Stored normalized cutoff (0.0-1.0)
  int32_t ampComp = (int32_t)(MAX_16 * 1.4f);
  float ampCompHalf_ = ampComp * 0.5f;  // Precomputed for output scaling
  static constexpr float a_ = 1.0f / 1.3f;   // 0.769230769f - filter coefficient
  static constexpr float b_ = 0.3f / 1.3f;   // 0.230769231f - filter coefficient
  static constexpr float softKnee_ = 1.5f;   // Soft-knee threshold for tanh saturation

  /** Initialize tanh lookup table */
  void initTanhLUT() {
    float* table = sharedTanhLUT();
    bool& initialized = sharedTanhLUTInitialized();
    if (initialized) return;
    for (int i = 0; i < LUT_SIZE; i++) {
      float x = ((float)i / (LUT_SIZE - 1)) * 2.0f * LUT_RANGE - LUT_RANGE;
      table[i] = tanhf(x);
    }
    initialized = true;
  }

  static float* sharedTanhLUT() {
    static float table[LUT_SIZE];
    return table;
  }

  static bool& sharedTanhLUTInitialized() {
    static bool initialized = false;
    return initialized;
  }

  /** Lookup tanh with linear interpolation */
  inline float tanhLookup(float x) {
    const float* tanhLUT = sharedTanhLUT();
    if (x >= LUT_RANGE) return 1.0f;
    if (x <= -LUT_RANGE) return -1.0f;
    float indexF = (x + LUT_RANGE) * LUT_SCALE;  // Multiplication instead of division
    int idx = (int)indexF;
    float frac = indexF - idx;
    if (idx < 0) idx = 0;
    if (idx >= LUT_SIZE - 1) idx = LUT_SIZE - 2;
    return tanhLUT[idx] + (tanhLUT[idx + 1] - tanhLUT[idx]) * frac;
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
