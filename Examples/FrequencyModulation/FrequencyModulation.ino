// M16 Frequency Modulation (Phase Modulation) example

#include "M16.h"
#include "Osc.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable

Osc aOsc1(sineTable);
Osc aOsc2(sineTable);

float modIndex = 0.2;
unsigned long msNow, pitchTime;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill wavetable
  audioStart();
  Serial.println();Serial.println("M16 running");
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  msNow = millis();
  if (msNow > pitchTime) {
    pitchTime = msNow + 1000;
    float pitch = random(48) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    aOsc2.setFreq(mtof(pitch) * 0.5); // set freq ratio
    modIndex = random(40) * 0.1;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* For ESP32 programs this function is called in teh background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  // modIndex to change mod depth. Values for PM about 1/10th of those typical for FM
  int16_t leftVal = aOsc1.phMod(aOsc2.next(), modIndex); 
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
