// M16 Spread example
// Spread reads the oscillator 3 times, each at a different frequency and sums the output
// Useful for oscillator detune or chordal effects
// For fun, this example frequency modulates then filters the spread oscillator output.
// ESP8266 struggles with spread, try using window transform or osc morph instead
#include "M16.h" 
#include "Osc.h"
#include "Env.h"

int16_t * wavetable; // empty wavetable for use by both oscillators
Osc aOsc1, modOsc;
Env modEnv;
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
unsigned long envTime = msNow;
float modVal, modIndex = 0.5;
int pitchDelta = 3000;
int envDelta = 4;

void setup() {
  Serial.begin(115200);
  Osc::allocateWaveMemory(&wavetable); // clear memory for wavetable
  Osc::sinGen(wavetable); // fill the wavetable
  aOsc1.setTable(wavetable); // assign wavetable to osc
  modOsc.setTable(wavetable); // assign wavetable to osc
  aOsc1.setPitch(69);
  modOsc.setPitch(69);
  modEnv.setAttack(400);
  modEnv.setRelease(1600);
  audioStart();
}

void loop() {
  msNow = millis();
  
  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
    pitchTime += pitchDelta;
    if (random(4) == 0) {
      float spreadVal = rand(1000) * 0.00001;
      Serial.print("Detune spread ");Serial.print(spreadVal);
      #if IS_ESP32() // 8266 can't manage spread as well as phMod
        aOsc1.setSpread(spreadVal); // close phase mod
      #endif
    } else { // chordal, up to one octave above or below
      int i1 = rand(25) - 12;
      int i2 = rand(25) - 12;
      #if IS_ESP32() // 8266 can't manage spread as well as phMod
        Serial.print("Chord spread ");Serial.print(i1);Serial.print(" ");Serial.println(i2);
        aOsc1.setSpread(i1, i2);
      #endif
      modIndex = rand(20) * 0.1 + 0.2;
      modVal = modIndex;
      Serial.print("Mod index ");Serial.print(modIndex);
    }
    int pitch = rand(48) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    modOsc.setPitch(pitch);
    modEnv.start();
  }

  if ((unsigned long)(msNow - envTime) >= envDelta) {
    envTime += envDelta; 
    modEnv.next();
    modVal = modIndex * modEnv.getValue() * MAX_16_INV;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t leftVal = (aOsc1.phMod(modOsc.next(), modVal) * vol)>>10; // no filtering
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
