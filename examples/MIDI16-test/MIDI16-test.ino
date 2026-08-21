// MIDI16.h example code
// Send and recieve MIDI messages using a hardware optocoupler-based circuit
// ESP may need to be manually put into boot mode to reflash when using Serial2
// Includes neopixel LED indicator. This may need to be disabled for some boards.
#include "M16.h" 
#include "MIDI16.h"

MIDI16 midi(16, 17); // MIDI in and out GPIO pins
unsigned long msNow = millis();
unsigned long onTime = msNow;
unsigned long offTime = msNow;
unsigned long tempoTime = msNow;
int onDelta = 1000; // time between note on messages
bool sounding = false;
uint8_t midiPitch = 0;
uint8_t chan = 0;
int prevBPM = 0;

#include <Adafruit_NeoPixel.h>
#define PIN 47
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_RGB + NEO_KHZ800);

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  Serial.println("Receive NoteOn: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  pixels.setPixelColor(0, pixels.Color(0, 255, 0));
  pixels.show(); 
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  Serial.println("Receive NoteOff: " + String(pitch) + " " + String(velocity) + " " + String(channel));
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show(); 
}

void handleControlChange(byte channel, byte control, byte value) {
  Serial.println("Receive CC: " + String(control) + " " + String(value) + " " + String(channel));
  pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  pixels.show(); 
}
void handlePitchBend(byte channel, byte data1, byte data2) {
  Serial.println("Receive PitchBend: " + String(channel) + " " + String(data1) + " " + String(data2) + " " + String(((uint16_t)data2 << 7) | data1));
  pixels.setPixelColor(0, pixels.Color(0, 255, 255));
  pixels.show(); 
}

void handleMidiClock() {
  int bpm = midi.clockToBpm();
  if (bpm != prevBPM) {
    prevBPM = bpm;
    Serial.println("Incomming BPM = " + String(bpm));
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
    // midi.setClockSendBpm(100);  // start clock task and send 140 BPM clock
    pixels.begin();
    pixels.setBrightness(40);
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show(); 
    Serial.println("MIDI16-test");
}

void loop() { 
  msNow = millis();

  // receive MIDI data - drain all available messages each loop iteration
  uint8_t status;
  while ((status = midi.read()) != 0) {
    if (status == MIDI16::noteOn) {
      handleNoteOn(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::noteOff) {
      handleNoteOff(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::controlChange) {
      handleControlChange(midi.getChannel(), midi.getData1(), midi.getData2());
    } else if (status == MIDI16::pitchBend) {
      handlePitchBend(midi.getChannel(), midi.getData1(), midi.getData2());
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
  if ((unsigned long)(msNow - onTime) >= onDelta) {
      onTime += onDelta;
    offTime = msNow + 250;
    midiPitch = rand(60) + 30;
    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
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

  // MIDI clock send is handled by the clock task via setClockSendBpm()
  // Call midi.setClockSendBpm(bpm) at any time to change tempo
  //  tempo change 
  if ((unsigned long)(msNow - tempoTime) >= 5000) {
      tempoTime += 5000;
      midi.setClockSendBpm(random(100) + 60);
      Serial.println("Tempo is " + String(midi.getBpm()));
  }
}
