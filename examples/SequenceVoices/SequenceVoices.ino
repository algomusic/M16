/* M16 sequencer example
 */
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Seq.h"
#include "FX.h"

int16_t sawTable [TABLE_SIZE]; // empty array

unsigned long msNow = millis();
unsigned long stepTime = msNow;
unsigned long envTime = msNow;

int stepDelta = 250;
int envDelta = 4;
int stepCnt = 0;
int pent [] = {0, 2, 4, 7, 9};
const int voices = 4; // change to alter texture and adjust for different CPU capabilities

Osc oscillators[voices];
Env ampEnvs[voices];
SVF filters[voices];
FX effect1;
Seq sequences[voices];

void seqGen() {
  for (int i=0; i<voices; i++) {
    for (int j=0; j<16; j++) {
      if (rand(max(2, voices / 2) + 1) == 0) {
        sequences[i].setStepValue(j, pitchQuantize(rand(48) + 36, pent, 0));
      } else sequences[i].setStepValue(j, 0);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::sawGen(sawTable);
  for (int i=0; i<voices; i++) {
    oscillators[i].setTable(sawTable);
    oscillators[i].setPitch(60);
    oscillators[i].setSpread(rand(100) * 0.000001);
    ampEnvs[i].setAttack(10);
    filters[i].setFreq(rand(4000) + 1000);
  }
  seqGen();
  effect1.setReverbSize(16);
  effect1.setReverbLength(0.7);
  // seti2sPins(25, 27, 12, 21); // or similar if required
  audioStart();
}

void loop() {
  msNow = millis();
 
  if (msNow - stepTime > stepDelta || msNow - stepTime < 0) {
    stepTime = msNow;
    if (stepCnt%64 == 0) seqGen();
    for (int i=0; i<voices; i++) {
      int p = sequences[i].next();
      oscillators[i].setPitch(p);
      if (p > 0) {
        ampEnvs[i].setMaxLevel((rand(5) + 5) * 0.1);
        ampEnvs[i].start();
      }
    }
    stepCnt++;
  }

  if (msNow - envTime > envDelta || msNow - envTime < 0) {
    envTime = msNow;
    for (int i=0; i<voices; i++) {
      ampEnvs[i].next();
    }
  }
}

void audioUpdate() {
  int32_t leftVal = 0;
  int16_t leftOut, rightOut;
  for (int i=0; i<voices; i++) {
    leftVal += (filters[i].nextLPF(oscillators[i].next()) * ampEnvs[i].getValue())>>18;
  }
  leftVal = effect1.clip(leftVal);
  effect1.reverbStereo(leftVal, leftVal, leftOut, rightOut); // bypass for ESP8266
  i2s_write_samples(leftOut, rightOut);
}
