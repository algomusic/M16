// Velocity sensitive monophonic MIDI synth and MIDI data generator
// Requires and ESP32 with support for TinyUSB - tested with the ESP32-S2
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Del.h"

int16_t sineWave [TABLE_SIZE]; // empty wavetable
int16_t sawtoothWave [TABLE_SIZE]; // empty wavetable
Osc osc1(sineWave);
Env ampEnv1;
SVF filter1;
Del delay1(500); // max delay time in ms
unsigned long msNow, envTime, glideTime, noteOnTime, noteOffTime;
float windowSize = 0;
float nextFreq = 440;
int outPitch = 60;
int pitchClass [] = {0, 2, 4, 5, 7, 9, 11};
int volume = 127; // 0 - 127

#include <Adafruit_TinyUSB.h>
// - MIDI Library by Forty Seven Effects
// https://github.com/FortySevenEffects/arduino_midi_library
#include <MIDI.h>

// // USB MIDI object
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);


void setup() {
  Serial.begin(115200);
  // audio
  Osc::sinGen(sineWave); // fill
  Osc::sawGen(sawtoothWave); // fill
  osc1.setSpread(0.0001); // make more complex
  ampEnv1.setAttack(5);
  ampEnv1.setSustain(0.5);
  ampEnv1.setRelease(200);
  delay1.setTime(375); // ms
  delay1.setLevel(0.8); // 0.0 - 1.0
  delay1.setFeedback(true); // bool
  audioStart();
  // midi
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleCC);
  // wait until device mounted
  while(!TinyUSBDevice.mounted()) delay(1);
}

void loop() {
  // read any new MIDI messages
  MIDI.read();

  msNow = millis();
  if (msNow > noteOnTime) {
    noteOnTime += 500;
    noteOffTime = noteOnTime + 100;
    outPitch = pitchQuantize(rand(24) + 48, pitchClass, 0);
    MIDI.sendNoteOn(outPitch, 100, 1); // pitch, vel, chan
    MIDI.sendControlChange(7, outPitch, 1); // CC, value, chan
  }

  if (msNow > noteOffTime) {
    MIDI.sendNoteOff(outPitch, 0, 1);
  }
   
  if (msNow > envTime) {
    envTime += 4;
    ampEnv1.next();
  }

  msNow = millis();
  if (msNow > glideTime) {
    glideTime += 21;
    osc1.slewFreq(nextFreq, 0.8);
  }
}

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  // Serial.print("Pitch: ");Serial.print(pitch);Serial.print(" Vel: ");Serial.println(velocity);
  // osc1.setPitch(pitch);
  nextFreq = mtof(pitch); // glide target
  windowSize = velocity / 127.0f;
  ampEnv1.start();
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  ampEnv1.startRelease();
}

void handleCC(byte channel, byte controller, byte value) {
  volume = value;
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  // nextWTrans args: second wave, amount of second wave, duel window?, invert second wave?
  int16_t leftVal = (filter1.simpleLPF(osc1.nextWTrans(sawtoothWave, windowSize, false, false)) * ampEnv1.getValue()) >> 16;
  leftVal = (leftVal + delay1.next(leftVal)) >> 1;
  leftVal = (leftVal * volume) >> 7;
  int16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
