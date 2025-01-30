/*
 * All.h
 *
 * An allpass Filter implementation
 *
 * by Andrew R. Brown 2025
 *
 * This filter is based on the allpass~ object in Max
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
    All(int32_t delay, float feedback) {
        setDelayTime(delay);
        setFeedbackLevel(feedback);
    }

    /** Calculate the next Allpass filter sample, given an input signal.
     *  @Input is an output sample from an oscillator or other audio element.
     *  Not technically a state variable filter, but...
     */
    inline
    int16_t next(int32_t input) {
      // y[n] = (-g * x[n]) + x[n - d] + (g * y[n - d]) // g = gain, d = delay, x = input, y = output
      // set up first time called
      if (!allpassInitiated) {
        initAllpass();
      }
      int32_t inG = -feedbackLevel * input;
      int bufferReadIndex = (bufferIndex - delayTime_samples);
      if (bufferReadIndex < 0) bufferReadIndex += bufferSize_samples; 
      int32_t inMinusD = inputBuffer[bufferReadIndex];
      int32_t outGminusD = (feedbackLevel * outputBuffer[bufferReadIndex])>>10; 
      int32_t output = clip16(inG + inMinusD + outGminusD);
      // update buffers
      inputBuffer[bufferIndex] = input;
      outputBuffer[bufferIndex] = output;
      bufferIndex = (bufferIndex + 1) % bufferSize_samples;
      return output;
    }

    /** Set the feedback level of the allpass filter.
     *  @param level is the feedback level, a float between 0 and 1.
     */
    inline
    void setFeedbackLevel(float level) {
      if (level >= 0 && level <= 1) {
        feedbackLevel = level * 1024;
      } else Serial.println("Feedback level must be between 0 and 1");
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
    void setDelayTime(int16_t time) {
      if (time >= 0) {
        if (time > allpassSize) {
          allpassSize = time * 10;
          createInputBuffer();
        }
        delayTime = time;
        delayTime_samples = delayTime * 0.001f * SAMPLE_RATE;
      } else Serial.println("Allpass delay time must be between 0 and allpass size");
    }

  private:

    bool allpassInitiated = false;
    int16_t allpassSize = 100; // in ms 
    int32_t bufferSize_samples;
    int16_t delayTime = 1; // in ms // 0 - allpassSize
    int32_t delayTime_samples;
    int16_t feedbackLevel = 700; // 0-1024
    int32_t * inputBuffer;
    int32_t * outputBuffer;
    int32_t bufferIndex = 0;

     /** Create the allpass filter input signal buffer */
    void createInputBuffer() {
      delete[] inputBuffer; // remove any previous memory allocation
      bufferSize_samples = allpassSize * 0.001f * SAMPLE_RATE;
      setDelayTime(delayTime);
      inputBuffer = new int32_t[bufferSize_samples]; // create a new buffer
      for(int i=0; i<bufferSize_samples; i++) {
        inputBuffer[i] = 0; // zero out the buffer
      }
    }

    /** Create the allpass filter output signal buffer */
    void createOutputBuffer() {
      delete[] outputBuffer; // remove any previous memory allocation
      bufferSize_samples = allpassSize * 0.001f * SAMPLE_RATE;
      setDelayTime(delayTime);
      outputBuffer = new int32_t[bufferSize_samples]; // create a new buffer
      for(int i=0; i<bufferSize_samples; i++) {
        outputBuffer[i] = 0; // zero out the buffer
      }
    }

    /** Set the allpass filter params */
    void initAllpass() { 
      createInputBuffer();
      createOutputBuffer();
      allpassInitiated = true;
    }
};

#endif /* ALL_H_ */