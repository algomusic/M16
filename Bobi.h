/*
 * Bobi.h
 *
 * Integer-optimized, drop-in alternative to Bob.h's Moog ladder lowpass.
 * The audio-rate path uses fixed-point state, coefficients and nonlinear
 * lookup; floating point is retained only for infrequent parameter updates
 * and one-time lookup-table generation.
 *
 * by Andrew R. Brown 2026
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef BOBI_H_
#define BOBI_H_

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  #include <atomic>
#endif

/** A fixed-point version of Bob with the same public control API.
 *
 * Bobi preserves Bob's two-times oversampled four-pole ladder, resonance
 * mapping, cutoff mapping, amplitude compensation and soft-knee tanh curve.
 * Existing code can normally change only `#include "Bob.h"` to
 * `#include "Bobi.h"` and `Bob` to `Bobi`.
 */
class Bobi {
public:
  Bobi() {
    transferTable_ = softTanhTable();  // Build once, outside audio callback.
    init();
  }

  /** Process one sample through the four-pole lowpass. */
  inline int16_t next(int32_t samp) {
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    if (_threadSafe) {
      bool expected = false;
      if (!_bobiLock.compare_exchange_strong(expected, true,
                                             std::memory_order_acquire)) {
        return prevOutput_;
      }
    }
    #endif

    const int32_t input = (int32_t)clip16(samp) * INPUT_TO_STATE_SCALE;
    const int32_t alpha = loadPublished(alphaQ_);
    const int32_t resonance = loadPublished(kqQ_);

    int32_t z0_0 = z0_[0], z0_1 = z0_[1], z0_2 = z0_[2], z0_3 = z0_[3];
    int32_t z1_0 = z1_[0], z1_1 = z1_[1], z1_2 = z1_[2], z1_3 = z1_[3];
    const int32_t pbgInput = input >> 1;

    // First sub-sample: current input.
    int64_t feedback = multiplyQ28(z1_3 - pbgInput, resonance);
    int32_t u = softTanhQ28((int64_t)input - feedback);
    int32_t ft0 = ladderStage(u, z0_0, z1_0, alpha);
    z0_0 = u;   z1_0 = ft0;
    int32_t ft1 = ladderStage(ft0, z0_1, z1_1, alpha);
    z0_1 = ft0; z1_1 = ft1;
    int32_t ft2 = ladderStage(ft1, z0_2, z1_2, alpha);
    z0_2 = ft1; z1_2 = ft2;
    int32_t ft3 = ladderStage(ft2, z0_3, z1_3, alpha);
    z0_3 = ft2; z1_3 = ft3;
    int32_t outputSum = ft3;

    // Second sub-sample: halfway between the previous and current inputs.
    const int32_t inputMid = (oldInput_ + input) >> 1;
    feedback = multiplyQ28(z1_3 - pbgInput, resonance);
    u = softTanhQ28((int64_t)inputMid - feedback);
    ft0 = ladderStage(u, z0_0, z1_0, alpha);
    z0_0 = u;   z1_0 = ft0;
    ft1 = ladderStage(ft0, z0_1, z1_1, alpha);
    z0_1 = ft0; z1_1 = ft1;
    ft2 = ladderStage(ft1, z0_2, z1_2, alpha);
    z0_2 = ft1; z1_2 = ft2;
    ft3 = ladderStage(ft2, z0_3, z1_3, alpha);
    z0_3 = ft2; z1_3 = ft3;
    outputSum += ft3;

    z0_[0] = clampState(z0_0); z0_[1] = clampState(z0_1);
    z0_[2] = clampState(z0_2); z0_[3] = clampState(z0_3);
    z1_[0] = clampState(z1_0); z1_[1] = clampState(z1_1);
    z1_[2] = clampState(z1_2); z1_[3] = clampState(z1_3);
    oldInput_ = input;

    // Average the two sub-samples and apply Bob's 1.4 output compensation.
    // Cast before multiplying: outputSum * AMP_COMP exceeds int32_t even at
    // ordinary signal levels. Casting only the destination would overflow
    // before assignment and reduce the output to a few sample units.
    int64_t scaled = (int64_t)outputSum * AMP_COMP;
    scaled += (int64_t)1 << AMP_OUTPUT_SHIFT_MINUS_ONE;
    int32_t output = (int32_t)(scaled >> AMP_OUTPUT_SHIFT);
    prevOutput_ = clip16(output);

    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    if (_threadSafe) _bobiLock.store(false, std::memory_order_release);
    #endif
    return prevOutput_;
  }

  /** Alias matching Bob and SVF-style filter code. */
  inline int16_t nextLPF(int32_t samp) { return next(samp); }

