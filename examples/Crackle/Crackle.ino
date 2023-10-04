// M16 Noise Crackle example
// Crackle charracter is randomly varied every 5 seconds
#include "M16.h" 
#include "Osc.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long changeTime = msNow;

void setup() {
  Serial.begin(115200);
  Osc::crackleGen(waveTable); // fill the wavetable
  aOsc1.setCrackle(true, 1000); // 0 - MAX_16
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - changeTime > 5000 || msNow - changeTime < 0) {
      changeTime = msNow;
    int16_t cAmnt = random(MAX_16);
    aOsc1.setCrackle(true, cAmnt); // 0 - MAX_16
    Serial.println(cAmnt);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (aOsc1.next() * vol)>>10;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
