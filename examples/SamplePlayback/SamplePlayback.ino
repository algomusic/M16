/*
 * M16 Sample Loading and Playback Example
 *
 * Demonstrates using the Wav and Samp classes from the M16 library
 * to load and play WAV files from an SD card.
 *
 * This sketch:
 * - Initializes SD card with SdFat
 * - Loads the first WAV file found into PSRAM/RAM
 * - Supports 8-bit, 16-bit, and 24-bit PCM WAV files
 * - Supports mono and stereo files
 * - Automatically adjusts playback speed for different sample rates
 * - Plays it back once through I2S
 *
 * Hardware:
 * - ESP32-S3 with PSRAM
 * - SD card module (SPI)
 * - I2S DAC for audio output
 */

#include "M16.h"
#include "Samp.h"
#include "Wav.h"

// SD Card SPI pins
#define SD_MISO 10
#define SD_SCLK 11
#define SD_MOSI 12
#define SD_CS   13

// Create objects
SdFs sd;
Wav wav;
Samp sample;

int16_t* ampEnv = nullptr;  // Pointer to envelope array (allocated in setup)
int sampCnt = 0;             // Sample count
int ampEnvIndex = 0;         // Envelope index counter
int panLevelL = 700; 
int panLevelR = 700; // 0-1024 10bit
int start = 0;
int end;
float currentSpeed = 1.0f;  // Track speed for delay calculation

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== WAV Playback Example ===");
    Serial.println("Setting up..");
    if (!wav.initSD(sd, SD_CS, SD_SCLK, SD_MISO, SD_MOSI, 16)) {
        Serial.println("ERROR: SD card initialization failed!");
        return;
    }
    // Load first WAV file found on SD card
    if (!wav.loadFirst()) {
        Serial.println("ERROR: No WAV file could be loaded!");
        return;
    }
    // Print first samples for verification
    wav.printFirstSamples(10);
    // Configure Samp object with loaded WAV data
    sample.setTable(wav.getBuffer(),
                   wav.getFrameCount(),
                   wav.getSampleRate(),
                   wav.getChannels());

    end = wav.getFrameCount();
    currentSpeed = sample.getSpeed();  // 1.0 for initial playback 
    // Initialize I2S audio
    // seti2sPins(I2S_BCK, I2S_WS, I2S_DOUT, I2S_DIN_NOT_USED); // if not using default
    audioStart();
    // Start sample playback from beginning
    sample.start();
    // sample.setLoopingOn(); // comment out the loop() contents to allow looping
    Serial.println("Setup complete. Audio should now be playing.");
}

 void loop() {
      // Wait for previous fragment to finish
      delay((int)((end - start) * 1000.0f / SAMPLE_RATE / currentSpeed));

      // Set up new fragment
      int size = wav.getFrameCount();
      start = random(size / 2); 
      end = start + random(min(50000, size - start));  

      sample.setStart(start);
      sample.setEnd(end);
      sample.generateCosineEnvelope(end - start, 0.2);
      sample.setFreq(mtof(random(36) + 48));

      currentSpeed = sample.getSpeed(); 

      float panPos = random(1000) / 1000.0f;
      panLevelL = panLeft(panPos) * 1024;
      panLevelR = panRight(panPos) * 1024;

      sample.start();
  }

/*
 * The audioUpdate function is required in all M16 programs
 * It's called automatically at the sample rate to generate audio
 */
void audioUpdate() {
    int16_t leftVal = 0;
    int16_t rightVal = 0;
    if (wav.getChannels() == 1) {
        // Mono: duplicate to both channels
        leftVal = sample.next();
        rightVal = leftVal;
    } else {
        // Stereo: read left and right separately
        leftVal = sample.nextLeft();
        rightVal = sample.nextRight();
    }
    // panning
    leftVal = (leftVal * panLevelL)>>10;
    rightVal = (rightVal * panLevelR)>>10;
    // update envelope position
    ampEnvIndex++;
    if (ampEnvIndex >= sampCnt) ampEnvIndex = 0; // reset to start
    // Write samples to I2S DAC
    i2s_write_samples(leftVal, rightVal);
}

