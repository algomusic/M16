// M32 Sinewave example
#include "M32.h"
#include "Osc.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sineTable);
int16_t vol = 1000; // 0 - 1024, 10 bit

//float phase_increment_fractional;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill the wavetable
  aOsc1.setPitch(69);
//  phase_increment_fractional = 220 / 440.0f * TABLE_SIZE / 109.25f;
  audioStart();
//  delay(200);
//  Serial.println(aOsc1.getPhase());
}



void loop() {
// int pitch = random(24) + 58;
// Serial.println(pitch);
// aOsc1.setPitch(pitch);
// Serial.print(sineTable[tableIndex++]);Serial.print(" ");Serial.println(aOsc1.getPhase());
// if (tableIndex > TABLE_SIZE) tableIndex = 0;
// Serial.print(aOsc1.next());Serial.print(" ");Serial.println(aOsc1.getPhase());
// Serial.println(millis());
// delay(1000);
// if (fillAudio) {
//  audioUpdate();
//  fillAudio = false;
// }
  audioUpdate();
}

/* This function is required in all M32 programs 
* to specify the sample values to be played
*/
void audioUpdate() {
  int16_t leftVal = (aOsc1.next() * vol)>>10;
  int16_t rightVal = leftVal;  
  i2s_write_samples(leftVal, rightVal);
}
