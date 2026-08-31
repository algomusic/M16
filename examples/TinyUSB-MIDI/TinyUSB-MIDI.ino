// Velocity sensitive monophonic MIDI synth and MIDI data generator
// Requires an ESP32 with support for TinyUSB 
// MIDI data is sent via the USB cable to/from attached computer
// - tested with the ESP32-S2
// - tested with ESP32-S3 
// The following changes from the default Arduino IDE setting for S3 boards are required:
//  USB CDC On Boot: Enabled
//  Upload Mode: "USB OTG CDC (TinyUSB)"
//  USB Mode: "USB-OTG (TinyUSB)"
// Also, on some boards it may be necessary to;
//  Put ESP32-S3 into boot mode to (re)upload the code
//  Press RST on ESP after loading
// After resetting the Arduino IDE port will need to be resablished to see print statements in the Serial Monitor

#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32S3)

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("TinyUSB-MIDI requires an ESP32-S2 or ESP32-S3 USB target");
}

void loop() {}

#else

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF.h"
#include "Del.h"
#include "Gain.h"

WaveTable sineWave;
WaveTable sawtoothWave;
Osc osc1;
Env ampEnv1;
SVF filter1;
Del delay1(500); // max delay time in ms
Gain outputGain;
unsigned long msNow = millis();
unsigned long envTime = msNow;
unsigned long glideTime = msNow;
unsigned long noteOnTime = msNow;
unsigned long noteOffTime = msNow;
unsigned long ccTime = msNow;
unsigned long noteDelta = 500;
unsigned long ccDelta = 800;
unsigned long envDelta = 4;
unsigned long glideDelta = 21;
float windowSize = 0;
float nextFreq = 440;
int outPitch = 60;
int pitchClass [] = {0, 2, 4, 5, 7, 9, 11};
bool notePlaying = false;

#include <Adafruit_TinyUSB.h>
// - MIDI Library by Forty Seven Effects
// https://github.com/FortySevenEffects/arduino_midi_library
#include <MIDI.h>

// // USB MIDI object
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);


void handleNoteOn(byte channel, byte pitch, byte velocity) {
  // Serial.print("Recieved Pitch: ");Serial.print(pitch);Serial.print(" Vel: ");Serial.println(velocity);
  nextFreq = mtof(pitch); // glide target
  windowSize = velocity / 127.0f;
  ampEnv1.start();
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  ampEnv1.startRelease();
}

void handleCC(byte channel, byte controller, byte value) {
  outputGain.setLevel((value * 1024) / 127);
}


void setup() {
  Serial.begin(115200);
  // audio
  sineWave.sinGen();
  sawtoothWave.sawGen();
  osc1.setTable(sineWave);
  osc1.setSpread(0.0001); // make more complex
  ampEnv1.setAttack(5);
  ampEnv1.setSustain(0.5);
  ampEnv1.setRelease(200);
  delay1.setTime(375); // ms
  delay1.setLevel(0.8); // 0.0 - 1.0
  delay1.setFeedback(true); // bool
  // seti2sPins(16, 17, 18, 21); // BCK, WS, DOUT, DIN
  // useInternalDAC();
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

  if (msNow - noteOnTime >= noteDelta) {
    noteOnTime += noteDelta;
    noteOffTime = noteOnTime + 100;
    notePlaying = true;
    outPitch = pitchQuantize(rand(24) + 48, pitchClass, 0);
    MIDI.sendNoteOn(outPitch, 100, 1); // pitch, vel, chan
    // Serial.print("Sent Pitch: ");Serial.print(outPitch);  
  }

  if (notePlaying && msNow > noteOffTime) {
    MIDI.sendNoteOff(outPitch, 0, 1);
    notePlaying = false;
  }

  if (msNow - ccTime >= ccDelta) {
    ccTime += ccDelta;
    int ccVal = rand(127);
    MIDI.sendControlChange(7, ccVal, 1); // CC, value, chan
    Serial.print(" Sent CC 7 value: ");Serial.print(ccVal);Serial.println();
  }
   
  if (msNow - envTime >= envDelta) {
    envTime += envDelta;
    ampEnv1.next();
  }

  if (msNow - glideTime >= glideDelta) {
    glideTime += glideDelta;
    osc1.slewFreq(nextFreq, 0.8);
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/
void audioUpdate() {
  // nextWTrans args: second wave, amount of second wave, duel window?, invert second wave?
  int32_t leftVal = (filter1.nextLPF(osc1.nextWTrans(sawtoothWave, windowSize, false, false)) * ampEnv1.getValue()) >> 16;
  leftVal = (leftVal + delay1.next(leftVal)) >> 1;
  leftVal = outputGain.next(leftVal);
  int32_t rightVal = leftVal;
  audioBlockWrite(leftVal, rightVal);
}

#endif
