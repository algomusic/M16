// Play the X component of M16's 2D Perlin noise gradient-vector field.
#include "M16.h"
#include "Osc.h"
#include "Gain.h"

Osc perlinOsc;
Gain outputGain(1000);

uint32_t lastSpeedUpdateMs = 0;
const uint32_t speedUpdateIntervalMs = 2000;
const float minXSpeed = 1.0f;
const float maxXSpeed = 1500.0f;
const float ySpeed = 217.0f;

void setup() {
  Serial.begin(115200);
  delay(200);
  // Move diagonally through the 2D field to hear its changing X component.
  perlinOsc.setPerlinVectorPosition(0.0f, 0.0f);
  perlinOsc.setPerlinVectorSpeed(350.0f, ySpeed); // use different x and y speeds for more interesting output
  lastSpeedUpdateMs = millis();
  audioStart();
  Serial.println("Perlin noise oscillator started");
}

void loop() {
  const uint32_t now = millis();

  if ((uint32_t)(now - lastSpeedUpdateMs) >= speedUpdateIntervalMs) {
    lastSpeedUpdateMs = now;

    // Use the current control-rate vector to set a positive X movement speed.
    const NoiseVector2 fieldValue = perlinOsc.getPerlinVectorNow(); // control rate (any time) function
    const int32_t controlValue = fieldValue.y; // fieldValue contains x and y values from the 2D noise
    const float normalizedControl = controlValue < 0 ? (float)controlValue / 32768.0f : (float)controlValue / 32767.0f;
    const float xSpeed = minXSpeed + (normalizedControl + 1.0f) * 0.5f * (maxXSpeed - minXSpeed);
    perlinOsc.setPerlinVectorSpeed(xSpeed, ySpeed);
    Serial.print("Perlin vector speeds: "); Serial.print(xSpeed, 2); Serial.print(" "); Serial.println(ySpeed, 2);
  }
}

void audioUpdate() {
  const NoiseVector2 fieldValue = perlinOsc.getPerlinVectorNext();
  const int32_t sample = outputGain.next(fieldValue.x);
  audioBlockWrite(sample, sample);
}
