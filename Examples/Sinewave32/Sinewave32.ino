// M32 Sinewave example
#include "M32.h"
#include "Osc.h"
#include "Env.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sineTable);
Env ampEnv1;

unsigned long msNow = millis();
unsigned long noteTime = msNow + 1000;
unsigned long ampEnvTime = msNow + 4;

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill the wavetable
  aOsc1.setPitch(69);
  ampEnv1.setRelease(1000);
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow > noteTime) {
    noteTime += 1000;
   int pitch = random(24) + 58;
   Serial.println(pitch);
   aOsc1.setPitch(pitch);
   ampEnv1.start();
  }

 if (msNow > ampEnvTime) {
    ampEnvTime += 4;
    ampEnv1.next();
  }
}

/* This function is required in all M32 programs 
* to specify the audio sample values to be played.
* Surround all code with a forever loop.
* Always finish with i2s_write_samples()
*/
void audioUpdate(void * paramRequiredButNotUsed) {
  for (;;) { // run forever in the background
    int16_t leftVal = (aOsc1.next() * ampEnv1.getValue())>>16;
    int16_t rightVal = leftVal;  
    i2s_write_samples(leftVal, rightVal);
  }
}
