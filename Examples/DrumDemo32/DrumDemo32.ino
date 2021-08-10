/* M32 Drum Demo
 * Inspired by MicroTonic
 */
//#include "WiFi.h" // to enable turning off, uses too much space

#include "M32.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Seq.h"
#include "FX.h"

int16_t sineTable [TABLE_SIZE]; // empty array
int16_t triTable [TABLE_SIZE]; // empty array
int16_t sqrTable [TABLE_SIZE]; // empty array
int16_t sawTable [TABLE_SIZE]; // empty array
int16_t noiseTable [TABLE_SIZE]; // empty array
Osc oscillators[16];
SVF filters[16];
Env ampEnvs[16];
Seq sequences[8];
Osc modOscs [2];
FX distort;

int kSeqVals [] = {28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 0, 0};
int snrSeqVals [] = {0, 0, 0, 0, 45, 0, 0, 0, 0, 0, 0, 0, 45, 0, 0, 0};
int cHatsSeqVals [] = {0, 0, 114, 0, 0, 0, 114, 0, 0, 0, 114, 0, 0, 0, 114, 0};
int perc1SeqVals [] = {56, 0, 56, 0, 56, 0, 56, 0, 56, 0, 56, 0, 56, 56, 56, 0};
int perc2SeqVals [] = {0, 0, 0, 54, 0, 0, 0, 54, 0, 0, 0, 54, 0, 0, 0, 54};
int perc3SeqVals [] = {33, 0, 0, 33, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 0};
int perc4SeqVals [] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 79, 0, 79, 79, 0, 0};
int cymbalSeqVals [] = {94, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

float basePitches [] = {28, 45, 114, 57, 44, 33, 64, 94}; // MIDI pitches
float pitchBends [] = {32, 48, 12, 41, 40, 64, 0, 1}; // semitones
float bendAmnts [] = {0.6, 0.45, 0.5, 0.7, 0.6, 0.6, 1.0, 1.00001};
int pAttacks [] = {0, 0, 0, 0, 0, 0, 0, 0}; // ms
int pReleases [] = {650, 130, 60, 20, 50, 200, 500, 2500}; // ms
int nAttacks [] = {0, 10, 0, 0, 0, 0, 0, 20}; // ms
int nReleases [] = {250, 200, 30, 183, 30, 50, 100, 1500}; // ms
float pMaxLevels [] = {0.96, 0.5, 0.25, 0.2, 0.1, 0.4, 0.1, 0.05}; // 0.0 - 1.0 
float nMaxLevels [] = {0.04, 0.7, 0.15, 0.0, 0.0, 0.1, 0.08, 0.06}; // 0.0 - 1.0 
int allCutoff [] = {125, 600, 11000, 1000, 921, 691, 100, 100}; // Hz
int nCutoff [] = {5587, 10700, 6200, 2000, 2046, 2000, 3000, 4000}; // Hz
float panLefts [] = {0.5, 0.5, panLeft(0.6)*2, panLeft(0.2)*2, panLeft(0.8)*2, 0.5, panLeft(0.3)*2, panLeft(0.7)*2};
float panRights [] = {0.5, 0.5, panRight(0.6)*2, panRight(0.2)*2, panRight(0.8)*2, 0.5, panRight(0.3)*2, panRight(0.7)*2};

unsigned long envTime, pitchTime, msNow = millis();
int bpm = 110;
double stepDelta = Seq::calcStepDelta(bpm, 4, 1);
double stepTime = millis() + stepDelta;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();Serial.println("M32 Drum Demo");
  // disable radios
//  btStop(); // uses 10% of memory
//  WiFi.mode( WIFI_OFF);
  //
  Osc::sinGen(sineTable);
  Osc::triGen(triTable);
  Osc::sqrGen(sqrTable);
  Osc::sawGen(sawTable);
  Osc::noiseGen(noiseTable);
  for (int i=0; i<16; i++ ) {
    if (i%2 == 1) {
      oscillators[i].setTable(noiseTable);
      oscillators[i].setNoise(true);
    } else {
      oscillators[i].setTable(sineTable);
      if (i==2) oscillators[i].setTable(triTable); // snare
      if (i==4) oscillators[i].setNoise(true); // cHats
      if (i==14) oscillators[i].setTable(sqrTable); // cymbal
    }
  }
  modOscs[0].setTable(sineTable);
  modOscs[0].setFreq(mtof(basePitches[6]) * 3.2);
  modOscs[1].setTable(sqrTable);
  modOscs[1].setFreq(mtof(basePitches[7]) * 2.55);
  initDrums();
  sequences[0].setSequence(kSeqVals, 16);
  sequences[1].setSequence(snrSeqVals, 16);
  sequences[2].setSequence(cHatsSeqVals, 16);
  sequences[3].setSequence(perc1SeqVals, 16);
  sequences[4].setSequence(perc2SeqVals, 16);
  sequences[5].setSequence(perc3SeqVals, 16);
  sequences[6].setSequence(perc4SeqVals, 16);
  sequences[7].setSequence(cymbalSeqVals, 16);
  audioStart();
}