  /** Set resonance from 0.0 to 1.0, using Bob's square-root response. */
  void setRes(float res) {
    if (res <= 0.0f) res = 0.0f;
    else if (res >= 1.0f) res = 1.0f;
    resonance_ = res;
    if (res > 0.0f && res < 1.0f) res = sqrtf(res);
    resonanceGain_ = 5.0f * res;
    publishKQ();
  }
  float getRes() const { return resonance_; }

  /** Set cutoff frequency in Hz, using Bob's high-frequency compression. */
  void setFreq(float freq) {
    if (!_pitchTrackingApplying) {
      _pitchTrackingUsesNormalised = false;
      setTrackingBase(max(0.0f, freq));
      if (_pitchTrackingAutoApply) freq = trackedFrequency();
    }
    if (freq > 5000.0f) freq = 5000.0f + (freq - 5000.0f) * 0.4f;
    freq = max(5.0f, min(20000.0f, freq));
    const float minimumCutoffChange =
        ((float)SAMPLE_RATE * 0.425f) * (8.0f / 32768.0f);
    if (fabsf(freq - Fbase_) < minimumCutoffChange) return;
    Fbase_ = freq;
    computeCoefficients(freq);
  }

  /** Set cutoff from 0.0 to 1.0 using Bob's cubic mapping. */
  void setNormalisedCutoff(float cutoff) {
    _normalisedCutoff = max(0.0f, min(1.0f, cutoff));
    const float base = 20000.0f * _normalisedCutoff * _normalisedCutoff *
                       _normalisedCutoff;
    if (_pitchTrackingApplying) {
      setFreq(base * _trackingMultiplier);
    } else if (_pitchTrackingAutoApply) {
      _pitchTrackingApplying = true;
      setFreq(base * _trackingMultiplier);
      _pitchTrackingApplying = false;
      _pitchTrackingUsesNormalised = true;
    } else {
      setFreq(base);
      _pitchTrackingUsesNormalised = true;
    }
  }

  inline void setCutoff(float cutoff) { setNormalisedCutoff(cutoff); }
  float getNormalisedCutoff() const { return _normalisedCutoff; }
  inline float getCutoff() const { return getNormalisedCutoff(); }

  /** Keyboard tracking: 0 disables tracking; +/-1 follows/inverts octaves.
   * MIDI note 69 (440 Hz) leaves the base cutoff unchanged.
   */
  void setPitchTrackingAmount(float amount) { if (setTrackingAmount(amount)) applyPitchTracking(); }
  void setPitchTracking(int pitch) { if (setTrackingPitch(pitch)) applyPitchTracking(); }
  void setPitchTracking(float frequency) { if (setTrackingFrequency(frequency)) applyPitchTracking(); }
  void setPitchTracking(double frequency) { setPitchTracking((float)frequency); }
  float getPitchTrackingAmount() const { return _trackingAmount; }
  float getPitchTrackingMultiplier() const { return _trackingMultiplier; }
  float getPitchTrackedFrequency() const { return trackedFrequency(); }
  void setPitchTrackingAutoApply(bool enabled) { _pitchTrackingAutoApply = enabled; if (enabled) applyPitchTracking(); }
  void setPitchTrackingBaseFrequency(float frequency) {
    if (!isfinite(frequency) || frequency < 0.0f) return;
    setTrackingBase(frequency);
    _pitchTrackingUsesNormalised = false;
    applyPitchTracking();
  }

  /** Enable shared-instance protection. Enabled by default. */
  void setThreadSafe(bool enable) { _threadSafe = enable; }

  float getFreq() const { return Fbase_; }

  /** Clear the filter history. */
  void reset() {
    for (int i = 0; i < 4; ++i) {
      z0_[i] = 0;
      z1_[i] = 0;
    }
    oldInput_ = 0;
    prevOutput_ = 0;
  }

private:
  // Q4.28 state: enough fractional precision for low cutoff frequencies and
  // enough headroom for Bob's explicit +/-4 state clamp.
  static constexpr int STATE_FRAC_BITS = 28;
  static constexpr int COEFF_FRAC_BITS = 30;
  static constexpr int INPUT_TO_STATE_SHIFT = STATE_FRAC_BITS - 15;
  static constexpr int32_t INPUT_TO_STATE_SCALE =
      (int32_t)1 << INPUT_TO_STATE_SHIFT;
  static constexpr int32_t STATE_ONE = (int32_t)1 << STATE_FRAC_BITS;
  static constexpr int32_t STATE_LIMIT = (int32_t)4 << STATE_FRAC_BITS;

