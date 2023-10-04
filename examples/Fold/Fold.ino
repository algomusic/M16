// M16 Wave Folding example
#include "M16.h" 
#include "Osc.h"
#include "FX.h"

int16_t waveTable[TABLE_SIZE]; // empty wavetable
Osc osc1(waveTable);
FX effects1;
int16_t vol = 1000; // 0 - 1024, 10 bit
float foldAmnt = 1.0;
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long foldTime = msNow;

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

  if (msNow - pitchTime > 5000 || msNow - pitchTime < 0) {
    pitchTime = msNow;
    int pitch = random(24) + 36;
    Serial.println(pitch);
    osc1.setPitch(pitch);
  }

  if (msNow - foldTime > 100 || msNow - foldTime < 0) {
    foldTime = msNow;
    foldAmnt += 0.05;
    if (foldAmnt > 10) foldAmnt = 1.0;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (effects1.waveFold(osc1.next(), foldAmnt) * vol)>>10;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
