// M16 Allpass filter example
#include "M16.h" 
#include "All.h"

// All allpass;
All allpass(10, 0.7);
unsigned long msNow = millis();
unsigned long impluseTime = msNow;
int impluseDelta = 1000;
int16_t impulse = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change defaults
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - impluseTime) >= impluseDelta) {
      impluseTime += impluseDelta;
    impulse = MAX_16;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = allpass.next(impulse);
  if (impulse > 0) impulse = 0;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
