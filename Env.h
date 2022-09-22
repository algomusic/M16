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
    void setAttack(int val) {
      if (val >= 0) envAttack = val * 1000; // micros
    }

    /** Set envHold time in ms. */
    void setHold(int val) {
      if (val >= 0) envHold = val * 1000;
    }

    /** Set envDecay time in ms. */
    void setDecay(int val) {
      if (val >= 0) envDecay = val * 1000;
    }

    /** Set the number of times to repeat the decay segment.
    * Useful for clap sounds or guiro scrapes...
    */
    void setDecayRepeats(int val) {
      if (val >= 0) {
        delayRepeats = val;
        delayExp = 1;
      }
    }

    /** Set envSustain level as a value from 0.0 - 1.0 */
    void setSustain(float val) {
      if (val >= 0 && val <= 1) {
        envSustain = val * MAX_ENV_LEVEL;
//        releaseState = false;
      }
    }

    /** Set releaseState time in ms. */
    void setRelease(int val) {
      if (val >= 0) envRelease = val * 1000;
    }

    /** Begin the current envelope */
    inline
    void start() {
//      Serial.println("start");
      peaked = false;
      envState = 1; // attack
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (rand(MAX_ENV_LEVEL * 0.35));
      releaseStartLevelDiff = JIT_MAX_ENV_LEVEL; // 0
      jitEnvRelease = envRelease + rand(envRelease * 0.2);
      jitEnvAttack = envAttack + rand(envAttack * 0.2);
      jitEnvDecay = envDecay + rand(envDecay * 0.2);
      envStartTime = micros(); //millis();
      prevEnvVal = 0;
      currDelayRepeats = delayRepeats;
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
//        Serial.print("release - envVal is ");Serial.println(envVal);
        releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - envVal;
        releaseStartTime = micros(); //millis();
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
            envState = 2; // go to hold
            envVal = JIT_MAX_ENV_LEVEL;
            return envVal;
          } else if (elapsedTime <= jitEnvAttack) {
            double attackPortion = elapsedTime / (double)jitEnvAttack;
            envVal = max(envVal, min(JIT_MAX_ENV_LEVEL, (uint32_t)(JIT_MAX_ENV_LEVEL * attackPortion)));
            return envVal;
          } else {
            envState = 2; // go to hold
            envVal = JIT_MAX_ENV_LEVEL;
            return envVal;
          }
          break;
        case 2:
          // hold
          if (envHold > 0 && elapsedTime <= jitEnvAttack + envHold) { // hold
            return envVal;
          } else {
            envState = 3; // go to decay
            return envVal;
          }
          break;
        case 3:
          // decay
          if (jitEnvDecay > 0 && elapsedTime <= jitEnvAttack + envHold + jitEnvDecay) { // decay
            //calulate and return envDecay value
            float dPercent = pow((microsTime - envStartTime - jitEnvAttack) / (float)jitEnvDecay, delayExp); // exp
            envVal = (JIT_MAX_ENV_LEVEL - envSustain) * (1 - dPercent) + envSustain;
            return envVal;
          } else {
            if (currDelayRepeats > 0) {
              currDelayRepeats -= 1;
              envStartTime += jitEnvDecay;
            } else {
              envState = 4; // go to sustain
              if (delayRepeats > 0 && envSustain == 0) envVal = JIT_MAX_ENV_LEVEL;
            }
            return envVal;
          }
          break;
        case 4:
          // sustain
          if (envSustain > 0) {// sustain
            return envSustain;
          } else {
            releaseStartLevelDiff = JIT_MAX_ENV_LEVEL - envVal;
            releaseStartTime = microsTime;
            envState = 5; // go to release
            return envVal;
          }
          break;
        case 5:
          // release
          if (microsTime < releaseStartTime + jitEnvRelease && abs((int)envVal) > 1) {
            // float rPercent = pow(1.0f - (microsTime - releaseStartTime) / (float)jitEnvRelease, 4); // exp
            float rPercent = 1.0f - (microsTime - releaseStartTime) / (float)jitEnvRelease;
            rPercent = rPercent * rPercent * rPercent; // faster exp
            envVal = (JIT_MAX_ENV_LEVEL - releaseStartLevelDiff) * rPercent;
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


    /** Return the current envelope value */
    inline
    uint16_t getValue() {
      // envVal = (prevEnvVal*4 + envVal) / 5; // smooth abrupt changes
      // prevEnvVal = envVal;
      return envVal;
    }

    /** Set the maximum envelope value
    * @param level from 0.0 - 1.0
    * Can work as a gain control
    */
    inline
    void setMaxLevel(float level) {
      MAX_ENV_LEVEL = MAX_16 * level;
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
    }

    /** Return the current maximum envelope value
    * level from 0.0 - 1.0
    */
    inline
    float getMaxLevel() {
      return MAX_ENV_LEVEL * (float)MAX_16_INV;
    }

  private:
    uint32_t MAX_ENV_LEVEL = MAX_16 - 1;
    uint32_t JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
    uint32_t jitEnvAttack, envAttack, envHold, envDecay, jitEnvDecay, envSustain = 0;
    uint32_t envRelease = 600 * 1000; // ms to micros
    uint32_t jitEnvRelease = envRelease;
    bool peaked = false;
    unsigned long envStartTime, releaseStartTime;
    uint32_t envVal = 0;
    uint32_t releaseStartLevelDiff = MAX_ENV_LEVEL;
    uint32_t prevEnvVal = 0;
    int delayRepeats = 0;
    int currDelayRepeats = 0;
    int delayExp = 4;

    int envState = 0; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5

};

#endif /* ENV_H_ */
