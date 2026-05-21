// M16 sequencer example
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Seq.h"
#include "FX.h"

int16_t * sawTable; // empty array pointer

unsigned long msNow = millis();
unsigned long stepTime = msNow;
unsigned long envTime = msNow;

int stepDelta = 250;
int envDelta = 4;
int stepCnt = 0;
int pent [] = {0, 2, 4, 7, 9};
const int voices = 4; // change to alter texture and adjust for different CPU capabilities

Osc oscillators[voices];
Env ampEnvs[voices];
SVF filters[voices];
FX effect1;
Seq sequences[voices];

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
  Osc::allocateWaveMemory(&sawTable); // init the wavetable
  Osc::sawGen(sawTable); // fill with a sawtooth waveform
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
  // seti2sPins(25, 27, 12, 21); // or similar if required
  // useInternalDAC();
  audioStart();
}

void loop() {
  msNow = millis();
 
  if ((unsigned long)(msNow - stepTime) >= stepDelta) {
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

  if ((unsigned long)(msNow - envTime) >= envDelta) {
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
// On dual-core ESP32, audioUpdate() runs simultaneously on both cores.
// audioPartitionOffset() / audioPartitionStride() split the voice array so
// Core 0 owns even voices (0, 2, …) and Core 1 owns odd voices (1, 3, …),
// preventing both cores from advancing the same filter/oscillator state.
// Each core accumulates its partial mix and calls audioBlockWrite(), which
// buffers M16_BLOCK_SIZE samples before synchronising: Core 1 signals Core 0,
// Core 0 combines both partials and writes one DMA burst, then releases Core 1.
// audioIsFinalizerCore() is true only on Core 0, so shared stateful effects
// (reverb) run once on the finaliser path rather than once per partial.
// On single-core targets the partition helpers are no-ops and audioBlockWrite
// behaves like i2s_write_samples().
void audioUpdate() {
  int32_t mix = 0;
  for (int i = audioPartitionOffset(); i < voices; i += audioPartitionStride()) {
    mix += ((filters[i].nextLPF(oscillators[i].next()) * ampEnvs[i].getValue()) >> 15) * 0.6;
  }
  #if IS_CAPABLE() // bypass reverb on ESP8266
  if (audioIsFinalizerCore()) {
    int32_t leftOut, rightOut;
    effect1.reverbStereo(clip16(mix), clip16(mix), leftOut, rightOut);
    audioBlockWrite(leftOut, rightOut);
    return;
  }
  #endif
  audioBlockWrite(mix, mix);
}
