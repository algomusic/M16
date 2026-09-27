# MIDI16 MPE support

Added 13 September 2026, based on M1-100-UM v1.1 (14 April 2022). `MIDI16::MPE` lives in `MIDI16.h`: no extra include, templates, heap allocation, or platform-specific APIs. The parser and ordinary MIDI routing remain unchanged. Instantiate the tracker only in applications that need MPE.

## API and ownership

All API channels are **zero-based**. Manager channels 0 and 15 are the human-facing MIDI channels 1 and 16.

```cpp
#include "M16.h"
#include "MIDI16.h"
MIDI16 midi(45, 46); // Example ESP32-S3 DIN pins; use your board's wiring
MIDI16::MPE mpe;    // Default lower zone: members 2–16

// In the control loop, for EACH event, before handling that event:
uint8_t status;
while ((status = midi.read()) != 0) {
  uint8_t ch = midi.getChannel();
  uint8_t changes = mpe.process(status, ch, midi.getData1(), midi.getData2());
  // Handle configuration changes, notes, pedals, and expression here.
  // See the lifecycle rules below; this tracker does not render sound.
}
```

`reset()` restores the default lower zone; `reset(false)` disables both zones and resets all channel state. `configure(managerChannel, memberCount)` configures a zone directly and returns false for invalid arguments. `manager(ch)` returns 0, 15, or -1 outside a zone. `isManager`, `isMember`, `enabled`, and `memberCount(upper=false)` inspect the configuration.

`process()` accepts either a channel-stripped or full channel status, plus the explicit channel and two data bytes. For Channel Pressure, pressure is data1. Feed events in receipt order, including expression preceding Note On. Its bit flags are `Expression`, `Configuration`, `Sustain`, and `ResetControllers`; zero means no tracked-state change, **not** that an application should ignore the event. Note On/Off and CC120/123 still require application handling.

The tracker supports:

- Lower and upper zones, up to 15 members in one zone. A newly configured zone takes precedence over overlapping channels.
- MPE Configuration RPN 6: CC101=0, CC100=6, CC6=member count, on channel 1 or 16. Zero disables that zone. Invalid managers/counts are ignored.
- Pitch Bend Sensitivity RPN 0: CC101=0, CC100=0, CC6=0–96 semitones. Manager default ±2; members default ±48. A member range update applies to every member of that zone. Whole semitones only; CC38 is ignored. RPN null selection and NRPN selection prevent unintended Data Entry updates.
- Pitch bend, Channel Pressure, and CC74 state retained even without active notes. Defaults: bend centre, pressure zero, CC74=64.
- Manager sustain CC64 (threshold 64); member sustain is ignored. CC121 resets that channel's bend, pressure, slide, sustain, and RPN selection, preserving its sensitivity and zone.

`bendSemitones(ch)` returns the channel's own bend; `combinedBendSemitones(ch)` adds member and manager bend. `combinedPressure(ch)` takes their maximum. `combinedTimbre(ch)` adds their offsets around 64 and clamps to 0–127. `sustainDown(ch)` reads the zone manager's pedal. These combination choices are useful defaults; the specification permits instrument-specific combinations.

Independent ordinary-MIDI helpers: `MIDI16::value14(lsb, msb)` decodes 14-bit data. `MIDI16::pitchBendSemitones(value, range=2)` maps centre 8192 to zero and endpoints to exactly ±range.

## Application responsibilities

Keep the tracker and voice ownership on one control thread. Publish any resulting audio targets using atomics or M16's AudioSnapshotQueue; smooth them in the audio owner. Do not access the tracker concurrently from audio and control.

1. On Note On, allocate a voice, retain channel + note, and initialize expression from the tracker. Multiple active notes may share a member channel. Allocation and stealing are instrument decisions.
2. On Note Off (including velocity-zero Note On), match channel + pitch and end key ownership. Freeze member expression even if the envelope or sustain pedal keeps the voice sounding. Retain enough source information to release pedal-held notes later.
3. On expression/range changes, update eligible voices on the affected channel or zone. Keep touch/local notes outside MIDI ownership.
4. On zone reconfiguration, stop affected sounding voices and reset their expression. `process()` updates tracker state only; compare old/new `manager()` results or track affected channels yourself. Reconfiguring the same zone resets controls and sensitivities too.
5. Handle pedal-up, All Notes Off, All Sound Off, and source takeover explicitly. Never leave a sustained envelope orphaned by a mapping change.
6. Route ordinary synth CCs on manager channels; reserve member messages for expression/configuration. Decide how channels outside zones behave.

