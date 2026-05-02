// TLVSetup - TLV320AIC3104 codec setup example for M16
//
// Plays a 440 Hz sinewave through TLV320AIC3104 headphone and line outputs.
// Demonstrates basic TLV codec initialisation before audioStart().
//
// Wiring:
//   TLV320AIC3104  →  ESP32
//   ─────────────────────────────────────────────
//   DIN            ←  GPIO18  (I2S data out)
//   DOUT           →  GPIO19  (I2S data in — moved from default GPIO21)
//   BCLK           ←  GPIO16  (I2S bit clock)
//   WCLK           ←  GPIO17  (I2S word clock)
//   SDA            ↔  GPIO21  (I2C data)
//   SCL            ↔  GPIO22  (I2C clock)
//   AVDD/DRVDD     —  3.3 V
//   DVDD           —  1.8 V   (or use on-board LDO if available)
//   AVSS/DVSS/DRVSS — GND
//
// GPIO19 is used for I2S DIN (instead of the default GPIO21) to free
// GPIO21 for I2C SDA. Call seti2sPins() before tlv.begin() and audioStart().

#include "M16.h"
#include "Osc.h"
#include "TLV.h"

TLV tlv;        // SDA=21, SCL=22 (default Arduino I2C pins)
Osc aOsc;
int16_t vol = 900;  // 0–1024

void setup() {
  Serial.begin(115200);
  delay(200);

  aOsc.sinGen();
  aOsc.setFreq(440.0f);

  // Move I2S DIN to GPIO19 so GPIO21 is free for I2C SDA
  seti2sPins(16, 17, 18, 19);

  // Start I2S first — audioStart() provides the BCLK that the codec PLL needs to lock
  // useInternalDAC();
  audioStart();

  // Initialise TLV codec AFTER audioStart() so BCLK is already running
  if (!tlv.begin()) {
    Serial.println("TLV codec init failed — check wiring and I2C address (0x18)");
  } else {
    Serial.println("TLV codec ready — 44100 Hz, 16-bit stereo");
  }
}

void loop() {}

void audioUpdate() {
  int32_t sample = (aOsc.next() * vol) >> 10;
  i2s_write_samples((int16_t)sample, (int16_t)sample);
}
