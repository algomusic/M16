/*
 * MIDI16.h by Andrew R. Brown 2023
 *
 * Lightweight MIDI send/receive for M16. ESP32: supports beginClockTask()
 * which captures incoming clock timestamps in a high-priority FreeRTOS task,
 * independent of loop() scheduling jitter.
 *
 * M16 is inspired by the 8-bit Mozzi audio library by Tim Barrass 2012.
 * Licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0.
 */

#ifndef MIDI16_H_
#define MIDI16_H_

#if IS_ESP32()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

class MIDI16 {

public:
  static const uint8_t noteOn              = 0x90;
  static const uint8_t noteOff             = 0x80;
  static const uint8_t polyphonicAfterTouch = 0xA0;
  static const uint8_t controlChange       = 0xB0;
  static const uint8_t programChange       = 0xC0;
  static const uint8_t channelAfterTouch   = 0xD0;
  static const uint8_t pitchBend           = 0xE0;
  static const uint8_t clock               = 0xF8;
  static const uint8_t start               = 0xFA;
  static const uint8_t cont                = 0xFB;
  static const uint8_t stop                = 0xFC;

  /** Default constructor – uses sProject PCB pins (rx=37, tx=38). */
  MIDI16() { MIDI16(37, 38); }

  /** @param rx  GPIO pin for MIDI receive
   *  @param tx  GPIO pin for MIDI transmit */
  MIDI16(int rx, int tx) : recievePin(rx), transmitPin(tx) {
    delete[] message;
    message = new uint8_t[3];
    #if IS_ESP32()
    #include <HardwareSerial.h>
    Serial2.begin(31250, SERIAL_8N1, rx, tx);
    #endif
    #if IS_ESP8266()
    Serial.begin(31250);
    #endif
  }

  // ── Clock task (ESP32 only) ───────────────────────────────────────────────

  #if IS_ESP32()
  /** Start a high-priority FreeRTOS task that reads all incoming MIDI bytes
   *  from Serial2 and timestamps 0xF8 clock pulses the instant they arrive,
   *  independent of loop() scheduling. All MIDI receive is transparently
   *  routed through an internal ring buffer — read() behaviour is unchanged.
   *
   *  Call once in setup() after MIDI16 construction.
   *
   *  @param coreID    Core to pin the task to. Default 1 (same as loop()).
   *  @param priority  FreeRTOS priority. Default 5 (above loop() at 1).
   *                   If M16 audio tasks run on Core 1, raise to
   *                   configMAX_PRIORITIES-2 to avoid being starved. */
  void beginClockTask(int coreID = 1, int priority = 5) {
    if (_clockTaskHandle) return;
    _clockTaskActive = true;   // switch read() to ring-buffer mode first
    xTaskCreatePinnedToCore(_clockTaskFn, "midi_clk", 2048,
                            this, priority, &_clockTaskHandle, coreID);
  }

  void endClockTask() {
    if (!_clockTaskHandle) return;
    vTaskDelete(_clockTaskHandle);
    _clockTaskHandle = NULL;
    _clockTaskActive = false;
  }

  /** Send outgoing MIDI clock at the given BPM (24 PPQN) from the clock task,
   *  decoupled from loop() scheduling jitter. Starts the clock task if not
   *  already running. Set bpm=0 or call stopClockSend() to stop. */
  void setClockSendBpm(float bpm) {
    if (bpm > 0) {
      _sendIntervalUs = (unsigned long)(60.0f / bpm * 1000000.0f / 24.0f);
      _nextSendTime   = micros() + _sendIntervalUs;
    } else {
      _sendIntervalUs = 0;
    }
    if (bpm > 0 && !_clockTaskHandle) beginClockTask();
  }

  void stopClockSend() { _sendIntervalUs = 0; }
  #endif

  // ── Send ─────────────────────────────────────────────────────────────────

  void sendNoteOn(int channel, int pitch, int velocity) {
    writeByte(144 + channel); writeByte(pitch); writeByte(velocity);
  }
  void sendNoteOff(int channel, int pitch, int velocity) {
    writeByte(128 + channel); writeByte(pitch); writeByte(velocity);
  }
  void sendControlChange(int channel, int control, int value) {
    writeByte(176 + channel); writeByte(control); writeByte(value);
  }
  void sendClock()    { writeByte(248); }
  void sendStart()    { writeByte(250); }
  void sendContinue() { writeByte(251); }
  void sendStop()     { writeByte(252); }

  // ── Receive ──────────────────────────────────────────────────────────────

  /** Read one MIDI event. Returns the status byte (channel stripped to 0)
   *  or 0 if nothing is available. */
  uint16_t read() {
    while (srcAvail()) {
      int b = srcRead();
      if (b < 0) break;
      if (b >= 248 && b <= 252) return b;
      if (b >  127 && b <  240) return handleChannelRead((uint8_t)b);
    }
    return 0;
  }

