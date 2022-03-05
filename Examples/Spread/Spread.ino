// M16 Spread example
#include "M16.h" 
#include "Osc.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow, pitchTime;
int noteCnt = 0;

void setup() {
  Serial.begin(115200);
  Osc::sawGen(waveTable); // fill the wavetable
  aOsc1.setPitch(69);
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  msNow = millis();
  if (msNow > pitchTime) {
    pitchTime = msNow + 1000;
    if (noteCnt++ % 4 == 0) {
      if (random(2) == 0) {
        Serial.println("Detune spread ");
        aOsc1.setSpread(random(1000) * 0.00001); // close phase mod
      } else { // chordal, up to one octave above or below
        int i1 = random(25) - 12;
        int i2 = random(25) - 12;
        Serial.print("Chord spread ");Serial.print(i1);Serial.print(" ");Serial.println(i2);
        aOsc1.setSpread(i1, i2);
      }
    }
    int pitch = random(36) + 48;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* For ESP32 programs this function is called in teh background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  uint16_t leftVal = (aOsc1.next() * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
