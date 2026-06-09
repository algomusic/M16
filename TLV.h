/*
 * TLV.h
 *
 * TLV320AIC3104 stereo audio codec driver for M16.
 * Configures the codec via I2C for 16-bit stereo I2S at 44.1 kHz or 48 kHz.
 * The codec runs as I2S slave; the ESP32 provides BCLK and WCLK.
 * No MCLK pin is required — the codec PLL locks directly to BCLK.
 *
 * Audio outputs: headphone (HPLOUT/HPROUT) and line out (LEFT_LOP/RIGHT_LOP)
 * Audio input:   stereo line in (LINE1LP/LINE1RP, single-ended)
 *
 * Connections:
 *   I2S (shared with M16 i2sPins):
 *     TLV DIN  ← ESP32 DOUT  (default GPIO18)
 *     TLV DOUT → ESP32 DIN   (default GPIO21, but see PIN CONFLICT NOTE)
 *     TLV BCLK ← ESP32 BCLK  (default GPIO16)
 *     TLV WCLK ← ESP32 WCLK  (default GPIO17)
 *   I2C:
 *     TLV SDA  ↔ ESP32 SDA   (default GPIO21)
 *     TLV SCL  ↔ ESP32 SCL   (default GPIO22)
 *
 * PIN CONFLICT NOTE:
 *   Default I2S DIN (GPIO21) conflicts with standard I2C SDA (GPIO21).
 *   If using ADC input, move I2S DIN to a free pin before calling audioStart():
 *     seti2sPins(16, 17, 18, 19);   // move DIN to GPIO19
 *   or construct TLV with a different SDA pin:
 *     TLV tlv(23, 22);              // SDA on GPIO23, SCL on GPIO22
 *
 * Supported sample rates: 44100 Hz and 48000 Hz.
 * PLL formula: f_S(ref) = (BCLK × J × R) / (2048 × P)
 * P=1, J=32, D=0, R=2 → f_S = BCLK / 64 = sampleRate (valid 39–53 kHz).
 *
 * Usage:
 *   #include "M16.h"
 *   #include "TLV.h"
 *
 *   TLV tlv;           // software reset (default, no extra GPIO needed)
 *   TLV tlv(21,22,15); // hardware reset on GPIO15 — more reliable if a pin is free
 *
 *   void setup() {
 *     seti2sPins(16, 17, 18, 19);  // move DIN if using ADC
 *     audioStart();                // MUST be called first — provides BCLK for codec PLL
 *     if (!tlv.begin()) Serial.println("TLV init failed");
 *   }
 *
 *   void audioUpdate() {
 *     i2s_write_samples(left, right);
 *   }
 *
 * Reference: TLV320AIC3104 datasheet SLAS510G (TI, Feb 2021)
 * Inspired by Bela platform I2C_Codec.cpp (BelaPlatform/Bela, Andrew McPherson)
 *
 * by Andrew R. Brown 2026
 *
 * This file is part of the M16 audio library.
 * M16 is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */

#ifndef TLV_H_
#define TLV_H_

#include "M16.h"

#if IS_ESP32()
#include <Wire.h>

// Fixed 7-bit I2C address (0b001_1000 = 0x18, hardwired on TLV320AIC3104)
#define TLV_I2C_ADDR 0x18

class TLV {
public:

    /** Construct a TLV codec driver.
     *  @param sdaPin   I2C SDA pin (default 21 — see pin conflict note above)
     *  @param sclPin   I2C SCL pin (default 22)
     *  @param resetPin GPIO connected to TLV /RESET pin, or -1 if not used
     */
    TLV(int sdaPin = 21, int sclPin = 22, int resetPin = -1)
        : _sdaPin(sdaPin), _sclPin(sclPin), _resetPin(resetPin) {}

    /** Set I2C pins. Call before begin() if not using constructor defaults.
     *  @param sdaPin I2C SDA pin
     *  @param sclPin I2C SCL pin
     */
    void seti2cPins(int sdaPin, int sclPin) {
        _sdaPin = sdaPin;
        _sclPin = sclPin;
    }

