// M16 Spread example
// Spread reads the oscillator 3 times, each at a different frequency and sums the output
// Useful for oscillator detune or chordal effects
// For fun, this example frequency modulates then filters the spread oscillator output.
// ESP8266 struggles with spread, try using window transform or osc morph instead
#include "M16.h" 
#include "Osc.h"
#include "SVF.h"
#include "Env.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
Osc modOsc(waveTable);
SVF filter;
Env modEnv;
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long envTime = msNow;
int noteCnt = 0;
float modVal, modIndex = 0.5;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(waveTable); // fill the wavetable
  aOsc1.setPitch(69);
  filter.setFreq(6000);
  modEnv.setAttack(400);
  modEnv.setRelease(1600);
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow - pitchTime > 2000 || msNow - pitchTime < 0) {
      pitchTime = msNow;
    if (noteCnt++ % 4 == 0) {
      if (random(2) == 0) {
        float spreadVal = random(1000) * 0.00001;
        Serial.print("Detune spread ");Serial.print(spreadVal);
        #if IS_ESP32() // 8266 can't manage spread as well as phMod
          aOsc1.setSpread(spreadVal); // close phase mod
        #endif
      } else { // chordal, up to one octave above or below
        int i1 = random(25) - 12;
        int i2 = random(25) - 12;
        #if IS_ESP32() // 8266 can't manage spread as well as phMod
          Serial.print("Chord spread ");Serial.print(i1);Serial.print(" ");Serial.println(i2);
          aOsc1.setSpread(i1, i2);
        #endif
        modIndex = random(20) * 0.1 + 0.2;
        modVal = modIndex;
        Serial.print("Mod index ");Serial.print(modIndex);
      }
    }
    int pitch = random(48) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    modOsc.setPitch(pitch);
    modEnv.start();
  }

  if (msNow - envTime > 11 || msNow - envTime < 0) {
    envTime = msNow;
    modEnv.next();
    modVal = modIndex * modEnv.getValue() / MAX_16;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  #if IS_ESP8266() // 8266 can't manage filter as well as phMod
    int16_t leftVal = (aOsc1.phMod(modOsc.next(), modVal) * vol)>>10;
  #elif IS_ESP32()
    int16_t leftVal = (filter.nextLPF(aOsc1.phMod(modOsc.next(), modVal)) * vol)>>10;
  #endif
  // 
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
