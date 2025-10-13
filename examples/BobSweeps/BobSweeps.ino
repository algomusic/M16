// Bob low pass filter example
// The ESP8266 can't quite cope with this filter
// Some ESP32s without an FPU, like the S2, also struggle
#include "M16.h"
#include "Osc.h"
#include "Bob.h"
// #include "SVF2.h" // for comparison
// #include "SVF.h" // for comparison
// #include "EMA.h" // for comparison

Osc osc1;
Osc lfo1;
Osc lfo2;
Bob lpf;
// SVF2 lpf; // for comparison
// SVF lpf; // for comparison
// EMA lpf; // for comparison
unsigned long msNow = millis();
unsigned long lfoTime = msNow;
unsigned long pitchTime = msNow;
int lfoDelta = 24;
int pitchDelta = 4000;

void setup() {
  Serial.begin(115200);
  osc1.sawGen();
  lfo1.sinGen();
  lfo2.sinGen();
  osc1.setPitch(55);
  // osc1.setSpread(0.0002, -0.0002);
  lfo1.setFreq(0.2);
  lfo2.setFreq(0.04);
  // seti2sPins(38, 39, 40, 41);
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - lfoTime) >= lfoDelta) {
      lfoTime += lfoDelta;
    lpf.setCutoff(lfo1.atTimeNormal(msNow));
    lpf.setRes(lfo2.atTimeNormal(msNow));
  }

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
      pitchTime += pitchDelta;
    float pitch = round(rand(36) + 24);
    osc1.setPitch(pitch);
    Serial.println(pitch);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = lpf.next(osc1.next());
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
