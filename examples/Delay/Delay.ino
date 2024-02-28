// M16 Delay Example 
#include "M16.h"
#include "Del.h"
#include "Osc.h"
#include "Env.h"

Del delay1(500); // max delay time in ms
int16_t triTable [TABLE_SIZE]; // empty array
Osc osc1(triTable);
//Osc lfo1(triTable);
Env ampEnv1;

unsigned long msNow, noteTime, envTime, delTime;
int scale [] = {0, 2, 4, 5, 7, 9, 0, 0, 0, 0, 0};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();Serial.println("M16 running");
  Serial.println(delay1.getBufferSize());
  delay1.setTime(300); // ms
  delay1.setLevel(0.8); // 0 - 1
  delay1.setFeedback(true); // bool
  Osc::triGen(triTable);
  osc1.setPitch(60);
//  lfo1.setFreq(0.1);
  ampEnv1.setAttack(10);
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  
  msNow = millis();

  if (msNow > noteTime) {
    noteTime = msNow + 1000;
    osc1.setPitch(pitchQuantize(random(25) + 48, scale, 0));
    ampEnv1.start();
//    Serial.println("Note");
  }

  if (msNow > envTime) {
    envTime = msNow + 4;
    ampEnv1.next();
  }

  if (msNow > delTime) {
     delTime = msNow + 3900;
     delay1.setTime(random(350) + 100);
     delay1.setFiltered(random(5));
//     delay1.setTime(300 + 100 * (lfo1.atTime(msNow)/(float)MAX_16));
  }
}

void audioUpdate() {
  int16_t oscVal = (osc1.next() * ampEnv1.getValue())>>16;
  int16_t leftVal = (oscVal + delay1.next(oscVal)) >>1;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
