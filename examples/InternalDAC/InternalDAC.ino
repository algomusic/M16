// M16 Internal DAC example
// Outputs audio via the ESP32's built-in DAC (no external I2S DAC needed)
// ESP32: outputs on GPIO25 (left) and GPIO26 (right)
// ESP32-S2: outputs on GPIO17 (left) and GPIO18 (right)
// Note: Output is 8-bit (reduced from M16's internal 16-bit resolution)
// Not available on ESP32-S3, C3, or other chips without internal DAC

#include "M16.h"
#include "Osc.h"

Osc aOsc1;
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
int pitchDelta = 1000;

void setup() {
  Serial.begin(115200);
  delay(200);
  aOsc1.sinGen();
  aOsc1.setPitch(69);
  useInternalDAC(); // enable internal DAC output, call before audioStart()
  audioStart();
}

void loop() {
  // audioLoop(); // required for internal DAC mode
  msNow = millis();

  if ((unsigned long)(msNow - pitchTime) >= pitchDelta) {
    pitchTime += pitchDelta;
    int pitch = random(48) + 36;
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
  }
}

void audioUpdate() {
  int32_t leftVal = (aOsc1.next() * vol) >> 10;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
