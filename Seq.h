/*
 * Seq.h
 *
 * Monophonic sequencer class. Sequences of integers
 *
 * by Andrew R. Brown 2021
 *
 * This file is part of the M16 audio library
 * Inspired by the Mozzi audio library by Tim Barrass 2012
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef Seq_H_
#define Seq_H_

class Seq {

  public:
    /** Constructor. */
    Seq() {
      seqValues = new int[seqMaxSize];
      for(int i=0; i<seqMaxSize; i++) {
        seqValues[i] = 0;
      }
    }

     /** Constructor.
    * @param VALUES the array of integers the sequencer will start with
    * @param NUMBER_VALUES the max size for the sequence
    */
    Seq(int * VALUES, int NUMBER_VALUES, int STEP_DIVISION): seqValues(VALUES), seqSize(NUMBER_VALUES), stepDiv(STEP_DIVISION) {}

    /** Add values to the sequence starting at a specified index
    * Make sure the seq has been initiated before calling
    * @param values An array of ints
    * @param size The number of items in the array
    * @param start The index from which to write data.
    */
    inline
    void setValues(int * values, int size, int start) {
      if (size + start <= seqMaxSize) {
        for(int i=start; i<size+start; i++) {
        	seqValues[i] = values[i];
        }
      }
    }

    /** Add values to the sequence starting at the beginning
    * @param seq An array of ints
    * @param size The number of items in the array
    * It can be best to manage seq contents outside this class in the referenced array
    */
    inline
    void setSequence(int * seq, int size) {
      seqValues = seq;
      seqSize = size;
    }

    /** Update the specified sequence step
    * @param index The seq step to update
    * @param val The new value for that step
    */
    inline
    void setStepValue(int index, int val) {
      if (index >=0 && index <= seqMaxSize) seqValues[index] = val;
    }

    /** Retrieve the specified sequence step value
    * @param index The seq step to get
    */
    inline
    int getStepValue(int index) {
      if (index >=0 && index <= seqMaxSize) {
        return seqValues[index];
      } else return seqValues[0];
    }

    /** Set all seq values to zero.
  	*/
    inline
  	void empty() {
      for (int i=0; i<seqMaxSize; i++) {
        seqValues[i] = 0;
      }
  	}

    /** Return the next sequence value
    * Loop around to the start if at the end of the sequence
    */
    inline
    int next() {
      int nextValue = seqValues[seqIndex];
      if (randomMode) {
        seqIndex = rand(seqSize);
      } else {
        seqIndex += 1;
        if (seqIndex >= seqSize) seqIndex = 0;
      }
      return nextValue;
    }

    /** Turn random seq on or off
    * @param state Either true or false
    */
    inline
    void setRandom(bool state) {
    		randomMode = state;
    }

    /** Return the prev sequence value */
    inline
    int again() {
      // int nextValue = seqValues[seqIndex];
      return seqValues[seqIndex];
    }

     /** Return the next sequence value and advance the seq index
     * @ jumpSize The amount to advance the sequence index by, modulo the seq length
     */
    inline
    int skip(int jumpSize) {
      int nextValue = seqValues[seqIndex];
      if (jumpSize > 0) {
      	seqIndex = (seqIndex + jumpSize) % seqSize;
			}
      return nextValue;
    }

    /** Reset the sequence back to the first step*/
    inline
    void start() {
      seqIndex = 0;
    }

    /** Set the sequence to the specified step*/
    inline
    void setToStep(int newStep) {
      if( newStep >= 0 && newStep < seqMaxSize) {
        seqIndex = newStep;
      }
    }

    /** Return the current sequence step*/
    inline
    int getCurrStep() {
      return seqIndex;
    }

    /** Update the base step subdivision */
    inline
    void setStepDiv(int newDiv) {
      if (newDiv > 0) stepDiv = newDiv;
    }

    /** Return the current base step subdivision */
    inline
    int getStepDiv() {
      return stepDiv;
    }

    /** Return the number of milliseconds between steps
    * at a particular beats per minute
    * divided by the number of slices (subdivisions) of the BPM
    * @param bpm The tempo in beats per minute
    * @param slice The number of BPM subdivisions, > 0
    * @param div The division of the BPM to use for a step
    * This is a class method, e.g. Seq::calcStepDelta(110, 4);
    */
   inline
   static double calcStepDelta(float bpm, int slice, int div) {
     if (bpm > 0 && slice > 0) {
       return 60.0 / bpm * 1000 / slice / div;
     } else return 250;
   }

     /** Return the number of milliseconds between steps
     * at a particular beats per minute
     * divided by the number of slices (subdivisions) of the BPM
     * @param bpm The tempo in beats per minute
     * @param slice The number of BPM subdivisions, > 0
     */
    inline
    double calcStepDelta(float bpm, int slice) {
			if (bpm > 0 && slice > 0) {
				return 60.0 / bpm * 1000 / slice / stepDiv;
			} else return 250;
		}

    /** Return the number of milliseconds between steps
    * at a particular beats per minute
    * @param bpm The tempo in beats per minute
    */
    inline
    double calcStepDelta(float bpm) {
      // calcStepDelta(bpm, 1);
      if (bpm > 0) {
				return 60.0 / bpm * 1000 / 1 / stepDiv;
			} else return 250;
    }

    /** Update the current BPM and
    * Return the number of milliseconds between steps
    * @param bpm The new tempo in beats per minute
    */
    inline
    double setTempo(float bpm) {
      double stepD;
      if (bpm > 0) {
        seqBPM = bpm;
        stepD = calcStepDelta(bpm, sliceVal);
      }
      return stepD;
    }

    /** Update the seq length
    * @param val The maximum number of steps in the sequence
    */
    inline void setMaxSize(int val) {
      if (val > 0) seqMaxSize = val;
    }

    /** Update the seq length
    * @param val The number of steps in the sequence
    */
    inline void setSize(int val) {
      if (val > 0) seqSize = val;
    }

    /** Fill sequence with a euclidean rhythm
    * @param value The number to put into each euclidean step
    * @param hits The number of onsets in the generated pattern (up to seq length)
    * @param rotate The number of steps to rotate the pattern (up to seq length - 1)
    */
    inline
    void euclideanGen(int value, int hits, int rotate) {
      hits = max(0, min(seqSize, hits));
      rotate = max(0, min(seqSize-1, rotate));
      for (int i=0; i<seqSize; i++) {
        if ((((i + rotate) * hits) % seqSize) < hits) {
          setStepValue(i, value);
        } else {
          setStepValue(i, 0);
        }
      }
    }

    /** Fill sequence with a euclidean rhythm
     * @param startVal The starting value for the sequence
     * @param maxDev The maximum amount to deviate from the previous value
     * @param minVal The minimum value for the sequence
     * @param maxVal The maximum value for the sequence
     */
    inline
    void randWalkGen(int startVal, int maxDev, int minVal, int maxVal) {
      int currVal = startVal;
      for (int i=0; i<seqMaxSize; i++) {
        setStepValue(i, currVal);
        currVal += rand(maxDev*2) - maxDev;
        currVal = max(minVal, min(maxVal, currVal));
      }
    }

  private:
    int * seqValues;
    int seqMaxSize = 16; // 0 - 1024
    int seqSize = 16; // 0 - 1024
    int stepDiv = 4;
    int seqIndex = 0;
    int sliceVal = 1;
    bool randomMode = false;
    float seqBPM = 120;

};

#endif /* Seq_H_ */
