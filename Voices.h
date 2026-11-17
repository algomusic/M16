/* Voices.h - note priority for MIDI and hardware keyboards.
 * Control-thread only: feed events and drain readChange() on the same thread.
 * No UART, envelope, audio, heap allocation or platform dependencies.
 */
#ifndef M16_VOICES_H_
#define M16_VOICES_H_

#include <stdint.h>
#include <math.h>

static const uint8_t MONO = 0;
static const uint8_t LEGATO = 1;
static const uint8_t POLY = 2;
static const uint8_t LAST_NOTE = 0;
static const uint8_t LOW_NOTE = 1;
static const uint8_t HIGH_NOTE = 2;
static const uint8_t HARDWARE_SOURCE = 1;
static const uint8_t MIDI_SOURCE = 2;
static const uint8_t VOICE_START = 1;
static const uint8_t VOICE_CHANGE = 2;
static const uint8_t VOICE_RELEASE = 3;

class Voices {
public:
    static const uint16_t noteCapacity = 132;
    static const uint8_t voiceCapacity = 32;

    struct Note {
        float pitch = 0;
        uint16_t key = 0; // physical key identity, independent of pitch
        uint8_t source = 0, channel = 0, velocity = 0;
    };
    struct Change {
        Note note;
        uint8_t voice = 0, action = 0;
    };

    /** POLY uses 1..32 voices. MONO and LEGATO always use one voice.
     * MONO retriggers selected notes; LEGATO retriggers only after all keys
     * were released. LAST_NOTE poly steals round-robin after idle/releasing
     * voices. LOW/HIGH_NOTE replace the least preferred held voice only when
     * the new pitch has higher priority. Stolen poly notes are not recalled.
     */
    explicit Voices(uint8_t voices = 1, uint8_t mode = POLY,
                    uint8_t priority = LAST_NOTE) {
        mode_ = mode <= POLY ? mode : POLY;
        voices_ = mode_ == POLY ? clampVoices(voices) : 1;
        priority_ = priority <= HIGH_NOTE ? priority : LAST_NOTE;
        retriggerOnPress_ = mode_ != LEGATO;
    }

    /** Change mode/count/priority only when all notes and release tails have
     * finished and changes have been consumed. Return false otherwise. */
    bool setVoiceMode(uint8_t mode) {
        if (mode > POLY || busy()) return false;
        mode_ = mode;
        if (mode != POLY) voices_ = 1;
        retriggerOnPress_ = mode != LEGATO;
        cursor_ = 0;
        return true;
    }
    bool setVoiceCount(uint8_t voices) {
        if (voices < 1 || voices > voiceCapacity || busy() ||
            (mode_ != POLY && voices != 1)) return false;
        voices_ = voices; cursor_ = 0; return true;
    }
    bool setNotePriority(uint8_t priority) {
        if (priority > HIGH_NOTE || busy()) return false;
        priority_ = priority; return true;
    }
    /** In LEGATO, enable this to retrigger on presses while returning to a
     * held note without retriggering (the sProto Mono behavior). */
    void setRetriggerOnNoteOn(bool enabled) { retriggerOnPress_ = enabled; }
    uint8_t getVoiceMode() const { return mode_; }
    uint8_t getVoiceCount() const { return voices_; }
    uint8_t getNotePriority() const { return priority_; }
    uint16_t getHeldNoteCount() const { return count_; }

    /** Channel is zero-based (0..15), including for hardware sources.
     * Repeated presses are separate notes; offs match oldest press first.
     * Returns false on invalid input/full note storage; no existing note is
     * discarded. Velocity zero is a note-off. Only feed locally accepted MIDI
     * events here: MIDI Extend remains responsible for forwarding and offs.
     */
    bool noteOn(uint8_t source, uint16_t key, float pitch,
                uint8_t velocity = 127, uint8_t channel = 0) {
        if (!source || channel > 15 || velocity > 127 || !isfinite(pitch)) return false;
        if (!velocity) return noteOff(source, key, channel);
        if (count_ == noteCapacity) return false;
        const uint16_t slot = count_++;
        notes_[slot].pitch = pitch; notes_[slot].key = key;
        notes_[slot].source = source; notes_[slot].channel = channel;
        notes_[slot].velocity = velocity;
        if (mode_ != POLY) {
            selectMono(true);
        } else {
            int voice = freeVoice();
            if (voice < 0) {
                voice = cursor_;
                if (priority_ != LAST_NOTE) {
                    for (uint8_t v = 0; v < voices_; ++v)
                        if (preferred(voicesState_[voice].slot, voicesState_[v].slot)) voice = v;
                    if (!preferred(slot, voicesState_[voice].slot)) return true;
                }
            }
            assign((uint8_t)voice, slot, VOICE_START);
            cursor_ = (uint8_t)((voice + 1) % voices_);
        }
        return true;
    }

