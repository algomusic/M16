// M16 Ring Modulation example
#include "M16.h"
#include "Osc.h"

Osc osc1, osc2;
unsigned long msNow = millis();
unsigned long modTime = msNow;
float freqRatio = 0;
int modDelta = 10;

void setup() {
  Serial.begin(115200);
  osc1.sqrGen(); // fill osc wavtetable
  osc2.triGen(); // fill
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - modTime) >= modDelta) {
    modTime += modDelta;
    float freq1 = mtof(60);
    osc1.setFreq(freq1);
    float freq2 = freq1 * freqRatio;
    osc2.setFreq(freq2);
    freqRatio += 0.001;
    if (freqRatio > 3) freqRatio = 0;
    if (random(100) == 0) {
      Serial.print("freq1 ");Serial.print(freq1);
      Serial.print(" freq2 ");Serial.print(freq2);
      Serial.print(" freqRatio ");Serial.println(freqRatio);
    }
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t leftVal = osc1.ringMod(osc2.next());
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
