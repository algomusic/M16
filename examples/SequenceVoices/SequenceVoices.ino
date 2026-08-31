// M16 sequencer example
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Seq.h"
#include "FX.h"

WaveTable sawTable; // one wavetable shared by every voice

unsigned long msNow = millis();
unsigned long stepTime = msNow;
unsigned long envTime = msNow;

unsigned long stepDelta = 250;
unsigned long envDelta = 4;
int stepCnt = 0;
int pent [] = {0, 2, 4, 7, 9};
const int voices = 4; // change to alter texture and adjust for different CPU capabilities

Osc oscillators[voices];
Env ampEnvs[voices];
SVF filters[voices];
FX effect1;
Seq sequences[voices];

#if IS_CAPABLE()
void audioPostProcess(int32_t &left, int32_t &right) {
  int32_t outL, outR;
  effect1.reverbStereo(clip16(left), clip16(right), outL, outR);
  left = outL;
  right = outR;
}
#endif

void seqGen() {
  for (int i=0; i<voices; i++) {
    for (int j=0; j<16; j++) {
      if (rand(max(2, voices / 2) + 1) == 0) {
        sequences[i].setStepValue(j, pitchQuantize(rand(48) + 36, pent, 0));
      } else sequences[i].setStepValue(j, 0);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  sawTable.sawGen(); // allocate and fill the shared wavetable
  for (int i=0; i<voices; i++) {
    oscillators[i].setTable(sawTable); // assign all osc to the same wavetable to save memory
    oscillators[i].setPitch(60);
    oscillators[i].setSpread(rand(100) * 0.000001);
    ampEnvs[i].setAttack(10);
    filters[i].setFreq(rand(4000) + 1000);
  }
  seqGen();
  effect1.setReverbSize(16);
  effect1.setReverbLength(0.25);
  effect1.setReverbMix(0.2);
  effect1.initReverbSafe();
  #if IS_CAPABLE()
    // Run reverb after M16 combines the per-core voice partitions.
    setAudioPostProcessCallback(audioPostProcess);
  #endif
  // seti2sPins(25, 27, 12, 21); // or similar if required
  // useInternalDAC();
  #if IS_CAPABLE()
    setIsDualCore(true);  // ESP32 and RP2040 partition independent voices.
  #else
    setIsDualCore(false);
  #endif
  audioStart();
}

void loop() {
  #if IS_RP2040()
    audioLoop(); // Pico worker service; a harmless no-op on Pico 2's auto worker.
  #endif

  msNow = millis();
 
  if (msNow - stepTime >= stepDelta) {
      stepTime += stepDelta;
    if (stepCnt%64 == 0) seqGen();
    for (int i=0; i<voices; i++) {
      int p = sequences[i].next();
      oscillators[i].setPitch(p);
      if (p > 0) {
        ampEnvs[i].setMaxLevel((rand(5) + 5) * 0.1);
        ampEnvs[i].start();
      }
    }
    stepCnt++;
  }

  if (msNow - envTime >= envDelta) {
      envTime += envDelta;
    for (int i=0; i<voices; i++) {
      ampEnvs[i].next();
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
  for (int i = audioPartitionOffset(); i < voices; i += audioPartitionStride()) {
    mix += ((filters[i].nextLPF(oscillators[i].next()) * ampEnvs[i].getValue()) >> 15) * 0.6;
  }
  audioBlockWrite(mix, mix);
}
