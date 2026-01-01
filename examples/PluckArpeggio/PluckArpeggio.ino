// M16 plucked string and arpeggiator example
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Arp.h"
#include "SVF.h"
#include "FX.h"

// int16_t noiseTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1;
Env ampEnv1;
Arp arp1;
SVF filter;
FX effect1;
int bpm = 120;
int stepDelta = 1000;
int envDelta = 1;
int32_t currEnvValue = 0;
unsigned long msNow, stepTime, envTime;
int16_t vol = 1000; // 0 - 1024, 10 bit
float feedback = 0.9;

void setup() {
  Serial.begin(115200);
  delay(200);
  aOsc1.noiseGen(); aOsc1.setNoise(true); // fill internal wavetable and set noise flag
  ampEnv1.setAttack(0);
  ampEnv1.setRelease(2);
  int newSet [] = {48, 52, 55, 58, 60, 64};
  arp1.setValues(newSet, 6);
  arp1.setDirection(ARP_UP_DOWN);
  arp1.setRange(3);
  stepDelta = arp1.calcStepDelta(120, 2); // ms between steps at bpm sliced into 2
  stepTime = millis() + stepDelta;
  arp1.start();
  aOsc1.setPitch(arp1.next());
  filter.setFreq(10000);
  stepTime = millis();
  envTime = millis();
  // seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN
  audioStart();
}

void loop() {
  msNow = millis();
  
  if ((unsigned long)(msNow - stepTime) >= stepDelta) {
    stepTime += stepDelta; 
    int pitch = arp1.next(); 
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    feedback = floatMap(pitch, 48, 88, 0.96, 0.995);
    vol = 600 + rand(400);
    filter.setFreq(rand(3000) + 1000);
    ampEnv1.start();
  }

  if ((unsigned long)(msNow - envTime) >= envDelta) {
    envTime += envDelta; 
    currEnvValue = ampEnv1.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t leftVal = (filter.nextLPF(effect1.pluck((aOsc1.next() * currEnvValue) >> 16, aOsc1.getFreq(), feedback)) * vol)>>10;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
