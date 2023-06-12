// M16 Audio Input example
// Audio passthrough for an I2S microphone
// Tested with ESP32-S3 and an INMP441 MEMS microphone
#include "M16.h" 
#include "Mic.h"

Mic audioIn; // create a audio input (Mic) object
int gain = 24; // amplify the input

void setup() {
  Serial.begin(115200);
  delay(200);
  audioStart();
}

void loop() {}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/

void audioUpdate() {
  int16_t leftVal = audioIn.nextLeft() * gain; // typically mic inputs are single channel
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}