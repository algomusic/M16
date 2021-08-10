// M16 Frequency Modulation example

#include "M16.h"
#include "Osc.h"

uint16_t sineTable [TABLE_SIZE]; // empty wavetable

Osc aOsc1(sineTable);
Osc aOsc2(sineTable);

void setup() {
  Serial.begin(115200);
  sinGen(sineTable); // fill wavetable
  audioStart();
  Serial.println();Serial.println("M16 running");
}

void loop() {
 float pitch = random(48) + 28;
 Serial.println(pitch);
 aOsc1.setPitch(pitch);
 aOsc2.setFreq(mtof(pitch) * 0.5); // set freq ratio
 delay(1000);
}

// This function required in all M16 programs to specify the samples to be played
uint32_t audioUpdate() {
  uint16_t leftVal = aOsc1.phMod(aOsc2.next() * 0.4); // multiple to scale modulation depth
  uint16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
