// M16 Noise Crackle example
// Crackle charracter is randomly varied every 5 seconds
#include "M16.h"
#include "Osc.h"
#include "Gain.h"

Osc aOsc1;
Gain outputGain(1000);
unsigned long msNow = millis();
unsigned long changeTime = msNow;
unsigned long changeDelta = 5000;

void setup() {
  Serial.begin(115200);
  aOsc1.crackleGen(); // fill the wavetable
  aOsc1.setCrackle(true, 1000); // 0 - MAX_16
  // seti2sPins(16, 17, 18, 21); // BCK, WS, DOUT, DIN
  // useInternalDAC();
  audioStart();
}

void loop() {
  msNow = millis();

  if (msNow - changeTime >= changeDelta) {
    changeTime += changeDelta;
    int16_t cAmnt = random(MAX_16);
    aOsc1.setCrackle(true, cAmnt); // 0 - MAX_16
    Serial.println(cAmnt);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  int32_t leftVal = outputGain.next(aOsc1.next());
  int32_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}
