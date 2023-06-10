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
      if (mBufIndex == 0) {
        readMic();
      }
      int16_t val = inputBuf[mBufIndex * 2];
      mBufIndex++;
      if (mBufIndex >= samples_read/2) {
        mBufIndex = 0;
      }
      return val;
    }

    /** 
    * Get the next left samples from the I2S audio input buffer
    */
    inline
    int16_t nextRight() {
      if (mBufIndex == 0) {
        readMic();
      }
      int16_t val = inputBuf[mBufIndex * 2 + 1];
      mBufIndex++;
      if (mBufIndex >= samples_read/2) {
        mBufIndex = 0;
      }
      return val;
    }


  private:
    int samples_read = 0;
    int micGain = 32; // 0 - 64
    static const int16_t bufferLen = dmaBufferLength * 4;
    uint16_t inputBuf[bufferLen];
    int mBufIndex = 0;

    #if IS_ESP8266()
      System.println("Mic class not yet implemented for ESP8266");
      void readMic() {
        // TBC
      }
    #elif IS_ESP32()
      inline
      void readMic() {
        size_t bytesIn = 0;
        esp_err_t result = i2s_read(I2S_NUM_0, inputBuf, bufferLen, &bytesIn, portMAX_DELAY);
        if (result == ESP_OK && bytesIn > 0) {
          samples_read = bytesIn / 2; // stereo 16 bit samples
        }
      }
    #endif
};

#endif /* MIC_H_ */