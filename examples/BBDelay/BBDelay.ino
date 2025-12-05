// M16 BBDelay Example
// Demonstrates BBD (Bucket Brigade Delay) with automated parameter changes
#include "M16.h"
#include "BBD.h"
#include "Osc.h"
#include "Env.h"

BBD delay1;
Osc osc1;
Env ampEnv1;
int noteDelta = 600;

unsigned long msNow, noteTime, envTime, sweepTime;
int scale[] = {0, 3, 5, 7, 10}; // minor pentatonic
float scanRate = 1.0;
int scanDir = -1;
int noteCount = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("M16 BBDelay Example");

  delay1.setTime(200);
  delay1.setLevel(0.8);
  delay1.setFeedbackLevel(0.6);
  delay1.setFiltered(2); // warmth / smoothing, 0 - 4

  osc1.triGen();
  osc1.setPitch(60);
  ampEnv1.setAttack(5);
  ampEnv1.setDecay(150);
  ampEnv1.setSustain(0);

  seti2sPins(38, 39, 40, 41); // change ESP32 defaults if needed
  audioStart();
}

void loop() {
  #if IS_ESP8266()
    audioUpdate();
  #endif

  msNow = millis();

  // trigger notes
  if ((unsigned long)(msNow - noteTime) >= noteDelta) {
    noteTime += noteDelta;
    int pitch = pitchQuantize(random(20) + 50, scale, 0);
    osc1.setPitch(pitch);
    ampEnv1.start();
    noteCount++;

    // every 8 notes, change feedback level
    if (noteCount % 8 == 0) {
      float newFB = random(30, 80) * 0.01;
      delay1.setFeedbackLevel(newFB);
      Serial.print("Feedback: "); Serial.println(newFB);
    }
  }

  // slowly sweep scan rate
  // lower scan rate = longer delay + darker sound
  if ((unsigned long)(msNow - sweepTime) >= 50) {
    sweepTime += 50;
    scanRate += scanDir * 0.005;
    if (scanRate <= 0.1) { scanDir = 1; scanRate = 0.1; }
    if (scanRate >= 1.0) { scanDir = -1; scanRate = 1.0; }
    delay1.setScanRate(scanRate);
  }

  if (msNow > envTime) {
    envTime = msNow + 4;
    ampEnv1.next();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  int32_t oscVal = (osc1.next() * ampEnv1.getValue()) >> 16;
  int32_t delayed = delay1.next(oscVal);
  int32_t mix = clip16(oscVal + delayed);
  i2s_write_samples(mix, mix);
}
