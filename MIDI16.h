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
    // passes sProject PCB pins for ESP32 hardware serial
    // ESP8266 uses default Serial pins (GPIO 3 for rx and 1 for tx)
    MIDI16(37, 33); 
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
    // writeByte(0x90 | channel);
    writeByte(144 + channel);
    writeByte(pitch);
    writeByte(velocity);
  }

  void sendNoteOff(int channel, int pitch, int velocity) {
    // writeByte(0x80 | channel);
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

/*
  void readMIDI() { // temp
  while(Serial2.available() > 2) {
    byte sVal = Serial2.read();
    if (sVal > 127 && sVal < 247) {
      byte message[3];
      // Serial.println(sVal);
      message[0] = sVal;
      sVal = Serial2.read();
      while (sVal > 127) {
        sVal = Serial2.read();
      }
      message[1] = sVal;
      sVal = Serial2.read();
      while (sVal > 127) {
        sVal = Serial2.read();
      }
      message[2] = sVal;
      if (message[0] == 144 && message[1] > 0) handleNoteOn(0, message[1], message[2]);
      if (message[0] == 128 || (message[0] == 144 && message[1] == 0)) handleNoteOff(0, message[1], message[2]);
    }
  }
}
*/

  // recieve MIDI messages
  #if IS_ESP32()
  uint16_t read() {
    // Serial.println("midi read " + millis());
    while(Serial2.available() > 2) {
      int inByte = readByte();
      // handle status byte or clock data
      if ((inByte > 127 && inByte < 240) || (inByte > 248 && inByte < 252)) { 
        return handleRead(inByte);
      }
    }
    return 0;
  }
  #elif IS_ESP8266()
  uint16_t read() {
    while(Serial.available() > 2) {
      int inByte = readByte();
      // handle status byte or clock data
      if ((inByte > 127 && inByte < 240) || (inByte > 248 && inByte < 252)) { 
        return handleRead(inByte);
      }
    }
    return 0;
  }
  #endif

  // access current MIDI message data
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

  #if IS_ESP32()
  uint8_t readByte() {
    return Serial2.read();
  }

  void writeByte(uint8_t val) {
    Serial2.write(val);
  }
  #endif

  #if IS_ESP8266()
  uint8_t readByte() {
    return Serial.read();
  }

  void writeByte(uint8_t val) {
    Serial.write(val);
  }
  #endif

  uint8_t handleRead(uint8_t inByte) { 
    message[0] = inByte;
    inByte = readByte();
    while (inByte > 127) {
      inByte = readByte();
    }
    message[1] = inByte;
    inByte = readByte();
    while (inByte > 127) {
      inByte = readByte();
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