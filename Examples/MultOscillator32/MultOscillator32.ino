// M32 Multi Oscillator example

#include "M32.h"
#include "Osc.h"
#include "SVF.h"

int16_t sqrTable [TABLE_SIZE]; // empty array
int16_t sawTable [TABLE_SIZE]; // empty array
Osc aOsc1(sawTable);
Osc aOsc2(sawTable);
Osc aOsc3(sqrTable);

SVF aFilter1;

unsigned long msNow, stepTime = millis();
int stepDelta = 1000;
int cutoff = 4000;

void setup() {
  Serial.begin(115200);
  sqrGen(sqrTable);
  sawGen(sawTable);
  aFilter1.setResonance(0.2); // 0.0- 1.0
  aFilter1.setCentreFreq(cutoff); // 60 - 11k
  audioStart();
  Serial.println();Serial.println("M32 running");
}

void loop() {
  msNow = millis();
  
  if (msNow > stepTime) {
    stepTime += stepDelta;
    float pitch = random(60) + 24;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    aOsc2.setPitch(pitch - 0.05);
    aOsc3.setPitch(pitch - 11.98);
    aFilter1.setCentreFreq(cutoff + (pitch - 60) * 10); // cutoff pitch tracking
  }

  // Include a call to audioUpdate in the main loop() to keep I2S buffer supplied.
  audioUpdate();
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  uint16_t leftVal = aFilter1.nextLPF((aOsc1.next() + aOsc2.next() + aOsc3.next())/3);
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