void initDrums() { // kick, snare, cHats, perc1, perc2
  for (int i=0; i<8; i++) {
    filters[i*2].setCentreFreq(allCutoff[i]); // overall - notch, 12db
    filters[i*2+1].setCentreFreq(nCutoff[i]); // noise cutoff
    // pitched
    oscillators[i*2].setPitch(basePitches[0] + pitchBends[0]);
    ampEnvs[i*2].setAttack(pAttacks[i]);
    ampEnvs[i*2].setRelease(pReleases[i]);
    ampEnvs[i*2].setMaxLevel(pMaxLevels[i]);
    // noise
    filters[i*2+1].setCentreFreq(nCutoff[i]); // noise - LPF
    ampEnvs[i*2+1].setAttack(nAttacks[i]);
    ampEnvs[i*2+1].setRelease(nReleases[i]);
    ampEnvs[i*2+1].setMaxLevel(nMaxLevels[i]);
  }
}

void loop() {
  msNow = millis();
  
  if (msNow > stepTime) {
    stepTime += stepDelta;
    for (int i=0; i<8; i++ ) {
//      Serial.println(sequences[i].again());
      if (sequences[i].next() > 0) {
        oscillators[i*2].setPitch(basePitches[i] + pitchBends[i]);
        oscillators[i*2].setPhase(0);
        if (i<7) {
          ampEnvs[i*2].start();
          ampEnvs[i*2+1].start();
        } else if (rand(4) == 0) {
          ampEnvs[i*2].start();
          ampEnvs[i*2+1].start();
        }
      }
    }
  }

  if (msNow > envTime) {
    envTime += 3;
    for (int i=0; i<16; i++) {
      ampEnvs[i].next();
    }
  }

  if (msNow > pitchTime) {
    pitchTime += 11;
    for (int i=0; i<8; i++ ) {
      oscillators[i*2].setFreq(max(mtof(basePitches[i]), oscillators[i*2].getFreq() * bendAmnts[i]));
    }
  }

 // Include a call to audioUpdate in the main loop() to keep I2S buffer supplied.
  audioUpdate();
}

// This function required in all M16 programs to specify the samples to be played
void audioUpdate() {
  int16_t voice1raw = ((oscillators[0].next() * ampEnvs[0].getValue()) >> 16) + 
    filters[1].nextLPF((oscillators[1].next() * ampEnvs[1].getValue()) >> 16);
  int16_t voice1Filt = filters[0].nextNotch(voice1raw);
  int16_t voice1 = (voice1raw + voice1Filt)/2; // mix notch filtered and raw signal
  int16_t voice2raw = ((oscillators[2].next() * ampEnvs[2].getValue()) >> 16) + 
    (oscillators[3].next() * ampEnvs[3].getValue() >> 16);
  int16_t voice2Filt = filters[2].nextNotch(voice2raw);
  int16_t voice2 = (voice2raw + voice2Filt + voice2Filt)/3; // mix notch filtered and raw signal
  int16_t voice3raw = ((oscillators[4].next() * ampEnvs[4].getValue()) >> 16) + 
    filters[5].nextHPF((oscillators[5].next() * ampEnvs[5].getValue()) >> 16);
  int16_t voice3Filt = filters[4].nextBPF(voice3raw);
  int16_t voice4raw = (oscillators[6].next() * ampEnvs[6].getValue()) >> 16;
  int16_t voice4Filt = filters[6].nextBPF(voice4raw);
  int16_t voice5raw = (oscillators[8].next() * ampEnvs[8].getValue()) >> 16;
  int16_t voice5Filt = filters[8].nextBPF(voice5raw);
  int16_t voice6raw = ((oscillators[10].next() * ampEnvs[10].getValue()) >> 16) + 
    filters[11].nextLPF((oscillators[11].next() * ampEnvs[11].getValue()) >> 16);
  int16_t voice6Filt = filters[10].nextNotch(voice6raw);
  int16_t voice6 = (voice6raw + voice6Filt)/2;
  int16_t voice7raw = ((oscillators[12].phMod(modOscs[0].next() * 50) * ampEnvs[12].getValue()) >> 16) + 
    filters[13].nextLPF((oscillators[13].next() * ampEnvs[13].getValue()) >> 16);
  int16_t voice7Filt = filters[12].nextNotch(voice7raw);
  int16_t voice7 = (voice7raw + voice7Filt)/2;
  int16_t voice8raw = ((oscillators[14].phMod(modOscs[1].next() * 110) * ampEnvs[14].getValue()) >> 16) + // 
    filters[15].nextHPF((oscillators[15].next() * ampEnvs[15].getValue()) >> 16);
  int16_t voice8Filt = filters[14].nextNotch(voice8raw);
  int16_t voice8 = (voice8raw + voice8Filt)/2;
  int16_t leftVal = (voice1 + voice2 + (int)(voice3Filt * panLefts[2]) + (int)(voice4Filt * panLefts[3]) + (int)(voice5Filt * panLefts[4]) + voice6 + (int)(voice7Filt * panLefts[6]) + (int)(voice8 * panLefts[7]))>>1;
  int16_t rightVal = (voice1 + voice2 + (int)(voice3Filt * panRights[2]) + (int)(voice4Filt * panRights[3]) + (int)(voice5Filt * panRights[4]) + voice6 + (int)(voice7Filt * panRights[6]) + (int)(voice8 * panRights[7]))>>1;
//  int16_t leftVal = voice8;
//  int16_t rightVal = voice8;
//  i2s_write_samples(distort.softClip(leftVal, 30), distort.softClip(rightVal, 30));
  i2s_write_samples(leftVal, rightVal);
}
