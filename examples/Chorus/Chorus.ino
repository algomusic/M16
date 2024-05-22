// M16 Chorus FX example
#include "M16.h" 
#include "Osc.h"
#include "FX.h"

int16_t sawTable[TABLE_SIZE]; 
int16_t triTable[TABLE_SIZE];
Osc osc(sawTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long lfoTime = msNow;
Osc lfo(triTable);
float lfoRate = 0.1; // hz
float pitch = 60;
FX effects;

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::sawGen(sawTable); // fill the sawTable
  osc.setPitch(pitch);
  Osc::triGen(triTable); // fill the sawTable
  lfo.setFreq(lfoRate);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - pitchTime > 3000 || msNow - pitchTime < 0) {
    pitchTime = msNow;
    pitch = random(24) + 36;
    Serial.println(pitch);
    osc.slewFreq(mtof(pitch), 0.2);
  }

  // vary the chorus depth
  if (msNow - lfoTime > 51 || msNow - lfoTime < 0) {
    lfoTime = msNow;  
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
  int16_t oscVal = osc.next();
  int16_t leftVal = (effects.chorus(oscVal) * vol)>>10;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
