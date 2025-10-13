// M16 Noise types example
#include "M16.h"
#include "Osc.h"
#include "Env.h"

Osc whiteOsc, pinkOsc, brownOsc, crackleOsc;
Env ampEnvW, ampEnvP, ampEnvB, ampEnvC; // envelopes

// handle control rate updates for envelope and notes
unsigned long msNow, stepTime, envTime = millis();
int vol = 1000; // 0 - 1023, 10 bit // keep to max 50% for MAX98357 which sums both channels to mono.
byte color = 0;
int envDelta = 4;
int stepDelta = 2000;

void setup() {
  Serial.begin(115200);
  delay(200);
  whiteOsc.noiseGen(); // fill wavetable
  pinkOsc.pinkNoiseGen();
  brownOsc.brownNoiseGen();
  crackleOsc.crackleGen();
  whiteOsc.setNoise(true);
  pinkOsc.setNoise(true);
  brownOsc.setNoise(true);
  crackleOsc.setCrackle(true, MAX_16 * 0.5);
  //set initial env values
  ampEnvW.setRelease(2000);
  ampEnvP.setRelease(2000);
  ampEnvB.setRelease(2000);
  ampEnvC.setRelease(2000);
  //
  audioStart();
}

void loop() {
  msNow = millis();
  
  if ((unsigned long)(msNow - stepTime) >= stepDelta) {
    stepTime += stepDelta; 
    if (color == 0) {
      ampEnvW.start();
      Serial.println("White Noise");
    }
    if (color == 1) {
      ampEnvP.start();
      Serial.println("Pink Noise");
    }
    if (color == 2) {
      ampEnvB.start();
      Serial.println("Brown Noise");
    }
    if (color == 3) {
      ampEnvC.start();
      Serial.println("Crackle Noise");
    }
    color = (color + 1) % 4;
  }

  if ((unsigned long)(msNow - envTime) >= envDelta) {
      envTime += envDelta; 
    ampEnvW.next();
    ampEnvP.next();
    ampEnvB.next();
    ampEnvC.next();
  }
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  int32_t whiteVal = (whiteOsc.next() * ampEnvW.getValue())>>16;
  int32_t pinkVal = (pinkOsc.next() * ampEnvP.getValue())>>16;
  int32_t brownVal = (brownOsc.next() * ampEnvB.getValue())>>16;
  int32_t crackleVal = (crackleOsc.next() * ampEnvC.getValue())>>16;
  int32_t leftVal = ((whiteVal + pinkVal + brownVal + crackleVal) * vol)>>10; // master volume
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
