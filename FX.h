/*
 * FX.h
 *
 * A class of various DSP effects
 *
 * by Andrew R. Brown 2021
 *
 * This file is part of the M16 audio library 
 * Inspired by the Mozzi audio library by Tim Barrass 2012
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef FX_H_
#define FX_H_

#include "Del.h"
#include "APF.h"
#include "Osc.h"

class FX {

  public:
    /** Constructor. */
    FX() {}

    /** Wave Folding
    *  Fold clipped values
    *  Pass in a signal and an amount to multiply its value by
    * @param sample_in The next sample value
    * @param amount The degree of amplifcation which is then folded; 1.0 +
    */
    inline
    int16_t waveFold(int32_t sample_in, float amount) {
      if (amount != 1.0f) sample_in *= amount;
      while(abs(sample_in) > MAX_16) {
        if (sample_in > 0) sample_in = MAX_16 - (sample_in - MAX_16);
        if (sample_in < 0) sample_in = -MAX_16 - (sample_in + MAX_16);
      }
      return clip(sample_in);
    }

    /** Clipping
    *  Clip values outside 16 bit values for max/min range
    *  Same as clip16 function in M16
    *  @sample_in Pass in a signal, such as output from an oscillator
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
    * @param amount The degree of clipping to be applied - 1 to 400 is a reasonable range
    * From amount of 1.0 (neutral) upward to about 2.0 for some compression to
    * about 3.0 for mild drive, to about 4 - 6 for noticiable overdrive, to 7 - 10 for distortion
    */
    inline
    int16_t softClip(int32_t sample_in, float amount) {
      // int32_t samp = sample_in;
      // if (samp > 26033) samp = min(MAX_16, 26033 + ((samp - 26033) >> 3));
      // if (samp < -26033) samp = max(-MAX_16, -26033 + ((samp + 26033) >> 3));
      // 2/pi * arctan(samp * depth) // 0.635748
      int32_t samp = 38000 * atan(amount * (sample_in * (float)MAX_16_INV)); // 20831
      // int16_t samp = (sample_in / (float)MAX_16) * MAX_16;
      // if (samp > MAX_16 || samp < MIN_16) Serial.println(samp);
      return clip(samp);
    }

    /** Soft Saturation
    *  Effects values above a threshold, adds
    *  Pass in a signal and multiply its value beforehand to change fold depth
    *  https://www.musicdsp.org/en/latest/Effects/42-soft-saturation.html
    */
    inline
    int16_t softSaturation(int32_t sample_in) { // 14745, 13016 // a + (x-a)/(1+((x-a)/(1-a))^2) // f(x)*(1/((a+1)/2))
      int16_t thresh = 26033;
      if (sample_in > thresh) sample_in *= ((float)MAX_16 / ((thresh + MAX_16) * 0.5f));
      if (sample_in < -26033) sample_in = (sample_in * -1 * ((float)MAX_16 / ((thresh + MAX_16) * 0.5f))) * -1;
      return clip(sample_in);
    }

    /** Compressor with gain compensation
    *  @param sample is the input sample value
    *  @param threshold is the threshold value between 0.0 and 1.0
    *  @param ratio is the ratio value, > 1.0, typically 2 to 4
    */
    inline
    int16_t compression(int32_t sample, float threshold, float ratio) {
      int16_t thresh = threshold * MAX_16;
      float invRatio = 1.0f / ratio;
      float gainCompensationRatio = 1.0f + (1.0f - threshold * (1.0f + 1.0f * invRatio));
      if (sample >= thresh || sample <= -thresh) {
          int32_t compressed_sample;
          if (sample > 0) {
              compressed_sample = (int32_t)((sample - thresh) * invRatio + thresh);
              if (compressed_sample > MAX_16) compressed_sample = MAX_16;
          } else {
              compressed_sample = (int32_t)((sample + thresh) * invRatio - thresh);
              if (compressed_sample < MIN_16) compressed_sample = MIN_16;
          }
          return compressed_sample * gainCompensationRatio;
      }
      return sample * gainCompensationRatio;
    }    

    /** Update the wave shaping table
  	* @param TABLE_NAME is the name of the array. Filled with 16bit values.
    * @param tableSize the length in samples, typically a power of 2.
    * Size of TABLE_SIZE is suggested, smaller or larger bit sizes will reduce or increase resolution.
  	*/
    inline
    void setShapeTable(int16_t * TABLE_NAME, int tableSize) {
      delete[] shapeTable; // remove any previous memory allocation
      shapeTableSize = tableSize;
      waveShaperStepInc = 65537.0 / shapeTableSize;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      shapeTable = TABLE_NAME;
    }

    /** Wave Shaper
    * Distorts wave input by wave shaping function
    * Shaping wave is, like osc wavetables, WAVE_TABLE wide from MIN_16 to MAX_16 values
    * @sample_in is the input sample value from the carrier wave
    * @amount is the degree of distortion, from 0.0 to 1.0
    */
    inline
    int16_t waveShaper(int16_t sample_in, float amount) {
      int index = sample_in;
      if (shapeTableSize > 0) index = (sample_in + MAX_16) / waveShaperStepInc;
      int16_t sampVal = shapeTable[index];
      if (amount >= 0 && amount < 1.0) sampVal = (sampVal * amount) + (sample_in * (1.0 - amount));
      return sampVal;
    }

    /** Create a dedicated soft clip wave shaper
    *  Distorts wave input by wave shaping function
    * @amount is the degree of distortion, from 1.0 being minimal to ~10.0 being a lot
    * It is not efficient to update this in real time,
    * if required, then manage wave shape function in your main code
    */
    inline
    void setShapeTableSoftClip(float amount) {
      delete[] shapeTable; // remove any previous memory allocation
      shapeTableSize = TABLE_SIZE;
      waveShaperStepInc = 65537.0 / shapeTableSize;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      for(int i=0; i<shapeTableSize; i++) {
        shapeTable[i] = 20813.0 * atan(amount * ((MIN_16 + i * waveShaperStepInc) * (float)MAX_16_INV));
      }
    }

    /** Create a dedicated s-wave wave shaper
    *  Distorts wave input by the wave shaping function
    * @amount is the degree of distortion, from 0.0 to 1.0
    * Smaller values for amount may require gain increase compensation
    * It is not efficient to update this in real time,
    * if required, then manage wave shape function in your main code
    */
    inline
    void setShapeTableSigmoidCurve(float amount) {
      delete[] shapeTable; // remove any previous memory allocation
      shapeTableSize = TABLE_SIZE;
      waveShaperStepInc = 65537.0 / shapeTableSize;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      float tabInc = 1.0 / shapeTableSize * 2;
      for(int i=0; i<shapeTableSize * 0.5f; i++) {
        float sVal = pow(i * tabInc, amount);
        shapeTable[i] = sVal * MAX_16 - MAX_16;
        shapeTable[TABLE_SIZE - i] = MAX_16 - sVal * MAX_16;
      }
    }

    /** Create a randomly varied wave shaper
    *  Distorts wave input by the wave shaping function
    * @amount is the degree of distortion, from 0 to MAX_16;
    * Because random values are static and looped, the sound is grainy rather than noisy.
    */
    inline
    void setShapeTableJitter(float amount) {
      delete[] shapeTable; // remove any previous memory allocation
      shapeTableSize = TABLE_SIZE;
      waveShaperStepInc = 65537.0 / shapeTableSize;
      shapeTable = new int16_t[shapeTableSize]; // create a new waveshape table
      for(int i=0; i<shapeTableSize; i++) {
        shapeTable[i] = waveShaperStepInc * i + rand(amount * 2) - amount;
      }
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
      if (!pluckBufferEstablished) initPluckBuffer();
      // read
      //float read_index_fractional = SAMPLE_RATE / pluckFreq;
      int pluck_buffer_read_index = pluck_buffer_write_index - SAMPLE_RATE / pluckFreq + 1;
      if (pluck_buffer_read_index < 0) pluck_buffer_read_index += PLUCK_BUFFER_SIZE;
      int bufferRead = pluckBuffer[pluck_buffer_read_index] * depth;
      // update buffer
      int16_t output = audioIn + bufferRead;
      pluckBuffer[(int)pluck_buffer_write_index] = output; // divide?
      int16_t aveOut = (output + prevPluckOutput) * 0.5f;
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
    int16_t reverb(int32_t audioIn) {
      // set up first time called
      if (!reverbInitiated) {
        initReverb(reverbSize);
      }
      processReverb(audioIn, audioIn);
      return clip(((audioIn * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>12) + ((revP2 * reverbMix)>>12));
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
    void reverbStereo(int32_t audioInLeft, int32_t audioInRight, int16_t &audioOutLeft, int16_t &audioOutRight) {
      // set up first time called
      if (!reverbInitiated) {
        initReverb(reverbSize);
      }
      processReverb(clip(audioInLeft), clip(audioInRight));
      // processReverb(apf1.next(audioInLeft), apf2.next(audioInRight));
      audioOutLeft = clip(((audioInLeft * (1024 - reverbMix))>>10) + ((revP1 * reverbMix)>>11));
      audioOutRight = clip(((audioInRight * (1024 - reverbMix))>>10) + ((revP2 * reverbMix)>>11));
    }

    /** Set the reverb length
    * @rLen The amount of feedback that effects reverb decay time. Values from 0.0 to 1.0.
    */
    inline
    void setReverbLength(float rLen) {
      rLen = max(0.0f, min(1.0f, rLen));
      reverbFeedbackLevel = pow(rLen, 0.2f);
      initReverb();
    }

    /** Set the reverb amount
    * @rMix The balance between dry and wet signal, as the amount of wet signal, from 0.0 to 1.0.
    */
    inline
    void setReverbMix(float rMix) {
      reverbMix = max(0, min(1024, (int)(rMix * 1204.0f)));
      // Serial.print("reverbMix ");Serial.println(reverbMix);
    }

    /** Set the reverb memory size
    * @newSize A multiple of the default, >= 1.0
    * Larger sizes take up more memory
    */
    inline
    void setReverbSize(float newSize) {
      reverbSize = max(1.0f, newSize);
      initReverb(reverbSize);
    }

    /** A mono chorus using a modulated delay line.
     * Based on Dattorro, Jon. 1997. Effect design, part 2: Delay line modulation and chorus. 
     * Journal of the Audio Engineering Society 45(10), 764–788.
    * @audioIn A mono audio signal
    */
    inline
    int16_t chorus(int32_t audioIn) {
      // set up first time called
      if (!chorusInitiated) {
        initChorus();
      }
      float chorusLfoVal = chorusLfo.next() * MAX_16_INV;
      chorusDelay.setTime(chorusDelayTime + chorusLfoVal * chorusLfoWidth);
      int32_t delVal = chorusDelay.next(audioIn);
      int32_t inVal = (audioIn * (chorusMixInput))>>10;
      delVal = (delVal * chorusMixDelay)>>10;
      return clip(inVal + delVal);
    }

    /** A stereo chorus using two modulated delay lines.
    * @audioInLeft An audio signal
    * @audioInRight An audio signal
    * @audioOutLeft A variable to receive audio output for the left channel
    * @audioOutLeft A variable to receive audio output for the right channel
    */
    inline
    void chorusStereo(int32_t audioInLeft, int32_t audioInRight, int16_t &audioOutLeft, int16_t &audioOutRight) {
      // set up first time called
      if (!chorusInitiated) {
        initChorus();
      }
      float chorusLfoVal = chorusLfo.next() * MAX_16_INV;
      chorusDelay.setTime(chorusDelayTime + chorusLfoVal * chorusLfoWidth);
      chorusDelay2.setTime(chorusDelayTime2 + chorusLfoVal * chorusLfoWidth);
      int32_t delVal = chorusDelay.next(audioInLeft);
      int32_t delVal2 = chorusDelay2.next(audioInRight);
      int32_t inVal = (audioInLeft * (chorusMixInput))>>10;
      int32_t inVal2 = (audioInRight * (chorusMixInput))>>10;
      delVal = (delVal * chorusMixDelay)>>10;
      delVal2 = (delVal2 * chorusMixDelay)>>10;
      audioOutLeft = clip(inVal + delVal);
      audioOutRight = clip(inVal2 + delVal2);
    }

    /** Set the chorus effect depth
    * @depth amount of chorus to add to the input signal, 0.0 to 1.0
    */
    inline
    void setChorusDepth(float depth) {
      depth = pow(depth, 0.8) * 0.5;
      chorusMixInput = panLeft(depth) * 1024;
      chorusMixDelay = panRight(depth) * 1024;
    }

    /** Set the chorus width
    * @depth pitch width of chorus LFO, 0.0 to 1.0
    */
    inline
    void setChorusWidth(float depth) {
      chorusLfoWidth = pow(max(0.0f, depth), 1.5) * 3.0d;
    }

    /** Set the chorus rate
    * @rate pitch frequency of chorus LFO, in hertz. Typically 0.01 to 3.0 
    */
    inline
    void setChorusRate(float rate) {
      chorusLfoRate = rate;
      chorusLfo.setFreq(chorusLfoRate);
    }

    /** Set the chorus feedback level
    * @val feedback level of the chorus delay line, from 0.0 to 1.0 
    */
    inline
    void setChorusFeedback(float val) {
      chorusFeedback = val;
      chorusDelay.setFeedback(true);
      chorusDelay.setFeedbackLevel(max(0.0f, min(1.0f, chorusFeedback)));
      chorusDelay2.setFeedback(true);
      chorusDelay2.setFeedbackLevel(max(0.0f, min(1.0f, chorusFeedback)));
    }

    /** Set the chorus delay time
    * @time delay time in ms, typically 20 to 40 ms
    */
    inline
    void setChorusDelayTime(float time) {
      chorusDelayTime = min(40.0f, max(0.0f, time));
      chorusDelayTime2 = min(40.0f, max(0.0f, time * 0.74f));
    }


  private:
    const static int16_t PLUCK_BUFFER_SIZE = 500;
    int * pluckBuffer; // [PLUCK_BUFFER_SIZE];
    float pluck_buffer_write_index = 0;
    int prevPluckOutput = 0;
    bool pluckBufferEstablished = false;
    bool reverbInitiated = false;
    float reverbFeedbackLevel = 0.980; // 0.0 to 1.0
    int reverbMix = 270; // 0 to 1024
    float reverbSize = 1.0; // >= 1, memory allocated to delay lengths
    // float reverbTime = 0.49999; // 0 to 0.5
    Del delay1, delay2, delay3, delay4;
    APF apf1, apf2, apf3, apf4;
    // APF apf1 = APF(0.4494, 0.9);
    // APF apf2 = APF(0.7214, 0.9);
    // APF apf3 = APF(3.875, 0.9);
    // APF apf4 = APF(11.125, 0.9);
    int32_t revD1, revD2, revD3, revD4, revP1, revP2, revP3, revP4, revP5, revP6, revM3, revM4, revM5, revM6;
    int16_t * shapeTable;
    int shapeTableSize = 0;
    float waveShaperStepInc = MAX_16 * 2.0 * TABLE_SIZE_INV;
    bool chorusInitiated = false;
    int chorusDelayTime = 38;
    int chorusDelayTime2 = 28;
    float chorusLfoRate = 0.65; // hz
    float chorusLfoWidth = 0.5; // ms
    int chorusMixInput = 600; // 0 - 1024
    int chorusMixDelay = 800; // 0 - 1024
    float chorusFeedback = 0.4; // 0.0 to 1.0
    int16_t * chorusLfoTable;
    Osc chorusLfo;
    Del chorusDelay, chorusDelay2;

    void initPluckBuffer() {
      pluckBuffer = new int[PLUCK_BUFFER_SIZE]; // create a new array
      for(int i=0; i<PLUCK_BUFFER_SIZE; i++) {
        pluckBuffer[i] = 0;
      }
      pluckBufferEstablished = true;
    }

    /** Set the reverb params */
    void initReverb() {
      initReverb(reverbSize);
    }

    /** Set the reverb params
    * @size Bigger sizes increase delay line times for better quality but use more memory (x2, x3, x4, etc.)
    */
    void initReverb(float size) { // 1/8 of Pd values
      delay1.setMaxDelayTime(8 * size); delay2.setMaxDelayTime(9 * size);
      delay3.setMaxDelayTime(11 * size); delay4.setMaxDelayTime(13 * size);
      delay1.setTime(7.5 * size); delay1.setLevel(reverbFeedbackLevel); delay1.setFeedback(true);
      delay2.setTime(8.993 * size); delay2.setLevel(reverbFeedbackLevel); delay2.setFeedback(true);
      delay3.setTime(10.844 * size); delay3.setLevel(reverbFeedbackLevel); delay3.setFeedback(true);
      delay4.setTime(12.118 * size); delay4.setLevel(reverbFeedbackLevel); delay4.setFeedback(true);
      apf1.setTime(0.4494 * size); apf1.setPhase(0.1); // apf1.setLevel(0.8);
      apf3.setTime(0.5964 * size); apf3.setPhase(0.3); // apf3.setLevel(0.8);
      apf4.setTime(3.875 * size);  apf4.setPhase(0.4); // apf4.setLevel(0.8);
      // apf4.setTime(11.125 * size); apf4.setLevel(0.95);
      // apf1.setTime(3.857 * size); apf1.setLevel(0.9);
      // apf2.setTime(11.125 * size); apf2.setLevel(0.9);
      apf2.setTime(2.875 * size); apf2.setPhase(0.2); // apf2.setLevel(0.8);
      // apf4.setTime(7.5 * size); apf4.setLevel(0.9);
      reverbInitiated = true;
    }

    /** Compute reverb */
    void processReverb(int16_t audioInLeft, int16_t audioInRight) {
      revD1 = delay1.read(); revD2 = delay2.read();
      // revD1 = apf1.next(delay1.read()); revD2 = apf2.next(delay2.read());
      // revD1 = apf1.next(delay1.read()); revD2 = delay2.read(); 
      revD3 = delay3.read(); revD4 = delay4.read();
      // revD3 = apf3.next(delay3.read()); revD4 = apf4.next(delay4.read());
      // revD3 = apf3.next(delay3.read()); revD4 = delay4.read();
      revP1 = audioInLeft + revD1; revP2 = audioInRight + revD2;
      // revP1 = apf1.next(audioInLeft) + revD1; revP2 = apf2.next(audioInRight) + revD2;
      revP3 = (revP1 + revP2); revM3 = (revP1 - revP2); revP4 = (revD3 + revD4); revM4 = (revD3 - revD4);
      revP5 = (revP3 + revP4)>>1; revP6 = (revM3 + revM4)>>1; revM5 = (revP3 - revP4)>>1; revM6 = (revM3 - revM4)>>1;
      // revP5 = (revP3 + revP4); revP6 = (revM3 + revM4); revM5 = (revP3 - revP4); revM6 = (revM3 - revM4);
      delay1.write(revP5); delay2.write(revP6); delay3.write(revM5); delay4.write(revM6);
      // delay1.write(revP5 * reverbTime); delay2.write(revP6 * reverbTime); delay3.write(revM5 * reverbTime); delay4.write(revM6 * reverbTime);
    }

    void initChorus() {
      chorusLfoTable = new int16_t[TABLE_SIZE]; // create a new array
      Osc::sinGen(chorusLfoTable); // fill the wavetable
      chorusLfo.setTable(chorusLfoTable);
      chorusLfo.setFreq(chorusLfoRate);
      chorusDelay.setMaxDelayTime(chorusDelayTime + 3);
      chorusDelay2.setMaxDelayTime(chorusDelayTime2 + 3);
      setChorusFeedback(chorusFeedback);
      chorusInitiated = true;
    }
};

#endif /* FX_H_ */