  // The complete soft-knee/tanh transfer spans +/-16 in 1/64 steps. This
  // removes sqrt and floating-point LUT interpolation from the sample loop.
  static constexpr int TRANSFER_INTERVALS = 2048;
  static constexpr int TRANSFER_TABLE_SIZE = TRANSFER_INTERVALS + 1;
  static constexpr int TRANSFER_STEP_SHIFT = STATE_FRAC_BITS - 6;
  static constexpr int32_t TRANSFER_FRACTION_MASK =
      ((int32_t)1 << TRANSFER_STEP_SHIFT) - 1;
  static constexpr int64_t TRANSFER_LIMIT = (int64_t)16 << STATE_FRAC_BITS;

  static constexpr int32_t B_Q30 = 247786574;  // (0.3 / 1.3) * 2^30
  static constexpr int32_t AMP_COMP = (int32_t)(32767.0f * 1.4f);
  // output = two-sample Q28 sum * ampComp / (2 * 2^28).
  static constexpr int AMP_OUTPUT_SHIFT = STATE_FRAC_BITS + 1;
  static constexpr int AMP_OUTPUT_SHIFT_MINUS_ONE = AMP_OUTPUT_SHIFT - 1;

  #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  std::atomic<bool> _bobiLock{false};
  #endif
  bool _threadSafe = true;
  int16_t prevOutput_ = 0;

  int32_t alphaQ_ = 0;
  int32_t kqQ_ = 0;
  int32_t z0_[4] = {0, 0, 0, 0};
  int32_t z1_[4] = {0, 0, 0, 0};
  int32_t oldInput_ = 0;
  const int16_t* transferTable_ = nullptr;

  float resonanceGain_ = 1.0f;
  float resonance_ = 0.0f;
  float qAdjust_ = 1.0f;
  float Fbase_ = 5000.0f;
  float _normalisedCutoff = 0.0f;
  float _trackingBase = 0.0f;
  float _trackingAmount = 0.0f;
  float _trackingMidiPitch = 69.0f;
  float _trackingMultiplier = 1.0f;
  bool _pitchTrackingAutoApply = true;
  bool _pitchTrackingApplying = false;
  bool _pitchTrackingUsesNormalised = false;

  void setTrackingBase(float hz) { if (isfinite(hz) && hz >= 0.0f) _trackingBase = hz; }
  bool setTrackingAmount(float value) {
    if (!isfinite(value)) return false;
    value = max(-1.0f, min(1.0f, value));
    if (_trackingAmount == value) return false;
    _trackingAmount = value;
    updateTrackingMultiplier();
    return true;
  }
  bool setTrackingPitch(int pitch) {
    pitch = max(0, min(127, pitch));
    if (_trackingMidiPitch == (float)pitch) return false;
    _trackingMidiPitch = (float)pitch;
    updateTrackingMultiplier();
    return true;
  }
  bool setTrackingFrequency(float hz) {
    if (!isfinite(hz) || hz <= 0.0f) return false;
    const float pitch = 69.0f + 12.0f * (log2f(hz) - log2f(440.0f));
    if (_trackingMidiPitch == pitch) return false;
    _trackingMidiPitch = pitch;
    updateTrackingMultiplier();
    return true;
  }
  float trackedFrequency() const {
    const float result = _trackingBase * _trackingMultiplier;
    return isfinite(result) ? result : 3.402823466e38f;
  }
  void updateTrackingMultiplier() {
    float exponent = (_trackingMidiPitch - 69.0f) * _trackingAmount * (1.0f / 12.0f);
    exponent = max(-100.0f, min(100.0f, exponent));
    _trackingMultiplier = _trackingAmount == 0.0f ? 1.0f : exp2f(exponent);
  }
  void applyPitchTracking() {
    if (!_pitchTrackingAutoApply) return;
    _pitchTrackingApplying = true;
    if (_pitchTrackingUsesNormalised) setNormalisedCutoff(_normalisedCutoff);
    else setFreq(trackedFrequency());
    _pitchTrackingApplying = false;
  }

  static inline int32_t loadPublished(const int32_t& value) {
    return __atomic_load_n(&value, __ATOMIC_ACQUIRE);
  }

  static inline void publish(int32_t& destination, int32_t value) {
    __atomic_store_n(&destination, value, __ATOMIC_RELEASE);
  }

  static inline int64_t roundedShift(int64_t product, int bits) {
    return (product + ((int64_t)1 << (bits - 1))) >> bits;
  }

  static inline __attribute__((always_inline))
  int32_t multiplyQ30(int32_t valueQ28, int32_t coeffQ30) {
    return (int32_t)roundedShift((int64_t)valueQ28 * coeffQ30,
                                 COEFF_FRAC_BITS);
  }

  static inline __attribute__((always_inline))
  int64_t multiplyQ28(int32_t valueQ28, int32_t coeffQ28) {
    return roundedShift((int64_t)valueQ28 * coeffQ28, STATE_FRAC_BITS);
  }

