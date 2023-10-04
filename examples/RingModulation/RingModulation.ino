// M16 Ring Modulation example
#include "M16.h"
#include "Osc.h"

int16_t sineWave[TABLE_SIZE]; // empty wavetable
int16_t squareWave[TABLE_SIZE]; // empty wavetable
int16_t triangleWave[TABLE_SIZE]; // empty wavetable
Osc osc1(squareWave); // experiment with different waveform combinations
Osc osc2(triangleWave);

unsigned long msNow = millis();
unsigned long modTime = msNow;
float freqRatio = 0;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineWave); // fill
  Osc::sqrGen(squareWave); // fill
  Osc::triGen(triangleWave); // fill
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - modTime > 10 || msNow - modTime < 0) {
      modTime = msNow;
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
  int16_t leftVal = osc1.ringMod(osc2.next());
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
