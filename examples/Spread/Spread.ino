// M16 Spread example
// Spread reads the oscillator 3 times, each at a different frequency and sums the output
// Useful for oscillator detune or chordal effects
// ESP8266 struggles with spread, try using window transform or osc morph instead
#include "M16.h" 
#include "Osc.h"
#include "SVF.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
SVF filter;
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow, pitchTime;
int noteCnt = 0;

void setup() {
  Serial.begin(115200);
  Osc::sawGen(waveTable); // fill the wavetable
  aOsc1.setPitch(69);
  filter.setFreq(5000);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - pitchTime > 1000 || msNow - pitchTime < 0) {
      pitchTime = msNow;
    if (noteCnt++ % 4 == 0) {
      if (random(2) == 0) {
        float detSpread = random(1000) * 0.00001;
        Serial.print("Detune spread "); Serial.println(detSpread);
        aOsc1.setSpread(detSpread); // close phase mod
      } else { // chordal, up to one octave above or below
        int i1 = random(25) - 12;
        int i2 = random(25) - 12;
        Serial.print("Chord spread ");Serial.print(i1);Serial.print(" ");Serial.println(i2);
        aOsc1.setSpread(i1, i2);
      }
    }
    int pitch = random(48) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = (filter.nextLPF(aOsc1.next()) * vol)>>10;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
