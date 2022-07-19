// M16 Noise types example

#include "M16.h"
#include "Osc.h"
#include "Env.h"

int16_t whiteTable [TABLE_SIZE]; // empty wavetable
int16_t pinkTable [TABLE_SIZE]; 
int16_t brownTable [TABLE_SIZE]; 
int16_t crackleTable [TABLE_SIZE]; 
Osc whiteOsc(whiteTable); //instantiate oscillator and assign a table
Osc pinkOsc(pinkTable);
Osc brownOsc(brownTable);
Osc crackleOsc(crackleTable);

Env ampEnvW, ampEnvP, ampEnvB, ampEnvC; // envelopes

// handle control rate updates for envelope and notes
unsigned long msNow, stepTime, envTime = millis();
byte vol = 125; // 0 - 255, 8 bit // keep to max 50% for MAX98357 which sums both channels!
byte color = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();Serial.println("M16 running");
  Osc::noiseGen(whiteTable); // fill wavetable
  Osc::pinkNoiseGen(pinkTable);
  Osc::brownNoiseGen(brownTable);
  Osc::crackleGen(crackleTable);
  whiteOsc.setNoise(true);
  pinkOsc.setNoise(true);
  brownOsc.setNoise(true);
  // set initial env values
  ampEnvW.setRelease(1000);
  ampEnvP.setRelease(1000);
  ampEnvB.setRelease(1000);
  ampEnvC.setRelease(1000);
  //
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif
  
  msNow = millis();

  if (msNow > stepTime) {
    stepTime += 1000;
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

  if (msNow > envTime) {
    envTime += 1; // in ms
    ampEnvW.next();
    ampEnvP.next();
    ampEnvB.next();
    ampEnvC.next();
  }
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  int16_t whiteVal = whiteOsc.next() * ampEnvW.getValue() >> 16;
  int16_t pinkVal = pinkOsc.next() * ampEnvP.getValue() >> 16;
  int16_t brownVal = brownOsc.next() * ampEnvB.getValue() >> 16;
  int16_t crackleVal = crackleOsc.next() * ampEnvC.getValue() >> 16;
  int16_t leftVal = ((whiteVal + pinkVal + brownVal + crackleVal) * vol)>>8; // master volume
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
