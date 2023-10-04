// M16 Example - polyphony
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "FX.h"

int16_t waveTable[TABLE_SIZE]; // empty array
const int poly = 2; // change polyphony as desired, each MCU type will handle particular amounts
Osc osc[poly];
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
  Osc::sawGen(waveTable);
  for (int i=0; i<poly; i++) {
    osc[i].setTable(waveTable);
    osc[i].setPitch(60);
    env[i].setAttack(30);
    filter[i].setRes(0);
    filter[i].setFreq(3000);
  }
  // reverb setup
  #if IS_ESP32() //8266 can't manage reverb as well
    effect1.setReverbSize(16); // quality and memory >= 1
    effect1.setReverbLength(0.6); // 0-1
    effect1.setReverbMix(0.7); // 0-1
  #endif
  // setI2sPins(25, 27, 12, 21);
  audioStart();
}

void loop() {
  msNow = millis();

  // Serial.print(msNow);Serial.print(" ");Serial.print(noteTime);Serial.print(" ");Serial.println(msNow - noteTime);
  if (msNow - noteTime > noteDelta || msNow - noteTime < 0) {
    noteTime = msNow;
    for (int i=0; i<poly; i++){
      if (random(10) < 5) {
        int p = pitchQuantize(random(36) + 48, scale, 0);
        osc[i].setPitch(p);
        filter[i].setFreq(min(3000.0f, mtof(p + 24)));
        env[i].start();
      }
    }
  }

  if (msNow - envTime > envDelta || msNow - envTime < 0) {
    envTime = msNow;
    for (int i=0; i<poly; i++) {
      env[i].next();
    }
  }
}

void audioUpdate() {
  int32_t mix = 0;
  for (int i=0; i<poly; i++) {
    #if IS_ESP8266()
      mix += filter[i].simpleLPF(filter[i].simpleLPF((osc[i].next() * env[i].getValue())>>16));
    #elif IS_ESP32()
      mix += filter[i].nextLPF((osc[i].next() * env[i].getValue())>>16);
    #endif
    mix *= 0.6;
  }
  // stereo
  int16_t leftVal = mix; 
  int16_t rightVal = leftVal;
  #if IS_ESP32() //8266 can't manage reverb as well
    effect1.reverbStereo(mix, mix, leftVal, rightVal);
  #endif
  i2s_write_samples(leftVal, rightVal);
}