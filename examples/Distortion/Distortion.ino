// M16 plucked string and arpeggiator example
#include "M16.h"
#include "Osc.h"
#include "Env.h"
// #include "Arp.h"
#include "SVF.h"
#include "FX.h"

Osc aOsc;
Env ampEnv;
// Arp arp1;
SVF filter;
FX effect1;
int bpm = 120;
int32_t currEnvValue = 0;

unsigned long msNow = millis();
unsigned long stepTime = msNow;
unsigned long envTime = msNow;
int16_t vol = 1000; // 0 - 1024, 10 bit
float feedback = 0.9;
float pitch = 48;
int stepDelta = 500;
int envDelta = 1;
int distLevel = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  ampEnv.setAttack(0);
  ampEnv.setRelease(800);
  aOsc.triGen();
  int newSet [] = {48, 52, 55, 58, 60, 64};
  aOsc.setPitch(pitch);
  filter.setFreq(3500);
  audioStart();
}

void loop() {
  msNow = millis();
  
  if ((unsigned long)(msNow - stepTime) >= stepDelta) {
      stepTime += stepDelta;
    pitch = clip(pitch + gaussRandNumb(24, 2) - 12, 36, 68); 
    Serial.println(pitch);
    aOsc.setPitch(pitch);
    vol = 200 + rand(800);
    aOsc.setPhase(0);
    distLevel = rand(10) + 1;
    Serial.println(distLevel);
    ampEnv.start();
  }

  if ((unsigned long)(msNow - envTime) >= envDelta) {
    envTime += envDelta;
    currEnvValue = ampEnv.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t leftVal = aOsc.next() * ampEnv.getValue() >> 16;
  leftVal = effect1.overdrive(leftVal, distLevel); // applies some gain and distortion
  // leftVal = effect1.softClip(leftVal, distLevel * 3); // applies some compression and saturation
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
