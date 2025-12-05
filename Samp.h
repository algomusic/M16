/*
 * Samp.h
 *
 * A sample wavetable playback class.
 * Supports mono and stereo audio with variable speed playback,
 * looping, and optional amplitude envelope.
 *
 * by Andrew R. Brown 2021, updated 2025
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef SAMP_H_
#define SAMP_H_

class Samp {

public:
  // Shared envelope table (static - single copy for all instances)
  static uint8_t* sharedEnvTable;
  static int sharedEnvSize;
  static bool sharedEnvInitialized;

  /** Initialize the shared envelope table (call once in setup() before audioStart())
   * @param size Number of samples in envelope table (default 2048)
   * @param curveAmount Attack/release curve proportion 0.0-1.0 (default 0.8)
   * @param type Envelope type: 0=Gaussian, 1=Cosine, 2=Linear (default 0)
   */
  static void initSharedEnvelope(int size = 2048, float curveAmount = 0.8f, int type = 0) {
    if (sharedEnvTable) {
      free(sharedEnvTable);
      sharedEnvTable = nullptr;
    }

    sharedEnvSize = size;

    #if IS_ESP32()
      if (isPSRAMAvailable() && ESP.getFreePsram() > (size_t)size + 1024) {
        sharedEnvTable = (uint8_t*)ps_calloc(size, sizeof(uint8_t));
      }
      if (!sharedEnvTable) {
        sharedEnvTable = (uint8_t*)calloc(size, sizeof(uint8_t));
      }
    #else
      sharedEnvTable = (uint8_t*)calloc(size, sizeof(uint8_t));
    #endif

    if (!sharedEnvTable) {
      Serial.println("ERROR: Failed to allocate shared envelope");
      sharedEnvInitialized = false;
      sharedEnvSize = 0;
      return;
    }

    curveAmount = max(0.0f, min(1.0f, curveAmount));
    int attackSamples = (int)(size * curveAmount * 0.5f);
    int releaseSamples = attackSamples;
    int sustainSamples = size - attackSamples - releaseSamples;
    int index = 0;

    if (type == 0) {
      // Gaussian envelope
      float sigma = 0.4f;
      float invSigma = 1.0f / sigma;
      float g0 = expf(-(invSigma * invSigma));
      float scale = 1.0f / (1.0f - g0);

      for (int i = 0; i < attackSamples && index < size; i++) {
        float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
        float x = (1.0f - t) * invSigma;
        sharedEnvTable[index++] = (uint8_t)((expf(-(x * x)) - g0) * scale * 255.0f);
      }
      for (int i = 0; i < sustainSamples && index < size; i++) {
        sharedEnvTable[index++] = 255;
      }
      for (int i = 0; i < releaseSamples && index < size; i++) {
        float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
        float x = t * invSigma;
        sharedEnvTable[index++] = (uint8_t)((expf(-(x * x)) - g0) * scale * 255.0f);
      }
    } else if (type == 1) {
      // Cosine envelope
      for (int i = 0; i < attackSamples && index < size; i++) {
        float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
        sharedEnvTable[index++] = (uint8_t)((1.0f - cosf(M_PI * t)) * 0.5f * 255.0f);
      }
      for (int i = 0; i < sustainSamples && index < size; i++) {
        sharedEnvTable[index++] = 255;
      }
      for (int i = 0; i < releaseSamples && index < size; i++) {
        float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
        sharedEnvTable[index++] = (uint8_t)((1.0f + cosf(M_PI * t)) * 0.5f * 255.0f);
      }
    } else {
      // Linear envelope
      for (int i = 0; i < attackSamples && index < size; i++) {
        float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
        sharedEnvTable[index++] = (uint8_t)(t * 255.0f);
      }
      for (int i = 0; i < sustainSamples && index < size; i++) {
        sharedEnvTable[index++] = 255;
      }
      for (int i = 0; i < releaseSamples && index < size; i++) {
        float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
        sharedEnvTable[index++] = (uint8_t)((1.0f - t) * 255.0f);
      }
    }

    sharedEnvInitialized = true;
    Serial.println("Shared envelope initialized: " + String(size) + " bytes, type " + String(type));
  }

  /** Free the shared envelope table */
  static void freeSharedEnvelope() {
    if (sharedEnvTable) {
      free(sharedEnvTable);
      sharedEnvTable = nullptr;
    }
    sharedEnvSize = 0;
    sharedEnvInitialized = false;
  }

  /** Default constructor */
  Samp() {}

  ~Samp() {}

  Samp(const Samp&) = delete;
  Samp& operator=(const Samp&) = delete;

  /** Constructor with buffer
   * @param BUFFER_NAME Pointer to sample data array
   * @param FRAME_COUNT Number of frames (for stereo: total_samples / 2)
   * @param NUM_CHANNELS Number of channels (1=mono, 2=stereo)
   */
  Samp(const int16_t * BUFFER_NAME, unsigned long FRAME_COUNT, uint8_t NUM_CHANNELS):
      buffer(BUFFER_NAME), buffer_size((unsigned long) FRAME_COUNT), num_channels(NUM_CHANNELS) {
    setLoopingOff();
    endpos_fractional = (uint64_t)buffer_size << 32;
    startpos_fractional = 0;
  }

  /** Set the sample buffer
   * @param BUFFER_NAME Pointer to sample data array
   * @param FRAME_COUNT Number of frames
   * @param BUFFER_SAMPLE_RATE Sample rate of the buffer data
   * @param NUM_CHANNELS Number of channels (1=mono, 2=stereo)
   */
  inline void setTable(int16_t * BUFFER_NAME, unsigned long FRAME_COUNT, uint32_t BUFFER_SAMPLE_RATE, uint8_t NUM_CHANNELS) {
    buffer = BUFFER_NAME;
    buffer_size = FRAME_COUNT;
    buffer_sample_rate = BUFFER_SAMPLE_RATE;
    num_channels = NUM_CHANNELS;
    startpos_fractional = 0;
    endpos_fractional = (uint64_t)buffer_size << 32;
    phase_increment_fractional = ((uint64_t)buffer_sample_rate << 16) / SAMPLE_RATE;

    Serial.println("frames: " + String(buffer_size) + " chans: " + String(num_channels) +
      " samples: " + String(buffer_size * num_channels) +
      " SR: " + String(buffer_sample_rate) + " Hz, phase_inc: " + String(phase_increment_fractional));
  }

  /** Set start position in frames
   * @param startpos Offset position in frames
   */
  inline void setStart(unsigned int startpos) {
    startpos_fractional = (uint64_t)startpos << 32;
  }

  /** Begin playback from start position */
  inline void start() {
    envPhase = 0;
    envComplete = false;

    // Calculate envelope increment for segment duration
    uint32_t segmentFrames = (uint32_t)((endpos_fractional - startpos_fractional) >> 32);
    if (segmentFrames > 0 && sharedEnvInitialized && sharedEnvSize > 0) {
      uint64_t numerator = (uint64_t)sharedEnvSize * phase_increment_fractional;
      envPhaseIncrement = (uint32_t)((numerator + segmentFrames - 1) / segmentFrames);
      envPhaseIncrement += (envPhaseIncrement + 15) >> 4;
      envelopeOn = true;
    } else {
      envPhaseIncrement = 65536;
      envelopeOn = false;
    }

    phase_fractional = startpos_fractional;

    #if defined(ESP32)
      __sync_synchronize();
    #endif

    playing = true;
  }

  /** Halt playback */
  inline void stop() {
    playing = false;
  }

  /** Set start position and begin playback
   * @param startpos Position in frames
   */
  inline void start(unsigned int startpos) {
    setStart(startpos);
    start();
  }

  /** Set end position in frames
   * @param end Position in frames
   */
  inline void setEnd(unsigned int end) {
    endpos_fractional = (uint64_t)end << 32;
  }

  /** Enable looping */
  inline void setLoopingOn() {
    looping = true;
  }

  /** Disable looping */
  inline void setLoopingOff() {
    looping = false;
  }

  /** Get next mono sample
   * @return Audio sample
   */
  inline int16_t next() {
    if (!playing) return 0;
    if (!buffer || buffer_size == 0) return 0;
    if (num_channels != 1) return 0;

    if (phase_fractional >= endpos_fractional) {
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
        envPhase = 0;
        envComplete = false;
      } else {
        playing = false;
        return 0;
      }
    }

    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size || sampleIndex > 0x7FFFFFFF) return 0;
    int16_t out = buffer[sampleIndex];

    // Apply envelope
    if (envelopeOn && sharedEnvTable && sharedEnvInitialized) {
      if (envComplete) {
        out = 0;
      } else {
        uint32_t envIndex = envPhase >> 16;
        if (envIndex < (uint32_t)sharedEnvSize) {
          out = (int16_t)(((int32_t)out * sharedEnvTable[envIndex]) >> 8);
          uint32_t newEnvPhase = envPhase + envPhaseIncrement;
          if (newEnvPhase >= envPhase) {
            envPhase = newEnvPhase;
          } else {
            envComplete = true;
          }
        } else {
          envComplete = true;
          out = 0;
        }
      }
    }

    incrementPhase();
    return out;
  }

  /** Get next left channel sample (call before nextRight)
   * @return Left channel sample
   */
  inline int16_t nextLeft() {
    if (!playing) return 0;
    if (!buffer || buffer_size == 0) return 0;
    if (num_channels != 2) return 0;

    if (phase_fractional >= endpos_fractional) {
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
        envPhase = 0;
        envComplete = false;
      } else {
        playing = false;
        return 0;
      }
    }

    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size || sampleIndex > 0x3FFFFFFF) return 0;
    int16_t out = buffer[sampleIndex * 2];

    // Apply envelope (phase advanced in nextRight)
    if (envelopeOn && sharedEnvTable && sharedEnvInitialized) {
      if (envComplete) {
        out = 0;
      } else {
        uint32_t envIndex = envPhase >> 16;
        if (envIndex < (uint32_t)sharedEnvSize) {
          out = (int16_t)(((int32_t)out * sharedEnvTable[envIndex]) >> 8);
        } else {
          envComplete = true;
          out = 0;
        }
      }
    }
    return out;
  }

  /** Get next right channel sample (call after nextLeft)
   * @return Right channel sample
   */
  inline int16_t nextRight() {
    if (!playing) return 0;
    if (!buffer || buffer_size == 0) return 0;
    if (num_channels != 2) return 0;

    if (phase_fractional >= endpos_fractional) {
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
        envPhase = 0;
        envComplete = false;
      } else {
        playing = false;
        return 0;
      }
    }

    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size || sampleIndex > 0x3FFFFFFF) return 0;
    int16_t out = buffer[sampleIndex * 2 + 1];

    // Apply envelope
    if (envelopeOn && sharedEnvTable && sharedEnvInitialized) {
      if (envComplete) {
        out = 0;
      } else {
        uint32_t envIndex = envPhase >> 16;
        if (envIndex < (uint32_t)sharedEnvSize) {
          out = (int16_t)(((int32_t)out * sharedEnvTable[envIndex]) >> 8);
          uint32_t newEnvPhase = envPhase + envPhaseIncrement;
          if (newEnvPhase >= envPhase) {
            envPhase = newEnvPhase;
          } else {
            envComplete = true;
          }
        } else {
          envComplete = true;
          out = 0;
        }
      }
    }

    incrementPhase();
    return out;
  }

  /** @return true if sample is currently playing */
  inline boolean isPlaying() {
    return phase_fractional < endpos_fractional;
  }

  /** Set base pitch (pitch at normal playback speed)
   * @param hz Frequency in Hz (default 440)
   */
  inline void setBasePitch(float hz) {
    if (hz > 0) base_pitch = hz;
  }

  /** @return Current base pitch in Hz */
  inline float getBasePitch() {
    return base_pitch;
  }

  /** Set playback pitch relative to base pitch
   * @param frequency Target pitch in Hz
   */
  inline void setFreq(float frequency) {
    phase_increment_fractional = (unsigned long)(((uint64_t)buffer_sample_rate << 16) * frequency / (SAMPLE_RATE * base_pitch));
  }

  /** Get sample at specific buffer index
   * @param index Buffer index
   * @return Sample value, or 0 if out of bounds
   */
  inline int16_t atIndex(unsigned int index) {
    if (!buffer || index >= buffer_size) return 0;
    return buffer[index];
  }

  /** Set playback speed multiplier
   * @param speed Speed multiplier (1.0=normal, 2.0=double, 0.5=half)
   */
  inline void setSpeed(float speed) {
    if (speed <= 0) speed = 1.0f;
    phase_increment_fractional = (unsigned long)(((uint64_t)buffer_sample_rate << 16) * speed / SAMPLE_RATE);
  }

  /** @return Current playback speed multiplier */
  inline float getSpeed() {
    return (float)phase_increment_fractional * SAMPLE_RATE / ((float)buffer_sample_rate * 65536.0f);
  }

  /** @return Current phase position as frame index */
  unsigned long getPhaseIndex() {
    return phase_fractional >> 32;
  }

  /** Disable amplitude envelope */
  void setEnvelopeOff() {
    envelopeOn = false;
  }

  /** Print envelope visualization to Serial
   * @param displayRows Number of rows to display (default 32)
   * @param displayWidth Bar graph width in characters (default 50)
   */
  static void printEnvelope(int displayRows = 32, int displayWidth = 50) {
    if (!sharedEnvTable || !sharedEnvInitialized || sharedEnvSize <= 0) {
      Serial.println("Samp: No shared envelope to display");
      return;
    }

    Serial.println();
    Serial.println("=== Shared Envelope ===");
    Serial.println("Size: " + String(sharedEnvSize) + " samples");
    Serial.println();

    int step = max(1, sharedEnvSize / displayRows);
    int actualRows = (sharedEnvSize + step - 1) / step;

    for (int row = 0; row < actualRows && row < displayRows; row++) {
      int envIndex = row * step;
      if (envIndex >= sharedEnvSize) break;

      uint8_t value = sharedEnvTable[envIndex];
      int barLength = (int)((long)value * displayWidth / 255);
      barLength = max(0, min(displayWidth, barLength));

      if (envIndex < 10) Serial.print("    ");
      else if (envIndex < 100) Serial.print("   ");
      else if (envIndex < 1000) Serial.print("  ");
      else if (envIndex < 10000) Serial.print(" ");
      Serial.print(envIndex);
      Serial.print(" |");

      for (int i = 0; i < barLength; i++) {
        Serial.print("#");
      }
      Serial.print(" ");
      Serial.println(value);
    }
  }

  /** Convert milliseconds to frames
   * @param milliseconds Time in ms
   * @return Frame count
   */
  inline unsigned long msToFrames(float milliseconds) {
    return (unsigned long)((milliseconds / 1000.0f) * SAMPLE_RATE);
  }

  /** Convert frames to milliseconds
   * @param frames Frame count
   * @return Time in ms
   */
  inline unsigned long framesToMs(unsigned long frames) {
    return (frames * 1000.0f) / (float)SAMPLE_RATE;
  }

  /** Convert frames to microseconds
   * @param frames Frame count
   * @return Time in microseconds
   */
  inline unsigned long framesToMicros(unsigned long frames) {
    return (frames * 1000000.0f) / (float)SAMPLE_RATE;
  }

  /** Derive BPM from frame count for power-of-2 beat subdivision
   * @param frames Total sample frames
   * @param beatsOut Pointer to receive beat count (optional)
   * @param sampleRate Sample rate (default SAMPLE_RATE)
   * @param targetBPM Target BPM (default 120)
   * @param minBPM Minimum acceptable BPM (default 60)
   * @param maxBPM Maximum acceptable BPM (default 180)
   * @return BPM closest to target that evenly divides frames
   */
  float deriveBPM(unsigned long frames, unsigned long* beatsOut = nullptr,
                  int sampleRate = SAMPLE_RATE, float targetBPM = 120.0f,
                  float minBPM = 60.0f, float maxBPM = 180.0f) {
    float closestBPM = targetBPM;
    unsigned long closestBeats = 1;
    float minDiff = maxBPM;

    for (int power = 0; power <= 16; power++) {
      unsigned long beats = 1UL << power;
      float bpm = (float)beats * (float)sampleRate * 60.0f / (float)frames;

      if (bpm < minBPM || bpm > maxBPM) continue;

      float diff = fabsf(bpm - targetBPM);
      if (diff < minDiff) {
        minDiff = diff;
        closestBPM = bpm;
        closestBeats = beats;
      }
    }

    if (beatsOut != nullptr) {
      *beatsOut = closestBeats;
    }

    return closestBPM;
  }

private:
  /** Increment playback phase */
  inline void incrementPhase() {
    phase_fractional += (uint64_t)phase_increment_fractional << 16;
  }

  volatile uint64_t phase_fractional = 0;
  volatile uint32_t phase_increment_fractional = 65536;
  const int16_t * buffer = nullptr;
  bool playing = false;
  bool looping = false;
  uint64_t startpos_fractional = 0;
  uint64_t endpos_fractional = 0;
  uint32_t buffer_size = 0;
  uint8_t num_channels = 0;
  uint32_t buffer_sample_rate = SAMPLE_RATE;
  float base_pitch = 440.0f;

  volatile bool envelopeOn = false;
  volatile uint32_t envPhase = 0;
  volatile uint32_t envPhaseIncrement = 0;
  volatile bool envComplete = false;
};

// Static member definitions
uint8_t* Samp::sharedEnvTable = nullptr;
int Samp::sharedEnvSize = 0;
bool Samp::sharedEnvInitialized = false;

#endif /* SAMP_H_ */