    /** Initialise the TLV320AIC3104 codec.
     *  Call from setup() BEFORE audioStart().
     *  Enables both headphone and line outputs, and stereo line input.
     *  @param sampleRate 44100 (default) or 48000 Hz
     *  @return true on success, false if I2C communication fails
     */
    bool begin(int sampleRate = SAMPLE_RATE) {
        if (sampleRate < 39000 || sampleRate > 53000) {
            Serial.println("TLV: sample rate must be 39000–53000 Hz (44100 or 48000 recommended)");
            return false;
        }

        // Force-release any device holding SDA low (e.g. after ESP32 reset mid-transaction)
        // by manually clocking SCL 9 times then issuing a STOP condition.
        // This must happen before Wire.begin() re-initialises the I2C peripheral.
        pinMode(_sclPin, OUTPUT);
        pinMode(_sdaPin, OUTPUT);
        digitalWrite(_sdaPin, HIGH);
        for (int i = 0; i < 9; i++) {
            digitalWrite(_sclPin, HIGH); delayMicroseconds(5);
            digitalWrite(_sclPin, LOW);  delayMicroseconds(5);
        }
        // STOP: SDA low → SCL high → SDA high
        digitalWrite(_sdaPin, LOW);
        delayMicroseconds(5);
        digitalWrite(_sclPin, HIGH);
        delayMicroseconds(5);
        digitalWrite(_sdaPin, HIGH);
        delayMicroseconds(5);

        Wire.end();   // Release any previous session
        Wire.begin(_sdaPin, _sclPin);
        Wire.setClock(400000);

        // Hardware reset: assert ~RESET low, then release.
        // Preferred over software reset — works regardless of I2C state.
        // If no reset pin is wired, fall back to software reset after first probe.
        if (_resetPin >= 0) {
            pinMode(_resetPin, OUTPUT);
            digitalWrite(_resetPin, LOW);
            delay(10);
            digitalWrite(_resetPin, HIGH);
            delay(5);  // Codec ready after ~1 ms; 5 ms gives margin
        }

        // First write doubles as bus probe — NACK on address means device not found
        Serial.print("TLV I2C probe 0x18 (SDA=");
        Serial.print(_sdaPin);
        Serial.print(", SCL=");
        Serial.print(_sclPin);
        Serial.println("):");
        if (!_wr(0x00, 0x00)) { _scan(); return false; }  // Select page 0
        Serial.println("TLV I2C OK");

        if (_resetPin < 0) {
            // No hardware reset pin — use software reset (reg 1 D7, self-clearing).
            // The codec briefly disrupts the I2C bus during reset, so reinitialise
            // Wire and poll until the codec responds.
            if (!_wr(0x01, 0x80)) return false;
            delay(20);
            Wire.end();
            Wire.begin(_sdaPin, _sclPin);
            Wire.setClock(400000);
            if (!_waitForCodec()) return false;
        }

        // ── Clock: PLL driven by BCLK ──────────────────────────────────────────
        // BCLK = sampleRate × 32  (16-bit × 2 ch = 32 bits per frame)
        // PLL: f_S(ref) = (BCLK × J × R) / (2048 × P)
        // P=1, J=32, D=0000, R=2  → f_S(ref) = sampleRate (exact for any fs)
        // PLL output = BCLK × 64 / 1 = sampleRate × 2048  (80–98 MHz, in spec)
        //
        // IMPORTANT: write clock-source register (0x66) BEFORE enabling PLL (0x03),
        // and switch CODEC_CLKIN to PLLDIV_OUT (0x65) only AFTER PLL is enabled.
        // Switching CODEC_CLKIN to an unlocked PLL output before the PLL is on
        // briefly leaves the codec clockless, which glitches SDA (→ err 4).
        if (!_wr(0x66, 0xA2)) return false;  // Reg 102: CLKDIV_IN=BCLK(10), PLLCLK_IN=BCLK(10)
        delay(2);                             // Settle after clock-source change
        if (!_wr(0x03, 0x91)) return false;  // Reg   3: PLL on, Q=2, P=1  (1_0010_001)
        if (!_wr(0x04, 0x80)) return false;  // Reg   4: PLL J = 32  (32<<2 = 0x80)
        if (!_wr(0x05, 0x00)) return false;  // Reg   5: PLL D MSB = 0
        if (!_wr(0x06, 0x00)) return false;  // Reg   6: PLL D LSB = 0  (D = 0000)
        if (!_wr(0x0B, 0x02)) return false;  // Reg  11: PLL R = 2
        delay(10);                            // Allow PLL to lock
        if (!_wr(0x65, 0x00)) return false;  // Reg 101: CODEC_CLKIN = PLLDIV_OUT (after PLL on)

        // ── Sample-rate dividers: NCODEC = NADC = NDAC = 1 (full rate) ───────
        if (!_wr(0x02, 0x00)) return false;  // Reg 2: ADC/DAC at f_S(ref) / 1

        // ── Codec datapath ─────────────────────────────────────────────────────
        // D7: AGC timing reference (0=48 kHz, 1=44.1 kHz; affects AGC only, not actual fs)
        // D4-D3 = 01: Left-DAC plays left-channel input
        // D2-D1 = 01: Right-DAC plays right-channel input
        uint8_t datapath = (sampleRate < 46000) ? 0x8A : 0x0A;
        if (!_wr(0x07, datapath)) return false;

        // ── I2S slave: BCLK and WCLK are inputs from the ESP32 ───────────────
        if (!_wr(0x08, 0x20)) return false;  // Reg  8: BCLK in, WCLK in, DOUT tri-state when idle
        if (!_wr(0x09, 0x00)) return false;  // Reg  9: I2S mode (00), 16-bit word length (00)
        if (!_wr(0x0A, 0x00)) return false;  // Reg 10: No bit-offset (standard I2S)

        // ── Output stage power and protection ─────────────────────────────────
        if (!_wr(0x0D, 0x00)) return false;  // Reg 13: Headset/button detection disabled
        if (!_wr(0x0E, 0x80)) return false;  // Reg 14: AC-coupled high-power output config
        if (!_wr(0x25, 0xC0)) return false;  // Reg 37: Left + Right DAC powered on
        if (!_wr(0x26, 0x04)) return false;  // Reg 38: Short-circuit protection enabled
        if (!_wr(0x28, 0x02)) return false;  // Reg 40: Output volume soft-stepping disabled
        if (!_wr(0x29, 0x00)) return false;  // Reg 41: DAC_L → DAC_L1 path, DAC_R → DAC_R1 path

        // DAC digital volume: unmuted, 0 dB
        if (!_wr(0x2B, 0x00)) return false;  // Reg 43: Left-DAC volume, unmuted
        if (!_wr(0x2C, 0x00)) return false;  // Reg 44: Right-DAC volume, unmuted

        // Pop reduction: 200 ms power-on delay, 4 ms ramp, band-gap reference
        if (!_wr(0x2A, 0x7E)) return false;  // Reg 42: (0111<<4)|(11<<2)|(1<<1) = 0x7E

        // ── Headphone output: DAC_L1 → HPLOUT, DAC_R1 → HPROUT ──────────────
        if (!_wr(0x2F, 0x80)) return false;  // Reg 47: DAC_L1 routed to HPLOUT, 0 dB
        if (!_wr(0x40, 0x80)) return false;  // Reg 64: DAC_R1 routed to HPROUT, 0 dB
        // Two-step enable: power up muted first, then unmute (minimises pop)
        if (!_wr(0x33, 0x01)) return false;  // Reg 51: HPLOUT  powered, muted
        if (!_wr(0x41, 0x01)) return false;  // Reg 65: HPROUT  powered, muted
        delay(5);
        if (!_wr(0x33, 0x09)) return false;  // Reg 51: HPLOUT  0 dB level, unmuted, powered
        if (!_wr(0x41, 0x09)) return false;  // Reg 65: HPROUT  0 dB level, unmuted, powered

        // ── Line output: DAC_L1 → LEFT_LOP, DAC_R1 → RIGHT_LOP ──────────────
        if (!_wr(0x52, 0x80)) return false;  // Reg 82: DAC_L1 routed to LEFT_LOP,  0 dB
        if (!_wr(0x5C, 0x80)) return false;  // Reg 92: DAC_R1 routed to RIGHT_LOP, 0 dB
        if (!_wr(0x56, 0x09)) return false;  // Reg 86: LEFT_LOP  0 dB, unmuted, powered
        if (!_wr(0x5D, 0x09)) return false;  // Reg 93: RIGHT_LOP 0 dB, unmuted, powered

        // ── ADC input: LINE1LP → Left ADC, LINE1RP → Right ADC (single-ended) ─
        if (!_wr(0x0F, 0x00)) return false;  // Reg 15: Left-ADC  PGA unmuted, 0 dB
        if (!_wr(0x10, 0x00)) return false;  // Reg 16: Right-ADC PGA unmuted, 0 dB
        if (!_wr(0x11, 0xFF)) return false;  // Reg 17: MIC2L/R → Left-ADC:  all disconnected
        if (!_wr(0x12, 0xFF)) return false;  // Reg 18: MIC2/LINE2 → Right-ADC: all disconnected
        if (!_wr(0x13, 0x04)) return false;  // Reg 19: LINE1LP → Left-ADC:  SE, 0 dB, ADC on
        if (!_wr(0x15, 0x78)) return false;  // Reg 21: LINE1RP → Left-ADC:  disconnected
        if (!_wr(0x16, 0x04)) return false;  // Reg 22: LINE1RP → Right-ADC: SE, 0 dB, ADC on
        if (!_wr(0x18, 0x78)) return false;  // Reg 24: LINE1LP → Right-ADC: disconnected
        if (!_wr(0x19, 0x00)) return false;  // Reg 25: MICBIAS powered down (line input, not mic)

        return true;
    }

