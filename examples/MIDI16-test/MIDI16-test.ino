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
unsigned long microsNow = micros(); 
unsigned long clockTime = microsNow;
int tempoDelta = 20833; // 120 BPM
bool sounding = false;
uint8_t midiPitch = 0;
uint8_t chan = 0;
int BPM = 120;
int prevBPM = 120;

#include <Adafruit_NeoPixel.h>
#define PIN 47
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_RGB + NEO_KHZ800);

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  Serial.println("Receive NoteOn: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  // leds[0] = CRGB::Green;
  // FastLED.show();
  pixels.setPixelColor(0, pixels.Color(0, 150, 0));
  pixels.show(); 
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  Serial.println("Receive NoteOff: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show(); 
}

void handleControlChange(byte channel, byte control, byte value) {
  Serial.println("Receive CC: " + String(control) + " " + String(value) + " " + String(channel));
}

void handleMidiClock() {
  BPM = midi.clockToBpm();
  if (BPM != prevBPM) {
    prevBPM = BPM;
    Serial.println("Incomming BPM = " + String(BPM));
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
    pixels.begin();
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show(); 
    Serial.println("MIDI16-test");
    tempoDelta = midi.calcTempoDelta(140); // in micros
    Serial.println("tempoDelta " + String(tempoDelta));
}

void loop() { 
  msNow = millis();

  // receive MIDI data
  if (msNow > midiTime) {
    midiTime += 1; // 8 is enough for note messages, 1 required for clock
    uint8_t status = midi.read();
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

  // Send MIDI channel messages
  if (msNow > onTime) {
    onTime = msNow + 1000;
    offTime = msNow + 250;
    midiPitch = rand(60) + 30;
    pixels.setPixelColor(0, pixels.Color(0, 0, 150));
    pixels.show(); 
    sounding = true;
    chan = rand(4);
    midi.sendNoteOn(chan, midiPitch, 100);
    Serial.print("Send note on "); Serial.print(midiPitch);
    Serial.print(" on chan "); Serial.println(chan);
    if (rand(10) < 3) {
      int cc = rand(127);
      int val = rand(127);
      midi.sendControlChange(0, cc, val); // check cc's work
      Serial.print("Send control change "); Serial.print(cc); Serial.print(" "); Serial.println(val);
    }
  }

  if (sounding && msNow > offTime) {
    Serial.println("Send note off");
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show(); 
    sounding = false;
    midi.sendNoteOff(chan, midiPitch, 0);
  }

  // send MIDI clock tempo
  // microsecond timing required for 24 PPQN accuracy
  microsNow = micros(); 

  if (microsNow > clockTime) {
    clockTime += tempoDelta;
    midi.sendClock();
  }
}
