// M16 Arpeggiator example
#include "M16.h" 
#include "Osc.h"
#include "Arp.h"

int16_t waveTable [TABLE_SIZE]; // empty wavetable
Osc osc1(waveTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
int pitchDelta = 250;
int arpPitches [] = {64, 60, 67, 69};
Arp arp1(arpPitches, 4, 2, ARP_UP_DOWN); // ARP_ORDER, ARP_UP, ARP_DOWN, ARP_UP_DOWN
// Arp arp1;

void setup() {
  Serial.begin(115200);
  delay(200);
  Osc::triGen(waveTable); // fill the wavetable
  osc1.setPitch(69);
  // arp1.setValues(arpPitches, 3);
  // arp1.setDirection(ARP_UP);
  // arp1.setRange(1);
  //setI2sPins(25, 27, 12, 16); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
}

void loop() {
  msNow = millis();
  if (msNow - pitchTime > pitchDelta || msNow - pitchTime < 0) {
    pitchTime = msNow;
    int pitch = arp1.next();
    Serial.println(pitch);
    osc1.setPitch(pitch);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  uint16_t leftVal = (osc1.next() * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
