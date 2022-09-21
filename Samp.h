/*
 * Samp.h
 *
 * A sample wavetable playback class
 *
 * by Andrew R. Brown 2021
 *
 * Mostly identical to the Sample class in the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef SAMP_H_
#define SAMP_H_

class Samp {

public:
  /** Constructor.
  * @param TABLE_NAME the name of the array with sample data in it.
  * @param TABLE_SIZE the number of samples in the table.
  */
  Samp(const int16_t * TABLE_NAME, unsigned long TABLE_SIZE):table(TABLE_NAME),table_size((unsigned long) TABLE_SIZE) {
    setLoopingOff();
    endpos_fractional = table_size;
    startpos_fractional = 0;
  }

  /** Change the sound table which will be played by Samp.
  * @param TABLE_NAME is the name of the array you're using.
  */
  inline
  void setTable(const int16_t * TABLE_NAME) {
    table = TABLE_NAME;
  }

  /** Sets the starting position in samples.
  * @param startpos is the offset position in samples.
  */
  inline
  void setStart(unsigned int startpos)
  {
    startpos_fractional = (unsigned long) startpos;
  }

  /** Resets the phase (the playhead) to the start position,
  * which will be 0 unless set to another value with setStart();
  */
  inline
  void start() {
    phase_fractional = startpos_fractional;
  }

  /** Sets a new start position plays the sample from that position.
  * @param startpos position in samples from the beginning of the sound.
  */
  inline
  void start(unsigned int startpos) {
    setStart(startpos);
    start();
  }

  /** Sets the end position in samples from the beginning of the sound.
  * @param end position in samples.
  */
  inline
  void setEnd(unsigned int end) {
    endpos_fractional = (unsigned long) end;
  }

  /** Turns looping on.*/
  inline
  void setLoopingOn() {
    looping = true;
  }

  /** Turns looping off.*/
  inline
  void setLoopingOff() {
    looping = false;
  }

  /** Returns the sample at the current phase position. */
  inline
  int16_t next() {
    if (phase_fractional>endpos_fractional){
      if (looping) {
        phase_fractional = startpos_fractional + (phase_fractional - endpos_fractional);
      } else {
        return 0;
      }
    }
    int16_t out = table[(int)phase_fractional];
    incrementPhase();
    return out;
  }

  /** Checks if the sample is playing by seeing if the phase is within the limits of its end position.*/
  inline
  boolean isPlaying() {
    return phase_fractional < endpos_fractional;
  }

  /** Set the sample frequency.
  * @param frequency to play the wave table.
  */
  inline
  void setFreq(float frequency) {
    phase_increment_fractional = (unsigned long)((float)table_size * frequency * SAMPLE_RATE_INV);
  }

  /**  Returns the sample at the given table index.
  * @param index between 0 and the table size.
  */
  inline
  int16_t atIndex(unsigned int index) {
    return table[index];
  }

  /** Set a specific phase increment.
  * @param phaseinc_fractional A phase increment value.
   */
  inline
  void setPhaseInc(unsigned long phaseinc_fractional) {
    phase_increment_fractional = phaseinc_fractional;
  }

private:
  /** Increments the phase of the oscillator without returning a sample. */
  inline
  void incrementPhase() {
    phase_fractional += phase_increment_fractional;
  }

  volatile unsigned long phase_fractional;
  volatile unsigned long phase_increment_fractional;
  const int16_t * table;
  bool looping;
  unsigned long startpos_fractional, endpos_fractional, table_size;
};

#endif /* SAMP_H_ */
