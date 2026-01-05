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

#define WAV_READ_BUFFER_SIZE 2048  // 2KB buffer - balance of speed vs stack safety on ESP32

class Wav {

public:
  /** Constructor */
  Wav() : audioBuffer(nullptr), totalSamples(0), wavSampleRate(44100), numChannels(0), bitsPerSample(16), wavAudioFormat(1), sd(nullptr), currentFileIndex(-1), wavFileCount(0), maxAllocationBytes(0), externalBuffer(nullptr), externalBufferSize(0), usingExternalBuffer(false) {
    currentFilename[0] = '\0';
  }

  /** Set maximum bytes to allocate for audio buffer
   * Call before load() to limit allocation (e.g., to reserve space for effects)
   * @param maxBytes Maximum bytes to allocate (0 = no limit, use all available)
   */
  void setMaxAllocation(size_t maxBytes) {
    maxAllocationBytes = maxBytes;
  }

  /** Set external pre-allocated buffer to use instead of allocating
   * Call once at startup to avoid fragmentation from repeated alloc/free
   * @param buffer Pointer to pre-allocated int16_t buffer
   * @param maxBytes Maximum size of buffer in bytes
   */
  void setExternalBuffer(int16_t* buffer, size_t maxBytes) {
    externalBuffer = buffer;
    externalBufferSize = maxBytes;
    usingExternalBuffer = (buffer != nullptr && maxBytes > 0);
    if (usingExternalBuffer) {
      Serial.printf("Wav: Using external buffer (%d bytes)\n", maxBytes);
    }
  }

