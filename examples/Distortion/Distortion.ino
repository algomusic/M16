// M16 plucked string and arpeggiator example
#include "M16.h"
#include "Osc.h"
#include "Env.h"
// #include "Arp.h"
#include "SVF.h"
#include "FX.h"

int16_t noiseTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(noiseTable);
Env ampEnv1;
// Arp arp1;
SVF filter;
FX effect1;
int bpm = 120;

unsigned long stepDelta = 250;
unsigned long msNow = millis();
unsigned long stepTime = msNow;
unsigned long envTime = msNow;
int16_t vol = 1000; // 0 - 1024, 10 bit
float feedback = 0.9;
float pitch = 48;

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::noiseGen(noiseTable); aOsc1.setNoise(true); // fill wavetable and set noise flag
  ampEnv1.setAttack(0);
  ampEnv1.setRelease(2);
  int newSet [] = {48, 52, 55, 58, 60, 64};
  // arp1.setValues(newSet, 6);
  // arp1.setDirection(ARP_UP_DOWN);
  // arp1.setRange(3);
  // stepDelta = arp1.calcStepDelta(120, 2); // ms between steps at bpm sliced into 2
  // stepTime = millis() + stepDelta;
  // arp1.start();
  aOsc1.setPitch(pitch);
  filter.setFreq(3500);
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow > stepTime) {
    int deltaMult = rand(2) + 1;
    stepTime = msNow + stepDelta * deltaMult;
    pitch = clip(pitch + gaussRandNumb(24, 2) - 12, 36, 68); 
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    feedback = floatMap(pitch, 36, 72, 0.96, 0.995);
    vol = 800 + rand(200);
    filter.setFreq(rand(2000) + 3000);
    ampEnv1.start();
  }

  if (msNow - envTime > 4 || msNow - envTime < 0) {
    envTime = msNow;
    ampEnv1.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  // int16_t leftVal = (filter.nextLPF(effect1.pluck((aOsc1.next() * ampEnv1.getValue() >> 16), aOsc1.getFreq(), feedback)) * vol)>>10;
  int16_t leftVal = (effect1.pluck((aOsc1.next() * ampEnv1.getValue() >> 16), aOsc1.getFreq(), feedback) * vol)>>10;
  leftVal = effect1.overdrive(leftVal, 2.5); // applies some gain and distortion
  // leftVal = effect1.softClip(leftVal, 2.5); // applies some compression and distortion
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
