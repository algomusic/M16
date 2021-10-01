// M16 Oscillator morphing example
#include "M16.h" 
#include "Osc.h"

int16_t sawTable [TABLE_SIZE]; // empty wavetable
int16_t triTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sawTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
float morphVal = 0;
bool morphUp = true;
unsigned long ms, noteTime, morphTime;

void setup() {
  Serial.begin(115200);
  Osc::sawGen(sawTable); // fill the wavetable
  Osc::triGen(triTable); // fill the wavetable
  aOsc1.setPitch(440);
  lfo1.setFreq(2);
  audioStart();
}

void loop() {
  ms = millis();
  if (ms > noteTime) {
    noteTime = ms + 1000;
   int pitch = random(36) + 48;
   aOsc1.setPitch(pitch);
  }

  if (ms > morphTime) {
    morphTime = ms + 32;
    if (morphUp) {
      morphVal += 0.01;
      if (morphVal > 1.0) {
        morphVal = 1.0;
        morphUp = false;
      }
    } else {
      morphVal -= 0.01;
      if (morphVal < 0) {
        morphVal = 0;
        morphUp = true;
      }
    }
  }
}

/* This function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  uint16_t leftVal = (aOsc1.nextMorph(triTable, morphVal) * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