  uint8_t getStatus()  { return message[0] - (message[0] & 0x0F); }
  uint8_t getChannel() { return message[0] & 0x0F; }
  uint8_t getData1()   { return message[1]; }
  uint8_t getData2()   { return message[2]; }

  /** Returns the last BPM computed by clockToBpm() without side effects.
   *  Use this to read BPM outside of the clock handler. */
  int16_t getBpm() { return _lastBPM; }

  // ── Clock timing ─────────────────────────────────────────────────────────

  /** Returns BPM derived from incoming 24-PPQN MIDI clock.
   *  With beginClockTask() active, uses timestamps captured at hardware
   *  read time for accuracy independent of loop() blocking.
   *  Without it, falls back to micros() at call time with a 3ms guard. */
  int16_t clockToBpm() {
    unsigned long ts;
    #if IS_ESP32()
    if (_clockTaskActive) {
      if (!tsPop(ts)) return _lastBPM;  // no new pulse captured yet
    } else {
      ts = micros();
      if (ts - _prevClockTime < 3000) return _lastBPM;
    }
    #else
    ts = micros();
    if (ts - _prevClockTime < 3000) return _lastBPM;
    #endif

    unsigned long delta = ts - _prevClockTime;
    if (delta < 3000) return _lastBPM;   // <3ms → drain-loop burst artefact, discard

    // Gap >1 second means clock was stopped. Zero the history so the next
    // pulse triggers a fresh-start pre-fill rather than a stale average.
    if (delta > 1000000UL) {
      for (int i = 0; i < 7; i++) _clockDeltas[i] = 0.0f;
      _prevClockTime = ts;
      return _lastBPM;
    }
    _prevClockTime = ts;

    // On fresh start (history is zeroed), pre-fill with the current delta so
    // the very first reading is accurate rather than 8x too high.
    if (_clockDeltas[0] == 0.0f)
      for (int i = 0; i < 7; i++) _clockDeltas[i] = (float)delta;

    // 8-sample rolling average: shift history, accumulate sum, compute BPM.
    // BPM = 2,500,000 * 8 / sum  →  20,000,000 / sum
    float sum = (float)delta;
    for (int i = 6; i >= 0; i--) {
      sum += _clockDeltas[i];
      if (i > 0) _clockDeltas[i] = _clockDeltas[i - 1];
      else       _clockDeltas[0] = (float)delta;
    }
    float newBpm = 20000000.0f / sum;
    if (fabsf(newBpm - (float)_lastBPM) > 0.75f)
      _lastBPM = (int16_t)round(newBpm);
    return _lastBPM;
  }

  /** Microseconds between MIDI clock pulses at the given BPM (24 PPQN). */
  int calcTempoDelta(float bpm) {
    return (bpm > 0) ? (int)(60.0f / bpm * 1000000 / 24.0f) : 20833;
  }

private:
  int      recievePin;
  int      transmitPin;
  uint8_t *message        = nullptr;
  unsigned long _prevClockTime = 0;
  float    _clockDeltas[7]     = {};  // 8-sample window (current + 7 stored)
  int16_t  _lastBPM            = 0;

  // ── Clock task internals (ESP32 only) ────────────────────────────────────

  #if IS_ESP32()
  // Two lock-free SPSC rings: clock task is sole writer, loop() is sole reader.
  // Both rings live on Core 1 so volatile write indices are sufficient —
  // no cross-core memory barrier needed.
  enum { RX_SIZE = 64, TS_SIZE = 32 };

  // RX ring: every incoming byte forwarded here from the clock task
  uint8_t      _rxBuf[RX_SIZE]  = {};
  volatile int _rxW              = 0;
  int          _rxR              = 0;

  // Timestamp ring: one entry per 0xF8 byte, captured at read time
  volatile unsigned long _tsBuf[TS_SIZE] = {};
  volatile int _tsW                       = 0;
  int          _tsR                       = 0;

  volatile bool          _clockTaskActive  = false;
  TaskHandle_t           _clockTaskHandle  = NULL;
  volatile unsigned long _sendIntervalUs   = 0;   // 0 = send disabled
  volatile unsigned long _nextSendTime     = 0;

  // RX ring helpers
  void rxPush(uint8_t b) {
    int n = (_rxW + 1) % RX_SIZE;
    if (n != _rxR) { _rxBuf[_rxW] = b; _rxW = n; }  // drop silently on overflow
  }
  int  rxPop()   { if (_rxR == _rxW) return -1; uint8_t b = _rxBuf[_rxR]; _rxR = (_rxR + 1) % RX_SIZE; return b; }
  bool rxAvail() { return _rxR != _rxW; }
  int  rxCount() { return (_rxW - _rxR + RX_SIZE) % RX_SIZE; }
  int  rxPeek()  { return rxAvail() ? _rxBuf[_rxR] : -1; }

