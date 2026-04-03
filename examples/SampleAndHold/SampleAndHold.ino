// M16 Sample and Hold example
// A noise oscillator in S&H mode at low frequency modulates
// the pitch of a triangle wave oscillator.
#include "M16.h"
#include "Osc.h"

Osc triOsc; // audio oscillator
Osc shOsc; // sample and hold modulator

int vol = 800; // 0 - 1024, 10 bit
int basePitch = 48; // base MIDI pitch
int pitchRange = 24; // semitone range of S&H modulation
float shRate = 4.0; // S&H rate in Hz - frequency

void setup() {
  Serial.begin(115200);
  delay(200);
  triOsc.triGen();
  triOsc.setPitch(basePitch);
  shOsc.noiseGen(); // can be any wave
  // shOsc.setNoise(true); // optional is using noise
  shOsc.setSandH(true);
  shOsc.setFreq(shRate);
  seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN
  audioStart();
}

int16_t prevSHVal = 0;

void loop() {
  // Read the held S&H value and update pitch only when it changes
  int16_t shVal = shOsc.getSandHValue();
  if (shVal != prevSHVal) {
    prevSHVal = shVal;
    float mod = (shVal + MAX_16) / (float)(MAX_16 * 2);
    int pitch = basePitch + (int)(mod * pitchRange);
    triOsc.setPitch(pitch);
  }
}

void audioUpdate() {
  shOsc.next(); // advance S&H phase
  int32_t sample = (triOsc.next() * vol) >> 10;
  i2s_write_samples(sample, sample);
}
