// M16 Example - polyphony
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "FX.h"

WaveTable wavetable; // one wavetable shared by every voice
const int poly = 16; // change polyphony as desired, each MCU type will handle particular amounts
Osc osc[poly]; // an array of oscillators
Env env[poly];
SVF filter[poly];
FX effect1;

unsigned long msNow = millis();
unsigned long noteTime = msNow;
unsigned long envTime = msNow;
int noteDelta = 250;
int envDelta = 4;
int scale [] = {0, 2, 4, 0, 7, 9, 0, 0, 0, 0, 0};

#if IS_CAPABLE()
void audioPostProcess(int32_t &left, int32_t &right) {
  int32_t outL, outR;
  effect1.reverbStereoInterp(left, right, outL, outR);
  left = outL;
  right = outR;
}
#endif

void setup() {
  Serial.begin(115200);
  // tone
  wavetable.sawGen(); // allocate and fill the shared wavetable
  for (int i=0; i<poly; i++) {
    osc[i].setTable(wavetable); // use the same wavetable for each osc to save memory
    osc[i].setPitch(60);
    env[i].setAttack(30);
    filter[i].setRes(0);
    filter[i].setFreq(3000);
  }
  // reverb setup
  #if IS_CAPABLE() //8266 can't manage reverb as well
    effect1.setReverbSize(4); // quality, decay and memory >= 1
    effect1.setReverbLength(0.6); // 0-1 feedback level
    effect1.setReverbMix(0.7); // 0-1 balance between dry and wet signals
    effect1.initReverbSafe();
  #endif
  #if IS_CAPABLE()
    // Run reverb after M16 combines the per-core voice partitions.
    setAudioPostProcessCallback(audioPostProcess);
  #endif
  // seti2sPins(38,39,40,41);
  // useInternalDAC(); // enable internal DAC output, call before audioStart()
  #if IS_CAPABLE()
    setIsDualCore(true);  // ESP32 and RP2040 partition independent voices.
  #else
    setIsDualCore(false);
  #endif
  noteTime = millis(); // schedule first note
  envTime = millis();
  audioStart();
}

void loop() {
  #if IS_RP2040()
    audioLoop(); // Pico worker service; a harmless no-op on Pico 2's auto worker.
  #endif

  msNow = millis();

  if ((unsigned long)(msNow - noteTime) >= noteDelta) {
      noteTime += noteDelta;
    for (int i=0; i<poly; i++){
      if (random(10) < 5) {
        int p = pitchQuantize(random(42) + 36, scale, 0);
        osc[i].setPitch(p);
        filter[i].setFreq(min(3000.0f, mtof(p + 24)));
        env[i].start();
      }
    }
  }

  if ((unsigned long)(msNow - envTime) >= envDelta) {
      envTime += envDelta;
    for (int i=0; i<poly; i++) {
      env[i].next();
    }
  }
}

// Note: use audioPartitionOffset/Stride/audioBlockWrite whenever audioUpdate()
// loops over arrays of per-voice stateful objects (Osc[], SVF[], Env[], etc.).
// Both cores run the full loop otherwise, advancing every voice state twice per
// sample — causing doubled frequency and filter corruption.
// On dual-core ESP32 and Pico-family partitioned block mode, both cores render.
// audioPartitionOffset() / audioPartitionStride() split the voice array so
// Core 0 owns even voices (0, 2, …) and Core 1 owns odd voices (1, 3, …),
// preventing both cores from advancing the same filter/oscillator state.
// Each core submits its partial mix through audioBlockWrite(). M16
// combines both partitions and then invokes audioPostProcess() on the full mix,
// so every voice feeds the shared reverb.
// Core 1 safely renders both partitions when the Core-0 worker misses a job.
void audioUpdate() {
  int32_t mix = 0;
  for (int i = audioPartitionOffset(); i < poly; i += audioPartitionStride()) {
    // Envelopes are derived from M16's audio-frame clock, so they must still be
    // evaluated while silent to notice the next attack. Avoid advancing the
    // oscillator or filter until the envelope actually contributes a sample.
    uint16_t envLevel = env[i].getValue();
    if (envLevel == 0) continue;

    int32_t voice = filter[i].nextLPF((osc[i].next() * envLevel) >> 16);
    voice *= 0.6; // compensate for poly mix
    mix += voice;
  }
  audioBlockWrite(mix, mix);
}
