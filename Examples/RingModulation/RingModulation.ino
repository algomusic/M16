// M16 Ring Modulation example

#include "M16.h"
#include "Osc.h"

int16_t sineWave [TABLE_SIZE]; // empty wavetable
int16_t squareWave [TABLE_SIZE]; // empty wavetable
int16_t triangleWave [TABLE_SIZE]; // empty wavetable
Osc osc1(triangleWave); // experiment with different waveform combinations
Osc osc2(squareWave);

unsigned long msNow, modTime;
float freqRatio = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  Osc::sinGen(sineWave); // fill
  Osc::sqrGen(squareWave); // fill
  Osc::triGen(triangleWave); // fill
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  msNow = millis();
  if (msNow > modTime) {
    modTime = msNow + 10;
    float freq1 = mtof(48);
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
* For ESP32 programs this function is called in the background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  uint16_t leftVal = osc1.ringMod(osc2.next());
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
