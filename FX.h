/*
 * FX.h
 *
 * A class of various DSP effects
 *
 * by Andrew R. Brown 2021
 *
 * Inspired by the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef FX_H_
#define FX_H_

class FX {

  public:
    /** Constructor. */
    FX() {
      initPluckBuffer();
    }

    /** Wave Folding
    *  Fold clipped values
    *  Pass in a signal and multiply its value beforehand to change fold depth
    */
    inline
    int16_t waveFold(int sample_in) {
      while(abs(sample_in) > MAX_16) {
        if (sample_in > 0) sample_in = MAX_16 - (sample_in - MAX_16);
        if (sample_in < 0) sample_in = -MAX_16 - (sample_in + MAX_16);
      }
      return sample_in;
    }

    /** Clipping
    *  Clip values outside max/min range
    *  Pass in a signal and multiply its value beforehand to change fold depth
    */
    inline
    int16_t clip(int32_t sample_in) {
      if (sample_in > MAX_16) sample_in = MAX_16;
      if (sample_in < -MAX_16) sample_in = -MAX_16;
      return sample_in;
    }

    /** Soft Clipping
    * Distort sound based on input level and depth amount
    * Pass in a signal and depth amount.
    * Output level can vary, so best to mix with original signal.
    * @param sample_in The next sample value
    * @param amount The degree of clipping to be applied - 0 to 400 is a reasonable range
    */
    inline
    int16_t softClip(int32_t sample_in, float amount) { // 14745, 13016
      // int32_t samp = sample_in;
      // if (samp > 26033) samp = min(MAX_16, 26033 + ((samp - 26033) >> 3));
      // if (samp < -26033) samp = max(-MAX_16, -26033 + ((samp + 26033) >> 3));
      // 2/pi * arctan(samp * depth) // 0.635748
      int16_t samp = 20831 * atan(amount * (sample_in / (float)MAX_16)); 
      // int16_t samp = (sample_in / (float)MAX_16) * MAX_16;
      return samp;
    }

    /** Soft Saturation
    *  Effects values above a threshold, adds
    *  Pass in a signal and multiply its value beforehand to change fold depth
    *  https://www.musicdsp.org/en/latest/Effects/42-soft-saturation.html
    */
    inline
    int16_t softSaturation(int32_t sample_in) { // 14745, 13016 // a + (x-a)/(1+((x-a)/(1-a))^2) // f(x)*(1/((a+1)/2))
      int16_t thresh = 26033;
      if (sample_in > thresh) sample_in *= ((float)MAX_16/((thresh+MAX_16)/2.0));
      if (sample_in < -26033) sample_in = (sample_in * -1 * ((float)MAX_16/((thresh+MAX_16)/2.0))) * -1;
      return sample_in;
    }

     /** Karplus Strong fedback model
    *  @audioIn Pass in an oscillator or other signal
    *  @pluckFreq Feedback freq, usually equal to the freq of the incoming osc,
    *  or desired freq of pluck for signal (noise) bursts. In Hz.
    *  @depth The level of feedback. Controls the amount of effect or length of pluck tail. 0.0 - 1.0
    *  Use smaller depth for continuous feedback, and very large depth values for pluck string effect on impulses.
    */
    inline
    int16_t pluck(int16_t audioIn, float pluckFreq, float depth) {
      // read
//      float read_index_fractional = SAMPLE_RATE / pluckFreq;
      int pluck_buffer_read_index = pluck_buffer_write_index - SAMPLE_RATE / pluckFreq + 1;
      if (pluck_buffer_read_index < 0) pluck_buffer_read_index += PLUCK_BUFFER_SIZE;
      int bufferRead = pluckBuffer[pluck_buffer_read_index] * depth;
      // update buffer
      int16_t output = audioIn + bufferRead;
      pluckBuffer[(int)pluck_buffer_write_index] = output; // divide?
      int16_t aveOut = (output + prevPluckOutput) / 2;
      prevPluckOutput = aveOut;
      // increment buffer phase
      pluck_buffer_write_index += 1;
      if (pluck_buffer_write_index > PLUCK_BUFFER_SIZE) pluck_buffer_write_index -= PLUCK_BUFFER_SIZE;
      // send output
      return aveOut;
    }

  private:
    const static int16_t PLUCK_BUFFER_SIZE = 500;
    int pluckBuffer [PLUCK_BUFFER_SIZE];
    float pluck_buffer_write_index = 0;
    int prevPluckOutput = 0;

    void initPluckBuffer() {
      for(int i=0; i<PLUCK_BUFFER_SIZE; i++) {
        pluckBuffer[i] = 0;
      }
    }
};

#endif /* FX_H_ */
