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

unsigned long msNow = millis();
unsigned long noteTime = msNow;
unsigned long envTime = msNow;
unsigned long delTime = msNow;
int scale [] = {0, 2, 4, 5, 7, 9, 0, 0, 0, 0, 0};
int reverbLength = 900; // 0 - 1024
int reverbMix = 800; // 0 - 1024
float leftPan, rightPan;

void setup() {
  Serial.begin(115200);
  // tone
  Osc::sawGen(waveTable);
  osc1.setPitch(60);
  ampEnv1.setAttack(30); 
  ampEnv1.setRelease(300);
  ampEnv1.setMaxLevel(1.0);
  filter1.setRes(0);
  filter1.setFreq(3000);
  // reverb
  #if IS_ESP8266()
    effect1.setReverbSize(4); // room size, quality and memory >= 1 
  #elif IS_ESP32()
    effect1.setReverbSize(16); // room size, quality and memory >= 1 
  #endif
  effect1.setReverbLength(0.9); // 0.0 - 1.0
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - noteTime > 1000 || msNow - noteTime < 0) {
    noteTime = msNow;
    int p = pitchQuantize(random(25) + 48, scale, 0);
    osc1.setPitch(p);
    filter1.setFreq(min(4000.0f, mtof(p + 32)));
    float pan = rand(1000) * 0.001;
    leftPan = panLeft(pan);
    rightPan = panRight(pan);
    ampEnv1.setAttack(30 + rand(30));
    ampEnv1.setMaxLevel(rand(5) * 0.1 + 0.5);
    ampEnv1.start();
    Serial.print("Pitch: ");Serial.print(p);Serial.print(" Pan: ");Serial.println(pan);
  }

  if (msNow - envTime > 4 || msNow - envTime < 0) {
    envTime = msNow;
    ampEnv1.next();
  }
}

void audioUpdate() {
  int16_t leftVal, rightVal;
  #if IS_ESP8266()
    int16_t oscVal = (osc1.next() * ampEnv1.getValue())>>16;
    effect1.reverbStereo(oscVal, oscVal, leftVal, rightVal);
  #elif IS_ESP32()
    int16_t oscVal = filter1.nextLPF((osc1.next() * ampEnv1.getValue())>>16);
    effect1.reverbStereo(oscVal * leftPan, oscVal * rightPan, leftVal, rightVal);
  #endif
  i2s_write_samples(leftVal, rightVal);
}
