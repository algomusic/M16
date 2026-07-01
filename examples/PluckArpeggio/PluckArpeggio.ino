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
SVF exciterFilter;
SVF dampeningFilter;
FX effect1;
int bpm = 120;
int stepDelta = 1000;
unsigned long msNow, stepTime;
int16_t vol = 1000; // 0 - 1024, 10 bit
float feedback = 0.9;

void setup() {
  Serial.begin(115200);
  delay(200);
  aOsc1.noiseGen(); aOsc1.setNoise(true); // fill internal wavetable and set noise flag
  ampEnv1.setAttack(1);
  ampEnv1.setDecay(150);
  ampEnv1.setSustain(0.02);
  ampEnv1.setHold(5);
  ampEnv1.setRelease(80);
  int newSet [] = {48, 52, 55, 58, 60, 64};
  arp1.setValues(newSet, 6);
  arp1.setDirection(ARP_UP_DOWN);
  arp1.setRange(3);
  stepDelta = arp1.calcStepDelta(120, 2); // ms between steps at bpm sliced into 2
  stepTime = millis() + stepDelta;
  arp1.start();
  aOsc1.setPitch(arp1.next());
  exciterFilter.setFreq(SAMPLE_RATE / 2); // at the Nyquist to avoid aliasing
  dampeningFilter.setFreq(10000);
  stepTime = millis();
  // seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN
  // useInternalDAC();
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
    dampeningFilter.setFreq(rand(3000) + 1000);
    ampEnv1.start();
  }
}

/* The audioUpdate function is required in all M16 programs
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* Read the envelope per audio sample with getValue() for a smooth pluck excitation.
*/
void audioUpdate() {
  int32_t leftVal = (dampeningFilter.nextLPF(effect1.pluck((exciterFilter.nextLPF(aOsc1.next()) * ampEnv1.getValue()) >> 16, aOsc1.getFreq(), feedback)) * vol)>>10;
  // int32_t leftVal = (dampeningFilter.nextLPF(effect1.waveguide((exciterFilter.nextLPF(aOsc1.next()) * ampEnv1.getValue()) >> 16, aOsc1.getFreq(), feedback)) * vol)>>10;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
