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

#define WAV_READ_BUFFER_SIZE 8192  // 8KB buffer - faster SD reads while keeping memory reasonable

class Wav {

public:
  /** Constructor */
  Wav() : audioBuffer(nullptr), totalSamples(0), fileTotalSamples(0), wavSampleRate(44100), numChannels(0), bitsPerSample(16), wavAudioFormat(1), dataStart(0), bytesPerFrame(0), sd(nullptr), currentFileIndex(-1), wavFileCount(0), maxAllocationBytes(0), externalBuffer(nullptr), externalBufferSize(0), usingExternalBuffer(false), streamFileOpen(false) {
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
    closeForStreaming();
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
    // On classic ESP32 (WROOM/WROVER), GPIO 6-11 are the Flash SPI bus.
    // Reassigning them crashes the device immediately via interrupt watchdog.
    #if defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && \
        !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    {
      auto isFlashPin = [](uint8_t p) { return p >= 6 && p <= 11; };
      if (isFlashPin(csPin) || isFlashPin(sckPin) || isFlashPin(misoPin) || isFlashPin(mosiPin)) {
        Serial.println("Wav: ERROR - SD pin conflicts with ESP32 Flash SPI (GPIO 6-11). Use VSPI (18/19/23/5) or HSPI (14/12/13/15) pins.");
        return false;
      }
    }
    #endif
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
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized. Call initSD() first.");
      return false;
    }
    if (audioBuffer != nullptr && !usingExternalBuffer) {
      free(audioBuffer);
      audioBuffer = nullptr;
    }
    if (!parseWavHeader(filename)) return false;
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
          return false;
        }
      }
    }  // end dynamic allocation branch

    // Re-open file to read audio data (parseWavHeader closed it)
    FsFile wavFile;
    if (!wavFile.open(currentFilename, O_RDONLY)) {
      Serial.println("Wav: Error reopening file for data.");
      if (!usingExternalBuffer) { free(audioBuffer); }
      audioBuffer = nullptr;
      return false;
    }
    // Seek to audio data
    if (!wavFile.seekSet(dataStart)) {
      Serial.println("Wav: Failed to seek to audio data.");
      if (!usingExternalBuffer) { free(audioBuffer); }
      audioBuffer = nullptr;
      wavFile.close();
      return false;
    }
    // Read audio data in chunks
    if (!readAudioData(wavFile)) {
      if (!usingExternalBuffer) { free(audioBuffer); }
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

  /** Parse WAV header only — no audio data loaded into RAM.
   * Use with readFramesFast() / openForStreaming() to stream from SD.
   * @param filename Path to WAV file (e.g. "/audio.wav")
   * @return true if header is valid
   */
  bool loadHeaderOnly(const char* filename) {
    if (sd == nullptr) {
      Serial.println("Wav: SD card not initialized. Call initSD() first.");
      return false;
    }
    return parseWavHeader(filename);
  }

  /** Find the first valid WAV file on the SD card and parse its header only.
   * No audio data is loaded into RAM — use with openForStreaming() to stream.
   * @return true if a valid WAV file was found
   */
  bool loadHeaderFirst() {
    countWavFiles(true);
    if (wavFileCount == 0) {
      Serial.println("Wav: No WAV files found on SD card.");
      return false;
    }
    for (int i = 0; i < wavFileCount; i++) {
      if (loadHeaderByIndex(i)) {
        currentFileIndex = i;
        return true;
      }
    }
    Serial.println("Wav: No valid WAV files found.");
    currentFileIndex = -1;
    return false;
  }

  /** Find WAV file by index and parse header only (no audio data loaded).
   * @param index 0-based file index
   * @return true if successful
   */
  bool loadHeaderByIndex(int index) {
    if (sd == nullptr || index < 0) return false;
    FsFile root, file;
    int count = 0;
    if (!root.open("/")) return false;
    while (file.openNext(&root, O_RDONLY)) {
      char fname[64];
      file.getName(fname, sizeof(fname));
      if (!file.isDir() && isValidFile(fname)) {
        if (count == index) {
          char fullPath[128];
          snprintf(fullPath, sizeof(fullPath), "/%s", fname);
          file.close(); root.close();
          currentFileIndex = index;
          return loadHeaderOnly(fullPath);
        }
        count++;
      }
      file.close();
    }
    root.close();
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

  /** Get total number of sample frames in the file (untruncated)
   * @return total frames in file
   */
  inline
  unsigned long getFileFrameCount() const {
    return fileTotalSamples;
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

  /** Read frames from current file into destination buffer (no wrap).
   * @param startFrame Frame offset from file start
   * @param frames Number of frames to read
   * @param dest Destination buffer (int16_t, interleaved if stereo)
   * @return true if any frames read
   */
  bool readFrames(uint64_t startFrame, uint32_t frames, int16_t* dest) {
    if (sd == nullptr) return false;
    if (currentFilename[0] == '\0') return false;
    if (fileTotalSamples == 0 || frames == 0) return false;

    uint64_t maxFrame = fileTotalSamples;
    if (startFrame >= maxFrame) startFrame = startFrame % maxFrame;

    FsFile wavFile;
    if (!wavFile.open(currentFilename, O_RDONLY)) {
      return false;
    }

    uint64_t byteOffset = dataStart + (startFrame * bytesPerFrame);
    if (!wavFile.seekSet(byteOffset)) {
      wavFile.close();
      return false;
    }

    uint32_t framesRead = 0;
    static uint8_t tempBuffer[WAV_READ_BUFFER_SIZE];
    while (framesRead < frames) {
      uint32_t framesLeft = frames - framesRead;
      uint32_t maxFramesInChunk = (WAV_READ_BUFFER_SIZE / bytesPerFrame);
      uint32_t chunkFrames = (framesLeft < maxFramesInChunk) ? framesLeft : maxFramesInChunk;
      size_t bytesToRead = (size_t)chunkFrames * bytesPerFrame;
      int readBytes = wavFile.read(tempBuffer, bytesToRead);
      if (readBytes <= 0) break;
      size_t gotFrames = readBytes / bytesPerFrame;
      if (gotFrames == 0) break;

      size_t sampleCount = gotFrames * numChannels;
      int16_t* out = dest + (framesRead * numChannels);

      if (bitsPerSample == 8) {
        for (size_t i = 0; i < sampleCount; i++) {
          uint8_t sample8 = tempBuffer[i];
          out[i] = (int16_t)((sample8 - 128) << 8);
        }
      } else if (bitsPerSample == 16) {
        for (size_t i = 0; i < sampleCount; i++) {
          size_t bytePos = i * 2;
          out[i] = (int16_t)(tempBuffer[bytePos] | (tempBuffer[bytePos + 1] << 8));
        }
      } else if (bitsPerSample == 24) {
        for (size_t i = 0; i < sampleCount; i++) {
          size_t bytePos = i * 3;
          int32_t sample24 = (int8_t)tempBuffer[bytePos + 2];
          sample24 = (sample24 << 8) | tempBuffer[bytePos + 1];
          sample24 = (sample24 << 8) | tempBuffer[bytePos];
          out[i] = (int16_t)(sample24 >> 8);
        }
      } else if (bitsPerSample == 32) {
        if (wavAudioFormat == 3) {
          for (size_t i = 0; i < sampleCount; i++) {
            size_t bytePos = i * 4;
            union { uint32_t u; float f; } converter;
            converter.u = tempBuffer[bytePos] |
                         (tempBuffer[bytePos + 1] << 8) |
                         (tempBuffer[bytePos + 2] << 16) |
                         (tempBuffer[bytePos + 3] << 24);
            float sample = converter.f;
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            out[i] = (int16_t)(sample * 32767.0f);
          }
        } else {
          for (size_t i = 0; i < sampleCount; i++) {
            size_t bytePos = i * 4;
            int32_t sample32 = tempBuffer[bytePos] |
                              (tempBuffer[bytePos + 1] << 8) |
                              (tempBuffer[bytePos + 2] << 16) |
                              ((int8_t)tempBuffer[bytePos + 3] << 24);
            out[i] = (int16_t)(sample32 >> 16);
          }
        }
      }

      framesRead += (uint32_t)gotFrames;
      if (gotFrames < chunkFrames) break;
    }

    wavFile.close();
    return framesRead > 0;
  }

  /** Read frames with wrap-around at file boundaries. */
  bool readFramesWrap(uint64_t startFrame, uint32_t frames, int16_t* dest) {
    if (fileTotalSamples == 0 || frames == 0) return false;
    uint64_t maxFrame = fileTotalSamples;
    if (startFrame >= maxFrame) startFrame = startFrame % maxFrame;
    uint64_t framesToEnd = maxFrame - startFrame;
    if (frames <= framesToEnd) {
      return readFrames(startFrame, frames, dest);
    }
    uint32_t firstFrames = (uint32_t)framesToEnd;
    uint32_t secondFrames = frames - firstFrames;
    bool ok1 = readFrames(startFrame, firstFrames, dest);
    bool ok2 = readFrames(0, secondFrames, dest + (firstFrames * numChannels));
    return ok1 || ok2;
  }

  /** Open a persistent file handle for streaming reads (avoids open/close per chunk).
   *  Call before streaming playback. Call closeForStreaming() when done.
   *  @return true if successful
   */
  bool openForStreaming() {
    if (streamFileOpen) return true; // already open
    if (currentFilename[0] == '\0') return false;
    if (!streamFile.open(currentFilename, O_RDONLY)) {
      Serial.println("Wav: Failed to open file for streaming");
      return false;
    }
    streamFileOpen = true;
    Serial.printf("Wav: Opened '%s' for streaming\n", currentFilename);
    return true;
  }

  /** Close the persistent streaming file handle. */
  void closeForStreaming() {
    if (streamFileOpen) {
      streamFile.close();
      streamFileOpen = false;
    }
  }

  /** Read frames using the persistent file handle (fast, no open/close per call).
   *  Falls back to readFrames() if streaming handle not open.
   */
  bool readFramesFast(uint64_t startFrame, uint32_t frames, int16_t* dest) {
    if (!streamFileOpen) return readFrames(startFrame, frames, dest);
    if (fileTotalSamples == 0 || frames == 0) return false;

    uint64_t maxFrame = fileTotalSamples;
    if (startFrame >= maxFrame) startFrame = startFrame % maxFrame;

    uint64_t byteOffset = dataStart + (startFrame * bytesPerFrame);
    if (!streamFile.seekSet(byteOffset)) {
      // Seek failed — try reopening the file handle
      Serial.println("Wav: Seek failed, reopening stream");
      streamFile.close();
      streamFileOpen = false;
      if (!openForStreaming()) return false;
      if (!streamFile.seekSet(byteOffset)) return false;
    }

    uint32_t framesRead = 0;
    static uint8_t tempBuffer[WAV_READ_BUFFER_SIZE];
    while (framesRead < frames) {
      uint32_t framesLeft = frames - framesRead;
      uint32_t maxFramesInChunk = (WAV_READ_BUFFER_SIZE / bytesPerFrame);
      uint32_t chunkFrames = (framesLeft < maxFramesInChunk) ? framesLeft : maxFramesInChunk;
      size_t bytesToRead = (size_t)chunkFrames * bytesPerFrame;
      int readBytes = streamFile.read(tempBuffer, bytesToRead);
      if (readBytes <= 0) break;
      size_t gotFrames = readBytes / bytesPerFrame;
      if (gotFrames == 0) break;

      size_t sampleCount = gotFrames * numChannels;
      int16_t* out = dest + (framesRead * numChannels);

      if (bitsPerSample == 8) {
        for (size_t i = 0; i < sampleCount; i++) {
          out[i] = (int16_t)((tempBuffer[i] - 128) << 8);
        }
      } else if (bitsPerSample == 16) {
        for (size_t i = 0; i < sampleCount; i++) {
          size_t bytePos = i * 2;
          out[i] = (int16_t)(tempBuffer[bytePos] | (tempBuffer[bytePos + 1] << 8));
        }
      } else if (bitsPerSample == 24) {
        for (size_t i = 0; i < sampleCount; i++) {
          size_t bytePos = i * 3;
          int32_t sample24 = (int8_t)tempBuffer[bytePos + 2];
          sample24 = (sample24 << 8) | tempBuffer[bytePos + 1];
          sample24 = (sample24 << 8) | tempBuffer[bytePos];
          out[i] = (int16_t)(sample24 >> 8);
        }
      } else if (bitsPerSample == 32) {
        if (wavAudioFormat == 3) {
          float* floatBuf = (float*)tempBuffer;
          for (size_t i = 0; i < sampleCount; i++) {
            out[i] = (int16_t)(floatBuf[i] * 32767.0f);
          }
        } else {
          for (size_t i = 0; i < sampleCount; i++) {
            size_t bytePos = i * 4;
            int32_t sample32 = tempBuffer[bytePos] | (tempBuffer[bytePos+1]<<8)
                             | (tempBuffer[bytePos+2]<<16) | ((int8_t)tempBuffer[bytePos+3]<<24);
            out[i] = (int16_t)(sample32 >> 16);
          }
        }
      }
      framesRead += gotFrames;
    }
    return framesRead > 0;
  }

  /** Read frames with wrap-around using persistent file handle. */
  bool readFramesWrapFast(uint64_t startFrame, uint32_t frames, int16_t* dest) {
    if (fileTotalSamples == 0 || frames == 0) return false;
    uint64_t maxFrame = fileTotalSamples;
    if (startFrame >= maxFrame) startFrame = startFrame % maxFrame;
    uint64_t framesToEnd = maxFrame - startFrame;
    if (frames <= framesToEnd) {
      return readFramesFast(startFrame, frames, dest);
    }
    uint32_t firstFrames = (uint32_t)framesToEnd;
    uint32_t secondFrames = frames - firstFrames;
    bool ok1 = readFramesFast(startFrame, firstFrames, dest);
    bool ok2 = readFramesFast(0, secondFrames, dest + (firstFrames * numChannels));
    return ok1 || ok2;
  }

  // -------------------------------------------------------------------------
  // Streaming helpers (generic, reusable)
  // -------------------------------------------------------------------------

  /** Normalize a frame index to [0, fileFrames). */
  static inline int64_t normalizeFrame(int64_t frame, int64_t fileFrames) {
    if (fileFrames <= 0) return 0;
    return ((frame % fileFrames) + fileFrames) % fileFrames;
  }

  /** Normalize loadedStart/loadedEnd to the playhead epoch (handles wrap). */
  static inline void normalizeEpoch(int64_t &loadedStart, int64_t &loadedEnd,
                                    int64_t playHead, int64_t fileFrames) {
    if (fileFrames <= 0) return;
    if (loadedStart > playHead) {
      int64_t shift = ((loadedStart - playHead - 1) / fileFrames + 1) * fileFrames;
      loadedStart -= shift;
      loadedEnd -= shift;
    } else if (playHead - loadedStart >= fileFrames) {
      int64_t shift = (playHead - loadedStart) / fileFrames * fileFrames;
      loadedStart += shift;
      loadedEnd += shift;
    }
  }

  /** Compute signed headroom relative to playhead and direction. */
  static inline int64_t computeHeadroom(int64_t loadedStart, int64_t loadedEnd,
                                       int64_t playHead, int direction) {
    return (direction > 0) ? (loadedEnd - playHead) : (playHead - loadedStart);
  }

  /** Write a chunk of file data into a ring buffer with extension mirror. */
  static inline bool writeChunkToRing(Wav& wav,
                                      int16_t* extendedBuffer,
                                      unsigned long bufferFrames,
                                      unsigned long extensionFrames,
                                      uint64_t fileFrames,
                                      int64_t filePos,
                                      unsigned long frames,
                                      int channels) {
    if (!extendedBuffer || bufferFrames == 0 || fileFrames == 0 || frames == 0) {
      return false;
    }

    int64_t ff = (int64_t)fileFrames;
    int64_t normPos = normalizeFrame(filePos, ff);
    unsigned long ringIdx = (unsigned long)(normPos % (int64_t)bufferFrames);

    if (ringIdx + frames <= bufferFrames) {
      if (!wav.readFramesWrapFast((uint64_t)normPos, frames,
                                  &extendedBuffer[ringIdx * channels])) {
        return false;
      }
    } else {
      unsigned long firstFrames = bufferFrames - ringIdx;
      unsigned long secondFrames = frames - firstFrames;
      if (!wav.readFramesWrapFast((uint64_t)normPos, firstFrames,
                                  &extendedBuffer[ringIdx * channels])) {
        return false;
      }
      uint64_t secondPos = (uint64_t)normalizeFrame(normPos + (int64_t)firstFrames, ff);
      if (!wav.readFramesWrapFast(secondPos, secondFrames,
                                  &extendedBuffer[0])) {
        return false;
      }
    }

    // Update extension mirror if we wrote near ring start
    bool overlapsExtension = false;
    if (ringIdx + frames > bufferFrames) {
      overlapsExtension = true;
    } else if (ringIdx < extensionFrames) {
      overlapsExtension = true;
    }
    if (overlapsExtension) {
      memcpy(&extendedBuffer[bufferFrames * channels],
             &extendedBuffer[0],
             extensionFrames * channels * sizeof(int16_t));
    }

    return true;
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

  /** Compress a mono PCM WAV to 4-bit IMA ADPCM at 11025 Hz and save to SD.
   * Offline one-time operation — output is used by exportToHeader().
   * Input can be any bit depth and sample rate that Wav::load() supports.
   * @param inputPath  Source mono PCM WAV path on SD
   * @param outputPath Destination ADPCM WAV path on SD
   * @return true if successful
   */
  bool compress(const char* inputPath, const char* outputPath) {
    if (!sd) { Serial.println("Wav: compress: SD not initialized"); return false; }
    if (!parseWavHeader(inputPath)) return false;
    if (numChannels != 1) { Serial.println("Wav: compress: mono input required"); return false; }

    const uint32_t OUT_RATE      = 11025;
    const uint32_t BLOCK_BYTES   = 512;
    const uint32_t BLOCK_SAMPLES = 1 + (BLOCK_BYTES - 4) * 2;  // 1017 samples per block

    uint32_t outSamples = (uint32_t)((uint64_t)fileTotalSamples * OUT_RATE / wavSampleRate);
    uint32_t numBlocks  = (outSamples + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
    uint32_t dataBytes  = numBlocks * BLOCK_BYTES;

    // Load entire source into temp buffer (compress is an offline one-time op)
    uint32_t srcByteSize = fileTotalSamples * (bitsPerSample / 8);
    uint8_t* srcRaw = nullptr;
    #if IS_ESP32()
    if (srcByteSize > 8192 && isPSRAMAvailable())
      srcRaw = (uint8_t*)psramAllocSafe(srcByteSize, "compress src");
    #endif
    if (!srcRaw) srcRaw = (uint8_t*)malloc(srcByteSize);
    if (!srcRaw) {
      Serial.printf("Wav: compress: need %lu bytes for source buffer\n", (unsigned long)srcByteSize);
      return false;
    }
    {
      FsFile in;
      if (!in.open(currentFilename, O_RDONLY)) { free(srcRaw); return false; }
      in.seekSet(dataStart);
      in.read(srcRaw, srcByteSize);
      in.close();
    }

    FsFile out;
    if (!out.open(outputPath, O_WRONLY | O_CREAT | O_TRUNC)) {
      free(srcRaw);
      Serial.printf("Wav: compress: can't create %s\n", outputPath);
      return false;
    }

    // Write IMA ADPCM WAV header
    uint32_t byteRate = (uint32_t)((uint64_t)OUT_RATE * BLOCK_BYTES / BLOCK_SAMPLES);
    _writeU32(out, 0x46464952UL);          // "RIFF"
    _writeU32(out, 52 + dataBytes);        // file size - 8
    _writeU32(out, 0x45564157UL);          // "WAVE"
    _writeU32(out, 0x20746D66UL);          // "fmt "
    _writeU32(out, 20);                    // fmt chunk size (extended)
    _writeU16(out, 0x0011);               // IMA ADPCM
    _writeU16(out, 1);                    // mono
    _writeU32(out, OUT_RATE);
    _writeU32(out, byteRate);
    _writeU16(out, (uint16_t)BLOCK_BYTES);
    _writeU16(out, 4);                    // bitsPerSample
    _writeU16(out, 2);                    // cbSize
    _writeU16(out, (uint16_t)BLOCK_SAMPLES);
    _writeU32(out, 0x74636166UL);          // "fact"
    _writeU32(out, 4);
    _writeU32(out, outSamples);
    _writeU32(out, 0x61746164UL);          // "data"
    _writeU32(out, dataBytes);

    // Encode: phase accumulator maps output sample index → source sample index (16.16)
    uint32_t srcStep  = (uint32_t)(((uint64_t)wavSampleRate << 16) / OUT_RATE);
    uint64_t srcPhase = 0;
    int32_t  predictor = 0;
    int      stepIdx   = 0;
    uint8_t  blockBuf[512];

    for (uint32_t b = 0; b < numBlocks; b++) {
      memset(blockBuf, 0, BLOCK_BYTES);

      // Block header: first sample is the starting predictor
      uint32_t si = (uint32_t)(srcPhase >> 16);
      int32_t  fr = (int32_t)((srcPhase >> 1) & 0x7FFF);
      int32_t  s0 = _getSrc(srcRaw, si,     fileTotalSamples, bitsPerSample);
      int32_t  s1 = _getSrc(srcRaw, si + 1, fileTotalSamples, bitsPerSample);
      predictor = s0 + (((s1 - s0) * fr) >> 15);
      predictor = constrain(predictor, -32768, 32767);
      blockBuf[0] = (uint8_t)( predictor        & 0xFF);
      blockBuf[1] = (uint8_t)((predictor >> 8)  & 0xFF);
      blockBuf[2] = (uint8_t)stepIdx;
      blockBuf[3] = 0;
      srcPhase += srcStep;

      // Encode BLOCK_SAMPLES-1 remaining samples as 4-bit nibbles, 2 per byte
      for (uint32_t s = 1; s < BLOCK_SAMPLES; s++) {
        si = (uint32_t)(srcPhase >> 16);
        fr = (int32_t)((srcPhase >> 1) & 0x7FFF);
        s0 = _getSrc(srcRaw, si,     fileTotalSamples, bitsPerSample);
        s1 = _getSrc(srcRaw, si + 1, fileTotalSamples, bitsPerSample);
        int16_t samp = (int16_t)constrain(s0 + (((s1 - s0) * fr) >> 15), -32767, 32767);
        int8_t  nib  = _adpcmEncode(samp, predictor, stepIdx);
        uint32_t byteOff = 4 + (s - 1) / 2;
        if ((s - 1) & 1) blockBuf[byteOff] |= (uint8_t)((nib & 0x0F) << 4);
        else              blockBuf[byteOff]  = (uint8_t)(nib  & 0x0F);
        srcPhase += srcStep;
      }
      out.write(blockBuf, BLOCK_BYTES);
      if ((b & 0x07) == 0) yield();
    }

    free(srcRaw);
    out.close();
    Serial.printf("Wav: compress → %s (%lu blocks, %lu bytes)\n",
                  outputPath, (unsigned long)numBlocks, (unsigned long)dataBytes);
    return true;
  }

  /** Write an ADPCM WAV file as a C++ PROGMEM byte array header to SD.
   * Copy the generated .h file from SD into your sketch folder, then #include it.
   * @param adpcmPath  Source ADPCM WAV on SD (created by compress())
   * @param headerPath Output .h file path on SD (e.g. "/snare_adpcm.h")
   * @param arrayName  C identifier prefix for defines and array (e.g. "SNARE")
   * @return true if successful
   */
  bool exportToHeader(const char* adpcmPath, const char* headerPath, const char* arrayName) {
    if (!sd) { Serial.println("Wav: exportToHeader: SD not initialized"); return false; }

    FsFile src;
    if (!src.open(adpcmPath, O_RDONLY)) {
      Serial.printf("Wav: exportToHeader: can't open %s\n", adpcmPath);
      return false;
    }
    uint32_t fileSize = (uint32_t)src.fileSize();

    // Read header to extract metadata for the #defines
    uint8_t hdr[128] = {};
    src.read(hdr, min(fileSize, (uint32_t)sizeof(hdr)));
    src.seekSet(0);

    int fmtPos  = findChunkStart(hdr, sizeof(hdr), "fmt ",  12);
    int dataPos = findChunkStart(hdr, sizeof(hdr), "data", fmtPos + 24);
    if (fmtPos < 0 || dataPos < 0) {
      src.close();
      Serial.println("Wav: exportToHeader: invalid ADPCM WAV");
      return false;
    }
    uint32_t srcRate    = hdr[fmtPos+12] | ((uint32_t)hdr[fmtPos+13]<<8) |
                          ((uint32_t)hdr[fmtPos+14]<<16) | ((uint32_t)hdr[fmtPos+15]<<24);
    uint16_t blockAlign = hdr[fmtPos+20] | (hdr[fmtPos+21] << 8);
    uint16_t spb        = hdr[fmtPos+26] | (hdr[fmtPos+27] << 8);
    uint32_t dataChunk  = hdr[dataPos+4] | ((uint32_t)hdr[dataPos+5]<<8) |
                          ((uint32_t)hdr[dataPos+6]<<16) | ((uint32_t)hdr[dataPos+7]<<24);
    uint32_t totalSamp  = (dataChunk / blockAlign) * spb;

    FsFile hdrFile;
    if (!hdrFile.open(headerPath, O_WRONLY | O_CREAT | O_TRUNC)) {
      src.close();
      Serial.printf("Wav: exportToHeader: can't create %s\n", headerPath);
      return false;
    }

    hdrFile.print("// Auto-generated by Wav::exportToHeader() — do not edit\n");
    hdrFile.printf("// %s | %lu Hz | IMA ADPCM | %lu samples | %lu bytes\n",
                   adpcmPath, (unsigned long)srcRate, (unsigned long)totalSamp, (unsigned long)fileSize);
    hdrFile.print("#pragma once\n#include <pgmspace.h>\n\n");
    hdrFile.printf("#define %s_SAMPLE_RATE   %lu\n", arrayName, (unsigned long)srcRate);
    hdrFile.printf("#define %s_TOTAL_SAMPLES %lu\n", arrayName, (unsigned long)totalSamp);
    hdrFile.printf("#define %s_BLOCK_SIZE    %u\n",  arrayName, blockAlign);
    hdrFile.printf("#define %s_SPB           %u\n",  arrayName, spb);
    hdrFile.printf("#define %s_DATA_SIZE     %lu\n\n",arrayName, (unsigned long)fileSize);
    hdrFile.printf("const uint8_t %s_DATA[] PROGMEM = {\n", arrayName);

    uint8_t row[16];
    uint32_t written = 0;
    while (written < fileSize) {
      uint32_t toRead = min(16UL, fileSize - written);
      src.read(row, toRead);
      hdrFile.print("  ");
      for (uint32_t i = 0; i < toRead; i++) {
        hdrFile.printf("0x%02X", row[i]);
        if (written + i + 1 < fileSize) hdrFile.print(",");
        if (i < toRead - 1) hdrFile.print(" ");
      }
      hdrFile.print("\n");
      written += toRead;
      if ((written & 0x1FF) == 0) yield();
    }
    hdrFile.print("};\n");

    src.close();
    hdrFile.close();
    Serial.printf("Wav: exported %lu bytes → %s (%s_DATA[])\n",
                  (unsigned long)fileSize, headerPath, arrayName);
    return true;
  }

  /** Decode a PROGMEM ADPCM byte array (from exportToHeader) into PSRAM or RAM.
   * Checks available space before allocating — fails cleanly if insufficient.
   * After return, getBuffer() and getFrameCount() work identically to after load().
   * @param pgmData  PROGMEM pointer to the byte array (e.g. SNARE_DATA)
   * @param dataSize Total bytes in array (e.g. SNARE_DATA_SIZE)
   * @return true if decoded and ready for setTable() or Samp::loadFromFlash()
   */
  bool loadFromFlash(const uint8_t* pgmData, uint32_t dataSize) {
    // Copy header region to stack for parsing
    uint8_t hdr[128] = {};
    for (uint32_t i = 0; i < min(dataSize, (uint32_t)sizeof(hdr)); i++)
      hdr[i] = pgm_read_byte(pgmData + i);

    int riffPos = findChunkStart(hdr, sizeof(hdr), "RIFF", 0);
    int fmtPos  = findChunkStart(hdr, sizeof(hdr), "fmt ", riffPos + 12);
    int dataPos = findChunkStart(hdr, sizeof(hdr), "data", fmtPos + 24);
    if (riffPos < 0 || fmtPos < 0 || dataPos < 0) {
      Serial.println("Wav: loadFromFlash: bad WAV header"); return false;
    }

    uint16_t fmt        = hdr[fmtPos+8]  | (hdr[fmtPos+9]  << 8);
    uint16_t channels   = hdr[fmtPos+10] | (hdr[fmtPos+11] << 8);
    uint32_t srcRate    = hdr[fmtPos+12] | ((uint32_t)hdr[fmtPos+13]<<8) |
                          ((uint32_t)hdr[fmtPos+14]<<16) | ((uint32_t)hdr[fmtPos+15]<<24);
    uint16_t blockAlign = hdr[fmtPos+20] | (hdr[fmtPos+21] << 8);
    uint16_t spb        = hdr[fmtPos+26] | (hdr[fmtPos+27] << 8);
    uint32_t rawDataOff = (uint32_t)(dataPos + 8);

    if (fmt != 0x0011 || channels != 1) {
      Serial.println("Wav: loadFromFlash: requires mono IMA ADPCM"); return false;
    }
    uint32_t numBlocks = (dataSize - rawDataOff) / blockAlign;
    uint32_t srcSamps  = numBlocks * spb;
    uint32_t outFrames = (uint32_t)((uint64_t)srcSamps * SAMPLE_RATE / srcRate);
    size_t   needed    = outFrames * sizeof(int16_t);

    Serial.printf("Wav: loadFromFlash: %lu src → %lu frames (%u bytes needed)\n",
                  (unsigned long)srcSamps, (unsigned long)outFrames, needed);

    if (audioBuffer && !usingExternalBuffer) { free(audioBuffer); audioBuffer = nullptr; }

    // Allocate output buffer: PSRAM preferred, internal RAM fallback
    #if IS_ESP32()
    if (isPSRAMAvailable() && getFreePSRAM() > needed + 32768)
      audioBuffer = (int16_t*)psramAllocSafe(needed, "flash decode");
    if (!audioBuffer && (size_t)ESP.getFreeHeap() > needed + 32768)
      audioBuffer = (int16_t*)malloc(needed);
    #else
    audioBuffer = (int16_t*)malloc(needed);
    #endif
    if (!audioBuffer) {
      Serial.printf("Wav: loadFromFlash: need %u bytes, alloc failed\n", needed);
      return false;
    }

    // Temp buffer for decoded samples at srcRate (before upsample)
    size_t   decSize = srcSamps * sizeof(int16_t);
    int16_t* decoded = nullptr;
    #if IS_ESP32()
    if (isPSRAMAvailable() && getFreePSRAM() > decSize + 8192)
      decoded = (int16_t*)psramAllocSafe(decSize, "adpcm tmp");
    #endif
    if (!decoded) decoded = (int16_t*)malloc(decSize);
    if (!decoded) {
      free(audioBuffer); audioBuffer = nullptr;
      Serial.println("Wav: loadFromFlash: can't alloc decode buffer");
      return false;
    }

    // Decode ADPCM blocks from PROGMEM into decoded[] at srcRate
    uint8_t  blockBuf[512];
    uint32_t decIdx  = 0;
    int32_t  pred    = 0;
    int      stepIdx = 0;

    for (uint32_t b = 0; b < numBlocks && decIdx < srcSamps; b++) {
      uint32_t base = rawDataOff + b * blockAlign;
      uint32_t bsz  = min((uint32_t)blockAlign, (uint32_t)sizeof(blockBuf));
      for (uint32_t i = 0; i < bsz; i++)
        blockBuf[i] = pgm_read_byte(pgmData + base + i);

      pred    = (int16_t)(blockBuf[0] | (blockBuf[1] << 8));
      stepIdx = max(0, min(88, (int)blockBuf[2]));
      if (decIdx < srcSamps) decoded[decIdx++] = (int16_t)pred;

      uint32_t nibbles = (bsz - 4) * 2;
      for (uint32_t n = 0; n < nibbles && decIdx < srcSamps; n++) {
        uint8_t byte   = blockBuf[4 + n / 2];
        int8_t  nibble = (n & 1) ? (int8_t)(byte >> 4) : (int8_t)(byte & 0x0F);
        decoded[decIdx++] = _adpcmDecode(nibble, pred, stepIdx);
      }
      if ((b & 0x0F) == 0) yield();
    }

    // Catmull-Rom upsample decoded[] (srcRate) → audioBuffer[] (SAMPLE_RATE)
    float ratio = (float)srcRate / (float)SAMPLE_RATE;
    for (uint32_t i = 0; i < outFrames; i++) {
      float    sf = i * ratio;
      uint32_t s1 = (uint32_t)sf;
      float    t  = sf - s1;
      int32_t p0 = decoded[(s1 > 0)           ? s1 - 1 : 0];
      int32_t p1 = decoded[s1];
      int32_t p2 = decoded[(s1+1 < srcSamps)  ? s1+1   : srcSamps-1];
      int32_t p3 = decoded[(s1+2 < srcSamps)  ? s1+2   : srcSamps-1];
      float t2   = t * t, t3 = t2 * t;
      float val  = 0.5f * ((2*p1) + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t2 + (-p0+3*p1-3*p2+p3)*t3);
      audioBuffer[i] = (int16_t)max(-32767.0f, min(32767.0f, val));
      if ((i & 0xFFF) == 0) yield();
    }

    free(decoded);
    totalSamples     = outFrames;
    fileTotalSamples = outFrames;
    wavSampleRate    = SAMPLE_RATE;
    numChannels      = 1;
    snprintf(currentFilename, sizeof(currentFilename), "[flash]");
    Serial.printf("Wav: loadFromFlash OK → %lu frames at %d Hz\n",
                  (unsigned long)outFrames, SAMPLE_RATE);
    return true;
  }

private:
  int16_t* audioBuffer;
  unsigned long totalSamples;
  unsigned long fileTotalSamples;
  uint32_t wavSampleRate;
  uint8_t numChannels;
  uint8_t bitsPerSample;
  uint8_t wavAudioFormat;     // 1 = PCM integer, 3 = IEEE float
  uint32_t dataStart;
  uint32_t bytesPerFrame;
  SdFs* sd;
  int currentFileIndex;       // Current file index (0-based), -1 if none loaded
  int wavFileCount;           // Total number of WAV files on SD card
  char currentFilename[128];  // Current loaded filename
  size_t maxAllocationBytes;  // Max bytes to allocate (0 = no limit)
  int16_t* externalBuffer;    // Pre-allocated external buffer (nullptr if not used)
  size_t externalBufferSize;  // Size of external buffer in bytes
  bool usingExternalBuffer;   // True if using external buffer instead of allocating
  FsFile streamFile;          // Persistent file handle for streaming reads
  bool streamFileOpen;        // True if streamFile is open

  /** Parse WAV header without loading audio data.
   * Sets dataStart, fileTotalSamples, numChannels, wavSampleRate, etc.
   * @param filename Full path to WAV file
   * @return true if header is valid
   */
  bool parseWavHeader(const char* filename) {
    FsFile wavFile;
    if (!wavFile.open(filename, O_RDONLY)) {
      Serial.println("Wav: Error opening file.");
      return false;
    }
    const int HEADER_SCAN_SIZE = 512;
    uint8_t header[HEADER_SCAN_SIZE];
    size_t bytesRead = wavFile.read(header, HEADER_SCAN_SIZE);
    if (bytesRead < 44) {
      Serial.println("Wav: Invalid WAV header.");
      wavFile.close(); return false;
    }
    int riffPos = findChunkStart(header, bytesRead, "RIFF", 0);
    if (riffPos == -1 || !checkWaveFormat(header, riffPos)) {
      Serial.println("Wav: RIFF/WAVE not found.");
      wavFile.close(); return false;
    }
    int fmtPos = findChunkStart(header, bytesRead, "fmt ", riffPos + 12);
    if (fmtPos == -1) {
      Serial.println("Wav: fmt chunk not found.");
      wavFile.close(); return false;
    }
    uint16_t audioFormat = header[fmtPos + 8] | (header[fmtPos + 9] << 8);
    numChannels   = header[fmtPos + 10] | (header[fmtPos + 11] << 8);
    wavSampleRate = header[fmtPos + 12] | (header[fmtPos + 13] << 8) |
                   (header[fmtPos + 14] << 16) | (header[fmtPos + 15] << 24);
    bitsPerSample = header[fmtPos + 22] | (header[fmtPos + 23] << 8);
    if (audioFormat != 1 && audioFormat != 3) {
      Serial.printf("Wav: Unsupported audio format %d\n", audioFormat);
      wavFile.close(); return false;
    }
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
      Serial.printf("Wav: Unsupported bit depth %d\n", bitsPerSample);
      wavFile.close(); return false;
    }
    wavAudioFormat = audioFormat;
    int dataPos = findChunkStart(header, bytesRead, "data", fmtPos + 24);
    if (dataPos == -1) {
      Serial.println("Wav: data chunk not found.");
      wavFile.close(); return false;
    }
    uint32_t dataSize = header[dataPos + 4] | (header[dataPos + 5] << 8) |
                        (header[dataPos + 6] << 16) | (header[dataPos + 7] << 24);
    bytesPerFrame    = numChannels * (bitsPerSample / 8);
    totalSamples     = dataSize / bytesPerFrame;
    fileTotalSamples = totalSamples;
    dataStart        = dataPos + 8;
    snprintf(currentFilename, sizeof(currentFilename), "%s", filename);
    wavFile.close();
    Serial.println("---- WAV Info ----");
    Serial.printf("File: %s\n", currentFilename);
    Serial.printf("Channels: %d, Rate: %lu Hz, Bits: %d\n", numChannels, wavSampleRate, bitsPerSample);
    Serial.printf("Frames: %lu\n", fileTotalSamples);
    Serial.println("------------------");
    return true;
  }

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
    static uint8_t tempBuffer[WAV_READ_BUFFER_SIZE];
    size_t bytesPerSample = bitsPerSample / 8;
    int yieldCounter = 0;
    const int YIELD_INTERVAL = 8;  // Yield every 8 chunks (16KB)

    while (samplesLoaded < totalSamples) {
      int readBytes = wavFile.read(tempBuffer, WAV_READ_BUFFER_SIZE);
      if (readBytes <= 0) break;

      size_t chunkSamples = readBytes / (numChannels * bytesPerSample);
      // Clamp to remaining frames to avoid buffer overrun when file is truncated
      size_t remainingSamples = (samplesLoaded < totalSamples) ? (totalSamples - samplesLoaded) : 0;
      if (chunkSamples > remainingSamples) {
        chunkSamples = remainingSamples;
      }
      if (chunkSamples == 0) break;

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

  // --- ADPCM helpers ---

  static void _writeU16(FsFile& f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v), (uint8_t)(v >> 8) };
    f.write(b, 2);
  }

  static void _writeU32(FsFile& f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v), (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
    f.write(b, 4);
  }

  static int16_t _getSrc(const uint8_t* raw, uint32_t i, uint32_t total, uint8_t bits) {
    if (i >= total) return 0;
    if (bits == 16) { uint32_t bp = i*2; return (int16_t)(raw[bp] | (raw[bp+1] << 8)); }
    if (bits == 8)  { return (int16_t)((raw[i] - 128) << 8); }
    if (bits == 24) {
      uint32_t bp = i*3;
      int32_t s = (int8_t)raw[bp+2];
      s = (s << 8) | raw[bp+1];
      s = (s << 8) | raw[bp];
      return (int16_t)(s >> 8);
    }
    return 0;
  }

  static int8_t _adpcmEncode(int16_t sample, int32_t& predictor, int& stepIdx) {
    static const int8_t  idxTab[16]   = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
    static const int16_t stepTab[89]  = {
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,
      66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,
      371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,
      1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
      5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
      18500,20350,22385,24623,27086,29794,32767
    };
    int16_t step   = stepTab[stepIdx];
    int32_t diff   = sample - predictor;
    int8_t  nibble = 0;
    if (diff < 0) { nibble = 8; diff = -diff; }
    if (diff >= step)       { nibble |= 4; diff -= step; }
    if (diff >= step >> 1)  { nibble |= 2; diff -= step >> 1; }
    if (diff >= step >> 2)  { nibble |= 1; }
    int32_t vpdiff = step >> 3;
    if (nibble & 4) vpdiff += step;
    if (nibble & 2) vpdiff += step >> 1;
    if (nibble & 1) vpdiff += step >> 2;
    if (nibble & 8) predictor -= vpdiff; else predictor += vpdiff;
    predictor = constrain(predictor, -32768, 32767);
    stepIdx   = max(0, min(88, stepIdx + idxTab[nibble]));
    return nibble;
  }

  static int16_t _adpcmDecode(int8_t nibble, int32_t& predictor, int& stepIdx) {
    static const int8_t  idxTab[16]   = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
    static const int16_t stepTab[89]  = {
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,
      66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,
      371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,
      1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
      5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
      18500,20350,22385,24623,27086,29794,32767
    };
    int16_t step   = stepTab[stepIdx];
    int32_t vpdiff = step >> 3;
    if (nibble & 4) vpdiff += step;
    if (nibble & 2) vpdiff += step >> 1;
    if (nibble & 1) vpdiff += step >> 2;
    if (nibble & 8) predictor -= vpdiff; else predictor += vpdiff;
    predictor = constrain(predictor, -32768, 32767);
    stepIdx   = max(0, min(88, stepIdx + idxTab[nibble & 0x07]));
    return (int16_t)predictor;
  }
};

#endif /* WAV_H_ */