  /** Destructor - free allocated memory (only if not using external buffer) */
  ~Wav() {
    if (audioBuffer != nullptr && !usingExternalBuffer) {
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
    // Initialize SPI with custom pins (platform-specific)
    #if defined(ARDUINO_ARCH_RP2040)
      SPI.setRX(misoPin);
      SPI.setTX(mosiPin);
      SPI.setSCK(sckPin);
      SPI.begin();
    #elif defined(ESP32)
      SPI.begin(sckPin, misoPin, mosiPin, csPin);
    #else
      SPI.begin();  // Default for other platforms
    #endif
    // Initialize SdFat with SPI configuration
    if (!sd->begin(SdSpiConfig(csPin, DEDICATED_SPI, SD_SCK_MHZ(spiSpeed)))) {
      Serial.println("Wav: SD Card mount failed!");
      return false;
    }
    Serial.println("Wav: SD Card mounted successfully.");

    // List valid WAV files on the SD card
    countWavFiles(true);

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
    // Free previous buffer if exists (only if not using external buffer)
    if (audioBuffer != nullptr && !usingExternalBuffer) {
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
    // Validate audio format (1 = PCM integer, 3 = IEEE float)
    if (audioFormat != 1 && audioFormat != 3) {
      Serial.printf("Wav: Unsupported audio format %d (only PCM and IEEE float supported)\n", audioFormat);
      wavFile.close();
      return false;
    }
    // Store format for use in readAudioData
    wavAudioFormat = audioFormat;
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
    // Validate bit depth (8, 16, 24, or 32-bit supported)
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
      Serial.printf("Wav: Unsupported bit depth %d (only 8, 16, 24, 32-bit supported)\n", bitsPerSample);
      wavFile.close();
      return false;
    }
    // Calculate buffer size
    // totalSamples is number of frames (mono sample or L/R pair)
    size_t bytesPerSample = bitsPerSample / 8;
    totalSamples = dataSize / (numChannels * bytesPerSample);
    // Always store as 16-bit (conversion happens during load)
    size_t bufferSize = totalSamples * numChannels * sizeof(int16_t);

    // Use external buffer if set (avoids fragmentation from repeated alloc/free)
    if (usingExternalBuffer) {
      audioBuffer = externalBuffer;
      if (bufferSize > externalBufferSize) {
        Serial.printf("Wav: File (%d bytes) exceeds buffer (%d bytes), truncating.\n",
                      bufferSize, externalBufferSize);
        bufferSize = externalBufferSize;
        totalSamples = bufferSize / (numChannels * sizeof(int16_t));
      }
      Serial.printf("Wav: Using pre-allocated buffer (%d bytes used)\n", bufferSize);
    } else {
      // Dynamic allocation - try PSRAM first (ESP32 only)
      #if IS_ESP32()
        if (isPSRAMAvailable()) {
          size_t availablePsram = getFreePSRAM();
          // Apply max allocation limit if set (reserves space for effects, etc.)
          // Use 90% of available to leave margin for fragmentation/alignment
          size_t safeAvailable = (availablePsram * 9) / 10;
          size_t maxAlloc = (maxAllocationBytes > 0) ? min(safeAvailable, (maxAllocationBytes * 9) / 10) : safeAvailable;
          if (bufferSize > maxAlloc) {
            Serial.printf("Wav: File (%d bytes) exceeds limit (%d bytes), truncating.\n",
                          bufferSize, maxAlloc);
            bufferSize = maxAlloc;
            totalSamples = bufferSize / (numChannels * sizeof(int16_t));
          }
          // Use safe allocation with size check
          audioBuffer = (int16_t*)psramAllocSafe(bufferSize, "WAV audio");
          if (!audioBuffer) {
            // PSRAM alloc failed, try smaller size
            Serial.printf("Wav: Trying 75%% size...\n");
            bufferSize = (bufferSize * 3) / 4;
            totalSamples = bufferSize / (numChannels * sizeof(int16_t));
            audioBuffer = (int16_t*)psramAllocSafe(bufferSize, "WAV audio (reduced)");
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
    }  // end dynamic allocation branch

    // Seek to audio data
    if (!wavFile.seekSet(dataChunkPos + 8)) {
      Serial.println("Wav: Failed to seek to audio data.");
      if (!usingExternalBuffer) {
        free(audioBuffer);
      }
      audioBuffer = nullptr;
      wavFile.close();
      return false;
    }
    // Read audio data in chunks
    if (!readAudioData(wavFile)) {
      if (!usingExternalBuffer) {
        free(audioBuffer);
      }
      audioBuffer = nullptr;
      wavFile.close();
      return false;
    }
    wavFile.close();
    Serial.printf("Wav: Loaded %lu samples\n", totalSamples);

    // Apply fade in/out to eliminate clicks at loop boundaries
    applyEdgeFades();

    Serial.println("------------------");
    return true;
  }

  /** Apply fade in/out to audio buffer edges
   * Eliminates clicks when audio loops or at file boundaries
   * @param fadeMs Fade duration in milliseconds (default 10ms)
   */
  void applyEdgeFades(float fadeMs = 10.0f) {
    if (!isLoaded()) return;

    // Calculate fade length in samples
    unsigned long fadeSamples = (unsigned long)(fadeMs * wavSampleRate / 1000.0f);
    if (fadeSamples < 2) fadeSamples = 2;
    if (fadeSamples > totalSamples / 4) fadeSamples = totalSamples / 4;  // Max 25% of file

    Serial.printf("Wav: Applying %lums fade (%lu samples)\n", (unsigned long)fadeMs, fadeSamples);

    // Apply cosine fade in at start
    for (unsigned long i = 0; i < fadeSamples; i++) {
      // Cosine curve: 0 to 1 over fadeSamples
      float gain = 0.5f * (1.0f - cosf(M_PI * i / fadeSamples));

      if (numChannels == 1) {
        audioBuffer[i] = (int16_t)(audioBuffer[i] * gain);
      } else {
        audioBuffer[i * 2] = (int16_t)(audioBuffer[i * 2] * gain);
        audioBuffer[i * 2 + 1] = (int16_t)(audioBuffer[i * 2 + 1] * gain);
      }
      // Yield periodically to prevent watchdog timeout
      if ((i & 0xFF) == 0) yield();
    }

    // Apply cosine fade out at end
    for (unsigned long i = 0; i < fadeSamples; i++) {
      unsigned long idx = totalSamples - 1 - i;
      // Cosine curve: 0 to 1 over fadeSamples (reversed)
      float gain = 0.5f * (1.0f - cosf(M_PI * i / fadeSamples));

      if (numChannels == 1) {
        audioBuffer[idx] = (int16_t)(audioBuffer[idx] * gain);
      } else {
        audioBuffer[idx * 2] = (int16_t)(audioBuffer[idx * 2] * gain);
        audioBuffer[idx * 2 + 1] = (int16_t)(audioBuffer[idx * 2 + 1] * gain);
      }
      // Yield periodically to prevent watchdog timeout
      if ((i & 0xFF) == 0) yield();
    }
  }

  /** Count WAV files on SD card and optionally print them
   * @param printFiles If true, print filenames to Serial
   * @return number of WAV files found
   */
  int countWavFiles(bool printFiles = false) {
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized.");
      return 0;
    }
    FsFile root;
    FsFile file;
    int count = 0;
    if (!root.open("/")) {
      Serial.println("Wav: Failed to open root directory.");
      return 0;
    }
    if (printFiles) Serial.println("Wav: WAV files on SD card:");
    while (file.openNext(&root, O_RDONLY)) {
      char filename[64];
      file.getName(filename, sizeof(filename));

      if (!file.isDir() && isValidFile(filename)) {
        if (printFiles) Serial.printf("  [%d] %s\n", count, filename);
        count++;
      }
      file.close();
    }
    root.close();
    wavFileCount = count;
    return count;
  }

  /** Find and load first valid WAV file on SD card (skips invalid files)
   * @return true if successful
   */
  bool loadFirst() {
    // Count files and reset index
    countWavFiles(true);
    if (wavFileCount == 0) {
      Serial.println("Wav: No WAV files found on SD card.");
      return false;
    }
    // Try each file until we find a valid one
    for (int i = 0; i < wavFileCount; i++) {
      Serial.printf("Wav: Trying file %d of %d\n", i + 1, wavFileCount);
      if (loadByIndex(i)) {
        currentFileIndex = i;
        return true;
      }
      Serial.printf("Wav: Skipping invalid file at index %d\n", i);
    }
    Serial.println("Wav: No valid WAV files found on SD card.");
    currentFileIndex = -1;
    return false;
  }

  /** Load next WAV file on SD card (wraps around, skips invalid files)
   * @return true if successful
   */
  bool loadNext() {
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized.");
      return false;
    }
    // If not initialized, start with first file
    if (currentFileIndex < 0 || wavFileCount == 0) {
      return loadFirst();
    }
    // Try each file, skipping invalid ones
    int startIndex = currentFileIndex;
    for (int attempts = 0; attempts < wavFileCount; attempts++) {
      currentFileIndex = (currentFileIndex + 1) % wavFileCount;
      Serial.printf("Wav: Loading file %d of %d\n", currentFileIndex + 1, wavFileCount);
      if (loadByIndex(currentFileIndex)) {
        return true;  // Found a valid file
      }
      Serial.printf("Wav: Skipping invalid file at index %d\n", currentFileIndex);
    }

    // All files failed, restore original index
    currentFileIndex = startIndex;
    Serial.println("Wav: No valid WAV files found.");
    return false;
  }

  /** Load a specific WAV file by number (0-based index)
   * @param fileNumber Index of file to load (0 = first, 1 = second, etc.)
   * @return true if successful
   */
  bool loadNumber(int fileNumber) {
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized.");
      return false;
    }
    // Count files if not already done
    if (wavFileCount == 0) {
      countWavFiles(false);
    }
    // Check bounds
    if (fileNumber < 0) {
      Serial.println("Wav: Invalid file number (must be >= 0).");
      return false;
    }
    if (fileNumber >= wavFileCount) {
      Serial.printf("Wav: File number %d out of range. Only %d WAV file(s) on SD card.\n",
                    fileNumber, wavFileCount);
      return false;
    }
    // Try to load the specific file
    Serial.printf("Wav: Loading file %d of %d\n", fileNumber + 1, wavFileCount);
    if (loadByIndex(fileNumber)) {
      currentFileIndex = fileNumber;
      return true;
    }
    Serial.printf("Wav: File at index %d is not a valid WAV file.\n", fileNumber);
    return false;
  }

