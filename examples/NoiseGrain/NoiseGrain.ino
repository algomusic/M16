// M16 Grainy Noise example
#include "M16.h" 
#include "Osc.h"
#include "Env.h"

Osc aOsc1;
Env ampEnv;
unsigned long msNow, changeTime, envTime;
unsigned long changeDelta = 1000;
unsigned long envDelta = 4;

void setup() {
  Serial.begin(115200);
  aOsc1.noiseGen(1); // fill the wavetable with a specified grain size
  aOsc1.setNoise(true);
  ampEnv.setRelease(1000);
  // seti2sPins(16, 17, 18, 21); // BCK, WS, DOUT, DIN
  // useInternalDAC();
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow - changeTime >= changeDelta) {
    changeTime += changeDelta;
    int grain = random(1000)+1;
    aOsc1.noiseGen(grain);
    Serial.println(grain);
    ampEnv.start();
  }

  if (msNow - envTime >= envDelta) {
    envTime += envDelta; 
    ampEnv.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  int16_t leftVal = (aOsc1.next() * ampEnv.getValue())>>16;
  int16_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}