  // Timestamp ring helpers
  void tsPush(unsigned long ts) { _tsBuf[_tsW] = ts; _tsW = (_tsW + 1) % TS_SIZE; }
  bool tsPop(unsigned long &ts) {
    if (_tsR == (int)_tsW) return false;
    ts = _tsBuf[_tsR]; _tsR = (_tsR + 1) % TS_SIZE; return true;
  }

  // Clock task: sole owner of Serial2 reads when active.
  // Timestamps 0xF8 immediately, then forwards all bytes to the RX ring.
  // tsPush before rxPush ensures the timestamp is always available by the
  // time read() returns 0xF8 and the sketch calls clockToBpm().
  static void _clockTaskFn(void *p) {
    MIDI16 *self = (MIDI16 *)p;
    while (true) {
      unsigned long now = micros();

      // Outgoing clock send — checked every tick for low jitter
      if (self->_sendIntervalUs > 0 && (long)(now - self->_nextSendTime) >= 0) {
        Serial2.write(0xF8);
        self->_nextSendTime += self->_sendIntervalUs;  // advance by fixed interval, no drift
      }

      // Incoming: drain Serial2 into RX ring, timestamp clock bytes immediately
      while (Serial2.available()) {
        uint8_t b = (uint8_t)Serial2.read();
        if (b == 0xF8) self->tsPush(micros());
        self->rxPush(b);
      }

      // Adaptive sleep: when sending clock, sleep until 1ms before the next
      // send deadline, then busy-wait the final millisecond for ~50µs accuracy.
      // Without clock send, yield one tick to avoid starving other tasks.
      if (self->_sendIntervalUs > 0) {
        long usToNext = (long)(self->_nextSendTime - micros());
        if (usToNext > 1000)
          vTaskDelay(pdMS_TO_TICKS((usToNext - 1000) / 1000));
        // else: busy-wait the final ≤1ms — tight loop, exits at top of while(true)
      } else {
        vTaskDelay(1);
      }
    }
  }
  #endif  // IS_ESP32()

  // ── Serial source abstraction ─────────────────────────────────────────────
  // Uniform interface over Serial2 (no clock task) or RX ring (clock task active).

  bool srcAvail() {
    #if IS_ESP32()
    if (_clockTaskActive) return rxAvail();
    #endif
    return Serial2.available() > 0;
  }

  int srcRead() {
    #if IS_ESP32()
    if (_clockTaskActive) return rxPop();
    #endif
    return Serial2.read();
  }

  int srcPeek() {
    #if IS_ESP32()
    if (_clockTaskActive) return rxPeek();
    #endif
    return Serial2.peek();
  }

  int srcCount() {
    #if IS_ESP32()
    if (_clockTaskActive) return rxCount();
    #endif
    return Serial2.available();
  }

  // Blocking read of one data byte from the active source.
  // Used inside handleChannelRead() to wait for the d1/d2 bytes of a message.
  uint8_t readByte() {
    #if IS_ESP32()
    if (_clockTaskActive) {
      unsigned long t0 = millis();
      while (!rxAvail()) {
        if (millis() - t0 > 50) return 0xFF;  // 50ms timeout — no data arriving
        taskYIELD();   // let clock task run to fill ring
      }
      return (uint8_t)rxPop();
    }
    #endif
    return Serial2.read();
  }

  void writeByte(uint8_t v) { Serial2.write(v); }

  uint8_t handleChannelRead(uint8_t inByte) {
    message[0] = inByte;
    // Read data byte 1
    inByte = readByte();
    while (inByte > 127) {
      inByte = readByte();
      if (inByte >= 248 && inByte <= 252) return inByte;
    }
    message[1] = inByte;
    // Read data byte 2
    inByte = readByte();
    while (inByte > 127) {
      inByte = readByte();
      if (inByte >= 248 && inByte <= 252) return inByte;
    }
    message[2] = inByte;

    // CC coalescing: drain queued messages for the same channel+controller,
    // keeping only the final value. Stops on an embedded RT byte.
    if ((message[0] & 0xF0) == 0xB0) {
      while (srcCount() >= 3 && (uint8_t)srcPeek() == message[0]) {
        srcRead();                             // consume repeated status byte
        uint8_t nc = (uint8_t)srcRead();
        if (nc >= 248 && nc <= 252) break;     // RT byte — stop coalescing
        uint8_t nv = (uint8_t)srcRead();
        if (nc == message[1]) message[2] = nv;
      }
    }

    if (message[0] >= 0x90 && message[0] <= 0x9F && message[2] == 0)
      message[0] = 0x80 | (message[0] & 0x0F);  // note-on vel=0 → note-off
    return (message[0] < 240)
      ? (uint8_t)(message[0] - (message[0] & 0x0F))
      : message[0];
  }
};

#endif /* MIDI16_H_ */
