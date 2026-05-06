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
      if (val >= 0) envAttack = val * 1000; // micros
    }

    /** Get attack time in ms. */
    float getAttack() {
      return envAttack * 0.001f;
    }

    /** Set envHold time in ms. */
    void setHold(float val) {
      if (val >= 0) envHold = val * 1000;
    }

    /** Set envDecay time in ms.
     * Set to 0 to skip decay phase (useful for AR envelopes with sustain=0)
     */
    void setDecay(float val) {
      if (val >= 0) envDecay = val * 1000;
    }

    /** Set the number of times to repeat the decay segment.
    * Useful for clap sounds or guiro scrapes...
    */
    void setDecayRepeats(int val) {
      if (val >= 0) {
        decayRepeats = val;
      }
    }

    /** Get decay time in ms. */
    int getDecay() {
      return envDecay * 0.001f;
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
        envRelease = fmaxf(10.0f, val) * 1000.0f;
        jitEnvRelease = envRelease;
      }
    }

    /** Get release time in ms. */
    float getRelease() {
      return envRelease * 0.001;
    }

    /** Begin the current envelope */
    inline
    void start() {
      peaked = false;
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      envState.store(1, std::memory_order_relaxed); // attack
      #else
      envState = 1; // attack
      #endif
      // Reset envelope value to 0 if enabled (useful for consistent drum attacks)
      if (resetOnStart) {
        #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
        envVal.store(0, std::memory_order_relaxed);
        #else
        envVal = 0;
        #endif
      }
      // JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (rand(MAX_ENV_LEVEL * 0.05));
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (audioRand(MAX_ENV_LEVEL * 0.05));
      releaseStartLevelDiff = JIT_MAX_ENV_LEVEL; // 0
      // jitEnvRelease = envRelease + rand(envRelease * 0.05);
      jitEnvRelease = envRelease + audioRand(envRelease * 0.05);
      jitEnvAttack = envAttack;
      jitEnvDecay = envDecay;
      invJitEnvDecay = (jitEnvDecay > 0) ? (1.0f / jitEnvDecay) : 0.0f;
      envStartTime = micros();
      currDecayRepeats = decayRepeats;
      next();
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

    /** Return the envelope's start time */
    inline
    unsigned long getStartTime() {
      return envStartTime;
    }

    /** Move to the releaseState phase
    * Only required when envSustain is > 0.
    */
    inline
    void startRelease() {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      int currState = envState.load(std::memory_order_relaxed);
      if (currState > 0 && currState < 5) {
        uint16_t currVal = envVal.load(std::memory_order_relaxed);
        releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - currVal;
        releaseStartlevel = currVal;
        releaseStartTime = micros();
        envState.store(5, std::memory_order_relaxed); // release
      }
      #else
      if (envState > 0 && envState < 5) {
        releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - envVal;
        releaseStartlevel = envVal;
        releaseStartTime = micros();
        envState = 5; // release
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

    /** Compute and return the next envelope value */
    inline
    uint16_t next() {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      int currState = envState.load(std::memory_order_relaxed);
      uint16_t currVal = envVal.load(std::memory_order_relaxed);
      #else
      int currState = envState;
      uint16_t currVal = envVal;
      #endif

      if (currState > 0 && currState != prevEnvState) {
        prevEnvState = currState;
      }

      unsigned long microsTime = micros();
      unsigned long elapsedTime = (unsigned long)(microsTime - envStartTime);

      if (currState == 0) {
        // env complete
        currVal = 0;
      }
      else if (currState == 1) {
        // attack
        if (jitEnvAttack == 0) {
          currVal = JIT_MAX_ENV_LEVEL;
          currState = 2; // go to hold
        } 
        else if (elapsedTime <= jitEnvAttack) {
          double attackPortion = elapsedTime / (double)jitEnvAttack;
          currVal = max(currVal, min(JIT_MAX_ENV_LEVEL,
                    (uint16_t)(JIT_MAX_ENV_LEVEL * attackPortion)));
        } 
        else {
          currVal = JIT_MAX_ENV_LEVEL;
          currState = 2; // go to hold
        }
      }
      else if (currState == 2) {
        // hold
        if (envHold > 0 && (unsigned long)(elapsedTime) <= (jitEnvAttack + envHold)) {
          // still holding - maintain peak level
          currVal = JIT_MAX_ENV_LEVEL;
        }
        else {
          // If decay=0, skip decay phase entirely
          if (jitEnvDecay == 0) {
            if (sustainLevel == 0) {
              // AR envelope: skip directly to release
              releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - currVal;
              releaseStartlevel = currVal;
              releaseStartTime = microsTime;
              currState = 5; // skip to release
            } else {
              // ASR envelope: skip to sustain
              currState = 4; // skip to sustain
            }
          } else {
            decayStartLevel = currVal;
            decayStartTime = microsTime;
            decayStartLevelDiff = decayStartLevel - sustainTriggerLevel;
            currState = 3; // go to decay
          }
        }
      }
      else if (currState == 3) {
        // decay
        unsigned long decayElapsed = (unsigned long)(microsTime - decayStartTime);
        if (jitEnvDecay > 0 && currVal > sustainLevel) {
          float dPercent = fmaxf(0.0f, 1.0f - decayElapsed * invJitEnvDecay);
          dPercent = dPercent * dPercent * dPercent * dPercent; // fast exp
          currVal = decayStartLevel * dPercent;
        } 
        else {
          if (currDecayRepeats > 0) {
            currDecayRepeats -= 1;
            decayStartTime += jitEnvDecay; // safe because unsigned long wraps
            currVal = JIT_MAX_ENV_LEVEL;
          } 
          else {
            currState = 4; // go to sustain
          }
        }
      }
      else if (currState == 4) {
        // sustain
        if (sustainLevel > 0) {
          currVal = sustainLevel; 
        } 
        else {
          releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - currVal;
          if (decayRepeats > 0) currVal = JIT_MAX_ENV_LEVEL;
          releaseStartlevel = currVal;
          releaseStartTime = microsTime;
          currState = 5; // go to release
        }
      }
      else if (currState == 5) {
        // release
        unsigned long releaseElapsed = (unsigned long)(microsTime - releaseStartTime);
        if (currVal > 10) {
          float rPercent = fmaxf(0.0f, 1.0f - releaseElapsed / (float)jitEnvRelease);
          rPercent = rPercent * rPercent * rPercent; // fast exp
          currVal = releaseStartlevel * rPercent;
        } 
        else {
          currState = 0; // go to complete
          currVal = 0;
        }
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

    /** Set the current envelope value - from 0 to MAX_16 */
    void setValue(uint16_t val) {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      envVal.store(val, std::memory_order_relaxed);
      #else
      envVal = val;
      #endif
    }

    /** Return the current envelope value - from 0 to MAX_16 */
    inline
    uint16_t getValue() {
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      return envVal.load(std::memory_order_relaxed);
      #else
      return envVal;
      #endif
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
    // Envelope level values (0 to MAX_16 * 2 - 1 = 65534)
    uint16_t MAX_ENV_LEVEL = MAX_16 * 2 - 1;
    uint16_t JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
    uint16_t sustainLevel = 0, sustainTriggerLevel = 0;
    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    std::atomic<uint16_t> envVal{0};
    #else
    uint16_t envVal = 0;
    #endif
    uint16_t releaseStartLevelDiff = MAX_ENV_LEVEL;
    uint16_t decayStartLevel = 0, decayStartLevelDiff = 0, releaseStartlevel = 0;
    // Timing values (microseconds - can be millions)
    unsigned long jitEnvAttack = 0, envAttack = 0, envHold = 0, envDecay = 0, jitEnvDecay = 0;
    unsigned long envRelease = 600 * 1000; // ms to micros
    unsigned long jitEnvRelease = envRelease;
    unsigned long envStartTime = 0, releaseStartTime = 0, decayStartTime = 0;
    float invJitEnvDecay = 0.0001f;
    float envSustain = 0.0f;
    bool peaked = false;
    int decayRepeats = 0;
    int currDecayRepeats = 0;

    #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
    std::atomic<int> envState{0}; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5
    #else
    int envState = 0; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5
    #endif
    bool resetOnStart = false; // If true, reset envVal to 0 on start() for consistent drum attacks

};

#endif /* ENV_H_ */