    /** Set DAC digital output volume applied to both channels.
     *  @param vol 0.0 = mute, 1.0 = 0 dB (unity gain), range 0.0–1.0
     */
    void setDACVolume(float vol) {
        if (vol <= 0.0f) {
            _wr(0x2B, 0x80);  // Left:  muted
            _wr(0x2C, 0x80);  // Right: muted
        } else {
            if (vol > 1.0f) vol = 1.0f;
            // Regs 43/44: D7=0 (unmuted), D6-D0 = attenuation in 0.5 dB steps (0=0 dB, 127=−63.5 dB)
            uint8_t bits = (uint8_t)((1.0f - vol) * 127.0f);
            _wr(0x2B, bits);
            _wr(0x2C, bits);
        }
    }

    /** Set ADC PGA input gain for both channels.
     *  @param gainDB 0.0 to 59.5 dB in 0.5 dB steps
     */
    void setADCGain(float gainDB) {
        if (gainDB < 0.0f)  gainDB = 0.0f;
        if (gainDB > 59.5f) gainDB = 59.5f;
        // Regs 15/16: D7=0 (unmuted), D6-D0 = gain in 0.5 dB steps (0=0 dB, 119=59.5 dB)
        uint8_t bits = (uint8_t)(gainDB * 2.0f) & 0x7F;
        _wr(0x0F, bits);  // Left  ADC PGA
        _wr(0x10, bits);  // Right ADC PGA
    }

