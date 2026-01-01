// M16 Chorus FX example
#include "M16.h" 
#include "Osc.h"
#include "FX.h"

Osc osc, lfo;
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long lfoTime = msNow;
float lfoRate = 0.1; // hz
float pitch = 60;
int pitchDelta = 3000;
FX effects;
int lfoDelta = 51;

void setup() {
  Serial.begin(115200);
  delay(200);
  osc.sawGen(); // fill the sawTable
  osc.setPitch(pitch);
  lfo.triGen(); // fill the sawTable
  lfo.setFreq(lfoRate);
  // seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
    pitchTime += pitchDelta;
    pitch = random(24) + 36;
    Serial.println(pitch);
    osc.slewFreq(mtof(pitch), 0.2);
  }

  // vary the chorus depth
  if ((unsigned long)(msNow - lfoTime) >= lfoDelta) {
    lfoTime += lfoDelta;
    osc.slewFreq(mtof(pitch), 0.2); // pitch glide
    float lfoVal = lfo.atTimeNormal(msNow);
    effects.setChorusDepth(lfoVal);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  // mono version
  // int32_t oscVal = osc.next();
  // int32_t leftVal = (effects.chorus(oscVal) * vol)>>10;
  // int32_t rightVal = leftVal;
  // stereo version
  int32_t oscVal = (osc.next() * vol)>>10;
  int32_t leftVal, rightVal;
  effects.chorusStereo(oscVal, oscVal, leftVal, rightVal);
  //
  i2s_write_samples(leftVal, rightVal);
}
