// Waveshare ESP32-S3-AUDIO-Board: M16 sinewave through its onboard speaker.
// Tested on the Waveshare AI Smart Speaker Development Board
#include "M16.h"
#include "CODECS.h"
#include "Gain.h"
#include "Osc.h"

// Board routing is declared here 
constexpr int I2C_SDA_PIN = 11;
constexpr int I2C_SCL_PIN = 10;
constexpr int I2S_MCLK_PIN = 12;
constexpr int I2S_BCLK_PIN = 13;
constexpr int I2S_WS_PIN = 14;
constexpr int I2S_DOUT_PIN = 16; // ESP32 -> ES8311
constexpr int I2S_DIN_PIN = 15;  // ES7210 -> ESP32; unused in this output-only example

Osc oscillator;
Gain outputGain(1000);
unsigned long pitchTime = 0;

void setup() {
  Serial.begin(115200);
  oscillator.sinGen();
  oscillator.setPitch(69);
  setIsDualCore(false);

  if (!m16CodecWireBegin(I2C_SDA_PIN, I2C_SCL_PIN) ||
      !tca9555SpeakerEnable(false)) {
    Serial.println("Waveshare I2C or amplifier setup failed");
    return;
  }
  waveshareAudioPins(I2S_MCLK_PIN, I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, -1);
  audioStart();
  if (!es8311Setup() || !es8311SetVolumeDb(-24) || !tca9555SpeakerEnable(true)) {
    Serial.println("Waveshare speaker setup failed");
    return;
  }
  if (!es8311SetVolumeDb(-6)) { // from -95 to +32 dB, always put after audioStart()
    Serial.println("ES8311 volume setting failed; speaker remains off");
    return;
  }
  Serial.println("M16 Waveshare sinewave running");
}

void loop() {
  const unsigned long now = millis();
  if (now - pitchTime >= 1000) {
    pitchTime = now;
    const int pitch = random(48) + 36;
    Serial.println(pitch);
    oscillator.setPitch(pitch);
  }
}

void audioUpdate() {
  const int32_t sample = outputGain.next(oscillator.next());
  audioBlockWrite(sample, sample);
}
