// M16 Example - polyphony
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "FX.h"

int16_t * wavetable; // empty array pointer
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

void setup() {
  Serial.begin(115200);
  // tone
  Osc::allocateWaveMemory(&wavetable);
  Osc::sawGen(wavetable); // fill the wavetable
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
  // seti2sPins(38,39,40,41);
  // useInternalDAC(); // enable internal DAC output, call before audioStart()
  noteTime = millis(); // schedule first note
  envTime = millis();
  audioStart();
}

void loop() {

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
  for (int i = audioPartitionOffset(); i < poly; i += audioPartitionStride()) {
    int32_t voice = filter[i].nextLPF((osc[i].next() * env[i].getValue()) >> 16);
    voice *= 0.6; // compensate for poly mix
    mix += voice;
  }
  #if IS_CAPABLE()
  if (audioIsFinalizerCore()) {
    int32_t L = mix, R = mix;
    effect1.reverbStereoInterp(mix, mix, L, R);
    audioBlockWrite(L, R);
    return;
  }
  #endif
  audioBlockWrite(mix, mix);
}
