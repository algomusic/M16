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

    /** Calculate the next Allpass filter sample, given an input signal.
     *  @Input is an output sample from an oscillator or other audio element.
     *  Not technically a state variable filter, but...
     */
    inline
    int next(int input) {
      // y[n] = a * x[n] + b * x[d-D] + c * y[n-D] // D = delay, a = input gain, b = feedforward gain , c = feedback gain
      // set up first time called
      if (!combInitiated) {
        initComb();
      }

      if (!inputBuffer || !outputBuffer) return 0;
      if (bufferSize_samples == 0) return 0;

      //write input to delay buffer
      inputBuffer[bufferWriteIndex] = input;
      // increment read index
      bufferReadIndex = bufferReadIndex + 1;
      if (bufferReadIndex >= bufferSize_samples) {
        bufferReadIndex = 0;
      }
      //get delayed values of x and y
      int delX = inputBuffer[bufferReadIndex];
      int delY = outputBuffer[bufferReadIndex];
      //figure out your current y term: y[n] = a*x[n] + b*x[n-d] + c*y[n-d]
      int output = clip16(((inputLevel * input)>>10) + ((feedforwardLevel * delX)>>10) + ((feedbackLevel * delY)>>10));
      //stick the output in the ybuffer
      outputBuffer[bufferWriteIndex] = output;
      //increment write index
      bufferWriteIndex = bufferWriteIndex + 1;
      if (bufferWriteIndex >= bufferSize_samples) {
        bufferWriteIndex = 0;
      }
      return output;
    }

    /** Set the input level of the comb filter.
     *  @param level is the input level, a float between 0 and 1.
     */
    inline
    void setInputLevel(float level) {
      if (level >= 0 && level <= 1) {
        inputLevel = pow(level, 0.4) * 1024;
      } else Serial.println("Input level must be between 0 and 1");
    }

    /** Get the input level of the allpass filter */
    float getInputLevel() {
      return inputLevel * 0.0009765625;
    }

    /** Set the feedforward level of the comb filter.
     *  @param level is the feedforward level, a float between 0 and 1.
     */
    inline
    void setFeedforwardLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedforwardLevel = pow(level, 0.4) * 1024;
      } else Serial.println("Feedforward level must be between 0 and 1");
    }

    /** Get the feedfoward level of the comb filter */
    float getFeedforwardLevel() {
      return feedforwardLevel * 0.0009765625;
    }

    /** Set the feedback level of the comb filter.
     *  @param level is the feedback level, a float between 0 and 1.
     */
    inline
    void setFeedbackLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedbackLevel = pow(level, 0.4) * 1024;
      } else Serial.println("Feedback level must be between 0 and 1");
    }

    /** Get the feedback level of the comb filter */
    float getFeedbackLevel() {
      return feedbackLevel * 0.0009765625;
    }

    /** Set the allpass filter size
     * @param size The length of the maximum delay line in milliseconds
     */
    inline
    void setMaxTime(int16_t size) {
      if (size <= delayTime) {
        allpassSize = size;
        createInputBuffer();
      } else Serial.println("Allpass size must be less than or equal to delay time");
    }

    /** Set the allpass filter delay time
     * @param time The length of the delay line in milliseconds
     */
    inline
    void setDelayTime(float time) {
      if (time >= 0) {
        if (time > allpassSize) {
          allpassSize = time * 10;
          createInputBuffer();
        }
        delayTime = time;
        delayTime_samples = delayTime * 0.001f * SAMPLE_RATE;
      } else Serial.println("Allpass delay time must be between 0 and allpass size");
      // set the read index
      bufferReadIndex = (bufferWriteIndex - delayTime_samples);
      if (bufferReadIndex < 0) {
        bufferReadIndex = bufferReadIndex + bufferSize_samples - 1;
      } else if  (bufferReadIndex >= bufferSize_samples) {
        bufferReadIndex = bufferReadIndex % bufferSize_samples;
      }
    }

  private:

    bool combInitiated = false;
    int16_t allpassSize = 100; // in ms
    int bufferSize_samples;
    float delayTime = 1; // in ms // 0 - allpassSize
    int delayTime_samples;
    int inputLevel = 0; // 0-1024
    int feedforwardLevel = 700; // 0-1024
    int feedbackLevel = 0; // 0-1024
    int * inputBuffer = nullptr;   
    int * outputBuffer = nullptr;  
    int bufferWriteIndex = 0;
    int bufferReadIndex = 0;
    int prevOutput = 0;
    bool usePSRAM = false;

     /** Create the allpass filter input signal buffer */
    void createInputBuffer() {
      if (inputBuffer) { delete[] inputBuffer; inputBuffer = nullptr; }
      bufferSize_samples = allpassSize * 0.001f * SAMPLE_RATE;
      setDelayTime(delayTime);

      #if IS_ESP32()
        if (usePSRAM) {
          inputBuffer = (int *) ps_malloc(bufferSize_samples * sizeof(int));
          if (!inputBuffer) {
            Serial.println("PSRAM alloc failed for comb input, using regular RAM");
            usePSRAM = false;
            inputBuffer = new int[bufferSize_samples];
          }
        } else {
          inputBuffer = new int[bufferSize_samples];
        }
      #else
        inputBuffer = new int[bufferSize_samples];
      #endif

      if (!inputBuffer) {
        Serial.println("ERROR: Comb input buffer allocation failed!");
        bufferSize_samples = 0;
        return;
      }

      for(int i=0; i<bufferSize_samples; i++) {
        inputBuffer[i] = 0; // zero out the buffer
      }
    }

    /** Create the allpass filter output signal buffer */
    void createOutputBuffer() {
      if (outputBuffer) { delete[] outputBuffer; outputBuffer = nullptr; }
      bufferSize_samples = allpassSize * 0.001f * SAMPLE_RATE;
      setDelayTime(delayTime);

      #if IS_ESP32()
        if (usePSRAM && ESP.getFreePsram() > bufferSize_samples * sizeof(int)) {
          outputBuffer = (int *) ps_calloc(bufferSize_samples, sizeof(int));
          if (!outputBuffer) {
            Serial.println("PSRAM alloc failed for comb output, using regular RAM");
            usePSRAM = false;
            outputBuffer = new int[bufferSize_samples];
          }
        } else {
          outputBuffer = new int[bufferSize_samples];
        }
      #else
        outputBuffer = new int[bufferSize_samples];
      #endif

      if (!outputBuffer) {
        Serial.println("ERROR: Comb output buffer allocation failed!");
        bufferSize_samples = 0;
        return;
      }

      for(int i=0; i<bufferSize_samples; i++) {
        outputBuffer[i] = 0; // zero out the buffer
      }
    }

    /** Set the comb filter params */
    void initComb() {
      #if IS_ESP32()
        // Use global PSRAM check instead of direct call
        usePSRAM = isPSRAMAvailable();
      #endif
      createInputBuffer();
      createOutputBuffer();
      combInitiated = true;
    }
};

#endif /* COMB_H_ */