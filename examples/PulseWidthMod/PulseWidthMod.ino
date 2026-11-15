// M16 pulse width mod example
#include "M16.h" 
#include "Osc.h"
#include "SVF.h"
#include "Gain.h"

Osc osc1, lfo1;
SVF filter1;
Gain outputGain(500);
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long widthTime = msNow;
unsigned long pitchDelta = 12000;
unsigned long lfoReadRate = 29; // duty target update interval in milliseconds
float pVal = 0.5;
const float pulseWidthCentre = 0.25f; 
const float pulseWidthDeviation = 0.2f;
  
void setup() {
  Serial.begin(115200);
  osc1.sqrGen(); // fill wavetable, try other waves e.g. sinGen, sawGen, triGen
  lfo1.triGen();
  osc1.setPitch(57);
  filter1.setFreq(1500);
  lfo1.setFreq(0.4); 
  osc1.setPulseWidthSmoothing((float)lfoReadRate); // ramp between control-rate duty updates
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
    osc1.setPitch(pitch);
  }
  
  if (msNow - widthTime >= lfoReadRate) {
    widthTime += lfoReadRate;
    // Map the bipolar LFO.
    // Pulse width changes the waveform shape; the master frequency stays fixed.    
    float lfo1Val = pulseWidthCentre + (lfo1.atTime(msNow) * MAX_16_INV) * pulseWidthDeviation;
    osc1.setPulseWidth(lfo1Val); // library accepts 0.05-0.95
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  int32_t leftVal = outputGain.next(filter1.nextLPF(osc1.next()));
  // int32_t leftVal = outputGain.next(osc1.next());
  int32_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}
