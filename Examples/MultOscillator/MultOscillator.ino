// M16 Multi Oscillator example

#include "M16.h"
#include "Osc.h"
#include "SVF.h"

uint16_t sqrTable [TABLE_SIZE]; // empty array
uint16_t sawTable [TABLE_SIZE]; // empty array
Osc aOsc1(sawTable);
Osc aOsc2(sawTable);
Osc aOsc3(sqrTable);

SVF aFilter1;

void setup() {
  Serial.begin(115200);
  sqrGen(sqrTable);
  sawGen(sawTable);
  aFilter1.setResonance(0.2); // 0.0- 1.0
  aFilter1.setCentreFreq(4000); // 60 - 11k
  audioStart();
  Serial.println();Serial.println("M16 running");
}

void loop() {
 float pitch = random(48) + 28;
 Serial.println(pitch);
 aOsc1.setPitch(pitch);
 aOsc2.setPitch(pitch - 0.05);
 aOsc3.setPitch(pitch - 11.98);
 delay(1000);
}

// This function required in all M16 programs to specify the samples to be played
uint32_t audioUpdate() {
  uint16_t leftVal = aFilter1.nextLPF((aOsc1.next() + aOsc2.next() + aOsc3.next())/3);
  uint16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
