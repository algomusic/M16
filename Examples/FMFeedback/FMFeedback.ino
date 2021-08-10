/** FM Feedback example
 *  M16 audio ibrary for the ESP8266
 */

#include "M16.h"
#include "Osc.h"
#include "Env.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable
int16_t sawTable [TABLE_SIZE]; // empty wavetable
int16_t triTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sineTable);
Env ampEnv1;

unsigned long msNow, stepTime, envTime, freqTime = millis();
int modIndex = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  sinGen(sineTable);
  sawGen(sawTable);
  triGen(triTable);
  aOsc1.setFreq(440);
  ampEnv1.setAttack(30);
  ampEnv1.setRelease(800);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > stepTime) {
    stepTime += 250;
    ampEnv1.setMaxLevel(rand(6) * 0.1 + 0.5);
    modIndex = pow(rand(100) * 0.01, 2.5) * 150;
    if (rand(3) < 2) ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
  }
}

void audioUpdate() {
  int16_t leftVal = aOsc1.feedback(modIndex);
  leftVal = (leftVal * ampEnv1.getValue()) >> 15;
  int16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