The MPE specification permits manager Note On/Off and requires manager bend to continue affecting released notes. An application needing that behavior must snapshot the member contribution at Note Off and continue combining the current manager bend. `combinedBendSemitones()` alone cannot provide that release snapshot.

This is a reusable state tracker, not a complete MPE instrument or conformance certification. Mono Mode 4, polyphonic key pressure, program changes, other pedals, SysEx, voice allocation, release velocity, and sound-parameter assignment are outside its implementation.

## Drone Machine Blue integration

| MIDI preset | Note mapping | CC layout |
|---|---|---|
| 1 | Standard: channel 1 polyphonic; 11–15 fixed voices | Original |
| 2 | Same standard mapping | Wander/timbre |
| 3 | MPE, default lower zone channels 2–16 | Same as preset 2 |
| 4–17 | Standard | Empty parameter table; existing special CCs remain |

Select preset 3 using the existing encoder MIDI-preset selector. Send preset 2's ordinary CCs on the MPE manager channel (normally channel 1). In particular, CC106 still controls both delay levels plus feedback, and CC107 reverb. Member CC74 is expression, not a mapped synth control. Both zones share the same five-voice instrument and CC layout. Unzoned channels retain standard routing. Selecting presets 1/2 keeps standard notes even if an external controller sends MPE configuration.

MPE notes use idle voices, then released voices, then held MIDI voices in round-robin order; physical-touch voices are excluded. If all pads are held, additional MPE notes are ignored. Pitch matching includes channel. Touch and CC45–49 takeover remove MIDI expression. Equal-pitch duplicate Note Off releases all matching active voices on that channel. With more than five sounding notes, voices are stolen; a late Note Off cannot distinguish an older stolen note from a newer identical channel/pitch, a MIDI 1.0 identity limitation.

Velocity sets an amplitude floor (`velocity/127`); pressure swells it toward 1.0. Thus velocity 127 is already full level. CC74 scales existing per-voice FM depth from 0 to 127/64, with 64 neutral; an existing depth of zero stays zero. These targets are transient, atomic, and smoothed by audio; they do not change saved mixer/timbre values. The existing serial FM chain means a MIDI voice's timbre can indirectly affect other voices, including touch voices.

MPE Note Off freezes **all** expression, including manager bend, to preserve the project's earlier requirement that released notes ignore subsequent bends. This intentionally differs from the specification's manager-bend release behavior. Standard Note Off retains the previous behavior: it removes bend immediately. Sustain extends MPE envelope lifetime but does not extend expression ownership. Only manager CC64 is supported; CC120 stops affected voices and CC123 releases their keys (respecting sustain).

Changing the MIDI preset releases MIDI voices, clears bend/shift state, and resets MPE zones. Reselecting the current preset leaves active notes and zone configuration intact. Existing touch voices continue. Note pitches still use the instrument's existing just-intonation conversion, glide, and wander behavior.

## Verification

Host tests (C++11 for the reusable tracker, C++17 for extracted sketch handlers) cover parser ordering, zones, ranges, expression, note lifetimes, sustain, touch exclusion and standard routing. Run from the M16 repository:

```sh
c++ -std=c++11 -fsanitize=address,undefined tests/midi_mpe_test.cpp -o /tmp/midi_mpe_test
/tmp/midi_mpe_test
python3 tests/drone_mpe_test.py '/path/to/Drone_Machine_Blue_Code.ino'
```

The sketch-handler test uses DSP/UI stubs; it does not verify audio quality or physical input. Hardware checks should include sliding/bending while holding multiple notes, note release followed by channel reuse, sustain, all five touch pads held, preset switching, CC106/107 on channel 1, and upper-zone/RPN configuration.

Build checks passed: full Drone sketch on Arduino-ESP32 3.3.11 (ESP32-S3, 8MB flash, OPI PSRAM); helper smoke sketches on Pico core6.1.0 and Teensy4.0 core1.62.0. Teensy emits the existing library architecture-metadata warning (`avr` versus `teensy4`), but compiles successfully. These checks do not test MIDI wiring or controller behavior.
