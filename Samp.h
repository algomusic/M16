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

// Thread-safety for 64-bit phase on 32-bit ESP32
// Uses lightweight spinlock (atomic_flag) that doesn't disable interrupts
// to avoid I2S DMA underruns while protecting against torn reads/writes
#if IS_ESP32() || IS_RP2040()
  #include <atomic>
  #define SAMP_LOCK_DECLARE() std::atomic_flag _phaseLock = ATOMIC_FLAG_INIT;
  #define SAMP_LOCK() while (_phaseLock.test_and_set(std::memory_order_acquire)) {}
  #define SAMP_UNLOCK() _phaseLock.clear(std::memory_order_release)
#else
  #define SAMP_LOCK_DECLARE()
  #define SAMP_LOCK()
  #define SAMP_UNLOCK()
#endif

class Samp {

public:
  // Shared envelope table (static - single copy for all instances)
  static uint8_t* sharedEnvTable;
  static int sharedEnvSize;
  static bool sharedEnvInitialized;

  /** Initialize or update the shared envelope table
   * Allocates once on first call, then refills in-place on subsequent calls.
   * Byte writes are atomic so audio callback sees consistent values.
   * @param size Number of samples in envelope table (default 2048)
   * @param curveAmount Attack/release curve proportion 0.0-1.0 (default 0.8)
   * @param type Envelope type: 0=Gaussian, 1=Cosine, 2=Linear (default 0)
   */
  static void initSharedEnvelope(int size = 2048, float curveAmount = 0.8f, int type = 0) {
    // Allocate only if needed (first call or size change)
    if (sharedEnvTable == nullptr || sharedEnvSize != size) {
      if (sharedEnvTable) {
        free(sharedEnvTable);
      }
      // Always use regular RAM for envelope table (not PSRAM)
      // PSRAM access latency causes timing issues in audio callbacks
      sharedEnvTable = (uint8_t*)calloc(size, sizeof(uint8_t));

      if (!sharedEnvTable) {
        Serial.println("ERROR: Failed to allocate shared envelope");
        sharedEnvInitialized = false;
        sharedEnvSize = 0;
        return;
      }
      sharedEnvSize = size;
    }

    // Refill envelope in-place (byte writes are atomic)
    curveAmount = max(0.0f, min(1.0f, curveAmount));
    int attackSamples = (int)(size * curveAmount * 0.5f);
    int releaseSamples = attackSamples;
    int sustainSamples = size - attackSamples - releaseSamples;
    int index = 0;

    // Yield periodically during envelope generation to prevent watchdog timeout
    // expf() is slow, and we may have ~1000+ iterations
    int yieldCounter = 0;
    const int YIELD_INTERVAL = 16;  // Yield every 16 samples (very aggressive for ESP32 watchdog)

    // Feed watchdog before starting
    #if IS_ESP32()
    vTaskDelay(1);  // Ensure IDLE task gets a chance to run
    #else
    delay(1);
    #endif

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
        #if IS_ESP32()
        if (++yieldCounter >= YIELD_INTERVAL) { vTaskDelay(1); yieldCounter = 0; }
        #else
        if (++yieldCounter >= YIELD_INTERVAL) { delay(1); yieldCounter = 0; }
        #endif
      }
      for (int i = 0; i < sustainSamples && index < size; i++) {
        sharedEnvTable[index++] = 255;
      }
      for (int i = 0; i < releaseSamples && index < size; i++) {
        float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
        float x = t * invSigma;
        sharedEnvTable[index++] = (uint8_t)((expf(-(x * x)) - g0) * scale * 255.0f);
        #if IS_ESP32()
        if (++yieldCounter >= YIELD_INTERVAL) { vTaskDelay(1); yieldCounter = 0; }
        #else
        if (++yieldCounter >= YIELD_INTERVAL) { delay(1); yieldCounter = 0; }
        #endif
      }
    } else if (type == 1) {
      // Cosine envelope
      for (int i = 0; i < attackSamples && index < size; i++) {
        float t = (attackSamples > 1) ? (float)i / (float)(attackSamples - 1) : 0.0f;
        sharedEnvTable[index++] = (uint8_t)((1.0f - cosf(M_PI * t)) * 0.5f * 255.0f);
        #if IS_ESP32()
        if (++yieldCounter >= YIELD_INTERVAL) { vTaskDelay(1); yieldCounter = 0; }
        #else
        if (++yieldCounter >= YIELD_INTERVAL) { delay(1); yieldCounter = 0; }
        #endif
      }
      for (int i = 0; i < sustainSamples && index < size; i++) {
        sharedEnvTable[index++] = 255;
      }
      for (int i = 0; i < releaseSamples && index < size; i++) {
        float t = (releaseSamples > 1) ? (float)i / (float)(releaseSamples - 1) : 1.0f;
        sharedEnvTable[index++] = (uint8_t)((1.0f + cosf(M_PI * t)) * 0.5f * 255.0f);
        #if IS_ESP32()
        if (++yieldCounter >= YIELD_INTERVAL) { vTaskDelay(1); yieldCounter = 0; }
        #else
        if (++yieldCounter >= YIELD_INTERVAL) { delay(1); yieldCounter = 0; }
        #endif
      }
    } else {
      // Linear envelope (no yield needed - no expensive math)
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

    // Final yield to ensure IDLE task runs after envelope generation
    #if IS_ESP32()
    vTaskDelay(1);
    #else
    delay(1);
    #endif

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
    phaseInc64 = (uint64_t)phase_increment_fractional << 16;  // Pre-compute for hot path
  }

  /** Set start position in frames
   * @param startpos Offset position in frames
   */
  inline void setStart(unsigned long startpos) {
    if (zeroCrossing && buffer) {
      startpos = findNearestZeroCrossing(startpos);
    }
    SAMP_LOCK();
    startpos_fractional = (uint64_t)startpos << 32;
    SAMP_UNLOCK();
  }

  /** Set envelope phase offset for out-of-phase multi-voice playback
   * @param offset Normalized offset 0.0-1.0 (0.5 = start at envelope midpoint)
   */
  inline void setEnvPhaseOffset(float offset) {
    offset = max(0.0f, min(1.0f, offset));
    envPhaseOffset = (uint32_t)(offset * 0xFFFFFFFF);
  }

  /** @return Current envelope phase offset (0.0-1.0) */
  inline float getEnvPhaseOffset() {
    return (float)envPhaseOffset / (float)0xFFFFFFFF;
  }

  /** Begin playback from start position (or end if reverse) */
  inline void start() {
    SAMP_LOCK();
    envPhase = envPhaseOffset;  // Apply phase offset for multi-voice envelope staggering
    envComplete = false;  // Never start as complete - let the envelope play through
    envHasWrapped = false;  // Reset wrap tracking
    envStartPhase = envPhaseOffset;  // Remember start position to detect full cycle completion

    // Calculate envelope increment for segment duration
    uint32_t segmentFrames = (uint32_t)((endpos_fractional - startpos_fractional) >> 32);
    if (segmentFrames > 0 && sharedEnvInitialized && sharedEnvSize > 0) {
      uint64_t numerator = (uint64_t)sharedEnvSize * phase_increment_fractional;
      envPhaseIncrement = (uint32_t)((numerator + segmentFrames - 1) / segmentFrames);
      envelopeOn = true;
    } else {
      envPhaseIncrement = 65536;
      envelopeOn = false;
    }

    // Start at end position for reverse playback, start position for forward
    if (reverse) {
      // Position just before end (end - 1 frame) so first sample is valid
      phase_fractional = endpos_fractional - ((uint64_t)1 << 32);
    } else {
      phase_fractional = startpos_fractional;
    }

    playing = true;
    SAMP_UNLOCK();
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
  inline void setEnd(unsigned long end) {
    if (zeroCrossing && buffer) {
      end = findNearestZeroCrossing(end);
    }
    SAMP_LOCK();
    endpos_fractional = (uint64_t)end << 32;
    SAMP_UNLOCK();
  }

  /** Get actual start position (after zero-crossing adjustment)
   * @return Start position in frames
   */
  inline unsigned long getStart() {
    return startpos_fractional >> 32;
  }

  /** Get actual end position (after zero-crossing adjustment)
   * @return End position in frames
   */
  inline unsigned long getEnd() {
    return endpos_fractional >> 32;
  }

  /** Enable looping */
  inline void setLoopingOn() {
    looping = true;
  }

  /** Disable looping */
  inline void setLoopingOff() {
    looping = false;
  }

  /** Enable reverse playback (plays from end to start)
   * @param rev true for reverse, false for forward (default)
   */
  inline void setReverse(bool rev) {
    reverse = rev;
  }

  /** @return true if reverse playback is enabled */
  inline bool getReverse() {
    return reverse;
  }

  /** Get next mono sample
   * @return Audio sample
   */
  inline int16_t next() {
    if (!playing) return 0;
    if (!buffer || buffer_size == 0) return 0;
    if (num_channels != 1) return 0;

    SAMP_LOCK();

    // Boundary check depends on playback direction
    if (reverse) {
      if (phase_fractional <= startpos_fractional) {
        if (looping) {
          phase_fractional = endpos_fractional - ((uint64_t)1 << 32);
          envPhase = 0;
          envComplete = false;
        } else {
          playing = false;
          SAMP_UNLOCK();
          return 0;
        }
      }
    } else {
      if (phase_fractional >= endpos_fractional) {
        if (looping) {
          phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
          envPhase = 0;
          envComplete = false;
        } else {
          playing = false;
          SAMP_UNLOCK();
          return 0;
        }
      }
    }

    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size) {
      SAMP_UNLOCK();
      return 0;
    }

    int16_t out;
    if (interpolate && sampleIndex < buffer_size - 1) {
      // Linear interpolation using 15-bit fractional phase
      // Using 15-bit keeps diff*frac within int32_t range (max 65534*32767 < INT32_MAX)
      int32_t frac = (int32_t)((phase_fractional >> 17) & 0x7FFF);  // 15-bit (0-32767)
      int32_t s0 = buffer[sampleIndex];
      int32_t s1 = buffer[sampleIndex + 1];
      // Interpolate and clamp to prevent overflow at extreme sample transitions
      int32_t interp = s0 + (((s1 - s0) * frac) >> 15);
      out = (int16_t)max((int32_t)MIN_16, min((int32_t)MAX_16, interp));
    } else {
      out = buffer[sampleIndex];
    }

    // Apply envelope
    if (envelopeOn && sharedEnvTable && sharedEnvInitialized) {
      if (envComplete) {
        out = 0;
      } else {
        // Wrap envelope index to table size (supports phase offset)
        uint32_t envIndex = (envPhase >> 16) % sharedEnvSize;
        out = (int16_t)(((int32_t)out * sharedEnvTable[envIndex]) >> 8);
        uint32_t newEnvPhase = envPhase + envPhaseIncrement;
        if (newEnvPhase >= envPhase) {
          envPhase = newEnvPhase;
          // Check for full cycle completion after wrap
          if (envHasWrapped && envPhase >= envStartPhase) {
            envComplete = true;
          }
        } else {
          // Phase wrapped around - mark wrapped but don't complete yet
          envPhase = newEnvPhase;
          envHasWrapped = true;
        }
      }
    }

    // Apply edge fade (fade in at start, fade out at end)
    if (edgeFadeEnabled) {
      float fadeGain = getEdgeFadeGain(sampleIndex);
      if (fadeGain < 1.0f) {
        out = (int16_t)(out * fadeGain);
      }
    }

    if (reverse) {
      decrementPhase();
    } else {
      incrementPhase();
    }

    SAMP_UNLOCK();
    return out;
  }

  /** Get next stereo sample pair (thread-safe, recommended for dual-core ESP32)
   * @param outLeft Reference to receive left channel sample
   * @param outRight Reference to receive right channel sample
   * @return true if still playing, false if grain ended (avoids separate isPlaying() call)
   */
  inline bool nextStereo(int16_t& outLeft, int16_t& outRight) {
    // Quick check without lock - playing is volatile so this is safe
    if (!playing || !buffer || buffer_size == 0 || num_channels != 2) {
      outLeft = 0;
      outRight = 0;
      return false;
    }

    // === LOCKED SECTION: Read phase, check bounds, advance phase ===
    // Keep lock time minimal - only for 64-bit phase operations
    SAMP_LOCK();

    // Cache 64-bit values to local 32-bit where possible
    uint32_t sampleIndex = (uint32_t)(phase_fractional >> 32);
    uint32_t phaseLow = (uint32_t)phase_fractional;  // Lower 32 bits for fraction
    uint32_t startIdx = (uint32_t)(startpos_fractional >> 32);
    uint32_t endIdx = (uint32_t)(endpos_fractional >> 32);

    // Boundary check using 32-bit integer comparison (faster than 64-bit)
    bool atBoundary = reverse ? (sampleIndex <= startIdx) : (sampleIndex >= endIdx);

    if (atBoundary) {
      if (looping) {
        phase_fractional = reverse ?
          (endpos_fractional - ((uint64_t)1 << 32)) :
          (startpos_fractional + (phase_fractional - endpos_fractional));
        sampleIndex = (uint32_t)(phase_fractional >> 32);
        phaseLow = (uint32_t)phase_fractional;
        envPhase = 0;
        envComplete = false;
      } else {
        playing = false;
        SAMP_UNLOCK();
        outLeft = 0;
        outRight = 0;
        return false;  // Grain ended
      }
    }

    // Bounds check
    if (sampleIndex >= buffer_size) {
      SAMP_UNLOCK();
      outLeft = 0;
      outRight = 0;
      return false;
    }

    // Advance phase now (while we have the lock)
    // Pre-computed 64-bit increment avoids shift in hot path
    if (reverse) {
      phase_fractional -= phaseInc64;
    } else {
      phase_fractional += phaseInc64;
    }

    // Cache envelope state
    uint32_t localEnvPhase = envPhase;
    uint32_t localEnvInc = envPhaseIncrement;
    bool localEnvOn = envelopeOn && sharedEnvInitialized && !envComplete;

    // Update envelope phase
    if (localEnvOn) {
      uint32_t newEnvPhase = envPhase + envPhaseIncrement;
      envPhase = newEnvPhase;
      if (newEnvPhase < localEnvPhase) {
        // Phase wrapped around - mark wrapped but don't complete yet
        envHasWrapped = true;
      } else if ((newEnvPhase >> 16) >= (uint32_t)sharedEnvSize) {
        envComplete = true;
      } else if (envHasWrapped && newEnvPhase >= envStartPhase) {
        // Full cycle completed (wrapped and returned to start)
        envComplete = true;
      }
    }

    SAMP_UNLOCK();
    // === END LOCKED SECTION ===

    // All calculations below use local variables - no lock needed
    uint32_t bufIdx = sampleIndex << 1;  // sampleIndex * 2, bit shift is faster

    if (interpolate && sampleIndex < buffer_size - 1) {
      // Extract 15-bit fraction from lower 32 bits only (avoids 64-bit shift)
      int32_t frac = (int32_t)((phaseLow >> 17) & 0x7FFF);

      // Cache buffer pointer for faster access
      const int16_t* buf = buffer + bufIdx;
      int32_t s0L = buf[0];
      int32_t s0R = buf[1];
      int32_t s1L = buf[2];
      int32_t s1R = buf[3];

      // Interpolate - result is in valid int16_t range for typical audio
      int32_t interpL = s0L + (((s1L - s0L) * frac) >> 15);
      int32_t interpR = s0R + (((s1R - s0R) * frac) >> 15);

      // Fast clamp using ternary (often compiles to conditional move)
      outLeft = (interpL > 32767) ? 32767 : ((interpL < -32767) ? -32767 : (int16_t)interpL);
      outRight = (interpR > 32767) ? 32767 : ((interpR < -32767) ? -32767 : (int16_t)interpR);
    } else {
      outLeft = buffer[bufIdx];
      outRight = buffer[bufIdx + 1];
    }

    // Apply envelope (using cached local values)
    if (localEnvOn && sharedEnvTable) {
      // Wrap envelope index to table size (supports phase offset)
      uint32_t envIndex = (localEnvPhase >> 16) % sharedEnvSize;
      uint32_t envVal = sharedEnvTable[envIndex];  // 0-255
      outLeft = (int16_t)(((int32_t)outLeft * envVal) >> 8);
      outRight = (int16_t)(((int32_t)outRight * envVal) >> 8);
    }

    // Apply edge fade (fade in at start, fade out at end) - only if enabled
    if (edgeFadeEnabled) {
      float fadeGain = getEdgeFadeGain(sampleIndex);
      if (fadeGain < 1.0f) {
        outLeft = (int16_t)(outLeft * fadeGain);
        outRight = (int16_t)(outRight * fadeGain);
      }
    }

    return true;  // Still playing
  }

  /** Get next left channel sample (call before nextRight)
   * WARNING: Not thread-safe as a pair on dual-core ESP32. Use nextStereo() instead.
   * @return Left channel sample
   */
  inline int16_t nextLeft() {
    if (!playing) return 0;
    if (!buffer || buffer_size == 0) return 0;
    if (num_channels != 2) return 0;

    SAMP_LOCK();

    // Boundary check depends on playback direction
    if (reverse) {
      if (phase_fractional <= startpos_fractional) {
        if (looping) {
          phase_fractional = endpos_fractional - ((uint64_t)1 << 32);
          envPhase = 0;
          envComplete = false;
        } else {
          playing = false;
          SAMP_UNLOCK();
          return 0;
        }
      }
    } else {
      if (phase_fractional >= endpos_fractional) {
        if (looping) {
          phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
          envPhase = 0;
          envComplete = false;
        } else {
          playing = false;
          SAMP_UNLOCK();
          return 0;
        }
      }
    }

    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size) {
      SAMP_UNLOCK();
      return 0;
    }
    int16_t out = buffer[sampleIndex * 2];

    // Apply envelope (phase advanced in nextRight)
    if (envelopeOn && sharedEnvTable && sharedEnvInitialized) {
      if (envComplete) {
        out = 0;
      } else {
        // Wrap envelope index to table size (supports phase offset)
        uint32_t envIndex = (envPhase >> 16) % sharedEnvSize;
        out = (int16_t)(((int32_t)out * sharedEnvTable[envIndex]) >> 8);
      }
    }

    SAMP_UNLOCK();
    return out;
  }

  /** Get next right channel sample (call after nextLeft)
   * WARNING: Not thread-safe as a pair on dual-core ESP32. Use nextStereo() instead.
   * @return Right channel sample
   */
  inline int16_t nextRight() {
    if (!playing) return 0;
    if (!buffer || buffer_size == 0) return 0;
    if (num_channels != 2) return 0;

    SAMP_LOCK();

    // Boundary check depends on playback direction
    if (reverse) {
      if (phase_fractional <= startpos_fractional) {
        if (looping) {
          phase_fractional = endpos_fractional - ((uint64_t)1 << 32);
          envPhase = 0;
          envComplete = false;
        } else {
          playing = false;
          SAMP_UNLOCK();
          return 0;
        }
      }
    } else {
      if (phase_fractional >= endpos_fractional) {
        if (looping) {
          phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
          envPhase = 0;
          envComplete = false;
        } else {
          playing = false;
          SAMP_UNLOCK();
          return 0;
        }
      }
    }

    uint32_t sampleIndex = phase_fractional >> 32;
    if (sampleIndex >= buffer_size) {
      SAMP_UNLOCK();
      return 0;
    }
    int16_t out = buffer[sampleIndex * 2 + 1];

    // Apply envelope
    if (envelopeOn && sharedEnvTable && sharedEnvInitialized) {
      if (envComplete) {
        out = 0;
      } else {
        // Wrap envelope index to table size (supports phase offset)
        uint32_t envIndex = (envPhase >> 16) % sharedEnvSize;
        out = (int16_t)(((int32_t)out * sharedEnvTable[envIndex]) >> 8);
        uint32_t newEnvPhase = envPhase + envPhaseIncrement;
        if (newEnvPhase >= envPhase) {
          envPhase = newEnvPhase;
          // Check for full cycle completion after wrap
          if (envHasWrapped && envPhase >= envStartPhase) {
            envComplete = true;
          }
        } else {
          // Phase wrapped around - mark wrapped but don't complete yet
          envPhase = newEnvPhase;
          envHasWrapped = true;
        }
      }
    }

    if (reverse) {
      decrementPhase();
    } else {
      incrementPhase();
    }

    SAMP_UNLOCK();
    return out;
  }

  /** @return true if sample is currently playing */
  inline boolean isPlaying() {
    if (!playing) return false;
    SAMP_LOCK();
    bool result;
    if (reverse) {
      result = (phase_fractional > startpos_fractional);
    } else {
      result = (phase_fractional < endpos_fractional);
    }
    SAMP_UNLOCK();
    return result;
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
    phaseInc64 = (uint64_t)phase_increment_fractional << 16;  // Pre-compute for hot path
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
    phaseInc64 = (uint64_t)phase_increment_fractional << 16;  // Pre-compute for hot path
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

  /** Enable or disable zero-crossing detection for start/end positions
   * @param enable true to snap to zero crossings (default), false for exact positions
   */
  inline void setZeroCrossing(bool enable) {
    zeroCrossing = enable;
  }

  /** @return Current zero-crossing setting */
  inline bool getZeroCrossing() {
    return zeroCrossing;
  }

  /** Enable or disable linear interpolation for smoother playback
   * Interpolation reduces aliasing/quantization noise at non-unity playback speeds.
   * Slightly increases CPU usage. Recommended for granular synthesis.
   * @param enable true for linear interpolation, false for nearest-neighbor (default)
   */
  inline void setInterpolation(bool enable) {
    interpolate = enable;
  }

  /** @return Current interpolation setting */
  inline bool getInterpolation() {
    return interpolate;
  }

  /** Enable or disable edge fades (fade in at start, fade out at end)
   * Edge fades smooth transitions at segment boundaries to eliminate clicks.
   * Applied dynamically during playback based on position relative to start/end.
   * @param enable true to enable edge fades, false to disable (default)
   */
  inline void setEdgeFade(bool enable) {
    edgeFadeEnabled = enable;
  }

  /** @return Current edge fade setting */
  inline bool getEdgeFade() {
    return edgeFadeEnabled;
  }

  /** Set edge fade duration in milliseconds
   * @param fadeMs Fade duration (default 10ms). Automatically enables edge fades.
   */
  inline void setEdgeFadeMs(float fadeMs) {
    edgeFadeSamples = (uint32_t)(fadeMs * SAMPLE_RATE / 1000.0f);
    if (edgeFadeSamples < 2) edgeFadeSamples = 2;
    edgeFadeEnabled = true;
  }

  /** @return Current edge fade duration in milliseconds */
  inline float getEdgeFadeMs() {
    return (float)edgeFadeSamples * 1000.0f / SAMPLE_RATE;
  }

  /** Find the next zero crossing forward from a given position
   * A zero crossing is where adjacent samples change from negative to non-negative.
   * Forward-only search ensures consistent grain boundaries for granular synthesis.
   * @param pos Position in frames to search from
   * @param maxSearch Maximum frames to search forward (default 256)
   * @return Frame index of zero crossing, or original pos if none found within range
   */
  unsigned long findNearestZeroCrossing(unsigned long pos, unsigned long maxSearch = 256) {
    if (!buffer || buffer_size == 0) return pos;
    if (pos >= buffer_size - 1) return pos;  // Need at least 2 samples to check crossing

    // Calculate safe search limit: need pos+i+1 < buffer_size, so i < buffer_size-pos-1
    unsigned long maxIterations = buffer_size - pos - 1;
    unsigned long searchLimit = min(maxSearch, maxIterations);

    // Search forward only for consistent grain boundaries
    for (unsigned long i = 0; i < searchLimit; i++) {
      int16_t curr, next;
      if (num_channels == 2) {
        // Use left channel for stereo
        curr = buffer[(pos + i) * 2];
        next = buffer[(pos + i + 1) * 2];
      } else {
        curr = buffer[pos + i];
        next = buffer[pos + i + 1];
      }
      // Found silence (2 consecutive zeros) - good boundary point
      if (curr == 0 && next == 0) {
        return pos + i;
      }
      // Zero crossing: negative to non-negative (ensures consistent wave direction)
      if (curr < 0 && next >= 0) {
        return pos + i + 1;  // Return the non-negative sample position
      }
    }

    return pos;  // No crossing found, return original position
  }

private:
  // Per-instance spinlock for thread-safe 64-bit phase access on ESP32
  SAMP_LOCK_DECLARE();

  /** Increment playback phase (forward) */
  inline void incrementPhase() {
    phase_fractional += (uint64_t)phase_increment_fractional << 16;
  }

  /** Decrement playback phase (reverse) */
  inline void decrementPhase() {
    uint64_t decrement = (uint64_t)phase_increment_fractional << 16;
    if (phase_fractional >= decrement) {
      phase_fractional -= decrement;
    } else {
      phase_fractional = 0;  // Prevent underflow
    }
  }

  volatile uint64_t phase_fractional = 0;
  volatile uint32_t phase_increment_fractional = 65536;
  uint64_t phaseInc64 = (uint64_t)65536 << 16;  // Pre-computed 64-bit increment for hot path
  const int16_t * buffer = nullptr;
  volatile bool playing = false;
  bool looping = false;
  volatile bool reverse = false;  // Reverse playback direction
  volatile uint64_t startpos_fractional = 0;
  volatile uint64_t endpos_fractional = 0;
  uint32_t buffer_size = 0;
  uint8_t num_channels = 0;
  uint32_t buffer_sample_rate = SAMPLE_RATE;
  float base_pitch = 440.0f;

  volatile bool envelopeOn = false;
  volatile uint32_t envPhase = 0;
  volatile uint32_t envPhaseIncrement = 0;
  volatile bool envComplete = false;
  volatile bool envHasWrapped = false;  // Track if envelope has wrapped around
  uint32_t envPhaseOffset = 0;  // Starting envelope phase offset (0 = normal, 0x80000000 = 50%)
  uint32_t envStartPhase = 0;  // Remember start phase for cycle completion detection
  bool zeroCrossing = true;  // Auto-adjust start/end to nearest zero crossing
  bool interpolate = false;  // Linear interpolation for smoother playback at non-unity speeds

  // Edge fade settings (fade in at start, fade out at end)
  bool edgeFadeEnabled = false;
  uint32_t edgeFadeSamples = 441;  // Default 10ms at 44100Hz

  /** Apply edge fade gain based on position relative to start/end
   * @param pos Current position in samples (integer part of phase)
   * @return Gain multiplier 0.0-1.0
   */
  inline float getEdgeFadeGain(uint32_t pos) {
    if (!edgeFadeEnabled || edgeFadeSamples == 0) return 1.0f;

    uint32_t startPos = (uint32_t)(startpos_fractional >> 32);
    uint32_t endPos = (uint32_t)(endpos_fractional >> 32);
    uint32_t segmentLength = endPos - startPos;

    // Ensure fade doesn't exceed half the segment
    uint32_t maxFade = segmentLength / 2;
    uint32_t fadeSamples = (edgeFadeSamples < maxFade) ? edgeFadeSamples : maxFade;
    if (fadeSamples < 2) return 1.0f;

    // Distance from start
    uint32_t fromStart = pos - startPos;
    if (fromStart < fadeSamples) {
      // Fade in: cosine curve 0 to 1
      return 0.5f * (1.0f - cosf(M_PI * fromStart / fadeSamples));
    }

    // Distance from end
    uint32_t fromEnd = endPos - pos;
    if (fromEnd < fadeSamples) {
      // Fade out: cosine curve 1 to 0
      return 0.5f * (1.0f - cosf(M_PI * fromEnd / fadeSamples));
    }

    return 1.0f;  // In the middle, no fade
  }
};

// Static member definitions
uint8_t* Samp::sharedEnvTable = nullptr;
int Samp::sharedEnvSize = 0;
bool Samp::sharedEnvInitialized = false;

#endif /* SAMP_H_ */
