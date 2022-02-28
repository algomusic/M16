// M16 plucked string and arpeggiator example

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "FX.h"
#include "Arp.h"

int16_t noiseTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(noiseTable);
Env ampEnv1;
FX aEffect1;
Arp arp1;
int bpm = 120;
double stepTime, stepDelta;
unsigned long msNow, envTime = millis();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();Serial.println("M16 running");
  Osc::noiseGen(noiseTable); aOsc1.setNoise(true); // fill wavetable and set noise flag
  ampEnv1.setRelease(100);
  int newSet [] = {48, 52, 55, 58, 60, 64};
  arp1.setPitches(newSet, 6);
  arp1.setDirection(ARP_UP_DOWN);
  arp1.setRange(3);
  stepDelta = arp1.calcStepDelta(120, 2); // ms between steps at bpm sliced into 2
  stepTime = millis() + stepDelta;
  arp1.start();
  aOsc1.setPitch(arp1.next());
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate(); //for ESP8266
  #endif 
  
  msNow = millis();
  
  if (msNow > stepTime) {
    stepTime += stepDelta;
    int pitch = arp1.next(); 
    aOsc1.setPitch(pitch);
    ampEnv1.start();
  }

  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
* For ESP32 programs this function is called in teh background
* for ESP8266 programs a call to audioUpdate() is required in the loop() function.
*/
void audioUpdate() {
  int16_t leftVal = aEffect1.pluck((aOsc1.next() * ampEnv1.getValue() >> 16), aOsc1.getFreq(), 0.99);
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