    /** Enable or disable headphone outputs (HPLOUT / HPROUT).
     *  @param enable true = on (default after begin()), false = off
     */
    void enableHeadphone(bool enable) {
        if (enable) {
            _wr(0x33, 0x09);  // HPLOUT: 0 dB level, unmuted, powered
            _wr(0x41, 0x09);  // HPROUT: 0 dB level, unmuted, powered
        } else {
            _wr(0x33, 0x04);  // HPLOUT: muted, hi-Z drive, powered down
            _wr(0x41, 0x04);  // HPROUT: muted, hi-Z drive, powered down
        }
    }

    /** Enable or disable line outputs (LEFT_LOP / RIGHT_LOP).
     *  @param enable true = on (default after begin()), false = off
     */
    void enableLineOut(bool enable) {
        if (enable) {
            _wr(0x56, 0x09);  // LEFT_LOP:  0 dB, unmuted, powered
            _wr(0x5D, 0x09);  // RIGHT_LOP: 0 dB, unmuted, powered
        } else {
            _wr(0x56, 0x01);  // LEFT_LOP:  muted, powered (graceful off)
            _wr(0x5D, 0x01);  // RIGHT_LOP: muted, powered
        }
    }

    /** Enable or disable ADC stereo input (LINE1LP / LINE1RP).
     *  @param enable true = on (default after begin()), false = off
     */
    void enableADC(bool enable) {
        if (enable) {
            _wr(0x13, 0x04);  // LINE1LP → Left-ADC:  0 dB, ADC powered up
            _wr(0x16, 0x04);  // LINE1RP → Right-ADC: 0 dB, ADC powered up
        } else {
            _wr(0x13, 0x78);  // LINE1LP: disconnected from PGA, ADC powered down
            _wr(0x16, 0x78);  // LINE1RP: disconnected from PGA, ADC powered down
        }
    }

private:
    int _sdaPin, _sclPin, _resetPin;

