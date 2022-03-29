// M16 pulse width mod example
#include "M16.h" 
#include "Osc.h"
#include "SVF.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
int16_t lfoTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
Osc LFO1(lfoTable); // use an oscillator as an LFO
SVF filter;
int16_t vol = 200; // 0 - 1024, 10 bit
unsigned long msNow, pitchTime, widthTime;
int lfoReadRate = 29; // update delta time in millis
float pVal = 0.5;
  
void setup() {
  Serial.begin(115200);
  Osc::sinGen(lfoTable); // fill wavetable
  Osc::triGen(waveTable); // fill wavetable, try other waves e.g. sqrGen, sawGen, triGen
  aOsc1.setPitch(69);
  filter.setCentreFreq(2500);
  LFO1.setFreq(0.2); 
  aOsc1.setPulseWidth(0.25);
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  msNow = millis();
  
  if (msNow > pitchTime) {
    pitchTime = msNow + 5000;
    int pitch = random(24) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
  
  if (msNow > widthTime) {
    widthTime = msNow + lfoReadRate; 
    // Compute the LFO value to modulate the duty cycle amount (freqency) by
    // = osc val / osc range * depth * val range reduction + offset (to make unipolar)
    float lfo1Val = (LFO1.atTime(msNow) * MAX_16_INV * 0.8) * 0.5 + 0.5; 
    aOsc1.setPulseWidth(lfo1Val);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* For ESP32 programs this function is called in teh background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  uint16_t leftVal = (filter.nextLPF(aOsc1.next()) * vol)>>10;
//  uint16_t leftVal = (aOsc1.next() * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
