/*
 * Samp.h
 *
 * A sample wavetable playback class
 *
 * by Andrew R. Brown 2021 updated 2025
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef SAMP_H_
#define SAMP_H_

class Samp {

public:
  /** Constructor.
   * 
   */
  Samp() {};
   
  /** Constructor.
  * @param BUFFER_NAME the name of the array with sample data in it.
  * @param FRAME_COUNT the number of frames in the buffer (for stereo: total_samples / 2).
  * @param NUM_CHANNELS the number of audio channels, 1 = mono, 2 = stereo
  */
  Samp(const int16_t * BUFFER_NAME, unsigned long FRAME_COUNT, uint8_t NUM_CHANNELS): buffer(BUFFER_NAME),
              buffer_size((unsigned long) FRAME_COUNT),
              num_channels(NUM_CHANNELS) {
    setLoopingOff();
    endpos_fractional = (uint64_t)buffer_size << 32; // 32.32 fixed-point for long audio support
    startpos_fractional = 0;
  }

  /** Change the sound buffer which will be played by Samp.
  * @param BUFFER_NAME is the name of the array you're using.
  * @param FRAME_COUNT the number of frames in the buffer (for stereo: total_samples / 2).
  * @param BUFFER_SAMPLE_RATE the sample rate of the buffer data
  * @param NUM_CHANNELS the number of audio channels, 1 = mono, 2 = stereo
  */
  inline
  void setTable(int16_t * BUFFER_NAME, unsigned long FRAME_COUNT, uint32_t BUFFER_SAMPLE_RATE, uint8_t NUM_CHANNELS) {
    buffer = BUFFER_NAME;
    buffer_size = FRAME_COUNT;  // buffer_size stores frame count
    buffer_sample_rate = BUFFER_SAMPLE_RATE;
    num_channels = NUM_CHANNELS;
    startpos_fractional = 0;
    endpos_fractional = (uint64_t)buffer_size << 32; // 32.32 fixed-point for long audio support

    // Calculate phase increment for correct playback speed
    // Use 16.16 fixed-point: phase_increment = (buffer_rate / M16_rate) * 65536
    phase_increment_fractional = ((uint64_t)buffer_sample_rate << 16) / SAMPLE_RATE;

    Serial.println("frames: " + String(buffer_size) + " chans: " + String(num_channels) +
      " samples: " + String(buffer_size * num_channels) +
      " SR: " + String(buffer_sample_rate) + " Hz, phase_inc: " + String(phase_increment_fractional));
  }

  /** Sets the starting position in frames.
  * @param startpos is the offset position in frames (for stereo, 1 frame = L+R pair).
  */
  inline
  void setStart(unsigned int startpos)
  {
    startpos_fractional = (uint64_t)startpos << 32; // Convert to 32.32 fixed point
  }

  /** Resets the phase (the playhead) to the start position,
  * which will be 0 unless set to another value with setStart();
  */
  inline
  void start() {
    phase_fractional = startpos_fractional;
  }

  /** Sets a new start position and plays the sample from that position.
  * @param startpos position in frames from the beginning of the sound.
  */
  inline
  void start(unsigned int startpos) {
    setStart(startpos);
    start();
  }

  /** Sets the end position in frames from the beginning of the sound.
  * @param end position in frames (for stereo, 1 frame = L+R pair).
  */
  inline
  void setEnd(unsigned int end) {
    endpos_fractional = (uint64_t)end << 32; // Convert to 32.32 fixed point
  }

  /** Turns looping on */
  inline
  void setLoopingOn() {
    looping = true;
  }

  /** Turns looping off */
  inline
  void setLoopingOff() {
    looping = false;
  }

  /** Returns the sample from mono audio data at the current phase position. */
  inline
  int16_t next() {
    if (num_channels != 1) return 0; // sample buffer not setup (0) or not a mono file (2)
    if (phase_fractional >= endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
      } else {
        return 0;
      }
    }
    // Extract integer part from 32.32 fixed-point
    int16_t out = buffer[phase_fractional >> 32];
    incrementPhase();
    return out;
  }

  /** Returns the left channel sample from stereo audio data at the current phase position.
   * Call this before nextRight() in the same audio frame.
   * Phase increments after nextRight() is called.
   */
  inline
  int16_t nextLeft() {
    if (num_channels != 2) return 0; // Not a stereo file
    if (phase_fractional >= endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
      } else {
        return 0;
      }
    }
    // For stereo interleaved data: L, R, L, R...
    // Left channel is at even indices: (phase >> 32) * 2
    uint32_t sampleIndex = phase_fractional >> 32;
    int16_t out = buffer[sampleIndex * 2];
    if (envelopeOn) out = clip16((out * envTable[sampleIndex - (uint32_t)(startpos_fractional >> 32)])>>15);
    return out;
  }

  /** Returns the right channel sample from stereo audio data at the current phase position.
   * Call this after nextLeft() in the same audio frame.
   * This function increments the phase position.
   */
  inline
  int16_t nextRight() {
    if (num_channels != 2) return 0; // Not a stereo file
    if (phase_fractional >= endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
      } else {
        return 0;
      }
    }
    // For stereo interleaved data: L, R, L, R...
    // Right channel is at odd indices: (phase >> 32) * 2 + 1
    uint32_t sampleIndex = phase_fractional >> 32;
    int16_t out = buffer[sampleIndex * 2 + 1];
    if (envelopeOn) out = clip16((out * envTable[sampleIndex - (uint32_t)(startpos_fractional >> 32)])>>15);
    incrementPhase();  // Increment after reading right channel
    return out;
  }

  /** Checks if the sample is playing by seeing if the phase is within the limits of its end position.*/
  inline
  boolean isPlaying() {
    return phase_fractional < endpos_fractional;
  }

  /** Set the base pitch of the sample (the pitch at normal playback speed).
  * @param hz frequency in Hz of the sample's natural pitch (default 440 Hz / A4)
  */
  inline
  void setBasePitch(float hz) {
    if (hz > 0) base_pitch = hz;
  }

  /** Get the current base pitch setting.
  * @return the base pitch in Hz
  */
  inline
  float getBasePitch() {
    return base_pitch;
  }

  /** Set the playback pitch - sample plays faster/slower relative to base_pitch.
  * @param frequency target pitch in Hz (e.g., 440 for A4, 261.63 for C4)
  */
  inline
  void setFreq(float frequency) {
    // 16.16 fixed-point with sample rate conversion and pitch shifting
    // phase_inc = 65536 * (buffer_sample_rate / SAMPLE_RATE) * (frequency / base_pitch)
    phase_increment_fractional = (unsigned long)(((uint64_t)buffer_sample_rate << 16) * frequency / (SAMPLE_RATE * base_pitch));
  }

  /**  Returns the sample at the given buffer index.
  * @param index between 0 and the buffer size.
  */
  inline
  int16_t atIndex(unsigned int index) {
    return buffer[index];
  }

  /** Set playback speed as a multiplier.
  * @param speed Playback speed multiplier (1.0 = normal, 2.0 = double speed, 0.5 = half speed)
   */
  inline
  void setSpeed(float speed) {
    if (speed <= 0) speed = 1.0f;
    // Calculate phase increment: base_increment * speed
    // base_increment = (buffer_sample_rate / SAMPLE_RATE) * 65536
    phase_increment_fractional = (unsigned long)(((uint64_t)buffer_sample_rate << 16) * speed / SAMPLE_RATE);
  }

  /** Get the current playback speed multiplier.
  * @return Current speed (1.0 = normal, 2.0 = double speed, 0.5 = half speed)
   */
  inline
  float getSpeed() {
    // Reverse the calculation: speed = phase_increment * SAMPLE_RATE / (buffer_sample_rate * 65536)
    return (float)phase_increment_fractional * SAMPLE_RATE / ((float)buffer_sample_rate * 65536.0f);
  }

  unsigned long getPhaseIndex() {
    // Extract integer part from 32.32 fixed-point
    return phase_fractional >> 32;
  }

  /** Generate a linear amplitude envelope
   *
   * Creates an envelope with linear attack and release ramps
   * and a flat sustain section. Peak value is 32767 (MAX_16).
   *
   * @param table Pointer to int16_t array to fill (must be pre-allocated)
   * @param tableSize Length of the envelope table
   * @param curveAmount Amount of table used for attack/release ramps (0.0 to 1.0)
   *              - 0.0: all sustain, no ramps (flat at 32767)
   *              - 0.5: 25% attack, 50% sustain, 25% release
   *              - 1.0: 50% attack, 0% sustain, 50% release
   */
  inline
  void generateLinearEnvelope(int tableSize, float curveAmount) {
    if (tableSize <= 0) return;
    if (envTable) {
      free(envTable); // clean up any prev memory
    }
    // Allocate memory, using PSRAM if available on ESP32
    #if IS_ESP32()
      if (isPSRAMAvailable() && ESP.getFreePsram() > tableSize * sizeof(int16_t)) {
        envTable = (int16_t *) ps_calloc(tableSize, sizeof(int16_t));
        if (!envTable) {
          envTable = (int16_t*)malloc(tableSize * sizeof(int16_t));
        }
      } else {
        envTable = (int16_t*)malloc(tableSize * sizeof(int16_t));
      }
    #else
      envTable = (int16_t*)malloc(tableSize * sizeof(int16_t));
    #endif

    if (!envTable) {
      envelopeOn = false;
      return;
    }
    envelopeOn = true;

    curveAmount = max(0.0f, min(1.0f, curveAmount));

    int attackSamples = (int)(tableSize * curveAmount * 0.5f);
    int releaseSamples = attackSamples;
    int sustainSamples = tableSize - attackSamples - releaseSamples;

    int index = 0;

    // Attack section: linear ramp from 0 to 32767
    for (int i = 0; i < attackSamples; i++) {
      float t = (float)i / (float)attackSamples;  // 0.0 to 1.0
      envTable[index++] = (int16_t)(t * 32767.0f);
    }

    // Sustain section: flat at 32767
    for (int i = 0; i < sustainSamples; i++) {
      envTable[index++] = 32767;
    }

    // Release section: linear ramp from 32767 to 0
    for (int i = 0; i < releaseSamples; i++) {
      float t = (float)i / (float)releaseSamples;  // 0.0 to 1.0
      envTable[index++] = (int16_t)((1.0f - t) * 32767.0f);
    }
  }

  /** Generate a cosine envelope lookup table
   *
   * Creates an envelope with cosine-shaped attack and release curves
   * and a flat sustain section. Peak value is 32767 (MAX_16).
   *
   * @param table Pointer to int16_t array to fill (must be pre-allocated)
   * @param tableSize Length of the envelope table
   * @param curveAmount Amount of table used for attack/release curves (0.0 to 1.0)
   *
   */
  inline
  void generateCosineEnvelope(int tableSize, float curveAmount) {
    if (tableSize <= 0) return;
    if (envTable) {
      free(envTable); // clean up any prev memory
    }
    // Allocate memory, using PSRAM if available on ESP32
    #if IS_ESP32()
      if (isPSRAMAvailable() && ESP.getFreePsram() > tableSize * sizeof(int16_t)) {
        envTable = (int16_t *) ps_calloc(tableSize, sizeof(int16_t));
        if (!envTable) {
          envTable = (int16_t*)malloc(tableSize * sizeof(int16_t));
        }
      } else {
        envTable = (int16_t*)malloc(tableSize * sizeof(int16_t));
      }
    #else
      envTable = (int16_t*)malloc(tableSize * sizeof(int16_t));
    #endif

    if (!envTable) {
      envelopeOn = false;
      return;
    }
    envelopeOn = true;
    curveAmount = max(0.0f, min(1.0f, curveAmount));

    int attackSamples = (int)(tableSize * curveAmount * 0.5f);
    int releaseSamples = attackSamples;
    int sustainSamples = tableSize - attackSamples - releaseSamples;

    int index = 0;

    // Attack section
    for (int i = 0; i < attackSamples; i++) {
      float t = (float)i / (float)attackSamples;
      float angle = M_PI * t;
      float value = (1.0f - cosf(angle)) * 0.5f;
      envTable[index++] = (int16_t)(value * 32767.0f);
    }

    // Sustain section
    for (int i = 0; i < sustainSamples; i++) {
      envTable[index++] = 32767;
    }

    // Release section
    for (int i = 0; i < releaseSamples; i++) {
      float t = (float)i / (float)releaseSamples;
      float angle = M_PI * t;
      float value = (1.0f + cosf(angle)) * 0.5f;
      envTable[index++] = (int16_t)(value * 32767.0f);
    }
  }

  /* Turn amplitude envelope off if already on*/
  void setEnvelopeOff() {
    if (envelopeOn) envelopeOn = false;
  }

private:
  /** Increments the phase of the buffer index without returning a sample. */
  inline
  void incrementPhase() {
    // phase_fractional is 32.32, phase_increment_fractional is 16.16
    // Shift increment left by 16 to align fractional parts
    phase_fractional += (uint64_t)phase_increment_fractional << 16;
  }

  volatile uint64_t phase_fractional = 0;           // 32.32 fixed-point for long audio support
  volatile uint32_t phase_increment_fractional = 65536; // 16.16 fixed-point (1.0 = normal speed)
  const int16_t * buffer;
  bool looping = false;
  uint64_t startpos_fractional = 0;   // 32.32 fixed-point
  uint64_t endpos_fractional = 0;     // 32.32 fixed-point
  uint32_t buffer_size = 0;           // frame count (not fixed-point)
  uint8_t num_channels = 0;           // 1 = mono, 2 = stereo
  uint32_t buffer_sample_rate = SAMPLE_RATE;
  float base_pitch = 440.0f; // A4 - the pitch at which sample plays at normal speed
  int16_t * envTable = nullptr;
  bool envelopeOn = false;
};

#endif /* SAMP_H_ */
