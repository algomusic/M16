// M16 Gain example
// Gain safely shares level changes from loop() with the audio task.

#include "M16.h"
#include "Osc.h"
#include "Gain.h"

Osc oscillator;
Gain outputGain(512); // 0 = silent, 512 = half, 1024 = full level

unsigned long levelTime = 0;
int level = 256;

void setup() {
  oscillator.sinGen();
  oscillator.setPitch(69);
  audioStart();
}

void loop() {
  if (millis() - levelTime >= 1000) {
    levelTime += 1000;
    level += 256;
    if (level > 1024) level = 256;
    outputGain.setLevel(level);
  }
}

void audioUpdate() {
  int32_t sample = outputGain.next(oscillator.next());
  audioBlockWrite(sample, sample);
}
