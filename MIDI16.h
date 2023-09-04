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

  /** Constructor 
   * @param rx The pin to recieve MIDI data on
   * @param tx The pin to transmit MIDI data on
  */
  MIDI16(int rx, int tx):recievePin(rx), transmitPin(tx) {
    delete[] message; // remove any previous memory allocation
    message = new uint8_t[3]; // create a new MIDI message buffer
    #if IS_ESP32()
    // handle MIDI on a separate Serial bus to keep the main Serial availible for println debugging
    #include <HardwareSerial.h>
    Serial2.begin(31250, SERIAL_8N1, rx, tx);
    #endif
  }

  #if IS_ESP32()
  // send MIDI messages
  void sendNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) {
    Serial2.write(0x90 | channel);
    Serial2.write(pitch);
    Serial2.write(velocity);
  }

  void sendNoteOff(uint8_t channel, uint8_t pitch, uint8_t velocity) {
    Serial2.write(0x80 | channel);
    Serial2.write(pitch);
    Serial2.write(velocity);
  }

  void sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    Serial2.write(0xB0 | channel);
    Serial2.write(control);
    Serial2.write(value);
  }

  void sendClock() {
    Serial2.write(0xF8);
  }

  void sendStart() {
    Serial2.write(0xFA);
  }

  void sendContinue() {
    Serial2.write(0xFB);
  }

  void sendStop() {
    Serial2.write(0xFC);
  }

  // recieve MIDI messages
  uint16_t read() {
    if (Serial2.available() > 2) {
      int inByte = Serial2.read();
      if ((inByte > 127 && inByte < 240) || (inByte > 248 && inByte < 252)) { // status byte or clock data
      // Serial.println(inByte);
      message[0] = inByte;
      inByte = Serial2.read();
      while (inByte > 127) {
        inByte = Serial2.read();
      }
      message[1] = inByte;
      inByte = Serial2.read();
      while (inByte > 127) {
        inByte = Serial2.read();
      }
      message[2] = inByte;

      if (message[0] >= 0x90 && message[0] <= 0x9F && message[2] == 0) {
        message[0] = 0x80 | (message[0] & 0x0F); // Convert to note off
      }
        return message[0] - (message[0] & 0x0F);
      }
    }
    return 0;
  }

#endif

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

private:
  int recievePin;
  int transmitPin;
  uint8_t * message;

};

#endif /* MIDI16_H_ */