// M16 Envelope example

#include "M16.h"
#include "Osc.h"
#include "SVF.h"
#include "Env.h"

int16_t triTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(triTable);
Osc aOsc2(triTable);

SVF aFilter1;

Env ampEnv1;

// handle control rate updates for envelope and notes
unsigned long msNow, stepTime, envTime = millis();
byte vol = 125; // 0 - 255, 8 bit // keep to max 50% for MAX98357 which sums both channels!

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  triGen(triTable); // fill wavetable
  aFilter1.setResonance(0.5); // 0.01 > res < 1.0
  aFilter1.setCentreFreq(1000); // 40 - 11k
  // set initial env values
  ampEnv1.setAttack(20); // in ms
  ampEnv1.setDecay(200);
  ampEnv1.setSustain(0.5); // percent of max level
  ampEnv1.setRelease(800);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > stepTime) {
    stepTime += 1000;
    if (random(3) < 2) {
      float pitch = random(24) + 48;
      Serial.println(pitch);
       aOsc1.setPitch(pitch);
       aOsc1.setPhase(0);
       aOsc2.setPitch(pitch - 12.2);
       aOsc2.setPhase(0);
       // vary envelope params
       ampEnv1.setAttack(random(400));
       ampEnv1.setRelease(random(3000) + 100);
       ampEnv1.start();
    }
  }

  if (msNow > envTime) {
    envTime += 4; // in ms
    ampEnv1.next();
    // trigger envelope release
    if (msNow > ampEnv1.getStartTime() + 500) ampEnv1.startRelease();
  }
}

// This function required in all M16 programs to specify the samples to be played
// Must end with i2s_write_lr();
void audioUpdate() {
  // Multiply signal by 16bit envelope then bit shift back into range 
  // (more efficient than multiplying by a float)
  int16_t leftVal = aFilter1.nextLPF((aOsc1.next() + aOsc2.next()) / 2) * ampEnv1.getValue() >> 16;
  leftVal = (leftVal * vol)>>8; // master volume
  int rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
