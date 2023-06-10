// M16 Example - polyphony
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "FX.h"

int16_t waveTable [TABLE_SIZE]; // empty array
const int poly = 2; // change polyphony as desired, each MCU type will handle this differently
Osc osc[poly];
Env env[poly];
SVF filter[poly];
FX effect1;

unsigned long msNow, noteTime, envTime, delTime;
int scale [] = {0, 2, 4, 0, 7, 9, 0, 0, 0, 0, 0};
int reverbSize = 16; // >= 1
int reverbLength = 700; // 0 - 1024
int reverbMix = 150; // 0 - 1024

void setup() {
  Serial.begin(115200);
  // tone
  Osc::sawGen(waveTable);
  for (int i=0; i<poly; i++) {
    osc[i].setTable(waveTable);
    osc[i].setPitch(60);
    env[i].setAttack(30);
    env[i].setMaxLevel(0.7);
    filter[i].setResonance(0);
    filter[i].setFreq(3000);
  }
  // reverb setup
  effect1.setReverbSize(reverbSize); // quality and memory >= 1
  effect1.setReverbLength(reverbLength); // 0-1024
  effect1.setReverbMix(reverbMix);
  // setI2sPins(25, 27, 12);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > noteTime) {
    noteTime = msNow + 250;
    for (int i=0; i<poly; i++){
      if (random(10) < 5) {
        int p = pitchQuantize(random(36) + 48, scale, 0);
        osc[i].setPitch(p);
        filter[i].setFreq(min(3000.0f, mtof(p + 24)));
        env[i].start();
      }
    }
  }

  if (msNow > envTime) {
    envTime = msNow + 4;
    for (int i=0; i<poly; i++) {
      env[i].next();
    }
  }
}

void audioUpdate() {
  int32_t mix = 0;
  for (int i=0; i<poly; i++) {
    mix += filter[i].nextLPF((osc[i].next() * env[i].getValue())>>16);
  }
  mix = min(MAX_16, max(MIN_16, mix>>1));
  // stereo
  int16_t leftVal, rightVal;
  effect1.reverbStereo(mix, mix, leftVal, rightVal);
  i2s_write_samples(leftVal, rightVal);
}