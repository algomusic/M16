// M16 Sinewave example
#include "M16.h" 
#include "Osc.h"

Osc aOsc1; // declare an instance of the oscillator class
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis(); // current time since starting in milliseconds
unsigned long pitchTime = msNow; // time for the next pitch update
int pitchDelta = 1000; // time between pitch updates

void setup() {
  Serial.begin(115200);
  delay(200); // let the Serial port get established
  aOsc1.sinGen(); // initialise the osc's built-in wavetable with a sine waveform
  aOsc1.setPitch(69); // MIDI pitch
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
    pitchTime += pitchDelta;
    int pitch = random(48) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t leftVal = (aOsc1.next() * vol)>>10;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
