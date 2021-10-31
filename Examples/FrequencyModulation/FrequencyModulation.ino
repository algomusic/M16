// M16 Frequency Modulation (Phase Modulation) example

#include "M16.h"
#include "Osc.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable

Osc aOsc1(sineTable);
Osc aOsc2(sineTable);

float modIndex = 0.2;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill wavetable
  audioStart();
  Serial.println();Serial.println("M16 running");
}

void loop() {
 float pitch = random(48) + 36;
 Serial.println(pitch);
 aOsc1.setPitch(pitch);
 aOsc2.setFreq(mtof(pitch) * 0.5); // set freq ratio
 modIndex = random(40) * 0.1;
 delay(1000);
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  // modIndex to change mod depth. Values for PM about 1/10th of those typical for FM
  int16_t leftVal = aOsc1.phMod(aOsc2.next(), modIndex); 
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
