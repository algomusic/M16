// M32 Polyphony Test 
#include "M32.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Seq.h"
#include "FX.h"
#include "Controls.h"

int16_t triTable [TABLE_SIZE]; // empty array
//int16_t sawTable [TABLE_SIZE]; // empty array
Osc oscillators[16];
SVF filters[16];
Env ampEnvs[8];
FX distort;

unsigned long envTime, msNow = millis();
int bpm = 96;
double stepDelta = 250;
double stepTime = millis() + stepDelta;
int cutoff = 5000;

unsigned long controlTime = millis();
int controlDelta = 100; // ms
int dialVal1 = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();Serial.println("M32 running");
  Osc::triGen(triTable);
  for (int i=0; i<16; i++ ) {
    oscillators[i].setTable(triTable);
    oscillators[i].setPitch(60);
  }
  oscillators[1].setPitch(60 * 1.0001);
  for (int i=0; i<8; i++ ) {
    filters[i].setCentreFreq(cutoff); // 60 - 11k
//    filters[i].setResonance(0); // 0.0- 1.0
    ampEnvs[i].setAttack(10);
    ampEnvs[i].setRelease(random(800) + 300);
    ampEnvs[i].start();
  } 
  controlInit();
  audioStart();
}

void loop() {
  msNow = millis();
  
  if (msNow > stepTime) {
    stepTime += stepDelta;
    for (int i=0; i<8; i++ ) {
      if (random(3) == 0) {
        float pitch = random(72) + 24;
//        Serial.println(pitch);
        oscillators[i*2].setPitch(pitch);
        oscillators[i*2+1].setPitch(pitch * 1.001);
        oscillators[i*2].setPhase(0);
        oscillators[i*2+1].setPhase(0);
        filters[i].setCentreFreq(cutoff + (pitch - 60) * 10); // cutoff pitch tracking
        ampEnvs[i].start();
      }
    }
  }

  if (msNow > envTime) {
    envTime += 4;
    for (int i=0; i<8; i++) {
      ampEnvs[i].next();
    }
  }

  if (msNow > controlTime) {
    controlTime += controlDelta;
    int read1 = readDial(analogin);
    if (abs(read1 - dialVal1) > 8 || (read1 == 0 && dialVal1 > 0)) {
      dialVal1 = read1;
      Serial.println(dialVal1);
    }
  }

 // Include a call to audioUpdate in the main loop() to keep I2S buffer supplied.
  audioUpdate();
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  int16_t voice1 = (filters[0].nextLPF((oscillators[0].phMod(0) + oscillators[1].phMod(0))>>1) * ampEnvs[0].getValue())>>16;
  int16_t voice2 = (filters[1].nextLPF((oscillators[2].phMod(0) + oscillators[3].phMod(0))>>1) * ampEnvs[1].getValue())>>16;
  int16_t voice3 = (filters[2].nextLPF((oscillators[4].phMod(0) + oscillators[5].phMod(0))>>1) * ampEnvs[2].getValue())>>16;
  int16_t voice4 = (filters[3].nextLPF((oscillators[6].phMod(0) + oscillators[7].phMod(0))>>1) * ampEnvs[3].getValue())>>16;
  int16_t voice5 = (filters[4].nextLPF((oscillators[8].phMod(0) + oscillators[9].phMod(0))>>1) * ampEnvs[4].getValue())>>16;
  int16_t voice6 = (filters[5].nextLPF((oscillators[10].phMod(0) + oscillators[11].phMod(0))>>1) * ampEnvs[5].getValue())>>16;
  int16_t voice7 = (filters[6].nextLPF((oscillators[12].phMod(0) + oscillators[13].phMod(0))>>1) * ampEnvs[6].getValue())>>16;
  int16_t voice8 = (filters[7].nextLPF((oscillators[14].phMod(0) + oscillators[15].phMod(0))>>1) * ampEnvs[7].getValue())>>16;
  int16_t leftVal = (voice1 + voice2 + voice3 + voice4 + voice5 + voice6 + voice7 + voice8)>>3;
//  int16_t leftVal = voice1;
//  leftVal = distort.softClip(leftVal, 40);
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
