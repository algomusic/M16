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
  Samp() {}

  /** Destructor - free allocated envelope buffers */
  ~Samp() {
    freeEnvelopeBuffers();
  }

  /** Disable copy constructor to prevent double-free issues */
  Samp(const Samp&) = delete;

  /** Disable copy assignment to prevent double-free issues */
  Samp& operator=(const Samp&) = delete;
   
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

  /** Begins playback and resets the phase (the playhead) to the start position,
  * which will be 0 unless set to another value with setStart();
  */
  inline
  void start() {
    // Reset envelope phase to start of envelope
    envPhase = 0;
    envComplete = false;
    // Calculate envelope increment to complete envelope before segment ends
    // Add ~3% margin to ensure envelope completes early with zero-padding
    // rather than risking discontinuity if next segment starts before envelope finishes
    uint32_t segmentFrames = (uint32_t)((endpos_fractional - startpos_fractional) >> 32);
    if (segmentFrames > 0 && activeEnvSize > 0) {
      uint64_t numerator = (uint64_t)activeEnvSize * phase_increment_fractional;
      envPhaseIncrement = (uint32_t)((numerator + segmentFrames - 1) / segmentFrames);
      // Add ~5% margin (>> 4 = divide by 16 ≈ 6.25%)
      envPhaseIncrement += (envPhaseIncrement + 15) >> 4;
    } else {
      envPhaseIncrement = 65536; // Default to 1.0 in 16.16 fixed-point
    }

    // Now reset audio phase
    phase_fractional = startpos_fractional;

    // Memory barrier to ensure all state updates are visible to audio core
    // before setting playing = true (ESP32 dual-core cache coherency)
    #if defined(ESP32)
      __sync_synchronize();
    #endif

    playing = true;
  }

  /** Halts playback */
  inline
  void stop() {
    playing = false;
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
    if (!playing) return 0; // silence until start() is called
    if (!buffer || buffer_size == 0) return 0; // buffer not set up
    if (num_channels != 1) return 0; // not a mono file
    if (phase_fractional >= endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
        envPhase = 0; // Reset envelope for loop
        envComplete = false; // Reset completion flag for loop
      } else {
        playing = false; // Mark as stopped for clean state
        return 0;
      }
    }
    // Extract integer part from 32.32 fixed-point
    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size || sampleIndex > 0x7FFFFFFF) return 0;
    int16_t out = buffer[sampleIndex];
    // Apply envelope using independent envelope phase (not tied to audio playback speed)
    if (envelopeOn && activeEnvTable) {
      if (envComplete) {
        out = 0; // Envelope already complete, output silence
      } else {
        uint32_t envIndex = envPhase >> 16; // Extract integer part from 16.16 fixed-point
        if (envIndex < (uint32_t)activeEnvSize) {
          out = clip16(((int32_t)out * activeEnvTable[envIndex]) >> 15);
          uint32_t newEnvPhase = envPhase + envPhaseIncrement;
          if (newEnvPhase >= envPhase) {
            envPhase = newEnvPhase; // Normal advance
          } else {
            envComplete = true; // Overflow detected, mark complete
          }
        } else {
          envComplete = true; // Envelope index reached end
          out = 0; // Ensure silence to prevent clicks
        }
      }
    }
    incrementPhase();
    return out;
  }

  /** Returns the left channel sample from stereo audio data at the current phase position.
   * Call this before nextRight() in the same audio frame.
   * Phase increments after nextRight() is called.
   */
  inline
  int16_t nextLeft() {
    if (!playing) return 0; // silence until start() is called
    if (!buffer || buffer_size == 0) return 0; // buffer not set up
    if (num_channels != 2) return 0; // Not a stereo file
    if (phase_fractional >= endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
        envPhase = 0; // Reset envelope for loop
        envComplete = false; // Reset completion flag for loop
      } else {
        playing = false; // Mark as stopped for clean state
        return 0;
      }
    }
    // For stereo interleaved data: L, R, L, R...
    // Left channel is at even indices: (phase >> 32) * 2
    uint32_t sampleIndex = phase_fractional >> 32;
    // Safety check: prevent buffer overrun from torn 64-bit reads
    // For stereo, actual buffer index is sampleIndex * 2, so check more strictly
    if (sampleIndex >= buffer_size || sampleIndex > 0x3FFFFFFF) return 0;
    int16_t out = buffer[sampleIndex * 2];
    // Apply envelope using independent envelope phase (not tied to audio playback speed)
    // Note: envelope phase is advanced in nextRight() to keep L/R in sync
    if (envelopeOn && activeEnvTable) {
      if (envComplete) {
        out = 0; // Envelope already complete, output silence
      } else {
        uint32_t envIndex = envPhase >> 16; // Extract integer part from 16.16 fixed-point
        if (envIndex < (uint32_t)activeEnvSize) {
          out = clip16(((int32_t)out * activeEnvTable[envIndex]) >> 15);
        } else {
          envComplete = true; // Envelope index reached end
          out = 0; // Ensure silence to prevent clicks
        }
      }
    }
    return out;
  }

  /** Returns the right channel sample from stereo audio data at the current phase position.
   * Call this after nextLeft() in the same audio frame.
   * This function increments the phase position.
   */
  inline
  int16_t nextRight() {
    if (!playing) return 0; // silence until start() is called
    if (!buffer || buffer_size == 0) return 0; // buffer not set up
    if (num_channels != 2) return 0; // Not a stereo file
    if (phase_fractional >= endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
        envPhase = 0; // Reset envelope for loop
        envComplete = false; // Reset completion flag for loop
      } else {
        playing = false; // Mark as stopped for clean state
        return 0;
      }
    }
    // For stereo interleaved data: L, R, L, R...
    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size || sampleIndex > 0x3FFFFFFF) return 0;
    int16_t out = buffer[sampleIndex * 2 + 1];
    // Apply envelope using independent envelope phase (not tied to audio playback speed)
    if (envelopeOn && activeEnvTable) {
      if (envComplete) {
        out = 0; // Envelope already complete, output silence
      } else {
        uint32_t envIndex = envPhase >> 16; // Extract integer part from 16.16 fixed-point
        if (envIndex < (uint32_t)activeEnvSize) {
          out = clip16(((int32_t)out * activeEnvTable[envIndex]) >> 15);
          // Advance envelope with overflow detection to prevent wrap-around jumps
          uint32_t newEnvPhase = envPhase + envPhaseIncrement;
          if (newEnvPhase >= envPhase) {
            envPhase = newEnvPhase; // Normal advance
          } else {
            envComplete = true; // Overflow detected, mark complete
          }
        } else {
          envComplete = true; // Envelope index reached end
          out = 0; // Ensure silence to prevent clicks
        }
      }
    }
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
  * @return sample value, or 0 if buffer not set or index out of bounds
  */
  inline
  int16_t atIndex(unsigned int index) {
    if (!buffer || index >= buffer_size) return 0;
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

  /** Generate a linear amplitude envelope (double-buffered for thread safety)
   * @param tableSize Length of the envelope table
   * @param curveAmount Amount of table used for attack/release ramps (0.0 to 1.0)
   */
  inline
  void generateLinearEnvelope(int tableSize, float curveAmount) {
    int16_t* envTable;
    int attackSamples, sustainSamples, releaseSamples;
    if (!prepareEnvelope(tableSize, curveAmount, envTable, attackSamples, sustainSamples, releaseSamples)) return;

    int index = 0;
    // Attack: linear ramp 0 to 1 (t spans exactly 0.0 to 1.0)
    for (int i = 0; i < attackSamples && index < tableSize; i++) {
      float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
      envTable[index++] = (int16_t)(t * 32767.0f);
    }
    // Sustain
    fillSustain(envTable, index, sustainSamples, tableSize);
    // Release: linear ramp 1 to 0 (t spans exactly 0.0 to 1.0)
    for (int i = 0; i < releaseSamples && index < tableSize; i++) {
      float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
      envTable[index++] = (int16_t)((1.0f - t) * 32767.0f);
    }
    finalizeEnvelope(envTable, tableSize);
  }

  /** Generate a cosine envelope lookup table (double-buffered for thread safety)
   * @param tableSize Length of the envelope table
   * @param curveAmount Amount of table used for attack/release curves (0.0 to 1.0)
   */
  inline
  void generateCosineEnvelope(int tableSize, float curveAmount) {
    int16_t* envTable;
    int attackSamples, sustainSamples, releaseSamples;
    if (!prepareEnvelope(tableSize, curveAmount, envTable, attackSamples, sustainSamples, releaseSamples)) return;

    int index = 0;
    // Attack: cosine curve 0 to 1 (t spans exactly 0.0 to 1.0)
    for (int i = 0; i < attackSamples && index < tableSize; i++) {
      float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
      envTable[index++] = (int16_t)((1.0f - cosf(M_PI * t)) * 0.5f * 32767.0f);
    }
    // Sustain
    fillSustain(envTable, index, sustainSamples, tableSize);
    // Release: cosine curve 1 to 0 (t spans exactly 0.0 to 1.0)
    for (int i = 0; i < releaseSamples && index < tableSize; i++) {
      float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
      envTable[index++] = (int16_t)((1.0f + cosf(M_PI * t)) * 0.5f * 32767.0f);
    }
    finalizeEnvelope(envTable, tableSize);
  }

  /** Generate a Gaussian envelope lookup table (double-buffered for thread safety)
   * @param tableSize Length of the envelope table
   * @param curveAmount Amount of table used for attack/release curves (0.0 to 1.0)
   * @param sigma Controls steepness (default 0.4, lower = steeper, range 0.1 to 1.0)
   */
  inline
  void generateGaussianEnvelope(int tableSize, float curveAmount, float sigma = 0.4f) {
    int16_t* envTable;
    int attackSamples, sustainSamples, releaseSamples;
    if (!prepareEnvelope(tableSize, curveAmount, envTable, attackSamples, sustainSamples, releaseSamples)) return;

    // Gaussian normalization constants
    sigma = max(0.1f, min(1.0f, sigma));
    float invSigma = 1.0f / sigma;
    float g0 = expf(-(invSigma * invSigma));
    float scale = 1.0f / (1.0f - g0);

    int index = 0;
    // Attack: Gaussian rise 0 to 1 (t spans exactly 0.0 to 1.0)
    for (int i = 0; i < attackSamples && index < tableSize; i++) {
      float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
      float x = (1.0f - t) * invSigma;
      envTable[index++] = (int16_t)((expf(-(x * x)) - g0) * scale * 32767.0f);
    }
    // Sustain
    fillSustain(envTable, index, sustainSamples, tableSize);
    // Release: Gaussian fall 1 to 0 (t spans exactly 0.0 to 1.0)
    for (int i = 0; i < releaseSamples && index < tableSize; i++) {
      float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
      float x = t * invSigma;
      envTable[index++] = (int16_t)((expf(-(x * x)) - g0) * scale * 32767.0f);
    }
    finalizeEnvelope(envTable, tableSize);
  }

  /** Turn amplitude envelope off if already on */
  void setEnvelopeOff() {
    envelopeOn = false;
  }

  /** Free envelope buffers to reclaim memory
   * Call this when you no longer need envelope functionality for this Samp instance
   */
  void freeEnvelopes() {
    freeEnvelopeBuffers();
  }

  /** Print envelope values graphically to Serial for debugging
   * @param displayRows Number of rows to display (samples envelope, default 32)
   * @param displayWidth Width of the bar graph in characters (default 50)
   */
  void printEnvelope(int displayRows = 32, int displayWidth = 50) {
    if (!activeEnvTable || activeEnvSize <= 0) {
      Serial.println("Samp: No active envelope to display");
      return;
    }

    Serial.println();
    Serial.println("=== Envelope ===");
    Serial.println("Size: " + String(activeEnvSize) + " samples (" +
                   String(framesToMs(activeEnvSize)) + " ms)");
    Serial.println();

    // Sample the envelope at regular intervals
    int step = max(1, activeEnvSize / displayRows);
    int actualRows = (activeEnvSize + step - 1) / step;

    for (int row = 0; row < actualRows && row < displayRows; row++) {
      int envIndex = row * step;
      if (envIndex >= activeEnvSize) break;

      int16_t value = activeEnvTable[envIndex];
      // Scale value (0-32767) to display width
      int barLength = (int)((long)value * displayWidth / 32767);
      barLength = max(0, min(displayWidth, barLength));

      // Print index (right-aligned)
      if (envIndex < 10) Serial.print("    ");
      else if (envIndex < 100) Serial.print("   ");
      else if (envIndex < 1000) Serial.print("  ");
      else if (envIndex < 10000) Serial.print(" ");
      Serial.print(envIndex);
      Serial.print(" |");

      // Print bar
      for (int i = 0; i < barLength; i++) {
        Serial.print("#");
      }

      // Print value at end
      Serial.print(" ");
      Serial.println(value);
    }
  }

  // Convert milliseconds to frames
  inline unsigned long msToFrames(float milliseconds) {
    // frames = (ms / 1000) * sampleRate
    unsigned long frames = (unsigned long)((milliseconds / 1000.0f) * SAMPLE_RATE);
    return frames;
  }


  // Convert frames to milliseconds
  inline unsigned long framesToMs(unsigned long frames) {
    // ms = (frames / sampleRate) * 1000
    return (frames * 1000.0f) / (float)SAMPLE_RATE;
  }

  // Convert frames to microseconds
  inline unsigned long framesToMicros(unsigned long frames) {
    // micros = (frames / sampleRate) * 1000000
    return (frames * 1000000.0f) / (float)SAMPLE_RATE;
  }

  /**
   * Derive BPM from frame count that evenly subdivides into power-of-2 beats
   * 
   * @param frames Total sample frames in audio file
   * @param beatsOut Pointer to receive the number of beats (optional, can be nullptr)
   * @param sampleRate Sample rate (default SAMPLE_RATE)
   * @param targetBPM Target BPM to get closest to (default 120)
   * @param minBPM Minimum acceptable BPM (default 60)
   * @param maxBPM Maximum acceptable BPM (default 180)
   * @return BPM closest to target that evenly divides frames into 2^n beats
   */
  // Usage Examples

  // // Basic usage - just get BPM
  // float bpm = sample.deriveBPM(wav.getFrameCount());
  // Serial.printf("Derived BPM: %.2f\n", bpm);

  // // Get both BPM and number of beats
  // unsigned long numBeats;
  // float bpm = sample.deriveBPM(wav.getFrameCount(), &numBeats);
  // Serial.printf("BPM: %.2f, Beats: %lu\n", bpm, numBeats);

  // Custom target BPM (e.g., closer to 100)
  // float bpm = sample.deriveBPM(wav.getFrameCount(), nullptr, SAMPLE_RATE, 100.0f);

  // Constrained range (e.g., only 90-150 BPM)
  // float bpm = sample.deriveBPM(wav.getFrameCount(), nullptr, SAMPLE_RATE, 120.0f, 90.0f, 150.0f);

  float deriveBPM(unsigned long frames, unsigned long* beatsOut = nullptr, 
                  int sampleRate = SAMPLE_RATE, float targetBPM = 120.0f, 
                  float minBPM = 60.0f, float maxBPM = 180.0f) {

      float closestBPM = targetBPM;
      unsigned long closestBeats = 1;
      float minDiff = maxBPM;  // Start with large difference

      // Try powers of 2: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024...
      for (int power = 0; power <= 16; power++) {
          unsigned long beats = 1UL << power;

          // BPM = beats * sampleRate * 60 / frames
          float bpm = (float)beats * (float)sampleRate * 60.0f / (float)frames;

          // Skip if outside acceptable range
          if (bpm < minBPM || bpm > maxBPM) continue;

          // Check if this BPM is closer to target
          float diff = fabsf(bpm - targetBPM);
          if (diff < minDiff) {
              minDiff = diff;
              closestBPM = bpm;
              closestBeats = beats;
          }
      }

      // Output beats if pointer provided
      if (beatsOut != nullptr) {
          *beatsOut = closestBeats;
      }

      return closestBPM;
  }


private:
  /** Increments the phase of the buffer index without returning a sample. */
  inline
  void incrementPhase() {
    // phase_fractional is 32.32, phase_increment_fractional is 16.16
    // Shift increment left by 16 to align fractional parts
    phase_fractional += (uint64_t)phase_increment_fractional << 16;
  }

  /** Prepare envelope buffer for writing (validation, allocation, segment calculation)
   * @return true if buffer is ready, false if envelope should be disabled
   */
  bool prepareEnvelope(int tableSize, float curveAmount, int16_t*& envTable,
                       int& attackSamples, int& sustainSamples, int& releaseSamples) {
    if (tableSize <= 0 || tableSize > 1000000) return false;
    if (curveAmount <= 0) {
      envelopeOn = false;
      return false;
    }

    // Determine which buffer is inactive
    int16_t** inactiveBuffer;
    int* inactiveSize;
    if (activeEnvTable == envTableA) {
      inactiveBuffer = &envTableB;
      inactiveSize = &envTableSizeB;
    } else {
      inactiveBuffer = &envTableA;
      inactiveSize = &envTableSizeA;
    }

    // Reallocate if needed
    if (*inactiveSize < tableSize) {
      if (*inactiveBuffer) {
        free(*inactiveBuffer);
        *inactiveBuffer = nullptr;
        *inactiveSize = 0;
      }
      *inactiveBuffer = allocEnvBuffer(tableSize);
      if (!*inactiveBuffer) {
        Serial.println("Samp: envelope allocation failed");
        envelopeOn = false;
        return false;
      }
      *inactiveSize = tableSize;
    }

    envTable = *inactiveBuffer;
    curveAmount = max(0.0f, min(1.0f, curveAmount));
    attackSamples = (int)(tableSize * curveAmount * 0.5f);
    releaseSamples = attackSamples;
    sustainSamples = tableSize - attackSamples - releaseSamples;
    return true;
  }

  /** Fill sustain section of envelope */
  inline void fillSustain(int16_t* envTable, int& index, int sustainSamples, int tableSize) {
    for (int i = 0; i < sustainSamples && index < tableSize; i++) {
      envTable[index++] = 32767;
    }
  }

  /** Finalize envelope (atomic swap to make it active) */
  inline void finalizeEnvelope(int16_t* envTable, int tableSize) {
    // Disable envelope during swap to prevent race condition where ISR reads
    // new size with old pointer (could read beyond buffer bounds)
    envelopeOn = false;

    // Memory barrier to ensure envelope buffer writes are visible to other core
    // before we update the pointer (ESP32 dual-core cache coherency)
    #if defined(ESP32)
      __sync_synchronize();
    #endif

    activeEnvTable = envTable;
    activeEnvSize = tableSize;
    envelopeOn = true;
  }

  /** Free envelope buffers safely */
  void freeEnvelopeBuffers() {
    envelopeOn = false;
    activeEnvTable = nullptr;
    activeEnvSize = 0;
    if (envTableA) {
      free(envTableA);
      envTableA = nullptr;
      envTableSizeA = 0;
    }
    if (envTableB) {
      free(envTableB);
      envTableB = nullptr;
      envTableSizeB = 0;
    }
  }

  /** Allocate envelope buffer, preferring PSRAM on ESP32, falling back to heap RAM
   * @param size Number of int16_t samples to allocate
   * @return Pointer to allocated buffer, or nullptr if allocation failed
   */
  int16_t* allocEnvBuffer(int size) {
    // Sanity check for invalid sizes (negative from underflow, zero, or unreasonably large)
    if (size <= 0 || size > 1000000) {
      Serial.println("Samp: allocEnvBuffer - invalid size requested: " + String(size));
      return nullptr;
    }

    size_t bytesNeeded = (size_t)size * sizeof(int16_t);
    int16_t* buf = nullptr;

    #if IS_ESP32()
      // Try PSRAM first if available and has enough space
      if (isPSRAMAvailable()) {
        size_t freePsram = ESP.getFreePsram();
        if (freePsram > bytesNeeded + 1024) {  // Keep 1KB headroom
          buf = (int16_t*)ps_calloc(size, sizeof(int16_t));
          if (buf) {
            // Serial.println("Samp: Allocated " + String(bytesNeeded) + " bytes in PSRAM");
            return buf;
          }
          Serial.println("Samp: PSRAM allocation failed despite available space");
        }
      }

      // Fall back to regular heap RAM
      size_t freeHeap = ESP.getFreeHeap();
      if (freeHeap > bytesNeeded + 4096) {  // Keep 4KB headroom for system
        buf = (int16_t*)calloc(size, sizeof(int16_t));
        if (buf) {
          // Serial.println("Samp: Allocated " + String(bytesNeeded) + " bytes in heap RAM");
          return buf;
        }
      }

      // Allocation failed
      Serial.println("Samp: Memory allocation failed! Requested: " + String(bytesNeeded) +
                     " bytes, Free heap: " + String(freeHeap) + " bytes");
    #else
      // Non-ESP32 platform - just use regular malloc
      buf = (int16_t*)calloc(size, sizeof(int16_t));
      if (buf) {
        return buf;
      }
      Serial.println("Samp: Memory allocation failed for " + String(bytesNeeded) + " bytes");
    #endif

    return nullptr;
  }

  volatile uint64_t phase_fractional = 0;           // 32.32 fixed-point for long audio support
  volatile uint32_t phase_increment_fractional = 65536; // 16.16 fixed-point (1.0 = normal speed)
  const int16_t * buffer = nullptr;  // Initialize to nullptr to prevent undefined behavior
  bool playing = false;
  bool looping = false;
  uint64_t startpos_fractional = 0;   // 32.32 fixed-point
  uint64_t endpos_fractional = 0;     // 32.32 fixed-point
  uint32_t buffer_size = 0;           // frame count (not fixed-point)
  uint8_t num_channels = 0;           // 1 = mono, 2 = stereo
  uint32_t buffer_sample_rate = SAMPLE_RATE;
  float base_pitch = 440.0f; // A4 - the pitch at which sample plays at normal speed

  // Double-buffered envelope tables to avoid race conditions
  int16_t* envTableA = nullptr;       // Envelope buffer A
  int16_t* envTableB = nullptr;       // Envelope buffer B
  int16_t* volatile activeEnvTable = nullptr;  // Volatile POINTER to active buffer (ISR must re-read)
  int envTableSizeA = 0;              // Allocated size of buffer A
  int envTableSizeB = 0;              // Allocated size of buffer B
  volatile bool envelopeOn = false;
  volatile int activeEnvSize = 0;     // Size of active envelope

  // Envelope phase tracking (independent of audio playback speed)
  volatile uint32_t envPhase = 0;           // 16.16 fixed-point envelope position
  volatile uint32_t envPhaseIncrement = 0;  // 16.16 fixed-point increment per output sample
  volatile bool envComplete = false;        // Flag to prevent wrap-around issues with long envelopes
};

#endif /* SAMP_H_ */
