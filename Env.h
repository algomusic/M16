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
      if (val >= 0) envHold = val;
    }

    /** Set envDecay time in ms. */
    void setDecay(int val) {
      if (val >= 0) envDecay = val * 1000;
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
      if (val >= 0) envRelease = val;
    }

    /** Begin the current envelope */
    inline
    void start() {
//      Serial.println("start");
      envStartTime = micros(); //millis();
      peaked = false;
      envState = 1; // attack
      JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL - (random(MAX_ENV_LEVEL * 0.35));
      releaseStartLevelDiff = JIT_MAX_ENV_LEVEL; // 0
      jitEnvRelease = envRelease + random(envRelease * 0.2);
      jitEnvAttack = envAttack + random(envAttack * 0.2);
      jitEnvDecay = envDecay + random(envDecay * 0.2);
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
          if (elapsedTime <= jitEnvAttack) {
            // attackPercent = (msTime - envStartTime) / (float)jitEnvAttack;
            envVal = JIT_MAX_ENV_LEVEL * (microsTime - envStartTime) / (float)jitEnvAttack;
            // envVal = ((uint32_t)envVal + (uint32_t)prevAttackVal) * 0.5; // smooth
            // prevAttackVal = envVal;
            return envVal; //min(JIT_MAX_ENV_LEVEL, envVal);
          } else if (!peaked) {
            envVal = JIT_MAX_ENV_LEVEL;
            peaked = true;
            envState = 2; // go to hold
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
            float dPercent = pow((microsTime - envStartTime - jitEnvAttack) / (float)jitEnvDecay, 4); // exp
            envVal = (JIT_MAX_ENV_LEVEL - envSustain) * (1 - dPercent) + envSustain;
            return envVal;
          } else {
            envState = 4; // go to sustain
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
            float rPercent = pow(1.0f - (microsTime - releaseStartTime) / (float)jitEnvRelease, 4); // exp
//              float rPercent = 1.0f - (millis() - releaseStartTime) / (float)envRelease; // linear
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
      uint16_t easeVal = (prevEnvVal * 4 + envVal)/5;
      prevEnvVal = easeVal;
      return prevEnvVal + envVal;
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

    /** Return the current maximum envelope value */
    inline
    uint16_t getMaxLevel() {
      return MAX_ENV_LEVEL;
    }

  private:
    uint32_t MAX_ENV_LEVEL = MAX_16 - 1;
    uint32_t JIT_MAX_ENV_LEVEL = MAX_ENV_LEVEL;
    uint32_t jitEnvAttack, envAttack, envHold, envDecay, jitEnvDecay, envSustain = 0;
    uint32_t envRelease = 600 * 1000; // ms to micros
    uint32_t jitEnvRelease = envRelease;
//    bool releaseState = false;
    bool peaked = false;
    unsigned long envStartTime, releaseStartTime;
    uint32_t envVal = 0;
    uint32_t prevEnvVal = 0;
    uint32_t releaseStartLevelDiff = MAX_ENV_LEVEL;
    // uint32_t prevAttackVal = 0;
//    unsigned long rStart;
    int envState = 0; // complete = 0, attack = 1, hold = 2, decay = 3, sustain = 4, release = 5

};

#endif /* ENV_H_ */
