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
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  msNow = millis();
  
  if (msNow > changeTime) {
    changeTime = msNow + 1000;
    int grain = random(500)+1;
    Osc::noiseGen(waveTable, grain);
    Serial.println(grain);
    ampEnv.start();
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* For ESP32 programs this function is called in teh background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  uint16_t leftVal = (aOsc1.next() * ampEnv.getValue())>>16;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
