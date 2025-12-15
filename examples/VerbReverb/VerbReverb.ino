// M16 Example - Verb Reverb
// Demonstrates the Verb.h Freeverb-style reverb
// Nicer, but quite a bit more expensive, than fx.reverb

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Verb.h"

Osc osc;
Env env;
Verb reverb;

unsigned long msNow = millis();
unsigned long noteTime = msNow;
unsigned long envTime = msNow;
int noteDelta = 500;
int envDelta = 4;

int scale[] = {0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0};

void setup() {
  Serial.begin(115200);
  delay(200);

  // Setup oscillator with internal wavetable allocation
  osc.triGen();
  osc.setPitch(60);

  // Setup envelope
  env.setAttack(30);
  env.setDecay(200);
  env.setSustain(0.0);

  // reverb.setHighQuality(false); // Use standard quality (4+2) for lower CPU
  reverb.setReverbLength(0.7);  // 0.0-1.0, room size / decay time
  reverb.setReverbMix(0.6);     // 0.0-1.0, wet/dry balance
  // reverb.setDampening(0.4); // 0.0 - 1.0, high frequency dampening
  // seti2sPins(7, 8, 9, 41); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();

  // Trigger notes
  if ((unsigned long)(msNow - noteTime) >= noteDelta) {
    noteTime += noteDelta;
    int pitch = pitchQuantize(random(36, 72), scale, 0);
    osc.setPitch(pitch);
    env.start();
    Serial.println(pitch);
  }

  // Update envelope
  if ((unsigned long)(msNow - envTime) >= envDelta) {
    envTime += envDelta;
    env.next();
  }
}

void audioUpdate() {
  // Generate oscillator with envelope
  int32_t sample = (osc.next() * env.getValue()) >> 16;

  // Apply Freeverb-style reverb (stereo output)
  int32_t leftVal, rightVal;
  reverb.reverbStereo(sample, sample, leftVal, rightVal);

  i2s_write_samples(leftVal, rightVal);
}
