// M16 Panning example
#include "M16.h" 
#include "Osc.h"
#include "Env.h"

int16_t waveTable[TABLE_SIZE]; // empty wavetable
Osc osc1(waveTable);
Env ampEnv1;
int16_t vol = 1000; // 0 - 1024, 10 bit
float panPos = 0.5;
int16_t panLevelL = panLeft(panPos) * 1024; // 0 - 1024
int16_t panLevelR = panRight(panPos) * 1024;
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
int8_t sweep = 2; // 0 to left, 1 to right, 2 no sweep

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::triGen(waveTable); // fill the wavetable
  osc1.setPitch(69);
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - pitchTime > 4 || msNow - pitchTime < 0) {
    ampEnv1.next();
    if (sweep < 2) {
      if (sweep == 0) panPos = max(0.0d, panPos - 0.0001);
      if (sweep == 1) panPos = min(1.0d, panPos + 0.0001);
      panLevelL = panLeft(panPos) * 1024; 
      panLevelR = panRight(panPos) * 1024;
    }
  }

  if (msNow - pitchTime > 1000 || msNow - pitchTime < 0) {
    pitchTime = msNow;
    int pitch = random(24) + 58;
    osc1.setPitch(pitch);
    panPos = rand(11) * 0.1;
    panLevelL = panLeft(panPos) * 1024; 
    panLevelR = panRight(panPos) * 1024;
    sweep = min(2, rand(3));
    Serial.println(String(pitch) + " " + String(sweep));
    ampEnv1.start();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (((osc1.next() * ampEnv1.getValue())>>16) * vol)>>10;
  int16_t rightVal = leftVal;
  leftVal = (leftVal * panLevelL)>>10;
  rightVal = (rightVal * panLevelR)>>10;
  i2s_write_samples(leftVal, rightVal);
}
