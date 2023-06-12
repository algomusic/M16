// M16 Sinewave example
#include "M16.h" 
#include "Osc.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow, pitchTime;

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::sinGen(waveTable); // fill the wavetable
  aOsc1.setPitch(69);
  //setI2sPins(25, 27, 12, 16); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();
  if (msNow > pitchTime) {
    pitchTime = msNow + 1000;
    int pitch = random(24) + 58;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  uint16_t leftVal = (aOsc1.next() * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
