/*
 * SampleDataPlay - M16 example
 * Plays a snare drum sample stored directly in sketch flash memory,
 * decoded into PSRAM at startup. No SD card required at runtime.
 *
 * --- How to create snare_adpcm.h ---
 *
 * This is a one-time offline step performed with an SD card attached:
 *
 * 1. Prepare your source sample
 *    - Start with a mono 16-bit PCM WAV file at any standard sample rate.
 *    - Name it snare.wav and copy it to the root of your SD card.
 *
 * 2. Compress and export (run generateHeader() once)
 *    - Wav::compress() downsamples to 11025 Hz and encodes as 4-bit IMA ADPCM,
 *      reducing a 0.5s sample from ~43 KB to ~3 KB on the card.
 *    - Wav::exportToHeader() reads that compressed file and writes a C++ header
 *      (snare_adpcm.h) containing the raw bytes as a PROGMEM array.
 *    - Uncomment the generateHeader() call in setup(), upload and run once,
 *      then re-comment it before your final build.
 *
 * 3. Copy the header into your sketch folder
 *    - Remove the SD card, read it on your computer.
 *    - Copy snare_adpcm.h from the card root into this sketch folder
 *      (alongside SampleDataPlay.ino).
 *
 * 4. Build and upload normally
 *    - snare_adpcm.h is now compiled into flash as a PROGMEM byte array.
 *    - At startup, loadFromFlash() decodes and upsamples the data into PSRAM
 *      (or internal RAM if no PSRAM is available), ready for Samp playback.
 *    - No SD card is needed at runtime.
 *
 * Flash cost: ~3 KB (compressed). PSRAM/RAM cost: ~43 KB (decoded, 44100 Hz).
 */

#include "M16.h"
#include "Wav.h"
#include "Samp.h"
#include "snare_adpcm.h"   // defines SNARE_DATA[], SNARE_DATA_SIZE

Wav  wav;
Samp samp;

uint32_t lastTrigger = 0;

void setup() {
    //setI2sPins(38, 39, 40, 41);
    useInternalDAC(); // for ESP32s that have it
    audioStart();
    samp.loadFromFlash(wav, SNARE_DATA, SNARE_DATA_SIZE);
    // generateHeader(); // uncomment, run once with SD card to generate snare_adpcm.h, then re-comment
}

void loop() {
    if (millis() - lastTrigger >= 2000) {
        lastTrigger = millis();
        samp.start();
    }
}

void audioUpdate() {
    int16_t s = samp.next();
    i2s_write_samples(s, s);
}

// One-time header generation — uncomment the generateHeader() call in setup(),
// upload and run once with SD card attached, then re-comment and copy
// snare_adpcm.h from the SD card root into this sketch folder.
/*
void generateHeader() {
    SdFs sd;
    const uint8_t CS=5, SCK=18, MISO=19, MOSI=23; // adjust SPI pins for your board
    wav.initSD(sd, CS, SCK, MISO, MOSI);
    wav.compress("/snare.wav", "/snare_adpcm.wav"); // downsample to 11025 Hz, encode as 4-bit ADPCM
    wav.exportToHeader("/snare_adpcm.wav", "/snare_adpcm.h", "SNARE"); // write C++ header to SD
}
*/
