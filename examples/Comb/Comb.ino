// M16 Comb filter example
#include "M16.h" 
#include "Comb.h"

// All allpass;
Comb comb(100, 0.5, 1, 0.5); // delay, input, feedforward, feedback
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
int16_t impulse = 0;
int pitchDelta = 1000;

void setup() {
  Serial.begin(115200);
  delay(200);
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
      pitchTime += pitchDelta;
    impulse = MAX_16;
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t leftVal = comb.next(impulse);
  if (impulse > 0) impulse = 0;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
