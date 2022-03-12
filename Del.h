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
  float maxDelayTime_ms;
  unsigned int delayBufferSize_samples;
  bool delayFeedback = 0;

public:
  /** Constructor.
	@param maxDelayTime The size of the delay buffer, in milliseconds.
	*/
	Del(unsigned int maxDelayTime) {
    maxDelayTime_ms = max((unsigned int)0, maxDelayTime);
    delayBufferSize_samples = maxDelayTime_ms * SAMPLE_RATE * 0.001;
    delayBuffer = new int16_t[delayBufferSize_samples];
    for(int i=0; i<delayBufferSize_samples; i++) {
      delayBuffer[i] = 0;
    }
  }

  ~Del() {
    delete[] delayBuffer;
  }

  /** Return the size of the delay buffer in ms */
  float getBufferSize() {
    return maxDelayTime_ms;
  }

  /** Return the delay length in milliseconds */
  float getTime() {
    return delayTime_ms;
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

  /** Specify the delay feedback level, from 0 to 1024 */
  void setLevel(int level) {
    delayLevel = min(1024, max(0, level));
  }

  /** Specify the delay feedback level, from 0 to 1024 */
  void setFeedback(bool state) {
    delayFeedback = state;
  }

  /** Input a value to the delay and retrieve the signal delayed by delayTime milliseconds .
	* @param inVal The signal input.
	*/
	inline
	int16_t next(int inValue) {
    int16_t outValue = 0;
    if (delayTime_samples > 0) {
      outValue = read();
    }
    if (delayFeedback) {
      inValue = (inValue + outValue)>>1;
    }
    write(inValue);
    return outValue;
  }

  /** Read the buffer at the delayTime without incrementing read/write index */
  inline
	int16_t read() {
    int outValue = 0;
    int readPos = writePos - delayTime_samples;
    if (readPos < 0) readPos += delayBufferSize_samples;
    outValue = min(MAX_16, max(MIN_16, (int)delayBuffer[readPos]));
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
