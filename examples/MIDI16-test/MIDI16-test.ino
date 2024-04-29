// MIDI16.h example code
// Send and recieve MIDI messages using a hardware optocoupler-based circuit
// If using 5v power for MIDI hardware circuit from ESP, then disconnect while booting
// ESP may need to be manually put into boot mode to flash
// Includes neopixel LED indicator. This may need to be disabled for some boards.
#include "M16.h" 
#include "MIDI16.h"

MIDI16 midi(37, 38); // MIDI in and out GPIO pins
unsigned long msNow = millis();
unsigned long midiTime = msNow;
unsigned long onTime = msNow;
unsigned long offTime = msNow;
bool sounding = false;
uint8_t midiPitch = 0;

#include <FastLED.h>
#define NUM_LEDS 1
#define DATA_PIN 47 // 47 for Lolin S3 mini // 21 for Waveshare S3 pico
#define CLOCK_PIN 13
CRGB leds[NUM_LEDS];

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  Serial.println("NoteOn: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  leds[0] = CRGB::Blue;
  FastLED.show();
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  Serial.println("NoteOff: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  leds[0] = CRGB::Black;
  FastLED.show();
}

void handleControlChange(byte channel, byte control, byte value) {
  Serial.println("CC: " + String(control) + " " + String(value) + " " + String(channel));
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
  msNow = millis();

  if (msNow > midiTime) {
    midiTime = msNow + 19;
    uint8_t status = midi.read();
    // if (status > 0) {Serial.print("Status: "); Serial.println(status);}
    if (status == MIDI16::noteOn) {
      handleNoteOn(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::noteOff) {
      handleNoteOff(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::controlChange) {
      handleControlChange(midi.getChannel(), midi.getData1(), midi.getData2());
    }
  }

  if (msNow > onTime) {
    onTime = msNow + 500;
    offTime = msNow + 300;
    midiPitch = rand(127);
    Serial.print("note on "); Serial.println(midiPitch);
    leds[0] = CRGB::Blue;
    FastLED.show();
    sounding = true;
    midi.sendNoteOn(0, midiPitch, 100);
    if (rand(10) < 3) {
      int cc = rand(127);
      int val = rand(127);
      midi.sendControlChange(0, cc, val); // check cc's work
      Serial.print("Control change "); Serial.print(cc); Serial.print(" "); Serial.println(val);
    }
  }

  if (sounding && msNow > offTime) {
    Serial.println("note off");
    leds[0] = CRGB::Black;
    FastLED.show();
    sounding = false;
    midi.sendNoteOff(0, midiPitch, 0);
  }
}
