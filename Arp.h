/*
 * Arp.h
 *
 * Arppegiator class. Integer based for MIDI note numbers.
 *
 * by Andrew R. Brown 2021
 *
 * Inspired by the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef Arp_H_
#define Arp_H_

#define ARP_ORDER 0
#define ARP_UP 1
#define ARP_UP_DOWN 2
#define ARP_DOWN 3
#define ARP_RANDOM 4

class Arp {

  public:
    /** Constructor. */
    Arp() {}

    /** Constructor.
    * @param values the array of values (MIDI values usually) the arpeggiator will be using, max of 12 values.
    * @param number_values the max size for the arpeggiator up to a maximum of 12.
    * @param octaves the base range for the arpeggiator
    * @param arp_direction the playback order
    */
    Arp(int * values, int number_values, int octaves, int arp_direction):arpSize(number_values), octaveRange(octaves), arpDirection(arp_direction) {
      for (int i=0; i<arpSize; i++) {
          initValues[i] = values[i];
          sortedValues[i] = values[i];
      }
      sort(sortedValues, arpSize);
      if (arpDirection == ARP_DOWN) {
        arpIndex = arpSize-1;
        currOctave = octaveRange - 1;
        upDownDirection == ARP_UP;
      }
    }

    /* Reset and restart the arpeggiator values*/
    inline
    void start() {
      if (arpDirection == ARP_DOWN) {
        arpIndex = arpSize-1;
        currOctave = octaveRange - 1;
        upDownDirection == ARP_UP;
      } else {
        arpIndex = 0;
        currOctave = 0;
      }
    }

    /* Return the next arpeggitor value */
    inline
    int next() {
      int nextValue;
      // in order
      if (arpDirection == ARP_ORDER) {
        nextValue = initValues[arpIndex++] + (currOctave * 12);
        updateUpIndex();
        return nextValue;
      }
      if (arpDirection == ARP_UP) {
        nextValue = sortedValues[arpIndex++] + (currOctave * 12);
        updateUpIndex();
        return nextValue;
      }
      if (arpDirection == ARP_UP_DOWN) {
        if (upDownDirection == ARP_UP) {
          nextValue = sortedValues[arpIndex++] + (currOctave * 12);
          if (arpIndex >= arpSize) {
            if (currOctave >= octaveRange - 1) {
              upDownDirection = ARP_DOWN;
              // Serial.println("down");
              arpIndex = max(0, arpSize-2);
              currOctave = octaveRange - 1;
            } else {
              currOctave++;
              arpIndex = 0;
            }
          }
        } else if (upDownDirection == ARP_DOWN) {
          nextValue = sortedValues[arpIndex--] + (currOctave * 12);
          if (arpIndex < 0) {
            if (currOctave > 0) {
              currOctave--;
              arpIndex = arpSize-1;
            } else {
              upDownDirection = ARP_UP;
              // Serial.println("up");
              arpIndex = 1;
            }
          }
        }
        prevValue = nextValue;
        return nextValue;
      }
      if (arpDirection == ARP_DOWN) {
        nextValue = sortedValues[arpIndex--] + (currOctave * 12);
        updateDownIndex();
        return nextValue;
      }
      return 0; // in case it ever gets here
    }

    /** Return the prev arp value */
    inline
    int again() {
      return prevValue;
    }

    /* Update the arp values and size (max 12 values)*/
    inline
    void setValues(int * values, int size) {
      if (size > 12) return;
      arpSize = size;
      for (int i=0; i<arpSize; i++) {
          initValues[i] = values[i];
          sortedValues[i] = values[i];
      }
      sort(sortedValues, size);
    }

    /* Specify the arpeggiation direction
    *  Choices are: ARP_ORDER, ARP_UP, ARP_UP_DOWN, ARP_DOWN, ARP_RANDOM
    */
    inline
    void setDirection(int newDir) {
      arpDirection = newDir; // add checks
    }

    /* Specify the number of octaves to span */
    inline
    void setRange(int range) {
      octaveRange = min(8, max(1, range));
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
    		return 60.0 / bpm * 1000.0 / slice / stepDiv;
    	} else return 250;
    }

    /** Return the number of milliseconds between steps
    * at a particular beats per minute
    */
    inline
    double calcStepDelta(float bpm) {
      if (bpm > 0) {
	      return 60.0 / bpm * 1000.0;
      } else return 250;
    }

  private:
    int initValues [12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int arpSize = 12;
    int sortedValues [12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int arpDirection = ARP_ORDER;
    int octaveRange = 1; // number of osctave
    int currOctave = 0;
    int arpIndex = 0;
    int upDownDirection = ARP_UP;
    int stepDiv = 1;
    int prevValue = 0;

    void updateUpIndex() {
      if (arpIndex >= arpSize) {
        if (currOctave >= octaveRange - 1) {
          currOctave = 0;
        } else {
          currOctave++;
        }
        arpIndex = 0;
      }
    }

    void updateDownIndex() {
      if (arpIndex < 0) {
        if (currOctave > 0) {
          currOctave--;
        } else {
          currOctave = octaveRange - 1;
        }
        arpIndex = arpSize - 1;
      }
    }

    void sort(int a[], int size) {
      for(int i=0; i<(size-1); i++) {
        for(int o=0; o<(size-(i+1)); o++) {
          if(a[o] != 0 && a[o+1] !=0 && a[o] > a[o+1]) {
            int t = a[o];
            a[o] = a[o+1];
            a[o+1] = t;
          }
        }
      }
    }
};

#endif /* Arp_H_ */