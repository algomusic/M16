/*
 * WavPlayer
 *
 * Streams a WAV file from SD card to audio output using double-buffering.
 * No PSRAM required — only ~8KB of heap is used regardless of file size.
 * The file loops continuously.
 *
 * Set FILE_INDEX to select which WAV file to play (0 = first file found).
 * Call wav.countWavFiles(true) to print available files and their indices.
 *
 * Supports mono and stereo, 8/16/24/32-bit PCM WAV files.
 *
 * Hardware: ESP32 with SD card (SPI) and I2S DAC or onboard DAC on OG ESP32.
 * Uncomment useInternalDAC() in setup() for ESP32 built-in DAC (GPIO25/26).
 */

#include "M16.h"
#include "Wav.h"

// --- File selection ---
// Set to 0 for the first WAV file found, 1 for the second, etc.
// Call wav.countWavFiles(true) in setup() to print available files.
#define FILE_INDEX 0

// --- SD card SPI pins — must match your wiring ---
// Classic ESP32 (WROOM/WROVER): GPIO 6-11 are reserved for Flash — do NOT use them.
// VSPI defaults: SCK=18, MISO=19, MOSI=23, CS=5
// HSPI defaults: SCK=14, MISO=12, MOSI=13, CS=15
#define SD_CS    5
#define SD_SCLK 18
#define SD_MISO 19
#define SD_MOSI 23

// --- Stream buffers ---
// CHUNK frames per half-buffer (~23ms at 44.1kHz). *2 accommodates stereo.
const uint16_t CHUNK = 1024;
int16_t streamBuf[2][CHUNK * 2];

volatile uint8_t  playHalf = 0;   // half currently read by audioUpdate
volatile uint16_t playPos  = 0;   // frame position within playHalf
volatile bool     needFill = false;
uint64_t filePos  = 0;
uint8_t  channels = 1;

SdFs sd;
Wav  wav;

void fillHalf(uint8_t half) {
  uint64_t total  = wav.getFileFrameCount();
  if (filePos >= total) filePos = 0;
  uint32_t toRead = min((uint32_t)CHUNK, (uint32_t)(total - filePos));
  wav.readFramesFast(filePos, toRead, streamBuf[half]);
  filePos += toRead;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!wav.initSD(sd, SD_CS, SD_SCLK, SD_MISO, SD_MOSI, 4)) {
    Serial.println("SD init failed"); return;
  }
  wav.countWavFiles(true);
  if (!wav.loadHeaderByIndex(FILE_INDEX)) {
    Serial.println("No WAV found at index " + String(FILE_INDEX)); return;
  }

  channels = wav.getChannels();
  wav.openForStreaming();
  fillHalf(0);

  fillHalf(1);
  //seti2sPins(38, 39, 40, -1); // bck, ws, data_out, data_in
  // useInternalDAC(); // ESP32 only (GPIO25/26), not available on S3
  audioStart();
}

void loop() {
  if (needFill) {
    fillHalf(1 - playHalf);
    needFill = false;
  }
}

void audioUpdate() {
  int16_t* frame = &streamBuf[playHalf][playPos * channels];
  int16_t  L     = frame[0];
  int16_t  R     = (channels > 1) ? frame[1] : L;
  if (++playPos >= CHUNK) {
    playPos   = 0;
    playHalf ^= 1;
    needFill  = true;
  }
  i2s_write_samples(L, R);
}
