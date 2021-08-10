// M16 Low Frequency Oscillator of pitch example

#include "M16.h"
#include "Osc.h"
#include "SVF.h"
#include "Env.h"
#include "Samp.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable
int16_t triTable [TABLE_SIZE]; // empty wavetable

Osc aOsc1(triTable);
Osc LFO1(sineTable); // use an oscillator as an LFO

Env ampEnv1;

unsigned long msNow, stepTime, envTime, freqTime = millis();

float freq1; // store the base freq for aOsc1

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  sinGen(sineTable); // fill wavetable
  triGen(triTable); // fill wavetable

  ampEnv1.setAttack(20);
  ampEnv1.setDecay(200);
  ampEnv1.setSustain(0.5);
  ampEnv1.setRelease(800);
  // Calulate the LFO frequency
  // = freq * sample rate * time (in secs) between reads
  LFO1.setFreq(6 * SAMPLE_RATE * 0.029); 
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > stepTime) {
    stepTime += 1000;
    float pitch = random(36) + 36; 
    freq1 = mtof(pitch);
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    aOsc1.setPhase(0);
    ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
    if (msNow > ampEnv1.getStartTime() + 500) {
      ampEnv1.startRelease();
    }
  }

  if (msNow > freqTime) {
    freqTime += 29; // update time (in milliseconds) is used above to set LFO freq 
    // Compute the LFO value to modulate the pitch (freqency) by
    // = osc val / osc range * depth + 1
    float lfo1Val = LFO1.next() / 16383.0 * 0.05 + 1; 
    // update the oscillator pitch (freqency)
    aOsc1.setFreq(freq1 * lfo1Val);
  }

}

// This function required in all M16 programs to specify the samples to be played
//uint32_t audioUpdate() {
void audioUpdate() {
  uint16_t leftVal = aOsc1.next() * ampEnv1.getValue() >> 16;
  int16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
