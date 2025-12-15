/*
 * Comb.h
 *
 * A comb Filter implementation
 *
 * by Andrew R. Brown 2025
 *
 * This filter is based on the comb~ object in Max
 * with thanks to Derek Kwan's implementation in Cylone for Pure Data
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef COMB_H_
#define COMB_H_

class Comb {

  public:
    /** Constructor */
    Comb() : inputBuffer(nullptr), outputBuffer(nullptr) {}

    /** Constructor */
    Comb(float delay, float inputGain, float feedforwardGain, float feedbackGain) {
        setDelayTime(delay);
        setInputLevel(inputGain);
        setFeedforwardLevel(feedforwardGain);
        setFeedbackLevel(feedbackGain);
    }

    /** Destructor */
    ~Comb() {
      if (inputBuffer) { delete[] inputBuffer; inputBuffer = nullptr; }
      if (outputBuffer) { delete[] outputBuffer; outputBuffer = nullptr; }
    }

    /** Calculate the next comb filter sample, given an input signal.
     *  @Input is an output sample from an oscillator or other audio element.
     */
    inline
    int16_t next(int input) {
      // y[n] = a * x[n] + b * x[d-D] + c * y[n-D] // D = delay, a = input gain, b = feedforward gain , c = feedback gain
      // set up first time called
      if (!combInitiated) {
        initComb();
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

      // Calculate output: y[n] = a*x[n] + b*x[n-d] + c*y[n-d]
      // With rounding (+512) to reduce quantization noise
      int32_t output = ((inputLevel * input + 512) >> 10)
                     + ((feedforwardLevel * delX + 512) >> 10)
                     + ((feedbackLevel * delY + 512) >> 10);
      output = clip16(output);

      // Store the output in the output buffer
      outputBuffer[bufferWriteIndex] = (int16_t)output;

      // Increment write index using bitwise AND for fast wrap
      bufferWriteIndex = (bufferWriteIndex + 1) & bufferMask;

      return (int16_t)output;
    }

    /** Set the input level of the comb filter.
     *  @param level is the input level, a float between 0 and 1.
     */
    inline
    void setInputLevel(float level) {
      if (level >= 0 && level <= 1) {
        inputLevel = (int16_t)(pow(level, 0.4) * 1024);
      } else Serial.println("Input level must be between 0 and 1");
    }

    /** Get the input level of the comb filter */
    float getInputLevel() {
      return inputLevel * 0.0009765625f;
    }

    /** Set the feedforward level of the comb filter.
     *  @param level is the feedforward level, a float between 0 and 1.
     */
    inline
    void setFeedforwardLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedforwardLevel = (int16_t)(pow(level, 0.4) * 1024);
      } else Serial.println("Feedforward level must be between 0 and 1");
    }

    /** Get the feedfoward level of the comb filter */
    float getFeedforwardLevel() {
      return feedforwardLevel * 0.0009765625f;
    }

    /** Set the feedback level of the comb filter.
     *  @param level is the feedback level, a float between 0 and 1.
     */
    inline
    void setFeedbackLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedbackLevel = (int16_t)(pow(level, 0.4) * 1024);
      } else Serial.println("Feedback level must be between 0 and 1");
    }

    /** Get the feedback level of the comb filter */
    float getFeedbackLevel() {
      return feedbackLevel * 0.0009765625f;
    }

    /** Set the comb filter maximum delay size
     * @param size The length of the maximum delay line in milliseconds
     */
    inline
    void setMaxTime(int16_t size) {
      if (size >= delayTime) {
        combSize = size;
        createBuffers();
      } else Serial.println("Comb size must be greater than or equal to delay time");
    }

    /** Set the comb filter delay time
     * @param time The length of the delay line in milliseconds
     */
    inline
    void setDelayTime(float time) {
      if (time >= 0) {
        if (time > combSize) {
          combSize = (int16_t)(time * 1.5f);  // Add some headroom
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
      } else Serial.println("Comb delay time must be >= 0");
    }

  private:

    bool combInitiated = false;
    int16_t combSize = 100; // in ms
    uint16_t bufferSize_samples = 0;
    uint16_t bufferMask = 0;  // For fast modulo with power-of-2 buffer
    float delayTime = 1; // in ms
    uint16_t delayTime_samples = 0;
    int16_t inputLevel = 0; // 0-1024
    int16_t feedforwardLevel = 700; // 0-1024
    int16_t feedbackLevel = 0; // 0-1024
    int16_t* inputBuffer = nullptr;
    int16_t* outputBuffer = nullptr;
    uint16_t bufferWriteIndex = 0;
    uint16_t bufferReadIndex = 0;
    bool usePSRAM = false;

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
      uint16_t requiredSize = (uint16_t)(combSize * 0.001f * SAMPLE_RATE);
      bufferSize_samples = 1;
      while (bufferSize_samples < requiredSize) {
        bufferSize_samples <<= 1;
      }
      bufferMask = bufferSize_samples - 1;

      // Allocate buffers
      #if IS_ESP32()
        if (usePSRAM && isPSRAMAvailable()) {
          inputBuffer = (int16_t*)ps_calloc(bufferSize_samples, sizeof(int16_t));
          outputBuffer = (int16_t*)ps_calloc(bufferSize_samples, sizeof(int16_t));
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
        Serial.println("ERROR: Comb buffer allocation failed!");
        bufferSize_samples = 0;
        bufferMask = 0;
        return;
      }

      // Reset indices
      bufferWriteIndex = 0;
      updateReadIndex();
    }

    /** Initialize the comb filter */
    void initComb() {
      #if IS_ESP32()
        usePSRAM = isPSRAMAvailable();
      #endif
      createBuffers();
      combInitiated = true;
    }
};

#endif /* COMB_H_ */
