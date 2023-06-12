// M16 Sinewave example
#include "M16.h" 
#include "Osc.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow, changeTime;

void setup() {
  Serial.begin(115200);
  Osc::crackleGen(waveTable); // fill the wavetable
  aOsc1.setCrackle(true, 1000); // 0 - MAX_16
  audioStart();
}

void loop() {
  msNow = millis();
  if (msNow > changeTime) {
    changeTime = msNow + 5000;
    int cAmnt = random(MAX_16);
    aOsc1.setCrackle(true, cAmnt); // 0 - MAX_16
    Serial.println(cAmnt);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  uint16_t leftVal = (aOsc1.next() * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
