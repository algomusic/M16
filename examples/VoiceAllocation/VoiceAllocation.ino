// M16 Voice  example
// Plays automatically: no MIDI connection, keyboard, or controls are required.
// Repeats seven demonstrations: MONO, LEGATO (last/low/high note priority),
// then two-voice POLY (last/low/high note priority).
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Voices.h"

const uint8_t voiceCount = 2;
Voices allocator(voiceCount, MONO, LAST_NOTE);
WaveTable wavetable; // shared by both oscillators
Osc osc[voiceCount];
Env env[voiceCount];

unsigned long noteTime = 0;
const unsigned long noteDelta = 500;
uint8_t step = 0;
uint8_t demonstration = 0;

// The sketch decides how allocation changes affect its sound.
void applyVoiceChanges() {
    Voices::Change change;
    while (allocator.readChange(change)) {
        const uint8_t voice = change.voice;
        if (change.action == VOICE_RELEASE) {
            env[voice].startRelease();
        } else {
            osc[voice].setPitch(change.note.pitch);
            env[voice].setMaxLevel(change.note.velocity / 127.0f);
            // VOICE_CHANGE moves pitch while keeping the envelope running.
            if (change.action == VOICE_START) env[voice].start();
        }
    }
}

// Change settings only after every note and envelope release has finished.
bool selectDemonstration() {
    const uint8_t mode = demonstration == 0 ? MONO : (demonstration < 4 ? LEGATO : POLY);
    uint8_t priority = LAST_NOTE;
    if (demonstration == 2 || demonstration == 5) priority = LOW_NOTE;
    if (demonstration == 3 || demonstration == 6) priority = HIGH_NOTE;
    if (!allocator.setVoiceMode(mode)) return false;
    if (!allocator.setVoiceCount(mode == POLY ? voiceCount : 1)) return false;
    if (!allocator.setNotePriority(priority)) return false;

    switch (demonstration) {
        case 0: Serial.println("MONO: last note, envelopes retrigger"); break;
        case 1: Serial.println("LEGATO: last note, envelopes continue"); break;
        case 2: Serial.println("LEGATO: lowest held note"); break;
        case 3: Serial.println("LEGATO: highest held note"); break;
        case 4: Serial.println("POLY: two voices, round-robin stealing"); break;
        case 5: Serial.println("POLY: two voices, prefer lower notes"); break;
        case 6: Serial.println("POLY: two voices, prefer higher notes"); break;
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    wavetable.triGen();
    for (uint8_t voice = 0; voice < voiceCount; ++voice) {
        osc[voice].setTable(wavetable);
        env[voice].setAttack(30);
        env[voice].setDecay(100);
        env[voice].setSustain(0.6f);
        env[voice].setRelease(250);
        env[voice].setResetOnStart(true); // makes retriggering easier to hear
    }
    selectDemonstration();
    // seti2sPins(38, 39, 40, 41); // BCK, WS, DOUT, DIN; change for your board
    // useInternalDAC();
    setIsDualCore(false); // two simple voices share one audio owner
    noteTime = millis();
    audioStart();
}

void loop() {
    #if IS_RP2040()
    audioLoop();
    #endif

    // Finished voices are preferred over voices whose envelopes still release.
    for (uint8_t voice = 0; voice < voiceCount; ++voice)
        allocator.setVoiceIdle(voice, env[voice].getEnvState() == 0);

    const unsigned long msNow = millis();
    if (msNow - noteTime < noteDelta) return;
    noteTime = msNow;

    // Both source types are simulated here. In an instrument these calls can
    // come from MIDI16 events or hardware key/pad handlers on the control thread.
    // A hardware key number is independent of its sounding pitch.
    switch (step) {
        case 0: allocator.noteOn(HARDWARE_SOURCE, 0, 60, 110); break; // C4
        case 1: allocator.noteOn(MIDI_SOURCE, 67, 67, 110); break;    // G4
        case 2: allocator.noteOn(HARDWARE_SOURCE, 1, 64, 110); break; // E4
        case 3: allocator.noteOn(MIDI_SOURCE, 72, 72, 110); break;    // C5
        case 4:
            // If G4 was stolen, its note-off must not stop the replacement.
            allocator.noteOff(MIDI_SOURCE, 67);
            break;
        case 5: allocator.noteOff(MIDI_SOURCE, 72); break;
        case 6: allocator.noteOff(HARDWARE_SOURCE, 1); break;
        case 7: allocator.noteOff(HARDWARE_SOURCE, 0); break;
        case 8: break; // leave time for release tails
        case 9:
            // Stay on this step until release tails have finished.
            if (!selectDemonstration()) return;
            step = 0;
            return;
    }
    applyVoiceChanges(); // drain after EVERY event, including note-offs
    if (step == 8) demonstration = (demonstration + 1) % 7;
    ++step;
}

/* audioUpdate() supplies the audio samples in all M16 programs.
 * Always finish with audioBlockWrite().
 */
void audioUpdate() {
    int32_t mix = 0;
    for (uint8_t voice = 0; voice < voiceCount; ++voice) {
        const uint16_t level = env[voice].next();
        if (level > 0) mix += (osc[voice].next() * (int32_t)level) >> 16;
    }
    mix /= voiceCount; // leave headroom when both voices sound
    audioBlockWrite(mix, mix);
}
