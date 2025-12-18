// M16 Example - polyphony
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "FX.h"

int16_t * wavetable; // empty array pointer
const int poly = 4; // change polyphony as desired, each MCU type will handle particular amounts
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
  #if IS_ESP32() //8266 can't manage reverb as well
    effect1.setReverbSize(16); // quality, decay and memory >= 1
    effect1.setReverbLength(0.6); // 0-1 feedback level
    effect1.setReverbMix(0.7); // 0-1 balance between dry and wet signals
  #endif
  // seti2sPins(7,8,9,41);
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

void audioUpdate() {
  int32_t mix = 0;
  for (int i=0; i<poly; i++) {
    mix += filter[i].nextLPF((osc[i].next() * env[i].getValue())>>16);
    mix = (mix * 717) >> 10;  // 717/1024 ≈ 0.7, integer multiply
  }
  // stereo
  int32_t leftVal = mix; 
  int32_t rightVal = leftVal;
  #if IS_ESP32() //8266 can't manage reverb as well
    effect1.reverbStereo(mix, mix, leftVal, rightVal);
  #endif
  i2s_write_samples(clip16(leftVal), clip16(rightVal));
}