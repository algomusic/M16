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

    /** Set envDecay time in ms. */
    void setDecay(float val) {
      if (val >= 0) envDecay = fmaxf(10.0f, val) * 1000;
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
      envState = 1; // attack
      // JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (rand(MAX_ENV_LEVEL * 0.05));
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (audioRand(MAX_ENV_LEVEL * 0.05));
      releaseStartLevelDiff = JIT_MAX_ENV_LEVEL; // 0
      // jitEnvRelease = envRelease + rand(envRelease * 0.05);
      jitEnvRelease = envRelease + audioRand(envRelease * 0.05);
      jitEnvAttack = envAttack;
      jitEnvDecay = envDecay;
      invJitEnvDecay = 1.0f / jitEnvDecay;
      envStartTime = micros(); 
      currDecayRepeats = decayRepeats;
      next();
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
      if (envState > 0 && envState < 5) {
        releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - envVal;
        releaseStartlevel = envVal;
        releaseStartTime = micros();
        envState = 5; // release
      }
    }

    /** Set the envelope's status 
     * newState 0 = complete, 1 = attack, 2 = hold, 3 = decay, 4 = sustain, 5 = release
    */
    inline
    void setEnvState(int newState) {
      envState = newState;
    }

    /** Return the envelope's AHDSR status */
    inline
    int getEnvState() {
      return envState;
    }

    int prevEnvState = 0;

    /** Compute and return the next envelope value */
    inline
    uint16_t next() {
      if (envState > 0 && envState != prevEnvState) {
        prevEnvState = envState;
        // Serial.println("env next " + String(envState));
      }

      unsigned long microsTime = micros();
      unsigned long elapsedTime = (unsigned long)(microsTime - envStartTime);

      if (envState == 0) {
        // env complete
        envVal = 0;
      }
      else if (envState == 1) {
        // attack
        if (jitEnvAttack == 0) {
          envVal = JIT_MAX_ENV_LEVEL;
          envState = 2; // go to hold
        } 
        else if (elapsedTime <= jitEnvAttack) {
          double attackPortion = elapsedTime / (double)jitEnvAttack;
          envVal = max(envVal, min(JIT_MAX_ENV_LEVEL,
                    (uint16_t)(JIT_MAX_ENV_LEVEL * attackPortion)));
        } 
        else {
          envVal = JIT_MAX_ENV_LEVEL;
          envState = 2; // go to hold
        }
      }
      else if (envState == 2) {
        // hold
        if (envHold > 0 && (unsigned long)(elapsedTime) <= (jitEnvAttack + envHold)) {
          // still holding
        } 
        else {
          decayStartLevel = envVal; 
          decayStartTime = microsTime;
          decayStartLevelDiff = decayStartLevel - sustainTriggerLevel;
          envState = 3; // go to decay
        }
      }
      else if (envState == 3) {
        // decay
        unsigned long decayElapsed = (unsigned long)(microsTime - decayStartTime);
        if (jitEnvDecay > 0 && envVal > sustainLevel) {
          float dPercent = fmaxf(0.0f, 1.0f - decayElapsed * invJitEnvDecay);
          dPercent = dPercent * dPercent * dPercent * dPercent; // fast exp
          envVal = decayStartLevel * dPercent;
        } 
        else {
          if (currDecayRepeats > 0) {
            currDecayRepeats -= 1;
            decayStartTime += jitEnvDecay; // safe because unsigned long wraps
            envVal = JIT_MAX_ENV_LEVEL;
          } 
          else {
            envState = 4; // go to sustain
          }
        }
      }
      else if (envState == 4) {
        // sustain
        if (sustainLevel > 0) {
          envVal = sustainLevel; 
        } 
        else {
          releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - envVal;
          if (decayRepeats > 0) envVal = JIT_MAX_ENV_LEVEL;
          releaseStartlevel = envVal;
          releaseStartTime = microsTime;
          envState = 5; // go to release
        }
      }
      else if (envState == 5) {
        // release
        unsigned long releaseElapsed = (unsigned long)(microsTime - releaseStartTime);
        if (envVal > 10) {
          float rPercent = fmaxf(0.0f, 1.0f - releaseElapsed / (float)jitEnvRelease);
          rPercent = rPercent * rPercent * rPercent; // fast exp
          envVal = releaseStartlevel * rPercent;
        } 
        else {
          envState = 0; // go to complete
          envVal = 0;
        }
      }

      return envVal;
    }

    /** Set the current envelope value - from 0 to MAX_16 */
    void setValue(uint16_t val) {
      envVal = val;
    }

    /** Return the current envelope value - from 0 to MAX_16 */
    inline
    uint16_t getValue() {
      return envVal;
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
      // Serial.println("setting sus level to " + String(sustainLevel));
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
    uint16_t envVal = 0;
    uint16_t releaseStartLevelDiff = MAX_ENV_LEVEL;
    uint16_t decayStartLevel = 0, decayStartLevelDiff = 0, releaseStartlevel = 0;
    // Timing values (microseconds - can be millions)
    unsigned long jitEnvAttack = 0, envAttack = 0, envHold = 0, envDecay = 10000, jitEnvDecay = 10000;
    unsigned long envRelease = 600 * 1000; // ms to micros
    unsigned long jitEnvRelease = envRelease;
    unsigned long envStartTime = 0, releaseStartTime = 0, decayStartTime = 0;
    float invJitEnvDecay = 0.0001f;
    float envSustain = 0.0f;
    bool peaked = false;
    int decayRepeats = 0;
    int currDecayRepeats = 0;

    int envState = 0; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5

};

#endif /* ENV_H_ */
