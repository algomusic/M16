// M16 pulse width mod example
#include "M16.h" 
#include "Osc.h"
#include "SVF.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
int16_t lfoTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
Osc LFO1(lfoTable); // use an oscillator as an LFO
SVF filter;
int16_t vol = 500; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long widthTime = msNow;
int lfoReadRate = 29; // update delta time in millis
float pVal = 0.5;
  
void setup() {
  Serial.begin(115200);
  Osc::triGen(lfoTable); // fill wavetable
  Osc::sqrGen(waveTable); // fill wavetable, try other waves e.g. sinGen, sawGen, triGen
  aOsc1.setPitch(57);
  filter.setFreq(1500);
  LFO1.setFreq(0.1); 
  aOsc1.setPulseWidth(0.25);
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow - pitchTime > 12000 || msNow - pitchTime < 0) {
      pitchTime = msNow;
    int pitch = random(24) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
  
  if (msNow - widthTime > lfoReadRate || msNow - widthTime < 0) {
      widthTime = msNow;
    // Compute the LFO value to modulate the duty cycle amount (freqency) by
    // = osc val / osc range * depth * val range reduction + offset (to make unipolar)
    float lfo1Val = (LFO1.atTime(msNow) * MAX_16_INV * 0.5) * 0.6 + 0.4; 
    aOsc1.setPulseWidth(lfo1Val); // 0.0 - 1.0
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (filter.nextLPF(aOsc1.next()) * vol)>>10;
  // uint16_t leftVal = (aOsc1.next() * vol)>>10;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