    // Translate Wire.endTransmission() error codes to a readable string.
    static void _printWireError(uint8_t err) {
        Serial.print("err ");
        Serial.print(err);
        Serial.print(" — ");
        switch (err) {
            case 1: Serial.print("data too long for TX buffer"); break;
            case 2: Serial.print("NACK on address: device not found at 0x18, check wiring and power"); break;
            case 3: Serial.print("NACK on data: device rejected register write"); break;
            case 4: Serial.print("bus error: SDA/SCL may be stuck or bus not released after previous transaction"); break;
            case 5: Serial.print("timeout: bus held low, possible SDA/SCL contention or missing pull-ups"); break;
            default: Serial.print("unknown error"); break;
        }
    }

    // Write one register via I2C.
    // Returns true on success. On failure prints register address and error.
    bool _wr(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(TLV_I2C_ADDR);
        Wire.write(reg);
        Wire.write(val);
        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            Serial.print("TLV I2C error — reg 0x");
            if (reg < 0x10) Serial.print("0");
            Serial.print(reg, HEX);
            Serial.print(" val 0x");
            if (val < 0x10) Serial.print("0");
            Serial.print(val, HEX);
            Serial.print(": ");
            _printWireError(err);
            Serial.println();
        }
        return err == 0;
    }

    // Poll silently until the codec responds after software reset.
    // Returns true when the codec ACKs a page-select write, false if it never comes back.
    bool _waitForCodec(int maxRetries = 20, int delayMs = 10) {
        for (int i = 0; i < maxRetries; i++) {
            delay(delayMs);
            Wire.beginTransmission(TLV_I2C_ADDR);
            Wire.write(0x00);  // page register
            Wire.write(0x00);  // page 0
            if (Wire.endTransmission() == 0) return true;
        }
        Serial.print("TLV: codec did not respond after reset (");
        Serial.print(maxRetries * delayMs);
        Serial.println(" ms)");
        return false;
    }

    // Scan all I2C addresses and print any that respond.
    // Called automatically when the probe fails.
    void _scan() {
        Serial.println("TLV I2C scan — searching addresses 0x01–0x7F:");
        int found = 0;
        for (uint8_t addr = 1; addr < 128; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.print("  device found at 0x");
                if (addr < 0x10) Serial.print("0");
                Serial.println(addr, HEX);
                found++;
            }
        }
        if (found == 0) {
            Serial.println("  no devices found — check power (AVDD=3.3V, DVDD=1.8V),");
            Serial.println("  I2C pull-ups (4.7k to 3.3V on SDA and SCL), and wiring.");
        }
        Serial.println("TLV I2C scan complete.");
    }

};

#else
  // Stub for non-ESP32 platforms
  #warning "TLV.h: TLV320AIC3104 driver is only supported on ESP32"
  class TLV {
  public:
    TLV(int sdaPin = 21, int sclPin = 20, int resetPin = -1) {}
    void seti2cPins(int sdaPin, int sclPin) {}
    bool begin(int sampleRate = 44100) { return false; }
    void setDACVolume(float vol) {}
    void setADCGain(float gainDB) {}
    void enableHeadphone(bool enable) {}
    void enableLineOut(bool enable) {}
    void enableADC(bool enable) {}
  };
#endif // IS_ESP32()

#endif // TLV_H_
