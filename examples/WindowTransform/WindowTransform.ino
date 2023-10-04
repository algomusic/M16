// M16 Window Transform example
// Inspired by the Window Transform Function by Dove Audio
// This example morphs between sine and sawtooth waveshapes
#include "M16.h"
#include "Osc.h"

int16_t sineWave[TABLE_SIZE]; // empty wavetable
// int16_t squareWave [TABLE_SIZE]; // empty wavetable
// int16_t triangleWave [TABLE_SIZE]; // empty wavetable
int16_t sawtoothWave[TABLE_SIZE]; // empty wavetable
Osc osc1(sineWave); // experiment with different waveform combinations
Osc lfo(sineWave);

unsigned long msNow, windowTime;
float windowSize = 0;
bool expanding = true;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineWave); // fill
  // Osc::sqrGen(squareWave); // try other waveshapes
  // Osc::triGen(triangleWave); 
  Osc::sawGen(sawtoothWave); // fill
  osc1.setPitch(48);
  lfo.setFreq(0.1); // Hertz
  // osc1.setSpread(0.005); // make more complex
  audioStart();
}

void loop() { 
  msNow = millis();

  if (msNow - windowTime > 10 || msNow - windowTime < 0) {
      windowTime = msNow;
    int lfoVal = lfo.atTime(msNow);
    windowSize = lfoVal / (float)MAX_16 / 2.0f + 0.5f;
    if (random(1000) == 0) {
      Serial.print(" windowSize ");Serial.println(windowSize);
    }
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  // nextWTrans args: second wave, amount of second wave, duel window?, invert second wave?
  int16_t leftVal = osc1.nextWTrans(sawtoothWave, windowSize, false, false); 
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
