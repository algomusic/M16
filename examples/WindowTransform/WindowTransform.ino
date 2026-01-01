// M16 Window Transform example
// Inspired by the Window Transform Function by Dove Audio
// This example morphs between sine and sawtooth waveshapes
#include "M16.h"
#include "Osc.h"

// int16_t sineWave[TABLE_SIZE]; // empty wavetable
int16_t * sawtoothWave; // empty wavetable
Osc osc1; // experiment with different waveform combinations
Osc lfo;

unsigned long msNow, windowTime;
float windowSize = 0;
bool expanding = true;
int windowDelta = 10;

void setup() {
  Serial.begin(115200);
  delay(1000);
  osc1.sinGen(); // fill
  // osc1.sqrGen(); // try other waveshapes
  // osc1.triGen(); 
  Osc::allocateWaveMemory(&sawtoothWave); // init
  Osc::sawGen(sawtoothWave); // fill
  osc1.setPitch(48);
  lfo.sinGen(); // fill
  lfo.setFreq(0.1); // Hertz
  // osc1.setSpread(0.005); // make more complex
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change defaults
  audioStart();
}

void loop() { 
  msNow = millis();

  if ((unsigned long)(msNow - windowTime) >= windowDelta) {
    windowTime += windowDelta;
    int lfoVal = lfo.atTime(msNow);
    // windowSize = lfoVal / (float)MAX_16 / 2.0f + 0.5f;
    windowSize = lfoVal * MAX_16_INV * 0.5f + 0.5f;
    if (random(500) == 0) {
      Serial.print(" lfoVal ");Serial.print(lfoVal);
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
  int32_t leftVal = osc1.nextWTrans(sawtoothWave, windowSize, false, false); 
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
