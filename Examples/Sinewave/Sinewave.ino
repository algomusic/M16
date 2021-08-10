// M16 Sinewave example
#include "M16.h"
#include "Osc.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sineTable);
int16_t vol = 1000; // 0 - 1024, 10 bit

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill the wavetable
  aOsc1.setPitch(440);
  audioStart();
}

void loop() {
 int pitch = random(24) + 58;
 Serial.println(pitch);
 aOsc1.setPitch(pitch);
 delay(1000);
}

/* This function is required in all M16 programs 
* to specify the sample values to be played
*/
void audioUpdate() {
  uint16_t leftVal = (aOsc1.next() * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
