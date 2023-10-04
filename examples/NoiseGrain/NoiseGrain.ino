// M16 Grainy Noise example
#include "M16.h" 
#include "Osc.h"
#include "Env.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
Env ampEnv;
unsigned long msNow, changeTime, envTime;

void setup() {
  Serial.begin(115200);
  Osc::noiseGen(waveTable, 1); // fill the wavetable
  aOsc1.setNoise(true);
  ampEnv.setRelease(1000);
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow - changeTime > 1000 || msNow - changeTime < 0) {
      changeTime = msNow;
    int grain = random(500)+1;
    Osc::noiseGen(waveTable, grain);
    Serial.println(grain);
    ampEnv.start();
  }

  if (msNow - envTime > 4 || msNow - envTime < 0) {
      envTime = msNow;
    ampEnv.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (aOsc1.next() * ampEnv.getValue())>>16;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
