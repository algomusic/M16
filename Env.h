/*
 * Env.h
 *
 * An envelope class
 *
 * by Andrew R. Brown 2021
 *
 * Based on the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library. Include M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

/*
 * Timing model (2026): the envelope is a function of the global audio-frame
 * clock (audioFrameCount() in M16.h), NOT of micros() wall-clock time and NOT
 * of how often next() is called.
 *
 * Why: the audio task fills the DMA ring in bursts much faster than real time,
 * so within one DMA buffer micros() is essentially frozen — a micros()-based
 * envelope sampled per audio sample produces one value per DMA buffer (~11.6 ms
 * at 44.1 kHz), an audible "zipper" staircase. Conversely a per-next()-call
 * counter only keeps correct time if next() is called once per audio frame,
 * which breaks the established M16 pattern of advancing the envelope at control
 * rate in loop() and reading it per sample via getValue(). The audio-frame clock
 * advances once per output frame on every platform, so it is smooth at audio
 * rate AND correctly timed no matter how the envelope is driven.
 *
 * Both next() and getValue() evaluate the envelope at the current frame clock —
 * they are equivalent. The whole pre-release contour (attack/hold/decay/sustain
 * and the deterministic AD/AR releases when sustain == 0) is a pure function of
 * frames-since-start, so it is safe to evaluate concurrently from both audio
 * cores (single-voice sketches call getValue() on both cores). The only
 * asynchronous input is startRelease() (note-off for sustaining envelopes); it
 * publishes a release snapshot guarded by an atomic flag so the evaluator never
 * needs per-call mutable state.
 *
 * Durations are stored in milliseconds and converted to samples (using the live
 * SAMPLE_RATE) in start()/startRelease(), so setSampleRate() in setup() is
 * respected.
 */

#ifndef ENV_H_
#define ENV_H_

#if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
#include <atomic>
#endif

class Env {

  public:
    /** Constructor. */
    Env() {}

    /** Set envAttack time in ms. */
    void setAttack(float val) {
      if (val >= 0) envAttack = val; // ms
    }

    /** Get attack time in ms. */
    float getAttack() {
      return envAttack;
    }

    /** Set envHold time in ms. */
    void setHold(float val) {
      if (val >= 0) envHold = val;
    }

    /** Set envDecay time in ms.
     * Set to 0 to skip decay phase (useful for AR envelopes with sustain=0)
     */
    void setDecay(float val) {
      if (val >= 0) envDecay = val;
    }

    /** Set the number of times to repeat the decay segment.
    * Useful for clap sounds or guiro scrapes. Applies when sustain == 0.
    */
    void setDecayRepeats(int val) {
      if (val >= 0) {
        decayRepeats = val;
      }
    }

    /** Get decay time in ms. */
    int getDecay() {
      return (int)envDecay;
    }

    /** Set envSustain level as a value from 0.0 - 1.0 */
    void setSustain(float val) {
      if (val >= 0 && val <= 1) {
        envSustain = val;
        sustainLevel = val * MAX_ENV_LEVEL;
      }
    }

    /** Get sustain time, 0.0 to 1.0. */
    float getSustain() {
      return envSustain;
    }

    /** Set releaseState time in ms. */
    void setRelease(float val) {
      if (val >= 0) {
        envRelease = fmaxf(10.0f, val); // ms, min 10ms
      }
    }

    /** Get release time in ms. */
    float getRelease() {
      return envRelease;
    }

    /** Begin the current envelope */
    inline
    void start() {
      // Snapshot current level so attack interpolates from here (avoids held-then-jump on retrigger).
      // resetOnStart forces a 0 start for consistent drum attacks.
      uint16_t startLevel;
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      if (resetOnStart) envVal.store(0, std::memory_order_relaxed);
      startLevel = resetOnStart ? 0 : envVal.load(std::memory_order_relaxed);
      #else
      if (resetOnStart) envVal = 0;
      startLevel = envVal;
      #endif
      attackStartLevel = startLevel;

      // Jittered peak level and release time, mirroring historical behaviour.
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (audioRand(MAX_ENV_LEVEL * 0.05));

      // Convert ms durations to samples using the live sample rate.
      const float samplesPerMs = SAMPLE_RATE * 0.001f;
      attackSamples = envAttack * samplesPerMs;
      invAttackSamples = (attackSamples > 0.0f) ? (1.0f / attackSamples) : 0.0f;
      holdSamples = envHold * samplesPerMs;
      decaySamples = envDecay * samplesPerMs;
      invDecaySamples = (decaySamples > 0.0f) ? (1.0f / decaySamples) : 0.0f;
      float jitReleaseMs = envRelease + audioRand((int)(envRelease * 0.05f));
      releaseSamples = jitReleaseMs * samplesPerMs;
      invReleaseSamples = (releaseSamples > 0.0f) ? (1.0f / releaseSamples) : 0.0f;

      // Anchor the per-note clock to the current audio frame and clear any
      // pending note-off. Publish the release flag with release ordering so the
      // audio cores see startFrame/params before they see "not released".
      startFrame = audioFrameCount();
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      releaseTriggered.store(false, std::memory_order_relaxed);
      envState.store(1, std::memory_order_release); // attack
      #else
      releaseTriggered = false;
      envState = 1; // attack
      #endif
    }

