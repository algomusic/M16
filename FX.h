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

#include "Del.h"

class FX {

  public:
    /** Constructor. */
    FX() {
      initPluckBuffer();
    }

    /** Wave Folding
    *  Fold clipped values
    *  Pass in a signal and an amount to multiply its value by
    * @param sample_in The next sample value
    * @param amount The degree of amplifcation with is then folded; 1.0 +
    */
    inline
    int16_t waveFold(int sample_in, float amount) {
      sample_in *= amount;
      while(abs(sample_in) > MAX_16) {
        if (sample_in > 0) sample_in = MAX_16 - (sample_in - MAX_16);
        if (sample_in < 0) sample_in = -MAX_16 - (sample_in + MAX_16);
      }
      return sample_in;
    }

    /** Clipping
    *  Clip values outside max/min range
    *  Pass in a signal
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

    /** A simple reverb using recursive delay lines.
    * Mono version that sums left and right channels
    * @audioIn A mono audio signal
    * Inspired by reverb example G08 in Pure Data.
    */
    inline
    int16_t reverb(int16_t audioIn) {
      // set up first time called
      if (!reverbInitiated) {
        initReverb();
      }
      processReverb(audioIn, audioIn);
      return ((audioIn * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>11) + ((revP2 * reverbMix)>>11);
    }

    /** A simple reverb using recursive delay lines.
    * Stereo version that takes two inputs (can be the same) and sets left and right channel outs
    * @audioInLeft A mono audio signal
    * @audioInRight A mono audio signal
    * @audioOutLeft A mono audio destination variable for the left channel
    * @audioOutRight A mono audio destination variable for the right channel
    * Inspired by reverb example G08 in Pure Data.
    */
    inline
    void reverbStereo(int16_t audioInLeft, int16_t audioInRight, int16_t &audioOutLeft, int16_t &audioOutRight) {
      // set up first time called
      if (!reverbInitiated) {
        initReverb();
      }
      processReverb(audioInLeft, audioInRight);
      audioOutLeft = ((audioInLeft * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>10);
      audioOutRight = ((audioInRight * (1024 - reverbMix))>>10) + ((revP2 * reverbMix)>>10);
    }

    /** Set the reverb length
    * @rLen The amount of feedback that effects reverb decay time. Values from 0 to 1024.
    */
    inline
    void setReverbLength(int rLen) {
      reverbLength = max(0, min(1024, rLen));
    }

    /** Set the reverb amount
    * @rMix The balance between dry and wet signal. Amount of wet signal, from 0 to 1024.
    */
    inline
    void setReverbMix(int rMix) {
      reverbMix = max(0, min(1024, rMix));
    }

  private:
    const static int16_t PLUCK_BUFFER_SIZE = 500;
    int pluckBuffer [PLUCK_BUFFER_SIZE];
    float pluck_buffer_write_index = 0;
    int prevPluckOutput = 0;
    bool reverbInitiated = false;
    int reverbLength = 900;
    int reverbMix = 400; // use wet only by default?
    Del delay1, delay2, delay3, delay4;
    int32_t revD1, revD2, revD3, revD4, revP1, revP2, revP3, revP4, revP5, revP6, revM3, revM4, revM5, revM6;

    void initPluckBuffer() {
      for(int i=0; i<PLUCK_BUFFER_SIZE; i++) {
        pluckBuffer[i] = 0;
      }
    }

    void initReverb() {
      delay1.setMaxDelayTime(61); delay2.setMaxDelayTime(72);
      delay3.setMaxDelayTime(89); delay4.setMaxDelayTime(97);
      delay1.setTime(60); delay1.setLevel(reverbLength); delay1.setFeedback(true);
      delay2.setTime(71.9435); delay2.setLevel(reverbLength); delay2.setFeedback(true);
      delay3.setTime(86.754); delay3.setLevel(reverbLength); delay3.setFeedback(true);
      delay4.setTime(96.945); delay4.setLevel(reverbLength); delay4.setFeedback(true);
      reverbInitiated = true;
    }

    void processReverb(int16_t audioInLeft, int16_t audioInRight) {
      revD1 = delay1.read(); revD2 = delay2.read(); revD3 = delay3.read(); revD4 = delay4.read();
      revP1 = audioInLeft + revD1; revP2 = audioInRight + revD2;
      revP3 = (revP1 + revP2); revM3 = (revP1 - revP2); revP4 = (revD3 + revD4); revM4 = (revD3 - revD4);
      revP5 = (revP3 + revP4)>>1; revP6 = (revM3 + revM4)>>1; revM5 = (revP3 - revP4)>>1; revM6 = (revM3 - revM4)>>1;
      delay1.write(revP5); delay2.write(revP6); delay3.write(revM5); delay4.write(revM6);
    }
};

#endif /* FX_H_ */
