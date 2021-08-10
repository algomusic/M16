// M16 pitch sequencer example

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Seq.h"


int16_t triTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(triTable);
Env ampEnv1;
int seqVals [] = {60, 64, 67, 70, 72, 76, 74, 72};
int seqSize = 8;
int stepDiv = 2;
Seq seq1(seqVals, seqSize, stepDiv);
unsigned long msNow, envTime = millis();
int bpm = 96;
double stepDelta = seq1.calcStepDelta(bpm); // time between sequence steps at BPM
double stepTime = millis() + stepDelta;
int repeatCnt = 1;
float swing = 0.3;

void setup() {
  Serial.begin(115200);
  delay(20);
  Serial.println();Serial.println("M16 running");
  triGen(triTable);
  ampEnv1.setRelease(600);
  audioStart();
//  seq1.start();
}

void loop() {
  msNow = millis();
  
  if (msNow > stepTime) {
    int pitch;
    if (repeatCnt <= 1) {
      pitch = seq1.next();
      int sliceVal = pow(2, rand(3)); // random slice values per beat
      stepDelta = seq1.calcStepDelta(bpm, sliceVal); // time between sequence steps at BPM / slice
      repeatCnt = sliceVal;
    } else {
      pitch = seq1.again() - 12;
      repeatCnt--;
    }
    if (seq1.getStep() % 2 == 0) {
      stepTime += stepDelta * (1 - swing); // add swing feel
    } else  stepTime += stepDelta * (1 + swing);
    aOsc1.setPitch(pitch);
    ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
  }
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  int16_t leftVal = aOsc1.next() * ampEnv1.getValue() >> 16;
  int16_t rightVal = leftVal;
  i2s_write_lr(leftVal, rightVal);
}
