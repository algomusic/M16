// Waveshare ESP32-S3-AUDIO-Board: two onboard microphones to speaker.
// Tested on the Waveshare AI Smart Speaker Development Board
#include "M16.h"
#include "CODECS.h"
#include "Mic.h"
#include "Gain.h"

// Board routing is declared here
constexpr int I2C_SDA_PIN = 11;
constexpr int I2C_SCL_PIN = 10;
constexpr int I2S_MCLK_PIN = 12;
constexpr int I2S_BCLK_PIN = 13;
constexpr int I2S_WS_PIN = 14;
constexpr int I2S_DOUT_PIN = 16; // ESP32 -> ES8311
constexpr int I2S_DIN_PIN = 15;  // ES7210 -> ESP32

Mic microphones;
Gain outputGain(1000); // 0 = silence, 1024 = unity; start at quarter level to reduce feedback
std::atomic<bool> audioReady{false};

void setup() {
  Serial.begin(115200);
  setIsDualCore(false);

  if (!m16CodecWireBegin(I2C_SDA_PIN, I2C_SCL_PIN) ||
      !tca9555SpeakerEnable(false)) {
    Serial.println("Waveshare I2C or amplifier setup failed");
    return;
  }
  // setup the DAC
  waveshareAudioPins(I2S_MCLK_PIN, I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN);
  // Start M16
  audioStart();
  // Check hardware
  if (!es8311Setup() || !es7210Setup()) {
    Serial.println("Codec setup failed; speaker remains off");
    return;
  }
  if (!es8311SetVolumeDb(-8)) { // from -95 to +32 dB, always put after audioStart()
    Serial.println("ES8311 volume setting failed; speaker remains off");
    return;
  }
  // outputGain scales samples in software; this sets the ES8311 DAC gain.
  audioReady.store(true, std::memory_order_release);
  if (!tca9555SpeakerEnable(true)) {
    audioReady.store(false, std::memory_order_release);
    Serial.println("Speaker amplifier setup failed");
    return;
  }
  Serial.println("M16 Waveshare microphone passthrough running");
}

void loop() {}

void audioUpdate() {
  if (!audioReady.load(std::memory_order_acquire)) {
    audioBlockWrite(0, 0);
    return;
  }
  int16_t left, right;
  microphones.nextStereo(left, right);
  const int32_t mono = ((int32_t)left + (int32_t)right) >> 1;
  const int32_t output = outputGain.next(mono);
  audioBlockWrite(output, output);
}
