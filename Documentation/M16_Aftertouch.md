# MIDI16 standard aftertouch

Added 13 September 2026. `MIDI16::Aftertouch` is an optional, portable C++11 helper inside `MIDI16.h`. No additional include or template is needed. It stores 16 channel-pressure bytes; the application supplies one polyphonic-pressure byte per voice. Voice allocation and held/released state remain application responsibilities.

All channels are zero-based (0 means MIDI channel1). Channel Pressure (`0xD0`) has pressure in data1. Polyphonic Key Pressure (`0xA0`) has note in data1 and pressure in data2.

```cpp
MIDI16::Aftertouch aftertouch;
uint8_t polyPressure = 0; // One per voice; clear at EVERY new Note On/reuse.

// For each received event, on the control thread:
aftertouch.process(status, ch, d1, d2);
// Then, for each held standard MIDI voice only:
if (aftertouch.updateNote(polyPressure, voiceChannel, voiceNote,
                          status, ch, d1, d2)) {
  float targetGain = aftertouch.gain(voiceChannel, polyPressure); // 1.0–1.3
  // Publish targetGain to the audio owner; smooth it there.
}
```

- `process()` retains Channel Pressure, including messages before Note On. CC121 resets that channel to zero. It returns true when it handles channel pressure/reset.
- `updateNote()` matches channel and (for polyphonic pressure) note. It updates the caller's poly-pressure byte for a matching polyphonic message or resets it on matching CC121. It returns true for matching channel-pressure events too, so the voice can recompute its gain. Call `process()` first.
- `pressure(channel, polyPressure=0)` returns the higher of channel and polyphonic pressure. They do not add together.
- `gain(channel, polyPressure=0, maxBoost=0.3f)` returns `1 + pressure/127 * maxBoost`. Thus pressure0 means unity gain, pressure127 means 1.3 by default. Use a different maximum boost for other projects.
- `reset()` clears all retained channel pressure. Clear application-owned poly-pressure bytes separately when resetting or reusing voices.

At Note On, clear that voice's poly-pressure byte and initialize gain from the retained channel pressure. At Note Off, stop calling `updateNote()` for the voice and preserve its last gain through release. On local/touch takeover restore unity. Do not call the tracker from audio and control concurrently; it is control-owned, not internally synchronized.

## Drone Machine Blue

Both standard presets1/2 (and other standard note-map slots) accept Channel Pressure and Polyphonic Key Pressure. Channel1 affects its held allocated voices; channels11–15 affect their corresponding held voice. Polyphonic pressure additionally requires the matching pitch. MPE-zoned messages continue through the separate MPE handler, with its previous pressure behavior.

The Drone explicitly passes `maxBoost=1.0f`; the reusable library default remains0.3f. Pressure linearly boosts the audible voice level up to 100% relative to the current mixer level. The boost is applied through `midiGainQ15` and audio-owned `midiGainSmoothQ15`, after envelope scaling and before stereo pan. This leaves saved mixer values, the physical dial's pickup target, and FM modulation depth unchanged. A muted voice stays muted. Overall output clipping/effects can limit the audible increase at high mix levels.

At standard Note Off, aftertouch gain freezes while pitch bend is removed as before. Later pressure cannot alter the release tail. Touch/CC-gate takeover and voice reuse clear poly pressure and restore neutral gain before initializing the new source. Preset changes reset channel-pressure state; reselecting the current preset retains it. CC121 clears channel and held-note poly pressure without changing the saved mix level.

Host tests are in `tests/midi_mpe_test.cpp` and `tests/drone_mpe_test.py`; the latter extracts the actual sketch handlers and uses DSP/UI stubs. Hardware checks should include channel pressure on multiple held channel1 notes, per-note pressure on just one of those notes, channels11–15, pressure returning to zero, release followed by pressure changes, and touch takeover.

The received-message debug line only confirms input. With `serialDebug` enabled, standard voice gain updates also log `MIDI Aftertouch -> voice 1: pressure 127 gain 200.0%`. This confirms a held standard voice was matched. MPE preset3 keeps its separate pressure/velocity behavior. Host regression additionally evaluates the actual audio gain smoother and sample multiplier, verifying +100% for both positive and negative samples and return to unity. Downstream drive/soft clipping may reduce the final audible difference.

The first standard aftertouch matched to a held voice after a new standard MIDI Note On selects Mixer (bank9). Subsequent pressure does not change pages until another Note On. Affected dials show effective mix level including pressure (`mixLevels/1024 * gain`), with numeric0–200% and dial range0–200%. Thus a50% base mix rises to100% at full pressure. Other voice dials retain their normal mixer control values. Updates use the OLED timer; editing mix level while the overlay is active refreshes the effective level. Leaving Mixer clears the overlays; touch/voice reuse clears the affected overlay. Note Off retains the final level through release. Stored mixer settings and control-bank values remain unchanged by aftertouch.

## Display and debug load limits

Aftertouch no longer marks the whole control display dirty for each message. Matching events update audio targets immediately and mark only affected voices pending for display. `serviceAftertouchDisplay()` services at most one voice every25ms (at most40 voice updates/second total; at most8 per voice if all five remain busy). The existing8ms OLED service can make the actual interval slightly longer. Round-robin selection avoids starving voices, and only the latest target is rendered. Dial-position and numeric-value caches skip unchanged tiles. The initial switch to Mixer still draws the normal page once; pressure already on Mixer does not redraw all five dials or the global dial.

Display timestamps advance to the current time rather than accumulating missed deadlines, preventing catch-up redraw bursts. Pending aftertouch redraws pause outside Mixer and during preset selection. Received-pressure debug output is sampled at most10 times/second in total; applied-gain output at most10 times/second per voice. Note On/Off logging is unchanged. These limits do not throttle audio pressure updates or alter the100% gain range. Regression tests exercise the real scheduler for fairness, tile caching, final-value delivery and absence of catch-up bursts; hardware glitch reduction still needs confirmation.

`aftertouchMixerSwitchArmed` is a single display latch: an accepted standard Note On arms it; the next Channel/Poly Aftertouch matching a held voice consumes it, even if Mixer is already visible. Unmatched pressure and CC121 do not consume it. A MIDI-preset change clears it. Pressure still updates sound and queues Mixer values after the latch is consumed; manual page changes remain in place until the next note arms a new switch. Touch triggers and MPE notes do not arm this standard-aftertouch behavior.

While an aftertouch level overlay is active on Mixer, physical voice-dial changes skip the immediate raw-BV dial/number drawing. `applyMixLevel` updates the base level and marks only that voice's aftertouch display dirty. The bounded effective-level renderer is then the sole writer for those live dial changes, avoiding alternation between raw dial percent and boosted level. Other pages and Mixer voices without an overlay retain their ordinary display behavior.
