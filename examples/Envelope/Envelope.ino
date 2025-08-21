// M16 Envelope example
#include "M16.h" 
#include "Osc.h"
#include "Env.h"

int16_t waveTable[TABLE_SIZE]; // empty wavetable
Osc osc1(waveTable);
Env ampEnv;
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long envTime = msNow;
int pitchTimeDelta = 1000;
int envTimeDelta = 1;
int currEnvValue = 0;
int8_t pitchClass [] = {0,2,4,7,9}; // major pentatonic

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::triGen(waveTable); // fill the wavetable
  osc1.setPitch(69);
  ampEnv.setAttack(5);
  ampEnv.setRelease(500);
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - envTime) >= envTimeDelta) {
      envTime += envTimeDelta; 
      currEnvValue = ampEnv.next();
  }

  if ((unsigned long)(msNow - pitchTime) >= pitchTimeDelta) {
      pitchTime += pitchTimeDelta;
    int pitch = pitchQuantize(random(24) + 58, pitchClass, 0);
    Serial.println(pitch);
    osc1.setPitch(pitch);
    ampEnv.start();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t leftVal = (osc1.next() * currEnvValue)>>16;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
