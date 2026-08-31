// M16 pulse width mod example
#include "M16.h" 
#include "Osc.h"
#include "SVF.h"
#include "Gain.h"

Osc aOsc1, LFO1;
SVF filter;
Gain outputGain(500);
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long widthTime = msNow;
unsigned long pitchDelta = 12000;
unsigned long lfoReadRate = 29; // update delta time in millis
float pVal = 0.5;
  
void setup() {
  Serial.begin(115200);
  aOsc1.sqrGen(); // fill wavetable, try other waves e.g. sinGen, sawGen, triGen
  LFO1.triGen();
  aOsc1.setPitch(57);
  filter.setFreq(1500);
  LFO1.setFreq(0.1); 
  aOsc1.setPulseWidth(0.25);
  // seti2sPins(16, 17, 18, 21); // BCK, WS, DOUT, DIN
  // useInternalDAC();
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow - pitchTime >= pitchDelta) {
    pitchTime += pitchDelta;
    int pitch = random(24) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
  
  if (msNow - widthTime >= lfoReadRate) {
    widthTime += lfoReadRate;
    // Compute the LFO value to modulate the duty cycle amount (freqency) by
    // = osc val / osc range * depth * val range reduction + offset (to make unipolar)
    float lfo1Val = (LFO1.atTime(msNow) * MAX_16_INV * 0.5) * 0.6 + 0.4; 
    aOsc1.setPulseWidth(lfo1Val); // 0.0 - 1.0
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  int32_t leftVal = outputGain.next(filter.nextLPF(aOsc1.next()));
  // int32_t leftVal = outputGain.next(aOsc1.next());
  int32_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}
