// M16 LFO example
#include "M16.h" 
#include "Osc.h"
#include "Gain.h"

Osc osc1, lfo1; // declare instances of the oscillator class
Gain outputGain(1000);
unsigned long msNow = millis(); // current time since starting in milliseconds
unsigned long pitchTime = msNow; // time for the next pitch update
unsigned long lfoTime = msNow; // time for the next pitch update
unsigned long pitchDelta = 2000; // time between pitch updates
unsigned long lfoDelta = 4;
int pitch = 69;
float lfoRate = 3.0; // Hz
float lfoDepth = 0.05; // 0 - 1+

void setup() {
  Serial.begin(115200);
  delay(200); // let the Serial port get established
  osc1.triGen(); // initialise the osc's built-in wavetable with a sine waveform
  lfo1.sinGen();
  osc1.setPitch(pitch); // MIDI pitch
  lfo1.setFreq(lfoRate); // Htz
  seti2sPins(16, 17, 18, 21); // bck, ws, data_out, data_in // change defaults
  // useInternalDAC();
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - lfoTime >= lfoDelta) {
    lfoTime += lfoDelta;
    float lfoVal = lfo1.atTimeNormal(msNow);
    // Serial.println(lfoVal);
    osc1.setPitch(pitch * (1 + (lfoVal - 0.5) * lfoDepth));
  }

  if (msNow - pitchTime >= pitchDelta) {
    pitchTime += pitchDelta;
    pitch = random(48) + 36;
    osc1.setPitch(pitch);
    lfoRate = random(100) * 0.05; // 0 - 5
    lfo1.setFreq(lfoRate);
    lfoDepth = random(100) * 0.02; // 0 - 2
    Serial.println("Pitch: " + String(pitch) + " lfoRate: " + String(lfoRate) + " lfoDepth: " + String(lfoDepth));
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  int32_t leftVal = outputGain.next(osc1.next());
  int32_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}
