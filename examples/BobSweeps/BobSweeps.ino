// Bob low pass filter example
// The ESP8266 can't quite cope with this filter
// Some ESP32s without an FPU, like the S2, also struggle
#include "M16.h"
#include "Osc.h"
#include "Bob.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
int16_t sineTable [TABLE_SIZE]; // empty wavetable
Osc osc1(waveTable);
Osc lfo1(sineTable);
Osc lfo2(sineTable);
Bob lpf;
unsigned long msNow = millis();
unsigned long lfoTime = msNow;
unsigned long pitchTime = msNow;
int lfoDelta = 24;
int pitchDelta = 4000;

void setup() {
  Serial.begin(115200);
  Osc::sawGen(waveTable);
  Osc::sinGen(sineTable);
  osc1.setPitch(55);
  // osc1.setSpread(0.0002, -0.0002);
  lfo1.setFreq(0.2);
  lfo2.setFreq(0.04);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - lfoTime > lfoDelta || msNow - lfoTime < 0) {
    lfoTime = msNow;
    lpf.setFreq(lfo1.atTimeNormal(msNow) * 20000);
    lpf.setRes(lfo2.atTimeNormal(msNow));
  }

  if (msNow - pitchTime > pitchDelta || msNow - pitchTime < 0) {
    pitchTime = msNow;
    osc1.setPitch(rand(36) + 24);
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
