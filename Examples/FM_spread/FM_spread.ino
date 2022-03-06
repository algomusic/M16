// M16 Spread example
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
unsigned long msNow, pitchTime, envTime;
int noteCnt = 0;
float modVal, modIndex = 0.5;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(waveTable); // fill the wavetable
  aOsc1.setPitch(69);
  filter.setCentreFreq(6000);
  modEnv.setAttack(400);
  modEnv.setRelease(1600);
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  
  msNow = millis();
  
  if (msNow > pitchTime) {
    pitchTime = msNow + 2000;
    if (noteCnt++ % 4 == 0) {
      if (random(2) == 0) {
        float spreadVal = random(1000) * 0.00001;
        Serial.print("Detune spread ");Serial.print(spreadVal);
        aOsc1.setSpread(spreadVal); // close phase mod
      } else { // chordal, up to one octave above or below
        int i1 = random(25) - 12;
        int i2 = random(25) - 12;
        Serial.print("Chord spread ");Serial.print(i1);Serial.print(" ");Serial.println(i2);
        aOsc1.setSpread(i1, i2);
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

  if (msNow > envTime) {
    envTime = msNow + 11;
    modEnv.next();
    modVal = modIndex * modEnv.getValue() / MAX_16;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* For ESP32 programs this function is called in teh background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  uint16_t leftVal = (filter.nextLPF(aOsc1.phMod(modOsc.next(), modVal)) * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
