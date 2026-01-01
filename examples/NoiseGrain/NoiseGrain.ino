// M16 Grainy Noise example
#include "M16.h" 
#include "Osc.h"
#include "Env.h"

// int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1;
Env ampEnv;
unsigned long msNow, changeTime, envTime;
int changeDelta = 1000;
int envDelta = 4;

void setup() {
  Serial.begin(115200);
  aOsc1.noiseGen(1); // fill the wavetable with a specified grain size
  aOsc1.setNoise(true);
  ampEnv.setRelease(1000);
  // seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN
  audioStart();
}

void loop() {
  msNow = millis();
  
  if ((unsigned long)(msNow - changeTime) >= changeDelta) {
    changeTime += changeDelta;
    int grain = random(1000)+1;
    aOsc1.noiseGen(grain);
    Serial.println(grain);
    ampEnv.start();
  }

  if ((unsigned long)(msNow - envTime) >= envDelta) {
    envTime += envDelta; 
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
