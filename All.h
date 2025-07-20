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
    All() {}

    /** Constructor */
    All(float delay, float feedback) {
      setDelayTime(delay);
      setFeedbackLevel(feedback);
    }

    /** Calculate the next Allpass filter sample, given an input signal.
     *  @Input is an output sample from an oscillator or other audio element.
     *  Not technically a state variable filter, but...
     */
    inline
    int next(int input) {
      // y[n] = (-g * x[n]) + x[n - d] + (g * y[n - d]) // g = gain, d = delay, x = input, y = output
      // set up first time called
      if (!allpassInitiated) {
        initAllpass();
      }
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
      //figure out your current y term: y[n] = -a*x[n] + x[n-d] + a*y[n-d]
      int output = clip16(((feedbackLevel * -1 * input)>>10) + delX + ((feedbackLevel * delY)>>10));
      //stick the output in the ybuffer
      outputBuffer[bufferWriteIndex] = output;
      //increment write index
      bufferWriteIndex = bufferWriteIndex + 1; 
      if (bufferWriteIndex >= bufferSize_samples) {
        bufferWriteIndex = 0;
      }
      return output;
    }

    /** Set the feedback level of the allpass filter.
     *  @param level is the feedback level, a float between 0 and 1.
     */
    inline
    void setFeedbackLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedbackLevel = pow(level, 0.4) * 1024;
      } else Serial.println("Feedback level must be between 0 and 1");
    }

    /** Get the feedback level of the allpass filter */
    float getFeedbackLevel() {
      return feedbackLevel * 0.0009765625;
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
      } else Serial.println("Allpass delay time must be between 0 and allpass size of " + String(allpassSize));
      // set the read index
      bufferReadIndex = (bufferWriteIndex - delayTime_samples);
      if (bufferReadIndex < 0) {
        bufferReadIndex = bufferReadIndex + bufferSize_samples - 1;
      } else if  (bufferReadIndex >= bufferSize_samples) {
        bufferReadIndex = bufferReadIndex % bufferSize_samples;
      }
    }

  private:

    bool allpassInitiated = false;
    int allpassSize = 100; // in ms 
    int bufferSize_samples;
    float delayTime = 1; // in ms // 0 - allpassSize
    int delayTime_samples;
    int feedbackLevel = 700; // 0-1024
    int * inputBuffer;
    int * outputBuffer;
    int bufferWriteIndex = 0;
    int bufferReadIndex = 0;
    int prevOutput = 0;
    bool usePSRAM = false;

     /** Create the allpass filter input signal buffer */
    void createInputBuffer() {
      delete[] inputBuffer; // remove any previous memory allocation
      bufferSize_samples = allpassSize * 0.001f * SAMPLE_RATE;
      setDelayTime(delayTime);
      #if IS_ESP32()
        if (usePSRAM) {
          inputBuffer = (int *) ps_malloc(bufferSize_samples * sizeof(int)); // calloc fills array with zeros
        } else inputBuffer = new int[bufferSize_samples]; // create a new buffer
      #else
        inputBuffer = new int[bufferSize_samples]; // create a new buffer
      #endif
      for(int i=0; i<bufferSize_samples; i++) {
        inputBuffer[i] = 0; // zero out the buffer
      }
    }

    /** Create the allpass filter output signal buffer */
    void createOutputBuffer() {
      delete[] outputBuffer; // remove any previous memory allocation
      bufferSize_samples = allpassSize * 0.001f * SAMPLE_RATE;
      setDelayTime(delayTime);
      #if IS_ESP32()
        if (usePSRAM && ESP.getFreePsram() > bufferSize_samples * sizeof(int)) {
          outputBuffer = (int *) ps_calloc(bufferSize_samples, sizeof(int)); // calloc fills array with zeros
        } else outputBuffer = new int[bufferSize_samples]; // create a new buffer
      #else
        inputBuffer = new int[bufferSize_samples]; // create a new buffer
      #endif
      for(int i=0; i<bufferSize_samples; i++) {
        outputBuffer[i] = 0; // zero out the buffer
      }
    }

    /** Set the allpass filter params */
    void initAllpass() {
      #if IS_ESP32()
        if (psramFound()) {
          // Serial.println("PSRAM is availible in allpass");
          usePSRAM = true;
        } else {
          // Serial.println("PSRAM not available in allpass");
          usePSRAM = false;
        }
      #endif
      createInputBuffer();
      createOutputBuffer();
      allpassInitiated = true;
    }
};

#endif /* ALL_H_ */