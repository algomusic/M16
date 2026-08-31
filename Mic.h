/*
 * Mic.h
 *
 * An I2S audio input class, particularly focused on I2S MEMS microphones.
 *
 * by Andrew R. Brown 2021
 *
 * Based on the Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library. Relies on M16.h
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef MIC_H_
#define MIC_H_

class Mic { 
  public:
    /** Constructor
    * Start a new i2s input stream.
    */
    Mic() {}

    /** 
    * Get the next left samples from the I2S audio input buffer
    */
    inline
    int16_t nextLeft() {
      int frameCount = samples_read / 2;
      if (leftBufIndex >= frameCount) {
        readMic();
        frameCount = samples_read / 2;
      }
      if (samples_read < 2) return 0;
      return (int16_t)inputBuf[leftBufIndex++ * 2];
    }

    /** 
    * Get the next left samples from the I2S audio input buffer
    */
    inline
    int16_t nextRight() {
      int frameCount = samples_read / 2;
      if (rightBufIndex >= frameCount) {
        readMic();
        frameCount = samples_read / 2;
      }
      if (samples_read < 2) return 0;
      return (int16_t)inputBuf[rightBufIndex++ * 2 + 1];
    }

    /** Read one synchronized stereo input frame.
     *  This is preferable to separate channel calls when both inputs are used.
     */
    inline
    void nextStereo(int16_t& left, int16_t& right) {
      int frameCount = samples_read / 2;
      if (leftBufIndex >= frameCount || rightBufIndex >= frameCount) {
        readMic();
        frameCount = samples_read / 2;
      }
      if (frameCount <= 0) {
        left = 0;
        right = 0;
        return;
      }

      int frame = max(leftBufIndex, rightBufIndex);
      if (frame >= frameCount) {
        left = 0;
        right = 0;
        return;
      }
      left = (int16_t)inputBuf[frame * 2];
      right = (int16_t)inputBuf[frame * 2 + 1];
      leftBufIndex = frame + 1;
      rightBufIndex = frame + 1;
    }


  private:
    int samples_read = 0;
    int micGain = 32; // 0 - 64
    static const int16_t bufferLen = 256; // buffer size in samples
    uint16_t inputBuf[bufferLen];
    int leftBufIndex = 0;
    int rightBufIndex = 0;
    uint32_t teensyInputSequence = 0;

    #if IS_ESP8266()
      void readMic() {
        // Mic class not yet implemented for ESP8266
        samples_read = 0;
        leftBufIndex = 0;
        rightBufIndex = 0;
      }
    #elif IS_ESP32()
      inline
      void readMic() {
        // Use new ESP32 I2S API (rx_handle defined in M16.h)
        extern i2s_chan_handle_t rx_handle;
        size_t bytesIn = 0;
        esp_err_t result = i2s_channel_read(rx_handle, inputBuf, bufferLen * sizeof(uint16_t), &bytesIn, portMAX_DELAY);
        if (result == ESP_OK && bytesIn > 0) {
          samples_read = bytesIn / 2; // stereo 16 bit samples
          leftBufIndex = 0;
          rightBufIndex = 0;
        } else {
          samples_read = 0;
        }
      }
    #elif IS_RP2040()
      inline
      void readMic() {
        // Pico uses separate I2S input instance (i2sIn from M16.h)
        // Requires audioInputStart() to be called in setup()
        extern I2S i2sIn;
        extern volatile bool picoInputEnabled;

        if (!picoInputEnabled) {
          samples_read = 0;
          return;
        }

        // Read available samples from I2S input
        int available = i2sIn.available();
        if (available <= 0) {
          samples_read = 0;
          return;
        }

        // Read up to bufferLen/2 stereo samples (each sample is 32-bit containing L+R)
        int samplesToRead = min(available, (int)(bufferLen / 2));
        samples_read = 0;

        for (int i = 0; i < samplesToRead; i++) {
          int32_t sample32 = i2sIn.read();
          // Split 32-bit sample into left (high 16 bits) and right (low 16 bits)
          inputBuf[samples_read * 2] = (int16_t)(sample32 >> 16);       // Left channel
          inputBuf[samples_read * 2 + 1] = (int16_t)(sample32 & 0xFFFF); // Right channel
          samples_read++;
        }
        samples_read *= 2;  // Convert to total samples (L+R)
        leftBufIndex = 0;
        rightBufIndex = 0;
      }
    #elif IS_TEENSY4()
      inline
      void readMic() {
        size_t framesRead = 0;
        bool received = m16TeensyReadInputBlock(
            (int16_t*)inputBuf, bufferLen / 2,
            teensyInputSequence, framesRead);
        samples_read = received ? (int)(framesRead * 2) : 0;
        leftBufIndex = 0;
        rightBufIndex = 0;
      }
    #else
      inline void readMic() { samples_read = 0; }
    #endif
};

#endif /* MIC_H_ */
