// M16 Frequency Modulation (Phase Modulation) example
#include "M16.h"
#include "Osc.h"

int16_t sineTable[TABLE_SIZE]; // empty wavetable

Osc aOsc1(sineTable);
Osc aOsc2(sineTable);

float modIndex = 0.2;
unsigned long msNow = millis();
unsigned long pitchTime = msNow;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill wavetable
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - pitchTime > 2000 || msNow - pitchTime < 0) {
    pitchTime = msNow;
    float pitch = rand(36) + 36;
    aOsc1.setPitch(pitch);
    float ratio = rand(8) * 0.25 + 0.25;  // set carrier to modulator freq ratio
    Serial.print("Ratio: ");Serial.print(ratio);
    aOsc2.setFreq(mtof(pitch) * ratio);
    modIndex = rand(50) * 0.1;  // set the modulation index (depth)
    Serial.print(" Mod Index: ");Serial.println(modIndex);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  // modIndex changes modulation depth. Values for PM are about 1/100th of those typical for FM
  int16_t leftVal = aOsc1.phMod(aOsc2.next(), modIndex); 
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
