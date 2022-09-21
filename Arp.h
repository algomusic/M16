/*
 * Arp.h
 *
 * Arppegiator class. Based on MIDI note numbers
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
    * @param PITCH_CLASS the array of pitch classes the arpeggiator will be using.
    * @param OCTAVE the base range for the arpeggiator
    * @param ARP_DIRECTION the playback order
    */
    Arp(int * PITCHES, int NUMBER_PITCHES):initPitches(PITCHES), initSize(NUMBER_PITCHES) {
      setPitches(initPitches, initSize);
    }

    inline
    void start() {
      arpIndex = 0;
      currOctave = 0;
      // upDownDirection = ARP_UP;
    }

    inline
    int next() {
      int nextPitch;
      // in order
      if (arpDirection == ARP_ORDER) {
          while (pitchSet[arpIndex] == 0) {
            arpIndex = (arpIndex+1)%12;
            updateOctaveUp();
          }
          nextPitch = pitchSet[arpIndex] + (currOctave * 12);
          arpIndex = (arpIndex+1)%12;
          updateOctaveUp();
          return nextPitch;
//        }
      }
      if (arpDirection == ARP_UP) {
        while (pitchSetUp[arpIndex] == 0) {
          arpIndex = (arpIndex+1)%12;
          updateOctaveUp();
        }
        nextPitch = pitchSetUp[arpIndex] + (currOctave * 12);
        arpIndex = (arpIndex+1)%12;
        updateOctaveUp();
        return nextPitch;
      }
      if (arpDirection == ARP_UP_DOWN) {
//        Serial.print(arpIndex);
        if (pitchSetUp[arpIndex] == 0) updateOctaveUpDown();
        if (upDownDirection == ARP_UP) {
          nextPitch = pitchSetUp[arpIndex] + (currOctave * 12);
          arpIndex = (arpIndex+1)%12;
          return nextPitch;
        }
        if (upDownDirection == ARP_DOWN) {
          nextPitch = pitchSetDown[arpIndex] + (currOctave * 12);
          arpIndex = (arpIndex+1)%12;
          return nextPitch;
        }
      }
      if (arpDirection == ARP_DOWN) {
//        Serial.println("arp down");
        while (pitchSetDown[arpIndex] == 0) {
          arpIndex = (arpIndex+1)%12;
          updateOctaveDown();
        }
        nextPitch = pitchSetDown[arpIndex] - (currOctave * 12);
        arpIndex = (arpIndex+1)%12;
        updateOctaveDown();
        return nextPitch;
      }
      if (arpDirection == ARP_RANDOM) {
        arpIndex = random(12);
        while (pitchSet[arpIndex] == 0) {
          arpIndex = random(12);
        }
        return pitchSet[arpIndex];
      }
      return 0; // in case it ever gets here
    }

    inline
    void setPitches(int * pitches, int size) {
      if (size > 12) return;
//      Serial.println("set pitches");
      for (int i=0; i<size; i++) {
          pitchSet[i] = pitches[i];
      }
      for (int i=size; i<12; i++) {
          pitchSet[i] = 0;
      }
    }

    inline
    void setDirection(int newDir) {
      arpDirection = newDir; // add checks
      if (newDir == ARP_UP) sortUp();
      if (newDir == ARP_UP_DOWN) sortUpDown();
      if (newDir == ARP_DOWN) sortDown();
    }

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
    int * initPitches;
    int initSize;
    int pitchSet [12] = {60, 64, 67, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int pitchSetUp [12] = {60, 64, 67, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int pitchSetDown [12] = {67, 64, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int arpDirection = ARP_ORDER;
    int octaveRange = 1; // number of octaves
    int currOctave = 0;
    int arpIndex = 0;
    int upDownDirection = ARP_UP;
    int stepDiv = 1;

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

    void reverseSort(int a[], int size) {
      for(int i=0; i<(size-1); i++) {
        for(int o=0; o<(size-(i+1)); o++) {
          if(a[o] != 0 && a[o+1] !=0 && a[o] < a[o+1]) {
            int t = a[o+1];
            a[o+1] = a[o];
            a[o] = t;
          }
        }
      }
    }

    void sortUp() {
      for (int i=0; i<12; i++) {
        pitchSetUp[i] = pitchSet[i];
      }
      sort(pitchSetUp, 12);
    }

    void sortUpDown() {
      sortUp();
      sortDown();
    }

    void sortDown() {
      for (int i=0; i<12; i++) {
        pitchSetDown[i] = pitchSet[i];
      }
      reverseSort(pitchSetDown, 12);
    }

    void updateOctaveUp() {
      if (arpIndex == 0) {
        if (currOctave < octaveRange - 1) {
          currOctave++;
        } else currOctave = 0;
      }
    }

    void updateOctaveDown() {
      if (arpIndex == 0) {
        if (currOctave < octaveRange - 1) {
          currOctave++;
        } else currOctave = 0;
      }
    }

    void updateOctaveUpDown() {
      if (upDownDirection == ARP_UP && currOctave == octaveRange - 1) {
        upDownDirection = ARP_DOWN;
        arpIndex = 1;
      } else if (upDownDirection == ARP_UP && currOctave < octaveRange - 1) {
        currOctave++;
        arpIndex = 0;
      } else if (upDownDirection == ARP_DOWN && currOctave > 0) {
         currOctave--;
         arpIndex = 0;
      } else if (upDownDirection == ARP_DOWN && currOctave == 0) {
         upDownDirection = ARP_UP;
         arpIndex = 1;
      }
    }

};

#endif /* Arp_H_ */
