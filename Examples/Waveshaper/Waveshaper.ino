// M16 waveshaper example
#include "M16.h" 
#include "Osc.h"
#include "FX.h"

int16_t sineTable [TABLE_SIZE]; // empty wavetable
Osc aOsc1(sineTable);
int16_t vol = 1000; // 0 - 1024, 10 bit
unsigned long msNow, pitchTime;
FX effect1;
int16_t waveShapeTable [TABLE_SIZE]; // empty wave shaping table
float stepInc = (MAX_16 * 2.0 - 1) / TABLE_SIZE;

void generateTransferFunction() {
  int shapeValue = MIN_16;
  int walkDeviation = rand(10000);
  for(int i=0; i<TABLE_SIZE; i++) { // create the shaping wavetable
    waveShapeTable[i] = shapeValue;
    int nextVal = stepInc + gaussRandNumb(walkDeviation, 3) - walkDeviation/2;
    shapeValue = max(MIN_16, min(MAX_16, shapeValue + nextVal));
  }
}

void setup() {
  Serial.begin(115200);
  Osc::sinGen(sineTable); // fill the wavetable
  aOsc1.setPitch(69);
  generateTransferFunction();
  effect1.setShapeTable(waveShapeTable, TABLE_SIZE); // install the shaping wavetable
  audioStart();
}

void loop() {
  msNow = millis();
  if (msNow > pitchTime) {
    pitchTime = msNow + 1000;
    int pitch = 36 + random(24);
    Serial.println(pitch);
    aOsc1.setPitch(pitch);
    generateTransferFunction();
  }
}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with i2s_write_samples()
*/
void audioUpdate() {
  uint16_t leftVal = (effect1.waveShaper(aOsc1.next()) * vol)>>10;
  uint16_t rightVal = leftVal;
  i2s_write_samples(leftVal, rightVal);
}