  /** One ladder pole. Since a+b=1, s*a+z0*b becomes s+b*(z0-s),
   * reducing the hot path from three multiplications to two per stage. */
  static inline __attribute__((always_inline))
  int32_t ladderStage(int32_t signal, int32_t previousInput,
                      int32_t previousOutput, int32_t alpha) {
    int32_t shaped = signal +
        multiplyQ30(previousInput - signal, B_Q30);
    return previousOutput +
           multiplyQ30(shaped - previousOutput, alpha);
  }

  static inline int32_t clampState(int32_t value) {
    if (value > STATE_LIMIT) return STATE_LIMIT;
    if (value < -STATE_LIMIT) return -STATE_LIMIT;
    return value;
  }

  static float startupFastSqrt(float x) {
    union { float f; uint32_t i; } conv = {x};
    conv.i = 0x5f3759dfU - (conv.i >> 1);
    float inverse = conv.f;
    inverse = inverse * (1.5f - 0.5f * x * inverse * inverse);
    return x * inverse;
  }

  struct TransferTable {
    // Q1.15 is sufficient for a 16-bit audio nonlinearity and halves the
    // table footprint/cache traffic compared with Q4.28 entries.
    int16_t sample[TRANSFER_TABLE_SIZE];

    TransferTable() {
      for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i) {
        float x = (float)(i - TRANSFER_INTERVALS / 2) * (1.0f / 64.0f);
        float magnitude = fabsf(x);
        float shaped = x;
        if (magnitude > 1.5f) {
          float compressed = 1.5f + startupFastSqrt((magnitude - 1.5f) * 0.5f);
          shaped = (x >= 0.0f) ? compressed : -compressed;
        }
        float output = tanhf(shaped) * 32767.0f;
        sample[i] = (int16_t)(output + ((output >= 0.0f) ? 0.5f : -0.5f));
      }
    }
  };

  static const int16_t* softTanhTable() {
    static TransferTable table;
    return table.sample;
  }

  inline __attribute__((always_inline)) int32_t softTanhQ28(int64_t input) {
    const int16_t* table = transferTable_;
    if (input >= TRANSFER_LIMIT) {
      return (int32_t)table[TRANSFER_INTERVALS] * INPUT_TO_STATE_SCALE;
    }
    if (input <= -TRANSFER_LIMIT) {
      return (int32_t)table[0] * INPUT_TO_STATE_SCALE;
    }

    uint64_t position = (uint64_t)(input + TRANSFER_LIMIT);
    uint32_t index = (uint32_t)(position >> TRANSFER_STEP_SHIFT);
    uint32_t fraction = (uint32_t)position & TRANSFER_FRACTION_MASK;
    int32_t first = (int32_t)table[index];
    int32_t difference = table[index + 1] - first;
    int32_t interpolated = first +
        (int32_t)(((int64_t)difference * fraction +
                   ((int64_t)1 << (TRANSFER_STEP_SHIFT - 1))) >>
                  TRANSFER_STEP_SHIFT);
    return interpolated * INPUT_TO_STATE_SCALE;
  }

  void init() {
    resonanceGain_ = 1.0f;
    qAdjust_ = 1.0f;
    publish(alphaQ_, 1 << COEFF_FRAC_BITS);
    publishKQ();
    setFreq(5000.0f);
    setRes(0.2f);
  }

  void publishKQ() {
    float kq = resonanceGain_ * qAdjust_;
    publish(kqQ_, (int32_t)(kq * (float)STATE_ONE + 0.5f));
  }

  void computeCoefficients(float freq) {
    freq = max(5.0f, min((float)SAMPLE_RATE * 0.425f, freq));
    const float wc = freq *
        (2.0f * 3.1415927410125732f / (2.0f * (float)SAMPLE_RATE));
    const float wc2 = wc * wc;
    const float alpha = 0.9892f * wc - 0.4324f * wc2 +
                        0.1381f * wc * wc2 - 0.0202f * wc2 * wc2;
    const float newQAdjust = 1.006f + 0.0536f * wc - 0.095f * wc2 -
               0.05f * wc2 * wc2;
    const int32_t newAlphaQ = (int32_t)(alpha * (float)((int64_t)1 << COEFF_FRAC_BITS) +
                                        0.5f);
    const int32_t currentAlphaQ = loadPublished(alphaQ_);
    if (abs(newAlphaQ - currentAlphaQ) < (8 << 15)) return;
    qAdjust_ = newQAdjust;
    publish(alphaQ_, newAlphaQ);
    publishKQ();
  }
};

#endif /* BOBI_H_ */
