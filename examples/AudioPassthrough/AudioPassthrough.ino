// M16 Audio Input example
// Audio passthrough for an I2S microphone
// Tested with ESP32-S3 and an INMP441 MEMS microphone
#include "M16.h" 
#include "Mic.h"

Mic audioIn; // create a audio input (Mic) object
int gain = 24; // amplify the input

// Optional Teensy Audio Board support (Teensy builds only):
// M16.h includes the PJRC Audio library on Teensy, so no additional include is
// required. Uncomment this controller and the matching setup block below when
// using the board's SGTL5000 codec.
// AudioControlSGTL5000 audioBoard;

void setup() {
  Serial.begin(115200);
  delay(200);
  #if IS_RP2040() || IS_TEENSY4()
    audioInputStart(); // Start the platform's separate I2S input path
  #endif
  // seti2sPins(16, 17, 18, 21); // BCK, WS, DOUT, DIN
  // useInternalDAC();
  audioStart();

  // Optional Teensy Audio Board microphone input:
  // audioBoard.enable();
  // audioBoard.inputSelect(AUDIO_INPUT_MIC);
  // audioBoard.micGain(30);  // dB; adjust for the connected microphone
  // audioBoard.volume(0.5f); // headphone/line-output level, 0.0-1.0

  // Alternatively, replace the MIC selection and gain above with line input:
  // audioBoard.inputSelect(AUDIO_INPUT_LINEIN);
  // audioBoard.lineInLevel(5); // 0-15; lower numbers accept a larger signal

  // Teensy Audio Board I2S routing is fixed by the PJRC Audio library:
  // DOUT from Teensy=7, DIN to Teensy=8, LRCLK=20, BCLK=21, MCLK=23.
  // SGTL5000 control uses the board's I2C connection (SDA=18, SCL=19).
}

void loop() {}

/* The audioUpdate function is required in all M16 programs 
* to specify the audio sample values to be played.
* Always finish with audioBlockWrite()
*/

void audioUpdate() {
  int16_t inputLeft, inputRight;
  audioIn.nextStereo(inputLeft, inputRight);
  // Mono I2S microphones normally drive either the left or right slot,
  // selected by the microphone's L/R pin. Summing accepts either setting.
  int32_t mono = clip16(((int32_t)inputLeft + inputRight) * gain);
  audioBlockWrite(mono, mono);
}
