/*
 * APF.h
 *
 * An allpass filter class.
 *
 * by Andrew R. Brown 2024
 *
 * This file is part of the M16 audio library. Relies on M16.h
 * M16 is Based on the Mozzi audio library by Tim Barrass 2012.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef APF_H_
#define APF_H_

class APF {

  public:
  /** Constructor.
	* Create but don't setup delay.
  * To use, setMaxTime() must be called to initiate the audio buffer.
	*/
	APF() {};

  /** Constructor.
	* Create and setup delay.
  * @param maxDur The initial delay time in milliseconds
  * @param level The allpass phase, from 0.0 to 1.0 
	*/
	APF(unsigned int maxDur, float phase) {
    setMaxTime(maxDur);
    setTime(maxDur);
    setPhase(phase);
  }

  /** 
  * Set the maximum delay time in milliseconds
  * @param maxDelayTime The maximum delay time in milliseconds
  */
  void setMaxTime(float maxDelayTime) {
    delete[] delayBuffer; // remove any previous memory allocation
    maxDelayTime_ms = max(0.0f, maxDelayTime);
    delayBufferSize_samples = maxDelayTime_ms * SAMPLE_RATE * 0.001 + 1;
    delayBuffer = new int16_t[delayBufferSize_samples]; // create a new audio buffer
    empty();
    initialised = true;
  }

  /** Retrieve the phase - 0.0 to 1.0 */
  float getMaxTime() {
    return maxDelayTime_ms;
  }

  /** Specify the delay duration in milliseconds */
  void setTime(float msDur) {
    if (!initialised || msDur > maxDelayTime_ms) setMaxTime(msDur);
    delayTime_ms = min(maxDelayTime_ms, max(0.0f, msDur));
    // Serial.print("delayTime_ms "); Serial.println(delayTime_ms);
    delayTime_samples = msDur * SAMPLE_RATE * 0.001;
    // Serial.print("delayTime_samples "); Serial.println(msDur);
  }

  /** Retrieve the delay time in ms */
  float getTime() {
    return delayTime_ms;
  }

  /** Specify the phase - 0.0 to 1.0 */
  void setPhase(float level) {
    invPhase = max(0.0f, min(1.0f, 1 - level));
    invPhaseInt = invPhase * 1024;
  }

  /** Retrieve the phase - 0.0 to 1.0 */
  float getPhase() {
    return 1.0f - invPhase;
  }

  /** Specify the feedback level - 0.0 to 1.0 */
  void setLevel(float level) {
    delayLevel = max(0, min(1024, (int)(level * 1024)));
  }

  /** Retrieve the feedback level - 0.0 to 1.0 */
  float getLevel() {
    return delayLevel * MAX_16_INV;
  }

  /** Input a value to the filter and retrieve the allpassed output.
	* @param inVal The signal input.
	*/
	inline
	int16_t next(int32_t inValue) {
    if (inValue > MAX_16) inValue = MAX_16;
    if (inValue < MIN_16) inValue = MIN_16;

    int32_t readValue = read();
    int32_t output = readValue + ((invPhaseInt * inValue)>>10);
    write(min(MAX_16, max(MIN_16, ((int)(inValue - ((invPhaseInt * output)>>10)) * delayLevel)>>10)));
    return output;
  }

  private:
    int16_t * delayBuffer;
    unsigned int writePos = 0;
    float maxDelayTime_ms = 0.0f;
    float delayTime_ms = 0.0f;
    unsigned int delayTime_samples = 0;
    int16_t delayLevel = 1000; // 0 to 1024
    unsigned int delayBufferSize_samples = 0;
    bool initialised = false;
    float invPhase = 1.0f;
    int invPhaseInt = 1024;

    /** Read the buffer at the delayTime and increment the read index */
    inline
    int16_t read() {
      int outValue = 0;
      int readPos = writePos - delayTime_samples;
      if (readPos < 0) readPos += delayBufferSize_samples;
      outValue = min(MAX_16, max(MIN_16, (int)delayBuffer[readPos]));
      return outValue;
    }

    /** Write the buffer at the delayTime and increment the write index
    * @param inVal The signal input.
    */
    inline
    void write(int inValue) {
      delayBuffer[writePos] = min(MAX_16, max(MIN_16, inValue));
      writePos = (writePos + 1) % delayBufferSize_samples;
    }

    /** Fill the delay with silence */
    void empty() {
      for(int i=0; i<delayBufferSize_samples; i++) {
        delayBuffer[i] = 0; // zero out the buffer
      }
    }

};

#endif /* APF_H_ */
