/*
 * by Andrew R. Brown 2023
 *
 * Light weight MIDI message send and recieve functionalty for M16
 * 
 * M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012
 *
 * This file is part of the M16 audio library.
 *
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef MIDI16_H_
#define MIDI16_H_

class MIDI16 {

public:
  static const uint8_t noteOn = 0x90;
  static const uint8_t noteOff = 0x80;
  static const uint8_t polyphonicAfterTouch = 0xA0;
  static const uint8_t controlChange = 0xB0;
  static const uint8_t programChange = 0xC0;
  static const uint8_t channelAfterTouch = 0xD0;
  static const uint8_t pitchBend = 0xE0;
  static const uint8_t clock = 0xF8;
  static const uint8_t start = 0xFA;
  static const uint8_t cont = 0xFB;
  static const uint8_t stop = 0xFC;

  /** Constructor */
  MIDI16() {
    // Uses sProject PCB pins for ESP32-S3 hardware serial
    // ESP8266 ignores this and uses default Serial pins (GPIO 3 for rx and 1 for tx)
    MIDI16(37, 38);  // 34, 35 sProject board v3
  }

  /** Constructor 
   * @param rx The ESP32 pin to recieve MIDI data on
   * @param tx The ESP32 pin to transmit MIDI data on
  */
  MIDI16(int rx, int tx):recievePin(rx), transmitPin(tx) {
    delete[] message; // remove any previous memory allocation
    message = new uint8_t[3]; // create a new MIDI message buffer
    #if IS_ESP32()
    // handle MIDI on a separate Serial bus to keep the main Serial availible for println debugging
    #include <HardwareSerial.h>
    Serial2.begin(31250, SERIAL_8N1, rx, tx);
    #endif
    #if IS_ESP8266()
    // use default Serial bus and pins (GPIO 3 for rx and 1 for tx), may mess with debug printing
    Serial.begin(31250);
    #endif
  }

  // send MIDI messages - values from 0 - 127 only
  void sendNoteOn(int channel, int pitch, int velocity) {
    writeByte(144 + channel);
    writeByte(pitch);
    writeByte(velocity);
  }

  void sendNoteOff(int channel, int pitch, int velocity) {
    writeByte(128 + channel);
    writeByte(pitch);
    writeByte(velocity);
  }

  void sendControlChange(int channel, int control, int value) {
    writeByte(176 + channel); // 0xB0
    writeByte(control);
    writeByte(value);
  }

  void sendClock() {
    writeByte(248); //0xF8
  }

  void sendStart() {
    writeByte(250); //0xFA
  }

  void sendContinue() {
    writeByte(251); //0xFB
  }

  void sendStop() {
    writeByte(252); //0xFC
  }

  /* Receive MIDI messages
  *  Return the status byte if its availible or else return 0
  */
  uint16_t read() {
    while(Serial2.available() > 0) { // 2
      int inByte = readByte();
      // handle clock data
      if (inByte >= 248 && inByte <= 252) { 
        return inByte;
      }
      // handle channel message data
      if ((inByte > 127 && inByte < 240)) { 
        return handleChannelRead(inByte);
      }
    }
    return 0;
  }

  /* Access current MIDI message data */
  uint8_t getStatus() {
    return message[0] - (message[0] & 0x0F);
  }

  uint8_t getChannel() {
    return message[0] & 0x0F;
  }

  uint8_t getData1() {
    return message[1];
  }

  uint8_t getData2() {
    return message[2];
  }

  /* Calulate the BPM based on MIDI clock pulses 
  *  Return the BPM
  */
  int16_t clockToBpm() {
    unsigned long cTime = micros();
    float deltaCT = cTime - prevClockTime;
    prevClockTime = cTime;
    float rollingCT = deltaCT;
    for(int i=14; i>=0; i--) {
      rollingCT += prevClockDeltas[i];
      if (i > 0) {
        prevClockDeltas[i] = prevClockDeltas[i-1];
      } else prevClockDeltas[0] = deltaCT;
    }
    int beatDelta = rollingCT * 15; // 15 for 14; 20 for 10; //24 for 8; // 30 for 6 // 11 for 20
    // int BPM = round(1000.0f / beatDelta * 6000000);
    int BPM = (int)((1000LL * 6000000 + (beatDelta >> 1)) / beatDelta);
    return round(BPM * 0.1f);
  }

  /* Calculate the microseconds between MIDI clock pulses at 24 PPQN
  *  @bpm The beats per minute for the tempo
  *  Return the number of microseconds
  */
  int calcTempoDelta(float bpm) {
    if (bpm > 0) {
      return 60.0f / bpm * 1000000 / 24.0f;
    } else return 20833; // 120 BPM default
  }

private:
  int recievePin;
  int transmitPin;
  uint8_t * message;
  unsigned long prevClockTime;
  int prevClockDeltas [15];

  uint8_t readByte() {
    return Serial2.read();
  }

  void writeByte(uint8_t val) {
    Serial2.write(val);
  }

  uint8_t handleChannelRead(uint8_t inByte) { 
    message[0] = inByte;
    inByte = readByte();
    while (inByte > 127) {
      inByte = readByte();
      if (inByte >= 248 && inByte <= 252) {
        return inByte;
      }
    }
    message[1] = inByte;
    inByte = readByte();
    while (inByte > 127) {
      inByte = readByte();
      if (inByte >= 248 && inByte <= 252) {
        return inByte;
      }
    }
    message[2] = inByte;

    if (message[0] >= 0x90 && message[0] <= 0x9F && message[2] == 0) {
      message[0] = 0x80 | (message[0] & 0x0F); // Convert to note off
    }
    if (message[0] < 240) {
      return message[0] - (message[0] & 0x0F); // return the status byte as ch 0
    } else return message[0]; // return non-channel status byte
  }

};

#endif /* MIDI16_H_ */