    /** Enable/disable resetting envelope value to 0 on start()
     * Useful for drum sounds where consistent attack transients are important
     * @param enable true to reset to 0, false (default) to allow retrigger from current level
     */
    void setResetOnStart(bool enable) {
      resetOnStart = enable;
    }

    /** Get resetOnStart setting */
    bool getResetOnStart() {
      return resetOnStart;
    }

    /** Return the audio frame at which the current note started. */
    inline
    unsigned long getStartTime() {
      return startFrame;
    }

    /** Move to the releaseState phase
    * Only required when envSustain is > 0.
    */
    inline
    void startRelease() {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      int currState = envState.load(std::memory_order_relaxed);
      if (currState > 0 && currState < 5 && !releaseTriggered.load(std::memory_order_relaxed)) {
        releaseStartLevel = envVal.load(std::memory_order_relaxed);
        releaseStartFrame = audioFrameCount();
        // Publish snapshot before the flag so the evaluator sees both atomically.
        releaseTriggered.store(true, std::memory_order_release);
        envState.store(5, std::memory_order_release);
      }
      #else
      if (envState > 0 && envState < 5 && !releaseTriggered) {
        releaseStartLevel = envVal;
        releaseStartFrame = audioFrameCount();
        releaseTriggered = true;
        envState = 5;
      }
      #endif
    }

    /** Set the envelope's status
     * newState 0 = complete, 1 = attack, 2 = hold, 3 = decay, 4 = sustain, 5 = release
    */
    inline
    void setEnvState(int newState) {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      envState.store(newState, std::memory_order_relaxed);
      #else
      envState = newState;
      #endif
    }

    /** Return the envelope's AHDSR status */
    inline
    int getEnvState() {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      return envState.load(std::memory_order_relaxed);
      #else
      return envState;
      #endif
    }

    int prevEnvState = 0;

    /** Compute and return the next envelope value.
     * Equivalent to getValue(): both evaluate the envelope at the current audio
     * frame. Safe to call at control rate (loop) or per sample (audioUpdate).
     */
    inline
    uint16_t next() {
      return evaluate();
    }

    /** Set the current envelope value - from 0 to MAX_16.
     * Note: the value is recomputed from the frame clock on the next
     * next()/getValue() call, so this is only a transient override.
     */
    void setValue(uint16_t val) {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      envVal.store(val, std::memory_order_relaxed);
      #else
      envVal = val;
      #endif
    }

    /** Return the current envelope value - from 0 to MAX_16.
     * Evaluates the envelope at the current audio frame (smooth at audio rate).
     */
    inline
    uint16_t getValue() {
      return evaluate();
    }

    /** Set the maximum envelope value
    * @param level from 0.0 - 1.0
    * Can work as a gain control
    */
    inline
    void setMaxLevel(float level) {
      MAX_ENV_LEVEL = (MAX_16 * 2 - 1) * fmaxf(0.0f,level);
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
      sustainLevel = envSustain * MAX_ENV_LEVEL;
    }

    /** Return the current maximum envelope value
    * level from 0.0 - 1.0
    */
    inline
    float getMaxLevel() {
      return MAX_ENV_LEVEL * 0.5 * (float)MAX_16_INV;
    }

