// M16 Reverb Example 
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "FX.h"

int16_t waveTable [TABLE_SIZE]; // empty array
Osc osc1(waveTable);
Env ampEnv1;
SVF filter1;
FX effect1;

unsigned long msNow, noteTime, envTime, delTime;
int scale [] = {0, 2, 4, 5, 7, 9, 0, 0, 0, 0, 0};
int reverbLength = 900; // 0 - 1024
int reverbMix = 300; // 0 - 1024 // 150

void setup() {
  Serial.begin(115200);
  // tone
  Osc::sawGen(waveTable);
  osc1.setPitch(60);
  ampEnv1.setAttack(30);
  ampEnv1.setMaxLevel(0.8);
  filter1.setResonance(0);
  filter1.setFreq(3000);
  // reverb
  #if IS_ESP8266()
    effect1.setReverbSize(8); // quality and memory >= 1 
  #elif IS_ESP32()
    effect1.setReverbSize(16); // quality and memory >= 1 
  #endif
  effect1.setReverbLength(970); // 0-1024
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > noteTime) {
    noteTime = msNow + 1000;
    int p = pitchQuantize(random(25) + 48, scale, 0);
    osc1.setPitch(p);
    filter1.setFreq(min(3000.0f, mtof(p + 24)));
    ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime = msNow + 4;
    ampEnv1.next();
  }
}

void audioUpdate() {
  #if IS_ESP8266()
    int16_t oscVal = (osc1.next() * ampEnv1.getValue())>>16;
  #elif IS_ESP32()
    int16_t oscVal = filter1.nextLPF((osc1.next() * ampEnv1.getValue())>>16);
  #endif
  
  // stereo
  int16_t leftVal, rightVal;
  effect1.reverbStereo(oscVal, oscVal, leftVal, rightVal);
  i2s_write_samples(leftVal, rightVal);
}