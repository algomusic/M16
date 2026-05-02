// M16 TLV_DAC example
// Sinewave output via TLV320AIC3104 codec
#include "M16.h"
#include "Osc.h"
#include "TLV.h"

// TLV320AIC3104 codec — SDA=13, SCL=12
// Optional: pass a third argument to use hardware reset instead of software reset:
//   TLV tlv(13, 12, 14);  // GPIO14 → TLV ~RESET (active low)
// Use hardware reset if a GPIO is available — more reliable across power cycles.
// Without it, software reset via I2C is used automatically.
TLV tlv(13, 12);

Osc aOsc1;
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow = millis();
unsigned long pitchTime = msNow;
int pitchDelta = 1000; // ms between pitch updates

void setup() {
  Serial.begin(115200);
  delay(200);
  aOsc1.sinGen();
  aOsc1.setPitch(69); // MIDI pitch A4

  // Move I2S DIN from default GPIO21 to GPIO19 — frees GPIO21 for I2C SDA
  seti2sPins(8, 9, 10, 11); // bck, ws, dout, din

  // Start I2S first — audioStart() provides the BCLK that the codec PLL needs to lock
  // useInternalDAC();
  audioStart();

  // Initialise TLV320AIC3104 after audioStart() so BCLK is already running
  if (!tlv.begin()) {
    Serial.println("TLV codec init failed — check I2C wiring");
  }
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
  int32_t leftVal = (aOsc1.next() * vol) >> 10;
  int32_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
