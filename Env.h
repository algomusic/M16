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
      if (val >= 0) envDecay = max(10.0f, val) * 1000;
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
        envRelease = max(10.0f, val) * 1000.0f;
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
//      Serial.println("start");
      peaked = false;
      envState = 1; // attack
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (rand(MAX_ENV_LEVEL * 0.05));
      releaseStartLevelDiff = JIT_MAX_ENV_LEVEL; // 0
      jitEnvRelease = envRelease + rand(envRelease * 0.05);
      jitEnvAttack = envAttack; // + rand(envAttack * 0.02);
      jitEnvDecay = envDecay; 
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

    /** Return the envelope's release status */
    inline
    int getEnvState() {
      return envState;
    }

    /** Compute and return the next envelope value */
    inline
    uint16_t next() {
      // unsigned long msTime = millis();
      unsigned long microsTime = micros();
      unsigned long elapsedTime = microsTime - envStartTime;
      switch (envState) {
        case 0:
          // env complete
          return 0;
          break;
        case 1:
          // attack
          if (jitEnvAttack == 0) {
            envVal = JIT_MAX_ENV_LEVEL;
            envState = 2; // go to hold
            return envVal;
          } else if (elapsedTime <= jitEnvAttack) {
            double attackPortion = elapsedTime / (double)jitEnvAttack;
            envVal = max(envVal, min(JIT_MAX_ENV_LEVEL, (uint32_t)(JIT_MAX_ENV_LEVEL * attackPortion)));
            return envVal;
          } else {
            envVal = JIT_MAX_ENV_LEVEL;
            envState = 2; // go to hold
            return envVal;
          }
          break;
        case 2:
          // hold
          if (envHold > 0 && elapsedTime <= jitEnvAttack + envHold) { // hold
            return envVal;
          } else {
            decayStartLevel = envVal; 
            decayStartTime = microsTime;
            decayStartLevelDiff = decayStartLevel - sustainTriggerLevel;
            envState = 3; // go to decay
            return envVal;
          }
          break;
        case 3:
          // decay
          if (jitEnvDecay > 0 && envVal > sustainLevel) { // decay
            float dPercent = max(0.0f, 1.0f - (microsTime - decayStartTime) / (float)jitEnvDecay);
            dPercent = dPercent * dPercent * dPercent * dPercent; // very fast exp
            envVal = decayStartLevel * dPercent;
            return envVal;
          } else {
            if (currDecayRepeats > 0) {
              currDecayRepeats -= 1;
              decayStartTime += jitEnvDecay;// microsTime; // reset decay start time
              envVal = JIT_MAX_ENV_LEVEL;
            } else {
              envState = 4; // go to sustain
            }
            return envVal;
          }
          break;
        case 4:
          // sustain
          if (sustainLevel > 0) {// sustain
            envVal = sustainLevel; 
            return envVal;
          } else {
            releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - envVal;
            if (decayRepeats > 0) envVal = JIT_MAX_ENV_LEVEL;
            releaseStartlevel = envVal;
            releaseStartTime = microsTime;
            envState = 5; // go to release
            return envVal;
          }
          break;
        case 5:
          // release
          if (envVal > 10) {
            float rPercent = max(0.0f, 1.0f - (microsTime - releaseStartTime) / (float)jitEnvRelease);
            rPercent = rPercent * rPercent * rPercent; // faster exp
            envVal = releaseStartlevel * rPercent;
            return envVal;
          } else {
            envState = 0; // go to complete
            envVal = 0;
            return 0;
          }
          break;
      }
      return 0; // should never reach this, but...
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
      MAX_ENV_LEVEL = (MAX_16 * 2 - 1) * max(0.0f,level);
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
    uint32_t MAX_ENV_LEVEL = MAX_16 * 2 - 1;
    uint32_t JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
    uint32_t jitEnvAttack, envAttack, envHold, envDecay, jitEnvDecay, sustainLevel, sustainTriggerLevel;
    float envSustain = 0.0f;
    uint32_t envRelease = 600 * 1000; // ms to micros
    uint32_t jitEnvRelease = envRelease;
    bool peaked = false;
    unsigned long envStartTime, releaseStartTime, decayStartTime;
    uint32_t envVal = 0;
    uint32_t releaseStartLevelDiff = MAX_ENV_LEVEL;
    uint32_t decayStartLevel, decayStartLevelDiff, releaseStartlevel;
    int decayRepeats = 0;
    int currDecayRepeats = 0;

    int envState = 0; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5

};

#endif /* ENV_H_ */
