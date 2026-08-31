// M16 Oscillator morphing example
// Waveform morphing can be a bit noisy, explore Window Transform as an alternative
#include "M16.h"
#include "Osc.h"
#include "Gain.h"

WaveTable triTable; // one shareable wavetable with hidden memory management
Osc aOsc1;
Gain outputGain(1000);
float morphVal = 0;
bool morphUp = true;
unsigned long msNow = millis();
unsigned long noteTime = msNow;
unsigned long morphTime = msNow;
unsigned long noteDelta = 5000;
unsigned long morphDelta = 32;

void setup() {
  Serial.begin(115200);
  aOsc1.sinGen(); // fill the internal wavetable
  triTable.triGen(); // allocate and fill the shared wavetable
  aOsc1.setPitch(60);
  // seti2sPins(16, 17, 18, 21); // BCK, WS, DOUT, DIN
  // useInternalDAC();
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - noteTime >= noteDelta) {
    noteTime += noteDelta;
    int pitch = random(24) + 36;
    aOsc1.setPitch(pitch);
  }

  if (msNow - morphTime >= morphDelta) {
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
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  int16_t leftVal = outputGain.next(aOsc1.nextMorph(triTable, morphVal));
  int16_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}
