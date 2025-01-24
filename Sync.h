/*
 * by Andrew R. Brown 2025
 *
 * Simple audio sync send and recieve functionalty for M16
 * Use GPIO pins for audio sync clock
 * 3.5mm jack tip is the audio pulse from Korg Volca and Teenage Engineering, and similar.
 * Jack sleve needs to be gounded to microcontroller
 * For sync input, connect audio pulse signal to an analog GPIO pin
 * For sync output, use a votage divider from GPIO pin at about 2:1 (e.g. 10k and 5k resistors) 
 *  to reduce 3.3v GPIO output to just below the 1.4v line level signal
 * This sync class defaults to 2 PPQN (TE and Korg standard) 
 *  and allows for others such as 4 PPQN (every 16th, used by modular).
 * If the GPIO pins are floating then sync behavior will be a bit crazy - careful.
 *
 * This file is part of the M16 audio library.
 * M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef SYNC_H_
#define SYNC_H_

class Sync {

public:

  /** Constructor */
  Sync() {
    Sync(45, 46);  // 45 & 46 for sProject board v3
  }

  /** Constructor */
  Sync(int inPin, int outPin):receivePin(inPin), transmitPin(outPin) {
    pinMode(receivePin, INPUT_PULLUP); // sync in
    pinMode(transmitPin, OUTPUT); // sync out
  }

  /** Set the pulses per quater note */
  void setPPQN(int val) {
    if (val > 0) PPQN = val;
  }

  /** Get the pulses per quater note */
  int getPPQN() {
    return PPQN;
  }

  /** Specify the PBM for sync out */
  void setOutBpm(float bpm) {
    if (bpm > 0) {
      outBpm = bpm;
      beatOutDelta = 60000 / bpm;
      pulseOutDelta = beatOutDelta / PPQN;
    }
  }

  /** Get the BPM used for sync out */
  float getOutBpm() {
    return outBpm;
  }

  /** Check if its time for the next out pulse */
  bool pulseOnTime(unsigned long currMs) {
    // Serial.println("currMs: " + String(currMs) + " pulseStartTime: " + String(pulseStartTime) + " pulseOutDelta: " + String(pulseOutDelta));
    if (!pulseOutIsOn && currMs - pulseStartTime > pulseOutDelta) {
      return true;
    } else {
      return false;
    }
  }

  /** Check if its time to end the out pulse */
  bool pulseOffTime(unsigned long currMs) {
    if (pulseOutIsOn & currMs - pulseStartTime > 4) {
      return true;
    } else {
      return false;
    }
  }

  /** Start the output pulse */
  void startPulse() {
    pulseOutIsOn = true;
    pulseStartTime = millis();
    digitalWrite(transmitPin, HIGH);
  }

  /** End the output pulse */
  void endPulse() {
    pulseOutIsOn = false;
    digitalWrite(transmitPin, LOW);
  }

  bool receivePulse(unsigned long currMs) {
    int val = analogRead(receivePin);
    if (val == 0 && prevVal > threshold) {
      prevVal = val;
    }
    bool result = false;
    if (val > threshold && prevVal == 0) {
      pulseDuration = currMs - oneOn;
      pulseInDeltas[pulseInDeltaIndex] = pulseDuration;
      pulseInDeltaIndex = (pulseInDeltaIndex + 1)%4;
      oneOn = currMs;
      beatInDelta = avePulseDelta() * 2;
      inBPM = msToBpm(beatInDelta);
      prevVal = val;
      pulseInCount = (pulseInCount + 1)%PPQN;
      if (pulseInCount == 0) {
        result = true;
      }
    } else result = false;
    return result;
  }

  /** Get the BPM from the sync in */
  float getInBpm() {
    return inBPM;
  }

private:
  uint8_t transmitPin, receivePin;
  bool pulseOutIsOn = false;
  uint8_t PPQN = 2;
  int16_t pulseOutDelta = 250;
  int16_t beatOutDelta = 500;
  float outBpm = 120;
  unsigned long pulseStartTime = 0;
  int16_t * pulseInDeltas = new int16_t[4]; 
  int16_t pulseInDeltaIndex = 0;
  int16_t pulseDuration; 
  int16_t threshold = 800;
  int16_t beatInDelta = 500; // ms, default to 120 BPM
  int16_t prevVal = 0;
  unsigned long oneOn = 0;
  int16_t pulseInCount = 0;
  float inBPM = 120;

  float msToBpm(int ms) {
    return 60000 / ms;
  }

  int bpmToMs(float bpm) {
    return 60000 / bpm;
  }

  int avePulseDelta() {
    int pd = 0;
    for(int i=0; i<4; i++) {
      pd += pulseInDeltas[i];
    }
    return pd * 0.25;
  }
};

#endif /* SYNC_H_ */