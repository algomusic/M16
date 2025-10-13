// M16 Delay Example 
#include "M16.h"
#include "Del.h"
#include "Osc.h"
#include "Env.h"

Del delay1(500); // max delay time in ms
Osc osc1;
Env ampEnv1;
int noteDelta = 1000;

unsigned long msNow, noteTime, envTime, delTime;
int scale [] = {0, 2, 4, 5, 7, 9, 0, 0, 0, 0, 0};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();Serial.println("M16 running");
  Serial.println(delay1.getBufferSize());
  delay1.setTime(300); // ms
  delay1.setLevel(0.9); // 0 - 1
  delay1.setFeedback(true); // bool
  osc1.triGen(); // fill the osc wavetable
  osc1.setPitch(60);
  ampEnv1.setAttack(10);
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  
  msNow = millis();

  if ((unsigned long)(msNow - noteTime) >= noteDelta) {
      noteTime += noteDelta;
    osc1.setPitch(pitchQuantize(random(25) + 48, scale, 0));
    delay1.setFiltered(random(5));
    delay1.setTime(random(350) + 100);
    ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime = msNow + 4;
    ampEnv1.next();
  }
}

void audioUpdate() {
  int32_t oscVal = (osc1.next() * ampEnv1.getValue())>>16;
  int32_t leftVal = (oscVal + delay1.next(oscVal)) * 0.75;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