    bool noteOff(uint8_t source, uint16_t key, uint8_t channel = 0) {
        int slot = -1;
        for (uint16_t i = 0; i < count_; ++i)
            if (notes_[i].source == source && notes_[i].key == key &&
                notes_[i].channel == channel) { slot = i; break; }
        if (slot < 0) return false;
        for (uint8_t v = 0; v < voices_; ++v) {
            State &state = voicesState_[v];
            if (state.slot == slot) {
                state.slot = -1;
                state.action = VOICE_RELEASE;
            } else if (state.slot > slot) --state.slot;
        }
        for (uint16_t i = (uint16_t)slot; i + 1 < count_; ++i) notes_[i] = notes_[i + 1];
        --count_;
        if (mode_ != POLY) selectMono(false);
        return true;
    }

    /** Retune a physical key without changing its identity, velocity or order.
     * Updates all repeated presses of that key. Drain changes afterwards. */
    void setNotePitch(uint8_t source, uint16_t key, float pitch, uint8_t channel = 0) {
        if (!isfinite(pitch)) return;
        for (uint16_t i = 0; i < count_; ++i) {
            if (notes_[i].source != source || notes_[i].key != key || notes_[i].channel != channel) continue;
            if (notes_[i].pitch == pitch) continue;
            notes_[i].pitch = pitch;
            for (uint8_t v = 0; v < voices_; ++v)
                if (voicesState_[v].slot == (int)i) assign(v, i, VOICE_CHANGE);
        }
        if (mode_ != POLY) selectMono(false);
    }

    /** Report true when the sketch's amplitude envelope has finished.
     * A held voice cannot be marked idle. Read this state from the control
     * thread, rather than calling the allocator from audioUpdate(). */
    void setVoiceIdle(uint8_t voice, bool idle) {
        if (voice < voices_ && voicesState_[voice].slot < 0)
            voicesState_[voice].idle = idle;
    }

    /** Consume all changes after EACH input event/retune, before the next
     * event. Changes coalesce per voice if the caller defers consumption. */
    bool readChange(Change &change) {
        for (uint8_t v = 0; v < voices_; ++v) {
            State &state = voicesState_[v];
            if (!state.action) continue;
            change.voice = v; change.action = state.action; change.note = state.note;
            state.action = 0;
            return true;
        }
        return false;
    }
    void allNotesOff() {
        count_ = 0;
        for (uint8_t v = 0; v < voices_; ++v)
            if (voicesState_[v].slot >= 0) {
                voicesState_[v].slot = -1;
                voicesState_[v].action = VOICE_RELEASE;
            }
    }

private:
    struct State {
        Note note;
        int16_t slot = -1;
        uint8_t action = 0;
        bool idle = true;
    };
    Note notes_[noteCapacity]; // chronological; stolen notes retain off identity
    State voicesState_[voiceCapacity];
    uint16_t count_ = 0;
    uint8_t mode_ = POLY, voices_ = 1, priority_ = LAST_NOTE, cursor_ = 0;
    bool retriggerOnPress_ = true;

    static uint8_t clampVoices(uint8_t voices) {
        return voices < 1 ? 1 : (voices > voiceCapacity ? voiceCapacity : voices);
    }
    bool busy() const {
        if (count_) return true;
        for (uint8_t v = 0; v < voices_; ++v)
            if (!voicesState_[v].idle || voicesState_[v].action) return true;
        return false;
    }
    bool preferred(int a, int b) const {
        if (priority_ == LOW_NOTE && notes_[a].pitch != notes_[b].pitch)
            return notes_[a].pitch < notes_[b].pitch;
        if (priority_ == HIGH_NOTE && notes_[a].pitch != notes_[b].pitch)
            return notes_[a].pitch > notes_[b].pitch;
        return a > b; // newest press wins ties
    }
    void assign(uint8_t voice, uint16_t slot, uint8_t action) {
        State &state = voicesState_[voice];
        state.slot = (int16_t)slot; state.note = notes_[slot]; state.idle = false;
        if (state.action != VOICE_START || action == VOICE_START) state.action = action;
    }
    void selectMono(bool pressed) {
        if (!count_) return;
        uint16_t best = 0;
        for (uint16_t i = 1; i < count_; ++i) if (preferred(i, best)) best = i;
        const bool wasHeld = voicesState_[0].slot >= 0;
        if (wasHeld && voicesState_[0].slot == (int)best) return;
        const bool trigger = pressed ? (!wasHeld || retriggerOnPress_) : mode_ == MONO;
        // A released active note can have alternatives even though its slot
        // was cleared. Returning to a held note in LEGATO never restarts it.
        assign(0, best, trigger ? VOICE_START : VOICE_CHANGE);
    }
    int freeVoice() const {
        for (uint8_t pass = 0; pass < 2; ++pass)
            for (uint8_t offset = 0; offset < voices_; ++offset) {
                const uint8_t v = (uint8_t)((cursor_ + offset) % voices_);
                if (voicesState_[v].slot < 0 && (pass || voicesState_[v].idle)) return v;
            }
        return -1;
    }
};
#endif
