// M16 Oscillator morphing example
// Waveform morphing can be a bit noisy, explore Window Transform as an alternative
#include "M16.h"
#include "Osc.h"

int16_t * triTable; // empty wavetable
Osc aOsc1;
int16_t vol = 1000; // 0 - 1024, 10 bit
float morphVal = 0;
bool morphUp = true;
unsigned long msNow = millis();
unsigned long noteTime = msNow;
unsigned long morphTime = msNow;
int noteDelta = 5000;
int morphDelta = 32;

void setup() {
  Serial.begin(115200);
  aOsc1.sinGen(); // fill the internal wavetable
  Osc::allocateWaveMemory(&triTable); // setup the external wavetable
  Osc::triGen(triTable); // fill the external wavetable
  aOsc1.setPitch(60);
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - noteTime) >= noteDelta) {
    noteTime += noteDelta;
    int pitch = random(24) + 36;
    aOsc1.setPitch(pitch);
  }

  if ((unsigned long)(msNow - morphTime) >= morphDelta) {
    morphTime += morphDelta;
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

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (aOsc1.nextMorph(triTable, morphVal) * vol)>>10;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
