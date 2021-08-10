// M16 Low Frequency Oscillator on Filter

#include "M16.h"
#include "Osc.h"
#include "SVF.h"
#include "Env.h"

int16_t sqrTable [TABLE_SIZE]; // empty wavetable
int16_t sawTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sawTable);
Osc aOsc2(sqrTable);

Osc LFO1(noiseTable); // acts as sample and hold

SVF aFilter1;

Env ampEnv1;

unsigned long msNow, stepTime, envTime = millis();

float cutoff1 = 200; // base LPF cutoff value

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  sqrGen(sqrTable); // fill wavetable
  sawGen(sawTable); // fill wavetable
  noiseGen(noiseTable); // fill wavetable and set noise flag
  aFilter1.setResonance(0.5); // 0.01 > res < 1.0

  ampEnv1.setAttack(20);
  ampEnv1.setRelease(800);
  // Set freq so that Osc returns each sample value on next()
  LFO1.setFreq(SAMPLE_RATE/TABLE_SIZE); 
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > stepTime) {
    stepTime += 250;
    // restrict pitches
    int scale [] = {0, 2, 4, 7, 9};
    float pitch = scale[random(5)] + 48; 
    freq1 = mtof(pitch);
    aOsc1.setPitch(pitch);
    aOsc1.setPhase(0);
    aOsc2.setPitch(pitch - 12.1);
    aOsc2.setPhase(0);
    // adjust the filter cutoff at the start of each note
    aFilter1.setCentreFreq(200 + ((abs(LFO1.next()) * 4000) >> 13));
    ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
  }
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  int16_t voice1 = aFilter1.nextLPF((aOsc1.next() + aOsc2.next()) / 2) * ampEnv1.getValue() >> 16;
  i2s_write_lr(voice1, voice1);
}
