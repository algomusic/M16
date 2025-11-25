/*
 * Wav.h
 *
 * WAV file loader for ESP32 with SD card support
 * Loads WAV files into PSRAM or RAM for playback with Samp class
 *
 * Part of the M16 audio library, and needs M16.h to be previously included
 * Uses the SdFat arduino library - https://github.com/greiman/SdFat
 * 
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef WAV_H_
#define WAV_H_

#include "SdFat.h"
#include "SPI.h"

#define WAV_READ_BUFFER_SIZE 512  // 512 bytes = 1 SD sector (optimal for SdFat)

class Wav {

public:
  /** Constructor */
  Wav() : audioBuffer(nullptr), totalSamples(0), wavSampleRate(44100), numChannels(0), bitsPerSample(16), sd(nullptr) {}

  /** Destructor - free allocated memory */
  ~Wav() {
    if (audioBuffer != nullptr) {
      free(audioBuffer);
      audioBuffer = nullptr;
    }
  }

  /** Initialize SD card with SdFat
   * @param sdObject Reference to SdFs object
   * @param csPin Chip select pin for SD card
   * @param sckPin SPI clock pin
   * @param misoPin SPI MISO pin
   * @param mosiPin SPI MOSI pin
   * @param spiSpeed SPI speed in MHz (default 16)
   * @return true if successful
   */
  bool initSD(SdFs& sdObject, uint8_t csPin, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin, uint8_t spiSpeed = 16) {
    sd = &sdObject;

    // Initialize SPI with custom pins
    SPI.begin(sckPin, misoPin, mosiPin, csPin);

    // Initialize SdFat with SPI configuration
    if (!sd->begin(SdSpiConfig(csPin, DEDICATED_SPI, SD_SCK_MHZ(spiSpeed)))) {
      Serial.println("Wav: SD Card mount failed!");
      return false;
    }
    Serial.println("Wav: SD Card mounted successfully.");
    return true;
  }

  /** Load WAV file from SD card into memory
   * @param filename Path to WAV file (e.g. "/audio.wav")
   * @return true if successful
   */
  bool load(const char* filename) {
    // Check if SD is initialized
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized. Call initSD() first.");
      return false;
    }

    // Free previous buffer if exists
    if (audioBuffer != nullptr) {
      free(audioBuffer);
      audioBuffer = nullptr;
    }

    FsFile wavFile;
    if (!wavFile.open(filename, O_RDONLY)) {
      Serial.println("Wav: Error opening file.");
      return false;
    }

    // Read first 512 bytes to find RIFF/WAVE and chunks
    const int HEADER_SCAN_SIZE = 512;
    uint8_t header[HEADER_SCAN_SIZE];
    size_t bytesRead = wavFile.read(header, HEADER_SCAN_SIZE);
    if (bytesRead < 44) {
      Serial.println("Wav: Invalid WAV header.");
      wavFile.close();
      return false;
    }

    // Find RIFF/WAVE
    int riffPos = findChunkStart(header, bytesRead, "RIFF", 0);
    if (riffPos == -1 || !checkWaveFormat(header, riffPos)) {
      Serial.println("Wav: RIFF/WAVE not found.");
      wavFile.close();
      return false;
    }

    // Find fmt chunk
    int fmtChunkPos = findChunkStart(header, bytesRead, "fmt ", riffPos + 12);
    if (fmtChunkPos == -1) {
      Serial.println("Wav: fmt chunk not found.");
      wavFile.close();
      return false;
    }

    // Parse format chunk (fmtChunkPos points to "fmt " chunk start)
    // +8: audio format, +10: num channels, +12: sample rate, +22: bits per sample
    uint16_t audioFormat = header[fmtChunkPos + 8] | (header[fmtChunkPos + 9] << 8);
    numChannels = header[fmtChunkPos + 10] | (header[fmtChunkPos + 11] << 8);
    wavSampleRate = header[fmtChunkPos + 12] | (header[fmtChunkPos + 13] << 8) |
                 (header[fmtChunkPos + 14] << 16) | (header[fmtChunkPos + 15] << 24);
    bitsPerSample = header[fmtChunkPos + 22] | (header[fmtChunkPos + 23] << 8);

    // Validate PCM format
    if (audioFormat != 1) {
      Serial.printf("Wav: Unsupported audio format %d (only PCM supported)\n", audioFormat);
      wavFile.close();
      return false;
    }

    // Find data chunk
    int dataChunkPos = findChunkStart(header, bytesRead, "data", fmtChunkPos + 24);
    if (dataChunkPos == -1) {
      Serial.println("Wav: data chunk not found.");
      wavFile.close();
      return false;
    }

    uint32_t dataSize = header[dataChunkPos + 4] | (header[dataChunkPos + 5] << 8) |
                        (header[dataChunkPos + 6] << 16) | (header[dataChunkPos + 7] << 24);

    // Print WAV info
    Serial.println("---- WAV Info ----");
    Serial.printf("File: %s\n", filename);
    Serial.printf("Channels: %d\n", numChannels);
    Serial.printf("Sample Rate: %d Hz\n", wavSampleRate);
    Serial.printf("Bits: %d\n", bitsPerSample);
    Serial.printf("Data Size: %d bytes\n", dataSize);
    Serial.println("------------------");

    // Validate bit depth (8, 16, or 24-bit PCM supported)
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24) {
      Serial.printf("Wav: Unsupported bit depth %d (only 8, 16, 24-bit PCM supported)\n", bitsPerSample);
      wavFile.close();
      return false;
    }

    // Calculate buffer size
    // totalSamples is number of frames (mono sample or L/R pair)
    size_t bytesPerSample = bitsPerSample / 8;
    totalSamples = dataSize / (numChannels * bytesPerSample);
    // Always allocate as 16-bit (conversion happens during load)
    size_t bufferSize = totalSamples * numChannels * sizeof(int16_t);

    // Try to allocate in PSRAM first (ESP32 only)
    #if IS_ESP32()
      if (isPSRAMAvailable()) {
        size_t availablePsram = ESP.getFreePsram();
        if (bufferSize > availablePsram) {
          Serial.printf("Wav: File (%d bytes) exceeds PSRAM (%d bytes), truncating.\n",
                        bufferSize, availablePsram);
          bufferSize = availablePsram;
          totalSamples = bufferSize / (numChannels * sizeof(int16_t));
        }
        audioBuffer = (int16_t*)ps_malloc(bufferSize);
        if (audioBuffer) {
          Serial.printf("Wav: Allocated %d bytes in PSRAM\n", bufferSize);
        }
      }
    #endif

    // Fallback to regular RAM if PSRAM failed or unavailable
    if (audioBuffer == nullptr) {
      audioBuffer = (int16_t*)malloc(bufferSize);
      if (audioBuffer) {
        Serial.printf("Wav: Allocated %d bytes in RAM\n", bufferSize);
      } else {
        Serial.println("Wav: Memory allocation failed!");
        wavFile.close();
        return false;
      }
    }

    // Seek to audio data
    if (!wavFile.seekSet(dataChunkPos + 8)) {
      Serial.println("Wav: Failed to seek to audio data.");
      free(audioBuffer);
      audioBuffer = nullptr;
      wavFile.close();
      return false;
    }

    // Read audio data in chunks
    if (!readAudioData(wavFile)) {
      free(audioBuffer);
      audioBuffer = nullptr;
      wavFile.close();
      return false;
    }

    wavFile.close();
    Serial.printf("Wav: Loaded %lu samples\n", totalSamples);
    return true;
  }

  /** Find and load first WAV file on SD card
   * @return true if successful
   */
  bool loadFirst() {
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized.");
      return false;
    }

    FsFile root;
    FsFile file;

    if (!root.open("/")) {
      Serial.println("Wav: Failed to open root directory.");
      return false;
    }

    Serial.println("Wav: Scanning SD card for WAV files...");

    while (file.openNext(&root, O_RDONLY)) {
      char filename[64];
      file.getName(filename, sizeof(filename));

      if (!file.isDir()) {
        Serial.printf("Found file: %s\n", filename);

        if (isWavFile(filename)) {
          Serial.printf("  -> WAV file: %s\n", filename);
          char fullPath[128];
          snprintf(fullPath, sizeof(fullPath), "/%s", filename);
          file.close();
          root.close();
          return load(fullPath);
        }
      }
      file.close();
    }

    root.close();
    Serial.println("Wav: No WAV files found on SD card.");
    return false;
  }

  /** Get pointer to audio buffer
   * @return pointer to int16_t buffer
   */
  inline
  int16_t* getBuffer() const {
    return audioBuffer;
  }

  /** Get total number of sample frames
   * @return number of frames (for stereo, one frame = L+R pair)
   */
  inline
  unsigned long getSampleCount() const {
    return totalSamples;
  }

  /** Get sample rate in Hz
   * @return sample rate
   */
  inline
  uint32_t getSampleRate() const {
    return wavSampleRate;
  }

  /** Get number of channels
   * @return 1 for mono, 2 for stereo
   */
  inline
  uint8_t getChannels() const {
    return numChannels;
  }

  /** Check if WAV file is loaded
   * @return true if buffer contains data
   */
  inline
  bool isLoaded() const {
    return audioBuffer != nullptr && totalSamples > 0;
  }

  /** Print first samples for debugging
   * @param numSamplesToPrint number of samples to print (default 10)
   */
  void printFirstSamples(size_t numSamplesToPrint = 10) {
    if (!isLoaded()) {
      Serial.println("Wav: No audio data loaded.");
      return;
    }

    Serial.println("---- First Samples ----");
    for (size_t i = 0; i < numSamplesToPrint && i < totalSamples; i++) {
      if (numChannels == 1) {
        Serial.printf("Sample %d: %d\n", i, audioBuffer[i]);
      } else {
        int16_t left = audioBuffer[i * 2];
        int16_t right = audioBuffer[i * 2 + 1];
        Serial.printf("Sample %d: L=%d, R=%d\n", i, left, right);
      }
    }
    Serial.println("------------------------");
  }

