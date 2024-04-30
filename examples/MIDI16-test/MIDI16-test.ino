// MIDI16.h example code
// Send and recieve MIDI messages using a hardware optocoupler-based circuit
// ESP may need to be manually put into boot mode to reflash when using Serial2
// Includes neopixel LED indicator. This may need to be disabled for some boards.
#include "M16.h" 
#include "MIDI16.h"

MIDI16 midi(37, 38); // MIDI in and out GPIO pins
unsigned long msNow = millis();
unsigned long midiTime = msNow;
unsigned long onTime = msNow;
unsigned long offTime = msNow;
unsigned long prevClockTime;
int prevClockDeltas [9];
int prevBPM = 0;
bool sounding = false;
uint8_t midiPitch = 0;

#include <FastLED.h>
#define NUM_LEDS 1
#define DATA_PIN 47 // 47 for Lolin S3 mini // 21 for Waveshare S3 pico
CRGB leds[NUM_LEDS];

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  Serial.println("Receive NoteOn: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  leds[0] = CRGB::Green;
  FastLED.show();
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  Serial.println("Receive NoteOff: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  leds[0] = CRGB::Black;
  FastLED.show();
}

void handleControlChange(byte channel, byte control, byte value) {
  Serial.println("Receive CC: " + String(control) + " " + String(value) + " " + String(channel));
}

void handleMidiClock() {
  unsigned long cTime = micros();
  float deltaCT = cTime - prevClockTime;
  prevClockTime = cTime;
  float rollingCT = deltaCT;
  for(int i=8; i>=0; i--) {
    rollingCT += prevClockDeltas[i];
    if (i > 0) {
      prevClockDeltas[i] = prevClockDeltas[i-1];
    } else prevClockDeltas[0] = deltaCT;
  }
  rollingCT *= 0.1;
  float beatDelta = rollingCT * 24;
  float BPM = 1000 / beatDelta * 60000;
  if (round(BPM) != prevBPM) {
    Serial.println("Clock delta: " + String(rollingCT) + " beat delta " + String(beatDelta) + " BPM " + String(BPM) + " round BPM " + String(round(BPM)));
    prevBPM = round(BPM);
  }
}

void handleMidiStart() {
  Serial.println("* Start *");
}

void handleMidiCont() {
  Serial.println("* Continue *");
}

void handleMidiStop() {
  Serial.println("* Stop *");
}

void setup() { 
    Serial.begin(115200);
    FastLED.addLeds<SM16703, DATA_PIN, RGB>(leds, NUM_LEDS);
    leds[0] = CRGB::Red;
    FastLED.show();
    delay(1000);
    leds[0] = CRGB::Black;
    FastLED.show();
    Serial.println("MIDI16-test");
}

void loop() { 
  msNow = micros();

  // receive MIDI data

  if (msNow > midiTime) {
    midiTime = msNow + 500; // + 19ms is adequate if no clock sync is required
    uint8_t status = midi.read();
    // if (status > 0) {Serial.print("Status: "); Serial.println(status);}
    if (status == MIDI16::noteOn) {
      handleNoteOn(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::noteOff) {
      handleNoteOff(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::controlChange) {
      handleControlChange(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::clock) {
      handleMidiClock();
    } else if (status == MIDI16::start) {
      handleMidiStart();
    } else if (status == MIDI16::cont) {
      handleMidiCont();
    } else if (status == MIDI16::stop) {
      handleMidiStop();
    }
  }

  // Send MIDI data
  if (msNow > onTime) {
    onTime = msNow + 500000;
    offTime = msNow + 300000;
    midiPitch = rand(127);
    Serial.print("Send note on "); Serial.println(midiPitch);
    leds[0] = CRGB::Blue;
    FastLED.show();
    sounding = true;
    midi.sendNoteOn(0, midiPitch, 100);
    if (rand(10) < 3) {
      int cc = rand(127);
      int val = rand(127);
      midi.sendControlChange(0, cc, val); // check cc's work
      Serial.print("Send control change "); Serial.print(cc); Serial.print(" "); Serial.println(val);
    }
  }

  if (sounding && msNow > offTime) {
    Serial.println("Send note off");
    leds[0] = CRGB::Black;
    FastLED.show();
    sounding = false;
    midi.sendNoteOff(0, midiPitch, 0);
  }
}
