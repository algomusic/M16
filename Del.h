/*
 * Del.h
 *
 * An audio delay line class.
 *
 * by Andrew R. Brown 2022
 *
 * Based on the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef DEL_H_
#define DEL_H_

class Del {

private:
  int16_t * delayBuffer;
  unsigned int writePos = 0;
  float delayTime_ms = 0.0f;
  unsigned int delayTime_samples = 0;
  int16_t delayLevel = 1024; // 0 to 1024
  float maxDelayTime_ms = 0;
  unsigned int delayBufferSize_samples = 0;
  bool delayFeedback = false;
  int16_t prevOutValue = 0;
  byte filtered = 1;

public:
  /** Constructor.
	* Create but don't setup delay.
  * To use, setMaxDecayTime() must be called to initiate the audio buffer.
	*/
	Del() {};

  /** Constructor.
	* Create and setup delay.
  * @param maxDelayTime The maximum delay time in milliseconds
  * @param msDur The initial delay time in milliseconds, up to maxDelayTime
  * @param level The delay feedback level, from 0.0 to 1.0 
  * @param feedback Multitap delay feedback on or off, true or false
	*/
	Del(unsigned int maxDelayTime, int msDur, float level, bool feedback) {
    setMaxDelayTime(maxDelayTime);
    setTime(msDur);
    setLevel(level);
    setFeedback(feedback);
  }

  /** 
   * Set the maximum delay time in milliseconds
   * @param maxDelayTime The maximum delay time in milliseconds
   */
  void setMaxDelayTime(unsigned int maxDelayTime) {
    delete[] delayBuffer; // remove any previous memory allocation
    maxDelayTime_ms = max((unsigned int)0, maxDelayTime);
    delayBufferSize_samples = maxDelayTime_ms * SAMPLE_RATE * 0.001;
    delayBuffer = new int16_t[delayBufferSize_samples]; // create a new audio buffer
    empty();
  }
  /** Constructor.
	@param maxDelayTime The size of the delay buffer, in milliseconds.
	*/
	Del(unsigned int maxDelayTime) {
    setMaxDelayTime(maxDelayTime);
  }

  ~Del() {
    delete[] delayBuffer;
  }

  /** Return the size of the delay buffer in ms */
  float getBufferSize() {
    return maxDelayTime_ms;
  }

  /** Return the delay length in samples */
  unsigned int getDelayLength() {
    return delayTime_samples;
  }

  /** Return the size of the delay buffer in samples */
  unsigned int getBufferLength() {
    return delayBufferSize_samples;
  }

  /** Specify the delay duration in milliseconds */
  void setTime(float msDur) {
    delayTime_ms = min(maxDelayTime_ms - 1.0f, max(0.0f, msDur));
    // Serial.print("delayTime_ms "); Serial.println(delayTime_ms);
    delayTime_samples = msDur * SAMPLE_RATE * 0.001;
    // Serial.print("delayTime_samples "); Serial.println(delayTime_samples);
  }

  /** Return the delay duration in milliseconds */
  float getTime() {
    return delayTime_ms;
  }

  /** Specify the delay feedback level, from 0.0 to 1.0 */
  void setLevel(float level) {
    delayLevel = min(1024, max(0, (int)(level * 1024)));
    // Serial.print("delayLevel "); Serial.println(delayLevel);
  }

  /** Return the delay feedback level, from 0.0 to 1.0 */
  float getLevel() {
    return delayLevel;
  }

  /** Turn delay feedback on or off */
  void setFeedback(bool state) {
    delayFeedback = state;
  }

  /** Specify the degree of filtering of the delay signal, from 0 (none) to 4 (most dull) */
  void setFiltered(byte newVal) {
    if (newVal >= 0) filtered = newVal;
  }

  /** Fill the delay with silence */
  void empty() {
    for(int i=0; i<delayBufferSize_samples; i++) {
      delayBuffer[i] = 0; // zero out the buffer
    }
  }

  /** Input a value to the delay and retrieve the signal delayed by delayTime milliseconds .
	* @param inVal The signal input.
	*/
	inline
	int16_t next(int32_t inValue) {
    int32_t outValue = 0;
    if (delayTime_samples > 0) {
      outValue = read();
    }
    if (delayFeedback) {
      inValue = (inValue + outValue)>>1;
    }
    if (inValue > MAX_16) inValue = MAX_16;
    if (inValue < -MAX_16) inValue = -MAX_16;
    write(inValue);
    if (outValue > MAX_16) outValue = MAX_16;
    if (outValue < -MAX_16) outValue = -MAX_16;
    return outValue;
  }

  /** Read the buffer at the delayTime without incrementing read/write index */
  inline
	int16_t read() {
    int outValue = 0;
    int readPos = writePos - delayTime_samples;
    if (readPos < 0) readPos += delayBufferSize_samples;
    outValue = min(MAX_16, max(MIN_16, (int)delayBuffer[readPos]));
    if(filtered > 0) {
      if (filtered == 1) {
        outValue = (outValue + outValue + outValue + prevOutValue)>>2; // smooth
      } else if (filtered == 2) {
        outValue = (outValue + prevOutValue)>>1; // smooth
      } else if (filtered == 3) {
        outValue = (outValue + prevOutValue + prevOutValue + prevOutValue)>>2; // smooth
      } else outValue = (outValue + prevOutValue + prevOutValue + prevOutValue +
          prevOutValue + prevOutValue + prevOutValue + prevOutValue)>>3; // smooth
      prevOutValue = outValue;
    }
    return (outValue * delayLevel)>>10;
  }

  /** Read the buffer at the delayTime and increment the read/write index
  * @param inVal The signal input.
  */
  inline
	void write(int inValue) {
    delayBuffer[writePos] = min(MAX_16, max(MIN_16, inValue));
    writePos = (writePos + 1) % delayBufferSize_samples;
  }
};

#endif /* DEL_H_ */
