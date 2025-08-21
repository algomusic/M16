// M16 Audio Sync example
// Use GPIO pins for audio sync clock
// 3.5mm jack tip pin is the audio pulse from Korg Volca and Teenage Engineering, 
// Jack sleve needs to be gounded to microcontroller
// For sync input, read the level on an analog GPIO pin
// For sync output, use a votage divider at about 2:1 (e.g. 10k and 5k) to reduce 3.3v GPIO output to just below the 1.4v line level signal
// Sync class defaults to 2 ppqn (TE and Korg standard) and allows for others such as 4 ppqn (every 16th, used by modular).
// If the GPIO pins are floating then sync behavior will be a bit crazy.

#include "M16.h"
#include "Sync.h"
#include "Osc.h"
#include "Env.h"

Sync audioSync(15, 33); // in and out GPIO pins
int16_t waveTable[TABLE_SIZE]; // empty wavetable
Osc aOsc1(waveTable);
Env ampEnv;
int envVal = 0;
unsigned long pitchTime = millis();
int readPulseDelta = 2;
int writePulseDelta = 2;

int prevVal = 0;
unsigned long msNow, ampEnvTime, readPulseTime, writePulseTime;
float inputBPM;

void playNote() {
  int pitch = random(24) + 58;
  aOsc1.setPitch(pitch);
  ampEnv.start();
}

void setup() {
  Serial.begin(115200);
  audioSync.setOutBpm(120);
  Osc::sinGen(waveTable); // fill the wavetable
  aOsc1.setPitch(69);
  // seti2sPins(25, 27, 12, 21); // bck, ws, data_out, data_in // change ESP32 defaults
  audioStart();
  Serial.println("Output BPM = " + String(audioSync.getOutBpm()));
}

void loop() {
  msNow = millis();

  // read sync
  if ((unsigned long)(msNow - readPulseTime) >= readPulseDelta) {
    // Sync pulse is about 4ms, so delta between reads should no longer than that 
    readPulseTime += readPulseDelta;
    if(audioSync.receivePulse(msNow)){
      playNote();
      float newBPM = audioSync.getInBpm();
      if (fabs(newBPM - inputBPM) >= 1) {
        inputBPM = newBPM;
        Serial.println("Input BPM = " + String(inputBPM));
      }
    }
  }

  // write sync
  if ((unsigned long)(msNow - writePulseTime) >= writePulseDelta) {
      writePulseTime += writePulseDelta;
    if (audioSync.pulseOnTime(msNow)) {
      audioSync.startPulse();
    }
    if (audioSync.pulseOffTime(msNow)) {
      audioSync.endPulse();
    }
  }

  if (msNow > ampEnvTime) {
    ampEnvTime = msNow + 1;
    ampEnv.next();
    envVal = ampEnv.getValue();
  }
  
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int16_t signal = (aOsc1.next() * envVal)>>16;
  i2s_write_samples(signal, signal);
}
