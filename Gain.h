/*
 * Gain.h
 *
 * A simple, cross-core-safe audio level control for M16.
 * 
 * by Andrew R. Brown 2026
 *
 * This file is part of the M16 audio library. Relies on M16.h.
 */

#ifndef GAIN_H_
#define GAIN_H_

#include <atomic>
#include <stdint.h>

#include "CODECS.h"

class Gain {
public:
  /** Construct at full level (1024). */
  Gain() = default;

  /** Construct with a 10-bit level from 0 (silent) to 1024 (full). */
  explicit Gain(int level) { setLevel(level); }

  /** Construct with a normalized level from 0.0 (silent) to 1.0 (full). */
  explicit Gain(float level) { setLevel(level); }

  /** Set a 10-bit level: 0 = silent, 512 = half, 1024 = full. */
  inline void setLevel(int level) {
    if (level < 0) level = 0;
    else if (level > 1024) level = 1024;
    levelQ10.store((int16_t)level, std::memory_order_relaxed);
  }

  /** Set a normalized level from 0.0 to 1.0. */
  inline void setLevel(float level) {
    if (level < 0.0f) level = 0.0f;
    else if (level > 1.0f) level = 1.0f;
    setLevel((int)(level * 1024.0f + 0.5f));
  }

  /** Return the current 10-bit level. */
  inline int getLevel() const {
    return levelQ10.load(std::memory_order_relaxed);
  }

  /** Return the current normalized level. */
  inline float getLevelNormal() const {
    return getLevel() * (1.0f / 1024.0f);
  }

  /** Apply the current level to one sample. */
  inline int32_t next(int32_t input) const {
    int32_t level = levelQ10.load(std::memory_order_relaxed);
    return (int32_t)(((int64_t)input * level) >> 10);
  }

private:
  std::atomic<int16_t> levelQ10{1024};
};

/** Control the Waveshare speaker amplifier through TCA9555 P1.0 (EXIO8).
 * This pin is an on/off switch. Set playback volume with es8311SetVolumeDb().
 * Call from setup()/loop(), never audioUpdate(). Other expander pins are kept.
 */
inline bool tca9555SpeakerEnable(bool enabled, uint8_t address = 0x20) {
  uint8_t output, direction;
  if (!M16CodecDetail::read(address, 0x03, output) ||
      !M16CodecDetail::read(address, 0x07, direction)) return false;
  const uint8_t nextOutput = enabled ? (uint8_t)(output | 0x01) : (uint8_t)(output & ~0x01);
  // Set the latch before changing pin direction to prevent an amplifier pulse.
  return M16CodecDetail::write(address, 0x03, nextOutput) &&
         M16CodecDetail::write(address, 0x07, (uint8_t)(direction & ~0x01));
}

/** Read the actual speaker amplifier state. Returns false on an I2C error. */
inline bool tca9555SpeakerEnabled(bool &enabled, uint8_t address = 0x20) {
  uint8_t output, direction;
  if (!M16CodecDetail::read(address, 0x03, output) ||
      !M16CodecDetail::read(address, 0x07, direction)) return false;
  enabled = (output & 0x01) != 0 && (direction & 0x01) == 0;
  return true;
}
#endif /* GAIN_H_ */
