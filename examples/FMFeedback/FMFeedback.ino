/*
 * FMFeedback.ino
 *
 * Demonstrates the feedback() function in Osc.h
 * FM feedback creates rich timbres from a single oscillator
 * by feeding its output back into its own phase modulation.
 *
 * Low feedback: sine-like
 * Medium feedback: sawtooth-like
 * High feedback: chaotic/noise-like
 */

#include <M16.h>
#include <Osc.h>

Osc osc;

// Feedback amount: 0-100 useful range
int32_t fbAmount = 0;

float freq = 110.0;  // A2

void setup() {
  Serial.begin(115200);
  Serial.println("FM Feedback Test");

  osc.sinGen();
  osc.setFreq(freq);

  seti2sPins(38, 39, 40, 41);
  // useInternalDAC();
  audioStart();

  Serial.println("Frequency: " + String(freq) + " Hz");
  Serial.println("Feedback: " + String(fbAmount));
  Serial.println("Range 0-100: 0=sine, 20=saw-like, 50+=chaotic");
}

void loop() {
  // Sweep feedback amount every 2 seconds
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 2000) {
    lastUpdate = millis();
    fbAmount += 500;
    if (fbAmount > 5000) fbAmount = 0;
    Serial.println("Feedback: " + String(fbAmount));
  }
}

void audioUpdate() {
  int16_t sample = osc.feedback(fbAmount);
  i2s_write_samples(sample, sample);
}
