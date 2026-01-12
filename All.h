/*
 * All.h
 *
 * An allpass Filter implementation
 *
 * by Andrew R. Brown 2025
 *
 * This filter is based on the allpass~ object in Max
 * with thanks to Derek Kwan's implementation in Cylone for Pure Data
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef ALL_H_
#define ALL_H_

class All {

  public:
    /** Constructor */
    All() : inputBuffer(nullptr), outputBuffer(nullptr) {}

    /** Constructor */
    All(float delay, float feedback) {
      setDelayTime(delay);
      setFeedbackLevel(feedback);
    }

    /** Destructor */
    ~All() {
      if (inputBuffer) { delete[] inputBuffer; inputBuffer = nullptr; }
      if (outputBuffer) { delete[] outputBuffer; outputBuffer = nullptr; }
    }

    /** Calculate the next Allpass filter sample, given an input signal.
     *  First-order Schroeder allpass: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
     *  @Input is an output sample from an oscillator or other audio element.
     */
    inline
    int16_t next(int input) {
      // y[n] = (-g * x[n]) + x[n - d] + (g * y[n - d]) // g = gain, d = delay, x = input, y = output
      // set up first time called
      if (!allpassInitiated) {
        initAllpass();
      }

      if (!inputBuffer || !outputBuffer) return 0;
      if (bufferSize_samples == 0) return 0;

      // Write input to delay buffer
      inputBuffer[bufferWriteIndex] = (int16_t)clip16(input);

      // Increment read index using bitwise AND for fast wrap
      bufferReadIndex = (bufferReadIndex + 1) & bufferMask;

      // Get delayed values of x and y
      int32_t delX = inputBuffer[bufferReadIndex];
      int32_t delY = outputBuffer[bufferReadIndex];

      // Calculate output: y[n] = -g*x[n] + x[n-d] + g*y[n-d]
      // With rounding (+512) to reduce quantization noise
      int32_t output = ((-feedbackLevel * input + 512) >> 10)
                     + delX
                     + ((feedbackLevel * delY + 512) >> 10);
      output = clip16(output);

      // Store the output in the output buffer
      outputBuffer[bufferWriteIndex] = (int16_t)output;

      // Increment write index using bitwise AND for fast wrap
      bufferWriteIndex = (bufferWriteIndex + 1) & bufferMask;

      return (int16_t)output;
    }

    /** Set the feedback level of the allpass filter.
     *  @param level is the feedback level, a float between 0 and 1.
     */
    inline
    void setFeedbackLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedbackLevel = (int16_t)(pow(level, 0.4) * 1024);
      } else Serial.println("Feedback level must be between 0 and 1");
    }

    /** Get the feedback level of the allpass filter */
    float getFeedbackLevel() {
      return feedbackLevel * 0.0009765625f;
    }

    /** Set the allpass filter maximum delay size
     * @param size The length of the maximum delay line in milliseconds
     */
    inline
    void setMaxTime(int16_t size) {
      if (size >= delayTime) {
        allpassSize = size;
        createBuffers();
      } else Serial.println("Allpass size must be greater than or equal to delay time");
    }

    /** Set the allpass filter delay time
     * @param time The length of the delay line in milliseconds
     */
    inline
    void setDelayTime(float time) {
      if (time >= 0) {
        if (time > allpassSize) {
          allpassSize = (int16_t)(time * 1.5f);  // Add some headroom
          createBuffers();
        }
        delayTime = time;
        delayTime_samples = (uint16_t)(delayTime * 0.001f * SAMPLE_RATE);

        // Ensure delay doesn't exceed buffer
        if (delayTime_samples >= bufferSize_samples && bufferSize_samples > 0) {
          delayTime_samples = bufferSize_samples - 1;
        }

        // Set the read index relative to write index
        updateReadIndex();
      } else Serial.println("Allpass delay time must be >= 0");
    }

    /** Calculate the next second-order allpass filter sample.
     *  y[n] = a2*x[n] + a1*x[n-1] + x[n-2] - a1*y[n-1] - a2*y[n-2]
     *  @param input is an audio sample value
     */
    inline
    int16_t secondOrder(int32_t input) {
      // y[n] = a2*x[n] + a1*x[n-1] + x[n-2] - a1*y[n-1] - a2*y[n-2]
      int32_t output = ((so_a2 * input) >> 10)
                     + ((so_a1 * so_x1) >> 10)
                     + so_x2
                     - ((so_a1 * so_y1) >> 10)
                     - ((so_a2 * so_y2) >> 10);

      output = clip16(output);

      // Shift delay states
      so_x2 = so_x1;
      so_x1 = input;
      so_y2 = so_y1;
      so_y1 = output;

      return (int16_t)output;
    }

    /** Set second-order allpass coefficients directly.
     *  @param a1 coefficient (-2.0 to 2.0, typically around -2*cos(w0))
     *  @param a2 coefficient (0.0 to 1.0, controls bandwidth)
     */
    inline
    void setSecondOrderCoeffs(float a1, float a2) {
      so_a1 = (int16_t)(a1 * 1024.0f);
      so_a2 = (int16_t)(a2 * 1024.0f);
    }

    /** Set second-order allpass from frequency and Q.
     *  @param freq center frequency in Hz
     *  @param q Q factor (0.1 to 10, higher = narrower notch in phase response)
     */
    inline
    void setSecondOrderFreq(float freq, float q) {
      float w0 = 2.0f * 3.14159265f * freq / SAMPLE_RATE;
      float alpha = sin(w0) / (2.0f * q);
      float a2 = (1.0f - alpha) / (1.0f + alpha);
      float a1 = -2.0f * cos(w0) / (1.0f + alpha);
      so_a1 = (int16_t)(a1 * 1024.0f);
      so_a2 = (int16_t)(a2 * 1024.0f);
    }

    /** Reset second-order filter state (call when changing parameters significantly) */
    inline
    void resetSecondOrder() {
      so_x1 = 0;
      so_x2 = 0;
      so_y1 = 0;
      so_y2 = 0;
    }

  private:

    bool allpassInitiated = false;
    int16_t allpassSize = 100; // in ms
    uint16_t bufferSize_samples = 0;
    uint16_t bufferMask = 0;  // For fast modulo with power-of-2 buffer
    float delayTime = 1; // in ms
    uint16_t delayTime_samples = 0;
    int16_t feedbackLevel = 700; // 0-1024
    int16_t* inputBuffer = nullptr;
    int16_t* outputBuffer = nullptr;
    uint16_t bufferWriteIndex = 0;
    uint16_t bufferReadIndex = 0;
    bool usePSRAM = false;

    // Second-order allpass state
    int32_t so_x1 = 0;  // x[n-1]
    int32_t so_x2 = 0;  // x[n-2]
    int32_t so_y1 = 0;  // y[n-1]
    int32_t so_y2 = 0;  // y[n-2]
    int16_t so_a1 = 0;  // a1 coefficient (fixed-point, /1024)
    int16_t so_a2 = 512; // a2 coefficient (fixed-point, /1024) default 0.5

    /** Update read index based on current write index and delay */
    void updateReadIndex() {
      if (bufferSize_samples == 0) return;
      // Calculate read position (write - delay, wrapped)
      int32_t readPos = (int32_t)bufferWriteIndex - (int32_t)delayTime_samples;
      if (readPos < 0) {
        readPos += bufferSize_samples;
      }
      bufferReadIndex = (uint16_t)readPos & bufferMask;
    }

    /** Create both buffers with power-of-2 size */
    void createBuffers() {
      // Free existing buffers
      if (inputBuffer) { delete[] inputBuffer; inputBuffer = nullptr; }
      if (outputBuffer) { delete[] outputBuffer; outputBuffer = nullptr; }

      // Calculate required size and round up to power of 2
      uint16_t requiredSize = (uint16_t)(allpassSize * 0.001f * SAMPLE_RATE);
      bufferSize_samples = 1;
      while (bufferSize_samples < requiredSize) {
        bufferSize_samples <<= 1;
      }
      bufferMask = bufferSize_samples - 1;

      // Allocate buffers
      #if IS_ESP32()
        // Check if enough PSRAM for both buffers with headroom
        size_t totalSize = bufferSize_samples * sizeof(int16_t) * 2;
        if (usePSRAM && isPSRAMAvailable() && getFreePSRAM() > totalSize + (totalSize / 10)) {
          inputBuffer = psramAllocInt16(bufferSize_samples, nullptr);
          outputBuffer = psramAllocInt16(bufferSize_samples, nullptr);
          if (!inputBuffer || !outputBuffer) {
            // Fallback to regular RAM
            if (inputBuffer) { free(inputBuffer); inputBuffer = nullptr; }
            if (outputBuffer) { free(outputBuffer); outputBuffer = nullptr; }
            usePSRAM = false;
          }
        }
        if (!inputBuffer) {
          inputBuffer = new int16_t[bufferSize_samples]();
        }
        if (!outputBuffer) {
          outputBuffer = new int16_t[bufferSize_samples]();
        }
      #else
        inputBuffer = new int16_t[bufferSize_samples]();
        outputBuffer = new int16_t[bufferSize_samples]();
      #endif

      if (!inputBuffer || !outputBuffer) {
        Serial.println("ERROR: Allpass buffer allocation failed!");
        bufferSize_samples = 0;
        bufferMask = 0;
        return;
      }

      // Reset indices
      bufferWriteIndex = 0;
      updateReadIndex();
    }

    /** Initialize the allpass filter */
    void initAllpass() {
      #if IS_ESP32()
        usePSRAM = isPSRAMAvailable();
      #endif
      createBuffers();
      allpassInitiated = true;
    }
};

#endif /* ALL_H_ */
