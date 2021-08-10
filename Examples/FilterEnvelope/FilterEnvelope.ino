// M16 Fiilter Envelope example

#include "M16.h"
#include "Osc.h"
#include "SVF.h"
#include "Env.h"

int16_t sawTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sawTable);
Osc aOsc2(sawTable);

SVF aFilter1;

Env ampEnv1, filtEnv1; // create both amp and filter envelopes

unsigned long msNow, stepTime, envTime = millis();

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  
  sawGen(sawTable); // fill wavetable
  
  aFilter1.setResonance(0.5); // 0.01 > res < 1.0
  aFilter1.setCentreFreq(3000); // 40 - 11k

  ampEnv1.setAttack(20);
  ampEnv1.setDecay(200);
  ampEnv1.setSustain(0.5);
  ampEnv1.setRelease(800);
  
  // setup filter envelope params
  filtEnv1.setAttack(400);
  filtEnv1.setRelease(500);
  
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > stepTime) {
    stepTime += 1000;
    float pitch = random(36) + 36;
    if (random(3) < 2) {
      aOsc1.setPitch(pitch);
      aOsc1.setPhase(0);
      aOsc2.setPitch(pitch - 12.1);
      aOsc2.setPhase(0);
      ampEnv1.start();
      filtEnv1.start(); // start filter env with amp envelope
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
    if (msNow > ampEnv1.getStartTime() + 500) {
      ampEnv1.startRelease();
    }
    // update cutoff with scaled env value offset by min cutoff
    aFilter1.setCentreFreq(3000 + filtEnv1.next()>>4);
  }
}

void audioUpdate() {
  int16_t leftVal = aFilter1.nextLPF((aOsc1.next() + aOsc2.next()) / 2) * ampEnv1.getValue() >> 16;
  int16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
