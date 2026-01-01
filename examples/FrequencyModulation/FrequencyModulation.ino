// M16 Frequency Modulation (Phase Modulation) example
#include "M16.h"
#include "Osc.h"

int16_t * sineTable; // empty wavetable for use by boith oscillators
Osc aOsc1, aOsc2;
float modIndex = 0.2;
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
int pitchDelta = 2000;

void setup() {
  Serial.begin(115200);
  Osc::allocateWaveMemory(&sineTable);
  Osc::sinGen(sineTable); // fill wavetable
  aOsc1.setTable(sineTable); // assign wave to osc1
  aOsc2.setTable(sineTable); // assign wave to osc2
  // seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
    pitchTime += pitchDelta;
    float pitch = rand(36) + 48;
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
  int32_t leftVal = aOsc1.phMod(aOsc2.next(), modIndex); 
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