private:
  int16_t* audioBuffer;
  unsigned long totalSamples;
  uint32_t wavSampleRate;
  uint8_t numChannels;
  uint8_t bitsPerSample;
  SdFs* sd;

  /** Find chunk start position in header
   * @param header Buffer containing WAV header
   * @param headerSize Size of header buffer
   * @param chunkId 4-character chunk identifier (e.g. "RIFF", "fmt ", "data")
   * @param startPos Position to start searching from
   * @return position of chunk, or -1 if not found
   */
  int findChunkStart(const uint8_t* header, size_t headerSize, const char* chunkId, int startPos) {
    for (int i = startPos; i < headerSize - 8; i++) {
      if (header[i] == chunkId[0] && header[i+1] == chunkId[1] &&
          header[i+2] == chunkId[2] && header[i+3] == chunkId[3]) {
        return i;
      }
    }
    return -1;
  }

  /** Check if RIFF header contains WAVE format
   * @param header Buffer containing WAV header
   * @param riffPos Position of RIFF chunk
   * @return true if WAVE format found
   */
  bool checkWaveFormat(const uint8_t* header, int riffPos) {
    return (header[riffPos + 8] == 'W' && header[riffPos + 9] == 'A' &&
            header[riffPos + 10] == 'V' && header[riffPos + 11] == 'E');
  }

  /** Check if filename has WAV extension
   * @param filename Filename to check
   * @return true if .wav or .WAV extension
   */
  bool isWavFile(const char* filename) {
    size_t len = strlen(filename);
    if (len < 4) return false;
    const char* ext = filename + len - 4;
    return (strcasecmp(ext, ".wav") == 0);
  }

  /** Read audio data from file into buffer
   * @param wavFile Open file positioned at start of audio data
   * @return true if successful
   */
  bool readAudioData(FsFile& wavFile) {
    size_t samplesLoaded = 0;
    size_t bufferIndex = 0;
    uint8_t tempBuffer[WAV_READ_BUFFER_SIZE];
    size_t bytesPerSample = bitsPerSample / 8;

    while (samplesLoaded < totalSamples) {
      int readBytes = wavFile.read(tempBuffer, WAV_READ_BUFFER_SIZE);
      if (readBytes <= 0) break;

      size_t chunkSamples = readBytes / (numChannels * bytesPerSample);

      // Convert samples to 16-bit based on bit depth
      if (bitsPerSample == 8) {
        // 8-bit: unsigned (0-255) -> signed 16-bit (-32768 to 32512)
        for (size_t i = 0; i < chunkSamples * numChannels; i++) {
          uint8_t sample8 = tempBuffer[i];
          audioBuffer[bufferIndex++] = (int16_t)((sample8 - 128) << 8);
        }
      } else if (bitsPerSample == 16) {
        // 16-bit: read little-endian signed samples
        for (size_t i = 0; i < chunkSamples * numChannels; i++) {
          audioBuffer[bufferIndex++] = (int16_t)(tempBuffer[i * 2] | (tempBuffer[i * 2 + 1] << 8));
        }
      } else if (bitsPerSample == 24) {
        // 24-bit: read 3 bytes, take upper 16 bits
        for (size_t i = 0; i < chunkSamples * numChannels; i++) {
          size_t bytePos = i * 3;
          // Combine 3 bytes into int32_t (little-endian, sign-extended)
          int32_t sample24 = (int8_t)tempBuffer[bytePos + 2]; // MSB (sign-extended)
          sample24 = (sample24 << 8) | tempBuffer[bytePos + 1];
          sample24 = (sample24 << 8) | tempBuffer[bytePos];
          // Take upper 16 bits by shifting right 8 bits
          audioBuffer[bufferIndex++] = (int16_t)(sample24 >> 8);
        }
      }

      samplesLoaded += chunkSamples;
    }

    return samplesLoaded > 0;
  }
};

#endif /* WAV_H_ */
