// M16 Sample-and-Hold Noise example
// Selects a random noise-table value at a controlled rate and holds that
// value between updates.

#include "M16.h"
#include "Osc.h"

WaveTable noiseTable;
Osc sampleAndHold;

constexpr float holdRate = 10.0f; // new random value 10 times per second

void setup() {
  noiseTable.noiseGen();
  sampleAndHold.setTable(noiseTable);
  sampleAndHold.setSandH(true);
  sampleAndHold.setFreq(holdRate);

  seti2sPins(38, 39, 40, -1); // BCK, WS, DOUT, DIN (unused)
  audioStart();
}

void loop() {
}

void audioUpdate() {
  // next() returns the previous random value until one oscillator period has
  // elapsed. The approximate hold time in samples is SAMPLE_RATE / holdRate.
  int16_t noise = sampleAndHold.next();
  audioBlockWrite(noise, noise);
}