  private:
    /** Evaluate the envelope at the current audio frame, cache state/value, and
     * return the value. Pure w.r.t. the frame clock for all pre-release phases,
     * so concurrent calls from both audio cores are safe; the only shared mutable
     * input is the release snapshot, published atomically by startRelease().
     */
    inline
    uint16_t evaluate() {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      int currState = envState.load(std::memory_order_acquire);
      bool released = releaseTriggered.load(std::memory_order_acquire);
      #else
      int currState = envState;
      bool released = releaseTriggered;
      #endif

      uint32_t frameNow = audioFrameCount();
      const uint16_t peak = JIT_MAX_ENV_LEVEL;
      uint16_t currVal;

      if (currState == 0) {
        // env complete
        currVal = 0;
      }
      else if (released) {
        // Asynchronous note-off release (sustaining envelopes).
        uint32_t relElapsed = frameNow - releaseStartFrame;
        float rPercent = 1.0f - relElapsed * invReleaseSamples;
        if (rPercent > 0.0f && releaseStartLevel > 10) {
          rPercent = rPercent * rPercent * rPercent; // fast exp
          currVal = (uint16_t)(releaseStartLevel * rPercent);
          currState = 5;
        } else {
          currVal = 0;
          currState = 0;
        }
      }
      else {
        // Deterministic pre-release contour, a pure function of frames-since-start.
        uint32_t elapsed = frameNow - startFrame;

        if (attackSamples > 0.0f && elapsed < (uint32_t)attackSamples) {
          // attack: ease-out (fast initial rise, smooth approach to peak),
          // interpolated from attackStartLevel for click-free retrigger.
          float t = elapsed * invAttackSamples;
          float inv = 1.0f - t;
          t = 1.0f - inv * inv;
          currVal = (uint16_t)(attackStartLevel + (peak - attackStartLevel) * t);
          currState = 1;
        }
        else if (elapsed < (uint32_t)(attackSamples + holdSamples)) {
          // hold: maintain peak
          currVal = peak;
          currState = 2;
        }
        else {
          uint32_t decayElapsed = elapsed - (uint32_t)(attackSamples + holdSamples);
          if (sustainLevel > 0) {
            // AHDSR: decay peak -> sustain, then hold sustain until note-off.
            if (decaySamples > 0.0f) {
              float t = decayElapsed * invDecaySamples;
              if (t < 1.0f) {
                float dp = 1.0f - t;
                dp = dp * dp * dp * dp; // fast exp
                float v = peak * dp;
                if (v <= (float)sustainLevel) { currVal = sustainLevel; currState = 4; }
                else { currVal = (uint16_t)v; currState = 3; }
              } else { currVal = sustainLevel; currState = 4; }
            } else { currVal = sustainLevel; currState = 4; }
          }
          else if (decaySamples > 0.0f) {
            // AD (sustain 0), with optional decay repeats: peak -> 0, then complete.
            float totalSegs = (float)(decayRepeats + 1);
            float seg = decayElapsed * invDecaySamples; // segments elapsed
            if (seg < totalSegs) {
              float local = seg - (float)((uint32_t)seg); // fractional position in segment
              float dp = 1.0f - local;
              dp = dp * dp * dp * dp; // fast exp
              currVal = (uint16_t)(peak * dp);
              currState = 3;
            } else { currVal = 0; currState = 0; }
          }
          else {
            // AR (sustain 0, no decay): deterministic release peak -> 0.
            float t = decayElapsed * invReleaseSamples;
            if (t < 1.0f) {
              float rp = 1.0f - t;
              rp = rp * rp * rp; // fast exp
              currVal = (uint16_t)(peak * rp);
              currState = 5;
            } else { currVal = 0; currState = 0; }
          }
        }
      }

      if (currState > 0 && currState != prevEnvState) {
        prevEnvState = currState;
      }

      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      envState.store(currState, std::memory_order_relaxed);
      envVal.store(currVal, std::memory_order_relaxed);
      #else
      envState = currState;
      envVal = currVal;
      #endif
      return currVal;
    }

    // Envelope level values (0 to MAX_16 * 2 - 1 = 65534)
    uint16_t MAX_ENV_LEVEL = MAX_16 * 2 - 1;
    uint16_t JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
    uint16_t sustainLevel = 0;
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    std::atomic<uint16_t> envVal{0};
    #else
    uint16_t envVal = 0;
    #endif
    uint16_t attackStartLevel = 0;
    uint16_t releaseStartLevel = 0;
    // Durations in milliseconds (converted to samples in start()).
    float envAttack = 0.0f, envHold = 0.0f, envDecay = 0.0f;
    float envRelease = 600.0f; // ms
    // Per-note durations in samples (computed in start()).
    float attackSamples = 0.0f, holdSamples = 0.0f, decaySamples = 0.0f, releaseSamples = 0.0f;
    float invAttackSamples = 0.0f, invDecaySamples = 0.0f, invReleaseSamples = 0.0001f;
    // Audio-frame anchors (see audioFrameCount() in M16.h).
    uint32_t startFrame = 0;
    uint32_t releaseStartFrame = 0;
    float envSustain = 0.0f;
    int decayRepeats = 0;

    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    std::atomic<int> envState{0}; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5
    std::atomic<bool> releaseTriggered{false}; // asynchronous note-off latch (sustaining envelopes)
    #else
    int envState = 0;
    bool releaseTriggered = false;
    #endif
    bool resetOnStart = false; // If true, reset envVal to 0 on start() for consistent drum attacks

};

#endif /* ENV_H_ */
