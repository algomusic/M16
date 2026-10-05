/*
 * CODECS.h
 * Minimal Arduino-Wire control for supported audio codecs used with M16.
 * I2S pin and clock setup remains the responsibility of the target board.
 */
#ifndef M16_CODECS_H_
#define M16_CODECS_H_

#include "M16.h"
#include <Wire.h>
#include <stdint.h>
#include <stddef.h>

namespace M16CodecDetail {
inline bool write(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

inline bool read(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)address, 1) != 1) return false;
  value = (uint8_t)Wire.read();
  return true;
}

inline bool writeSequence(uint8_t address, const uint8_t (*registers)[2], size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (!write(address, registers[i][0], registers[i][1])) return false;
  return true;
}

} // namespace M16CodecDetail

/** Start the default Arduino I2C bus at 400 kHz (or a supplied clock). */
inline bool m16CodecWireBegin(uint32_t frequency = 400000) {
  Wire.begin();
  Wire.setClock(frequency);
  return true;
}

/** Start Arduino I2C on explicit pins; available on ESP32 Arduino cores. */
#if defined(ESP32)
inline bool m16CodecWireBegin(int sda, int scl, uint32_t frequency = 400000) {
  if (!Wire.begin(sda, scl)) return false;
  Wire.setClock(frequency);
  return true;
}

/** Configure the M16 ESP32-S3 I2S pin and MCLK fields for the Waveshare board. */
inline bool waveshareAudioPins(int mclk, int bclk, int ws, int dout, int din) {
  seti2sPins(bclk, ws, dout, din);
  std_cfg.gpio_cfg.mclk = (gpio_num_t)mclk;
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  return true;
}
#endif

/** Set ES8311 DAC output volume from -95 dB to +32 dB. */
inline bool es8311SetVolumeDb(int db, uint8_t address = 0x18) {
  if (db < -95 || db > 32) return false;
  const uint8_t value = (uint8_t)(0xBF + db * 2);
  return M16CodecDetail::write(address, 0x32, value);
}

/** Mute or unmute the ES8311 DAC output. */
inline bool es8311Mute(bool mute, uint8_t address = 0x18) {
  uint8_t value;
  if (!M16CodecDetail::read(address, 0x31, value)) return false;
  value = mute ? (uint8_t)(value | 0x20) : (uint8_t)(value & ~0x20);
  return M16CodecDetail::write(address, 0x31, value);
}

/**
 * Configure an ES8311 for M16 playback. Call after Wire.begin() and after
 * audioStart() has started matching I2S clocks. This sets codec registers only;
 * the board must provide 16-bit Philips stereo I2S and 256fs MCLK at the chosen
 * sample rate. The current register sequence supports 44.1 kHz only. Starts
 * at 0 dB.
 */
inline bool es8311Setup(uint32_t sampleRate = (uint32_t)SAMPLE_RATE,
                        uint16_t mclkRatio = 256,
                        uint8_t address = 0x18) {
  if (mclkRatio != 256 || sampleRate != 44100) return false;
  if (!M16CodecDetail::write(address, 0x00, 0x1F)) return false;
  delay(20);
  const uint8_t registers[][2] = {
    {0x00, 0x00}, {0x00, 0x80},
    {0x01, 0x3F}, {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10},
    {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},
    {0x00, 0x80}, {0x09, 0x0C}, {0x0A, 0x0C},
    {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00}, {0x13, 0x10},
    {0x1C, 0x6A}, {0x37, 0x08}, {0x31, 0x00}
  };
  return M16CodecDetail::writeSequence(address, registers,
      sizeof(registers) / sizeof(registers[0])) && es8311SetVolumeDb(0, address);
}

#endif // M16_CODECS_H_
