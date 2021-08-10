// M16 Ring Modulation example

#include "M16.h"
#include "Osc.h"

uint16_t sineTable [TABLE_SIZE]; // empty array

Osc aOsc1(sineTable);
Osc aOsc2(sineTable);

void setup() {
  Serial.begin(115200);
  sinGen(sineTable);
  audioStart();
  Serial.println();Serial.println("M16 running");
}

void loop() {
 float pitch = random(48) + 28; 
 Serial.println(pitch);
 aOsc1.setPitch(pitch);
 aOsc2.setPitch(random(24) + 60);
 delay(1000);
}

// This function required in all M16 programs to specify the samples to be played
uint32_t audioUpdate() {
  uint16_t leftVal = aOsc1.ringMod(aOsc2.next() * 1.55);
  uint16_t rightVal = leftVal;
  return (uint32_t)leftVal << 16 | (uint32_t)rightVal;
}
