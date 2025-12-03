/*
 * PhaseModulation.ino
 *
 * Simple test of the phMod function in Osc.h
 * Demonstrates FM synthesis with carrier and modulator oscillators
 *
 */

#include <M16.h>
#include <Osc.h>

// Carrier oscillator (audible output)
Osc carrier;
// Modulator oscillator (modulates the carrier's phase)
Osc modulator;

// Modulation parameters
float modIndex = 2.0;  // Try 0.5-10.0 for different timbres
float carrierFreq = 220.0;  // A3
float modRatio = 1.0;  // Modulator frequency ratio (modFreq = carrierFreq * modRatio)
// loop params
unsigned long msNow, pitchTime;
int pitchDelta = 1000; //ms

void setup() {
  Serial.begin(115200);
  Serial.println("Phase Modulation Test");

  // Set up carrier with sine wave
  carrier.sinGen();
  carrier.setFreq(carrierFreq);

  // Set up modulator with sine wave
  modulator.sinGen();
  modulator.setFreq(carrierFreq * modRatio);
  // seti2sPins(38, 39, 40, 41);
  audioStart();

  Serial.println("Carrier: " + String(carrierFreq) + " Hz");
  Serial.println("Modulator: " + String(carrierFreq * modRatio) + " Hz");
  Serial.println("Mod Index: " + String(modIndex));
  Serial.println("Try changing modIndex (0.5-10) and modRatio (0.5-8) for different timbres");
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
    pitchTime += pitchDelta;
    int pitch = rand(48) + 36;
    carrier.setPitch(pitch);
    modulator.setPitch(pitch * modRatio);
    modIndex += 0.5;
    if (modIndex > 7.0) modIndex = 0.5;
    Serial.println("Mod Index: " + String(modIndex));
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t sample = carrier.phMod(modulator.next(), modIndex);
  // Output to both channels
  i2s_write_samples(sample, sample);
}
