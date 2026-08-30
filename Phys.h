/*
 * Phys.h
 *
 * Physical modelling synthesis: Karplus-Strong pluck and digital waveguide
 *
 * by Andrew R. Brown 2026
 *
 * This file is part of the M16 audio library
 * Inspired by the Mozzi audio library by Tim Barrass 2012
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef PHYS_H_
#define PHYS_H_

#include "All.h"
#include "EMA.h"

#if IS_ESP32() || IS_RP2040()
#include <atomic>
#endif

class Phys {

  public:
    #if IS_ESP32() || IS_RP2040()
    std::atomic<bool> _physLock{false};
    #endif

    /** Constructor */
    Phys() {
      pluckAllpass.setFeedbackLevel(0);
      pluckAllpass.setDelayTime(0);
      wgAllpass.setFeedbackLevel(0);
      wgAllpass.setDelayTime(0);
      pluckDampFilter.setCutoff(1.0f);
      wgDampFilterR.setCutoff(1.0f);
      wgDampFilterL.setCutoff(1.0f);
    }

    /** Set the allpass filter delay time for the feedback loop.
    *  @stiffness Normalised value 0.0-1.0 as a proportion of max delay.
    *  0.0 = no delay (no effect), 1.0 = maximum delay.
    *  Applies to both pluck and waveguide feedback paths.
    */
    void setStiffnessTime(float stiffness) {
      float t = stiffness < 0.0f ? 0.0f : (stiffness > 1.0f ? 1.0f : stiffness);
      float maxMs = pluckAllpass.getMaxTime();
      pluckAllpass.setDelayTime(t * maxMs);
      wgAllpass.setDelayTime(t * maxMs);
    }

    /** Set the allpass filter feedback level for the feedback loop.
    *  @level Feedback level 0.0-1.0. Default 0 (no effect).
    *  Applies to both pluck and waveguide feedback paths.
    */
    void setStiffnessFeedback(float level) {
      pluckAllpass.setFeedbackLevel(level);
      wgAllpass.setFeedbackLevel(level);
    }

    /** Set the wet/dry mix for the allpass stiffness filter.
    *  @mix Normalised value 0.0-1.0. 0.0 = fully dry, 1.0 = fully wet.
    *  Default 1.0 (fully wet).
    */
    void setStiffnessMix(float mix) {
      pluckStiffMix = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
      wgStiffMix = pluckStiffMix;
    }

    /** Set the pluck excitation position in the delay line.
    *  @pos Normalised position 0.0-1.0 relative to delay length.
    *  0.0 = at read head (far end), 1.0 = at write head (current default behaviour).
    *  Recomputed automatically when pitch changes.
    */
    void setPluckPosition(float pos) {
      pluckPosition = pos < 0.0f ? 0.0f : (pos > 1.0f ? 1.0f : pos);
    }

    /** Set the waveguide excitation position in the delay line.
    *  @pos Normalised position 0.0-1.0 relative to delay length.
    *  0.0 = at read head (far end), 1.0 = at write head (current default behaviour).
    *  Recomputed automatically when pitch changes.
    */
    void setWgPosition(float pos) {
      wgPosition = pos < 0.0f ? 0.0f : (pos > 1.0f ? 1.0f : pos);
    }

    /** Set the pluck damping filter cutoff.
    *  @cutoff Normalised value 0.0-1.0. 0.0 = bypass (no filtering).
    *  Higher values apply more low-pass filtering in the feedback loop,
    *  causing high frequencies to decay faster (realistic string damping).
    */
    void setPluckDampCutoff(float cutoff) {
      pluckDampCutoff = cutoff < 0.0f ? 0.0f : (cutoff > 1.0f ? 1.0f : cutoff);
      pluckDampFilter.setCutoff(pluckDampCutoff);
    }

    float getPluckDampCutoff() {
      return pluckDampFilter.getCutoff();
    }

    /** Set the waveguide damping filter cutoff.
    *  @cutoff Normalised value 0.0-1.0. 0.0 = bypass (no filtering).
    *  Higher values apply more low-pass filtering in the feedback loop,
    *  causing high frequencies to decay faster (realistic string damping).
    */
    void setWgDampCutoff(float cutoff) {
      wgDampCutoff = cutoff < 0.0f ? 0.0f : (cutoff > 1.0f ? 1.0f : cutoff);
      wgDampFilterR.setCutoff(wgDampCutoff);
      wgDampFilterL.setCutoff(wgDampCutoff);
    }

    /** Set the frequency used by the two-argument pluck() overload.
    *  Safe to call from control code while audio is running.
    *  @param frequency Fundamental frequency in Hz. Values must be positive.
    */
    inline void setPluckFreq(float frequency) {
      if (frequency <= 0.0f) return;
      #if IS_ESP32() || IS_RP2040()
      storedPluckFreq.store(frequency, std::memory_order_relaxed);
      #else
      storedPluckFreq = frequency;
      #endif
    }

    /** Return the frequency used by the two-argument pluck() overload. */
    inline float getPluckFreq() const {
      #if IS_ESP32() || IS_RP2040()
      return storedPluckFreq.load(std::memory_order_relaxed);
      #else
      return storedPluckFreq;
      #endif
    }

    /** Request a clean Karplus-Strong delay state.
    *  Safe to call from control code while audio is running. The audio owner
    *  clears 64 entries per call and returns silence until the reset completes,
    *  avoiding both concurrent access and a one-sample buffer-clear spike.
    */
    inline void resetPluck() {
      #if IS_ESP32() || IS_RP2040()
      pluckResetRequested.store(true, std::memory_order_release);
      #else
      pluckResetRequested = true;
      #endif
    }

    /** Set the feedback deadband used to terminate inaudible integer tails.
    *  Values at or below the threshold are written to the delay as zero.
    *  Default 4; set to 0 to disable the deadband.
    */
    inline void setPluckSilenceThreshold(int threshold) {
      if (threshold < 0) threshold = 0;
      else if (threshold > MAX_16) threshold = MAX_16;
      #if IS_ESP32() || IS_RP2040()
      pluckSilenceThreshold.store((int16_t)threshold, std::memory_order_relaxed);
      #else
      pluckSilenceThreshold = (int16_t)threshold;
      #endif
    }

    /** Return the current pluck feedback silence threshold. */
    inline int getPluckSilenceThreshold() const {
      #if IS_ESP32() || IS_RP2040()
      return pluckSilenceThreshold.load(std::memory_order_relaxed);
      #else
      return pluckSilenceThreshold;
      #endif
    }

    /** Karplus-Strong synthesis using the frequency set by setPluckFreq().
    *  @param audioIn Excitation signal
    *  @param depth Feedback level 0.0-1.0
    */
    inline int16_t pluck(int16_t audioIn, float depth) {
      return pluck(audioIn, getPluckFreq(), depth);
    }

    /** Karplus-Strong plucked string synthesis
    *  @audioIn Excitation signal (noise burst, impulse, oscillator)
    *  @pluckFreq Fundamental frequency in Hz. Determines delay line length.
    *  @depth Feedback level 0.0-1.0. Higher = longer decay.
    *  Use large depth values (0.96-0.995) for pluck string effect on impulses.
    */
    inline
    int16_t pluck(int16_t audioIn, float pluckFreq, float depth) {
      if (!pluckBufferEstablished) initPluckBuffer();
    M16_ATOMIC_GUARD(_physLock, {
      #if IS_ESP32() || IS_RP2040()
      if (pluckResetRequested.exchange(false, std::memory_order_acq_rel)) {
        beginPluckReset();
      }
      #else
      if (pluckResetRequested) {
        pluckResetRequested = false;
        beginPluckReset();
      }
      #endif
      if (pluckResetActive) clearPluckResetChunk();
      if (!pluckResetActive) {
      // Pitch and decay normally remain fixed for many thousands of samples.
      // Cache their expensive float conversions instead of repeating a divide
      // and float feedback multiply at audio rate.
      if (pluckFreq != cachedPluckFreq) {
        cachedPluckFreq = pluckFreq;
        cachedPluckDelayLen = (float)SAMPLE_RATE / pluckFreq;
        if (cachedPluckDelayLen < 2.0f) cachedPluckDelayLen = 2.0f;
        if (cachedPluckDelayLen >= PLUCK_BUFFER_SIZE) {
          cachedPluckDelayLen = PLUCK_BUFFER_SIZE - 1;
        }
      }
      if (depth != cachedPluckDepth) {
        cachedPluckDepth = depth;
        cachedPluckDepthQ15 = (int32_t)(depth * 32768.0f + 0.5f);
        if (cachedPluckDepthQ15 < 0) cachedPluckDepthQ15 = 0;
        if (cachedPluckDepthQ15 > 32768) cachedPluckDepthQ15 = 32768;
      }
      const float delayLenF = cachedPluckDelayLen;

      int wPos = (int)pluck_buffer_write_index;

      // Read position: fractional delay behind write head
      float readPosF = pluck_buffer_write_index - delayLenF;
      if (readPosF < 0) readPosF += PLUCK_BUFFER_SIZE;
      int readPos0 = (int)readPosF;
      int readPos1 = readPos0 + 1;
      if (readPos1 >= PLUCK_BUFFER_SIZE) readPos1 = 0;
      float frac = readPosF - (int)readPosF;

      // Linear interpolation for fractional delay
      int32_t out = pluckBuffer[readPos0] + (int32_t)((pluckBuffer[readPos1] - pluckBuffer[readPos0]) * frac);
      // Truncate symmetrically toward zero. Arithmetic right shift alone rounds
      // negative odd values downward and can create a permanent negative tail.
      int32_t averageSum = out + prevPluckOutput;
      int32_t avg = averageSum >= 0
          ? (averageSum >> 1) : -((-averageSum) >> 1);
      prevPluckOutput = out;

      // Inject excitation at pluckPosition along the delay line
      // 0.0 = at read head (far end), 1.0 = at write head (near end)
      if (audioIn != 0) {
        int exciteOffset = (int)(delayLenF * pluckPosition);
        if (exciteOffset >= (int)delayLenF) exciteOffset = (int)delayLenF - 1;
        int ePos = readPos0 + exciteOffset;
        if (ePos >= PLUCK_BUFFER_SIZE) ePos -= PLUCK_BUFFER_SIZE;
        pluckBuffer[ePos] = clip16(pluckBuffer[ePos] + audioIn);
        int ePos1 = ePos + 1;
        if (ePos1 >= PLUCK_BUFFER_SIZE) ePos1 = 0;
        pluckBuffer[ePos1] = clip16(pluckBuffer[ePos1] + audioIn);
      }

      // Write damped feedback at write position
      int64_t feedbackProduct = (int64_t)avg * cachedPluckDepthQ15;
      int32_t feedback = feedbackProduct >= 0
          ? (int32_t)(feedbackProduct >> 15)
          : -(int32_t)((-feedbackProduct) >> 15);
      if (pluckDampCutoff > 0.0f) {
        feedback = pluckDampFilter.next(feedback);
      }
      #if IS_ESP32() || IS_RP2040()
      int32_t silenceThreshold =
          pluckSilenceThreshold.load(std::memory_order_relaxed);
      #else
      int32_t silenceThreshold = pluckSilenceThreshold;
      #endif
      if (feedback >= -silenceThreshold && feedback <= silenceThreshold) {
        feedback = 0;
      }
      pluckBuffer[wPos] = clip16(feedback);

      pluck_buffer_write_index += 1.0f;
      if (pluck_buffer_write_index >= PLUCK_BUFFER_SIZE) pluck_buffer_write_index -= PLUCK_BUFFER_SIZE;
      float dry = (1.0f - pluckStiffMix) * out;
      float wet = pluckStiffMix * pluckAllpass.next(out);
      pluckCached = clip16(dry + wet);
      }
      });
      return pluckCached;
    }

    /** Digital Waveguide model
    *  Two counter-propagating delay lines as in Figure 6.3 of
    *  "Physical Audio Signal Processing" (Julius O. Smith III).
    *  Both buffers advance in sync. y+ enters at x=0 (left end),
    *  y- enters at x=L (right end). At each time step:
    *   1. Read y+ and y- at the boundary (halfDelay behind write head)
    *   2. Read y+ and y- at the pickup position (wgPosition 0.0-1.0)
    *   3. Write reflected waves (sign inversion at rigid terminations)
    *   4. Inject excitation at wgPosition
    *  Velocity waves reflect with sign inversion at rigid terminations.
    *  @audioIn Excitation signal (noise burst, impulse, oscillator)
    *  @wgFreq Fundamental frequency in Hz. Determines delay line length.
    *  @depth Feedback/damping level 0.0-1.0. Higher = longer decay.
    *  Use large depth values (0.96-0.995) for sustained waveguide resonance.
    */
    inline
    int16_t waveguide(int16_t audioIn, float wgFreq, float depth) {
      if (!wgBufferEstablished) initWgBuffers();
      M16_ATOMIC_GUARD(_physLock, {
        // Half delay: one-way travel time (N/2 in the article)
        float halfDelayF = SAMPLE_RATE * 0.5f / wgFreq;
        if (halfDelayF < 2.0f) halfDelayF = 2.0f;
        if (halfDelayF >= WG_BUFFER_SIZE) halfDelayF = WG_BUFFER_SIZE - 1;

        // Boundary read position: halfDelay behind write head
        // y+ at x=L and y- at x=0 are both at this position
        float boundaryRead = wgWritePos - halfDelayF;
        if (boundaryRead < 0) boundaryRead += WG_BUFFER_SIZE;
        int br0 = (int)boundaryRead;
        int br1 = br0 + 1; if (br1 >= WG_BUFFER_SIZE) br1 = 0;
        float fracB = boundaryRead - br0;

        // Read y+ at boundary (x=L) for reflection → becomes -y-
        int32_t yPlusAtBoundary = wgRight[br0] + (int32_t)((wgRight[br1] - wgRight[br0]) * fracB);
        // Read y- at boundary (x=0) for reflection → becomes -y+
        int32_t yMinusAtBoundary = wgLeft[br0] + (int32_t)((wgLeft[br1] - wgLeft[br0]) * fracB);

        // Precompute pickup position offsets (reused for reads and excitation)
        float posR = wgPosition * halfDelayF;
        float posL = (1.0f - wgPosition) * halfDelayF;
        if (posR < 1.0f) posR = 1.0f;
        if (posL < 1.0f) posL = 1.0f;

        // Read y+ at pickup position ξ = wgPosition (0.0=left, 1.0=right)
        // y+ entered at x=0, so at ξ it has traveled ξ*halfDelay samples
        float readPosR = wgWritePos - posR;
        if (readPosR < 0) readPosR += WG_BUFFER_SIZE;
        int rr0 = (int)readPosR;
        int rr1 = rr0 + 1; if (rr1 >= WG_BUFFER_SIZE) rr1 = 0;
        float fracR = readPosR - rr0;
        int32_t yPlus = wgRight[rr0] + (int32_t)((wgRight[rr1] - wgRight[rr0]) * fracR);

        // Read y- at pickup position ξ = wgPosition
        // y- entered at x=L, so at ξ it has traveled (1-ξ)*halfDelay samples
        float readPosL = wgWritePos - posL;
        if (readPosL < 0) readPosL += WG_BUFFER_SIZE;
        int rl0 = (int)readPosL;
        int rl1 = rl0 + 1; if (rl1 >= WG_BUFFER_SIZE) rl1 = 0;
        float fracL = readPosL - rl0;
        int32_t yMinus = wgLeft[rl0] + (int32_t)((wgLeft[rl1] - wgLeft[rl0]) * fracL);

        // Output = sum of both traveling waves at pickup position
        int32_t out = yPlus + yMinus;

        // Write reflected waves at boundary (sign inversion at rigid terminations)
        // y+ at x=L → becomes -y- (written to wgLeft)
        // y- at x=0 → becomes -y+ (written to wgRight)
        int32_t feedbackR = -yMinusAtBoundary * depth;
        int32_t feedbackL = -yPlusAtBoundary * depth;
        if (wgDampCutoff > 0.0f) {
          feedbackR = wgDampFilterR.next(feedbackR);
          feedbackL = wgDampFilterL.next(feedbackL);
        }
        wgRight[wgWritePos] = clip16(feedbackR);
        wgLeft[wgWritePos] = clip16(feedbackL);

        // Inject excitation at wgPosition along the delay line
        if (audioIn != 0) {
          // y+ excitation offset: reuse precomputed posR (min 1 already applied)
          int exciteOffsetR = (int)posR;
          if (exciteOffsetR >= (int)halfDelayF) exciteOffsetR = (int)halfDelayF - 1;
          int ePosR = wgWritePos - exciteOffsetR;
          if (ePosR < 0) ePosR += WG_BUFFER_SIZE;
          wgRight[ePosR] = clip16(wgRight[ePosR] + audioIn);

          // y- excitation offset: reuse precomputed posL (min 1 already applied)
          int exciteOffsetL = (int)posL;
          if (exciteOffsetL >= (int)halfDelayF) exciteOffsetL = (int)halfDelayF - 1;
          int ePosL = wgWritePos - exciteOffsetL;
          if (ePosL < 0) ePosL += WG_BUFFER_SIZE;
          wgLeft[ePosL] = clip16(wgLeft[ePosL] + audioIn);
        }

        // Advance write position (both buffers in sync)
        wgWritePos++;
        if (wgWritePos >= WG_BUFFER_SIZE) wgWritePos = 0;

        float wgDry = (1.0f - wgStiffMix) * out;
        float wgWet = wgStiffMix * wgAllpass.next(out);
        wgCached = clip16(wgDry + wgWet);
      });
      return wgCached;
    }

  private:
    // Allpass filters for feedback path
    All pluckAllpass;
    All wgAllpass;

    // Damping filters for feedback path
    EMA pluckDampFilter;
    EMA wgDampFilterR;
    EMA wgDampFilterL;
    float pluckDampCutoff = 1.0f;
    float wgDampCutoff = 1.0f;

    // Pluck (Karplus-Strong) state
    const static int16_t PLUCK_BUFFER_SIZE = 1500; // lowest MIDI pitch is 24
    int * pluckBuffer;
    float pluck_buffer_write_index = 0;
    int prevPluckOutput = 0;
    bool pluckBufferEstablished = false;
    int16_t pluckCached = 0;
    float pluckPosition = 0.2f;
    float pluckStiffMix = 1.0f;
    float cachedPluckFreq = -1.0f;
    float cachedPluckDelayLen = 2.0f;
    float cachedPluckDepth = -1.0f;
    int32_t cachedPluckDepthQ15 = 0;
    static constexpr int16_t PLUCK_RESET_CHUNK_SIZE = 64;
    int16_t pluckResetIndex = PLUCK_BUFFER_SIZE;
    bool pluckResetActive = false;
    #if IS_ESP32() || IS_RP2040()
    std::atomic<float> storedPluckFreq{440.0f};
    std::atomic<bool> pluckResetRequested{false};
    std::atomic<int16_t> pluckSilenceThreshold{4};
    #else
    float storedPluckFreq = 440.0f;
    bool pluckResetRequested = false;
    int16_t pluckSilenceThreshold = 4;
    #endif

    // Waveguide state
    const static int16_t WG_BUFFER_SIZE = 1500;
    int16_t * wgRight;
    int16_t * wgLeft;
    int wgWritePos = 0;
    bool wgBufferEstablished = false;
    int16_t wgCached = 0;
    float wgPosition = 0.2f;
    float wgStiffMix = 1.0f;

    void initPluckBuffer() {
      size_t totalSize = PLUCK_BUFFER_SIZE * sizeof(int);
      if (isPSRAMAvailable() && getFreePSRAM() > totalSize + (totalSize / 10)) {
        pluckBuffer = (int*)psramAllocSafe(totalSize, "phys pluck");
      } else {
        pluckBuffer = new int[PLUCK_BUFFER_SIZE];
      }
      for (int i = 0; i < PLUCK_BUFFER_SIZE; i++) {
        pluckBuffer[i] = 0;
      }
      pluckBufferEstablished = true;
    }

    void beginPluckReset() {
      pluckResetIndex = 0;
      pluckResetActive = true;
      pluck_buffer_write_index = 0.0f;
      prevPluckOutput = 0;
      pluckCached = 0;
      pluckDampFilter.reset();
    }

    void clearPluckResetChunk() {
      int end = pluckResetIndex + PLUCK_RESET_CHUNK_SIZE;
      if (end > PLUCK_BUFFER_SIZE) end = PLUCK_BUFFER_SIZE;
      for (int i = pluckResetIndex; i < end; i++) pluckBuffer[i] = 0;
      pluckResetIndex = end;
      if (pluckResetIndex >= PLUCK_BUFFER_SIZE) pluckResetActive = false;
    }

    void initWgBuffers() {
      size_t totalSize = WG_BUFFER_SIZE * sizeof(int16_t) * 2;
      if (isPSRAMAvailable() && getFreePSRAM() > totalSize + (totalSize / 10)) {
        wgRight = psramAllocInt16(WG_BUFFER_SIZE, "phys wgRight");
        wgLeft = psramAllocInt16(WG_BUFFER_SIZE, "phys wgLeft");
      } else {
        wgRight = new int16_t[WG_BUFFER_SIZE];
        wgLeft = new int16_t[WG_BUFFER_SIZE];
      }
      for (int i = 0; i < WG_BUFFER_SIZE; i++) {
        wgRight[i] = 0;
        wgLeft[i] = 0;
      }
      wgWritePos = 0;
      wgBufferEstablished = true;
    }
};

#endif