  /** Load previous WAV file on SD card (wraps around, skips invalid files)
   * @return true if successful
   */
  bool loadPrev() {
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized.");
      return false;
    }
    // If not initialized, start with first file
    if (currentFileIndex < 0 || wavFileCount == 0) {
      return loadFirst();
    }
    // Try each file backwards, skipping invalid ones
    int startIndex = currentFileIndex;
    for (int attempts = 0; attempts < wavFileCount; attempts++) {
      currentFileIndex = (currentFileIndex - 1 + wavFileCount) % wavFileCount;
      Serial.printf("Wav: Loading file %d of %d\n", currentFileIndex + 1, wavFileCount);
      if (loadByIndex(currentFileIndex)) {
        return true;  // Found a valid file
      }
      Serial.printf("Wav: Skipping invalid file at index %d\n", currentFileIndex);
    }
    // All files failed, restore original index
    currentFileIndex = startIndex;
    Serial.println("Wav: No valid WAV files found.");
    return false;
  }

  /** Get current file index (0-based)
   * @return current file index, or -1 if no file loaded
   */
  int getCurrentFileIndex() const {
    return currentFileIndex;
  }

  /** Get total number of WAV files on SD card
   * @return number of WAV files
   */
  int getFileCount() const {
    return wavFileCount;
  }

  /** Get current filename
   * @return pointer to current filename string
   */
  const char* getFilename() const {
    return currentFilename;
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
  unsigned long getFrameCount() const {
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
  uint8_t wavAudioFormat;     // 1 = PCM integer, 3 = IEEE float
  SdFs* sd;
  int currentFileIndex;       // Current file index (0-based), -1 if none loaded
  int wavFileCount;           // Total number of WAV files on SD card
  char currentFilename[128];  // Current loaded filename
  size_t maxAllocationBytes;  // Max bytes to allocate (0 = no limit)
  int16_t* externalBuffer;    // Pre-allocated external buffer (nullptr if not used)
  size_t externalBufferSize;  // Size of external buffer in bytes
  bool usingExternalBuffer;   // True if using external buffer instead of allocating

  /** Load WAV file by index (0-based)
   * @param index Index of file to load
   * @return true if successful
   */
  bool loadByIndex(int index) {
    if (sd == nullptr || index < 0) {
      return false;
    }

    FsFile root;
    FsFile file;
    int count = 0;

    if (!root.open("/")) {
      Serial.println("Wav: Failed to open root directory.");
      return false;
    }

    while (file.openNext(&root, O_RDONLY)) {
      char filename[64];
      file.getName(filename, sizeof(filename));

      if (!file.isDir() && isValidFile(filename)) {
        if (count == index) {
          // Found the file we want
          snprintf(currentFilename, sizeof(currentFilename), "/%s", filename);
          file.close();
          root.close();
          return load(currentFilename);
        }
        count++;
      }
      file.close();
    }

    root.close();
    Serial.printf("Wav: File index %d not found.\n", index);
    return false;
  }

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

  /** Check if filename is a valid WAV file (not hidden/system)
   * @param filename Filename to check
   * @return true if .wav or .WAV extension and not a hidden file
   */
  bool isValidFile(const char* filename) {
    size_t len = strlen(filename);
    if (len < 4) return false;

    // Skip macOS resource fork files (start with "._")
    if (filename[0] == '.' && filename[1] == '_') return false;

    // Skip other hidden files (start with ".")
    if (filename[0] == '.') return false;

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
    int yieldCounter = 0;
    const int YIELD_INTERVAL = 8;  // Yield every 8 chunks (16KB)

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
      } else if (bitsPerSample == 32) {
        if (wavAudioFormat == 3) {
          // 32-bit IEEE float: convert from -1.0..1.0 to -32768..32767
          for (size_t i = 0; i < chunkSamples * numChannels; i++) {
            size_t bytePos = i * 4;
            // Read 32-bit float (little-endian)
            union { uint32_t u; float f; } converter;
            converter.u = tempBuffer[bytePos] |
                         (tempBuffer[bytePos + 1] << 8) |
                         (tempBuffer[bytePos + 2] << 16) |
                         (tempBuffer[bytePos + 3] << 24);
            // Clamp and convert to 16-bit
            float sample = converter.f;
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            audioBuffer[bufferIndex++] = (int16_t)(sample * 32767.0f);
          }
        } else {
          // 32-bit integer PCM: take upper 16 bits
          for (size_t i = 0; i < chunkSamples * numChannels; i++) {
            size_t bytePos = i * 4;
            // Read 32-bit signed integer (little-endian), take upper 16 bits
            int32_t sample32 = tempBuffer[bytePos] |
                              (tempBuffer[bytePos + 1] << 8) |
                              (tempBuffer[bytePos + 2] << 16) |
                              ((int8_t)tempBuffer[bytePos + 3] << 24);
            audioBuffer[bufferIndex++] = (int16_t)(sample32 >> 16);
          }
        }
      }

      samplesLoaded += chunkSamples;

      // Yield periodically to prevent watchdog timeout during large file loads
      if (++yieldCounter >= YIELD_INTERVAL) {
        yield();
        yieldCounter = 0;
      }
    }

    return samplesLoaded > 0;
  }
};

#endif /* WAV_H_ */