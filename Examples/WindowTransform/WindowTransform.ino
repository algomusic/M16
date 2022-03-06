// M16 Window Transform example

#include "M16.h"
#include "Osc.h"

int16_t sineWave [TABLE_SIZE]; // empty wavetable
int16_t squareWave [TABLE_SIZE]; // empty wavetable
int16_t triangleWave [TABLE_SIZE]; // empty wavetable
int16_t sawtoothWave [TABLE_SIZE]; // empty wavetable
Osc osc1(sineWave); // experiment with different waveform combinations
Osc lfo(sineWave);

unsigned long msNow, windowTime;
float windowSize = 0;
bool expanding = true;

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  Osc::sinGen(sineWave); // fill
  Osc::sqrGen(squareWave); // fill
  Osc::triGen(triangleWave); // fill
  Osc::sawGen(sawtoothWave); // fill
  osc1.setPitch(48);
  lfo.setFreq(0.1); // Hertz
//  osc1.setSpread(0.005);
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  msNow = millis();
  if (msNow > windowTime) {
    windowTime = msNow + 10;
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
* For ESP32 programs this function is called in the background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  // nextWTrans args: second wave, amount of second wave, duel window?, invert second wave?
  uint16_t leftVal = osc1.nextWTrans(sawtoothWave, windowSize, false, false); 
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
