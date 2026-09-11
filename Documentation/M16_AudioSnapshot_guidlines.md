# M16 AudioSnapshot Guidelines

Use `AudioSnapshotQueue` when control code changes several related parameters that audio must receive as one complete update—especially during sound regeneration, preset changes, or note triggering.

The queue is defined directly in `M16.h`. No separate AudioSnapshot header or include is required.

## When to use it

| Situation | Approach |
|---|---|
| Settings applied before `audioStart()` | Set the DSP objects directly |
| One independent value with an explicitly control-safe setter | Use that setter |
| Several related voice settings changed during playback | Publish a snapshot |
| Resets, envelope starts, or other render-state changes | Send a request for the audio owner to apply |
| Events where every occurrence must be preserved | Use an event FIFO; snapshots can skip intermediate updates |

A single value may also need a snapshot if its setter is not safe to call concurrently with audio. Do not assume every M16 setter is control-safe simply because the object has a render-state lock.

## Basic usage

This example assumes one audio renderer owns both oscillators. Configure their waveforms before starting audio, and use dedicated rendering (`setIsDualCore(false)`) for this example. In partitioned rendering, only the voice's owning core may consume its queue.

Control code prepares frequency and FM depth together:

```cpp
#include "M16.h"  // Defines AudioSnapshotQueue

struct VoiceParams {
  float frequency;
  int32_t fmDepth;
};

VoiceParams updateStorage[4]{};
AudioSnapshotQueue updates(updateStorage, sizeof(VoiceParams), 4);
VoiceParams target{220.0f, 2048};  // Control-owned
bool dirty = true;

// Call from loop(), after updating all related target fields.
void publishParameters() {
  if (dirty && updates.publish(&target)) {
    dirty = false;
  }
}
```

Whenever a dial or preset changes `target`, set `dirty = true`. Call `publishParameters()` on subsequent loop passes until publication succeeds. If the queue is full, publication returns false, leaving the latest target available for retry. Control may replace that target with a newer desired state while waiting.

The audio owner receives and applies the snapshot before rendering:

```cpp
VoiceParams rendered{220.0f, 2048};  // Audio-owned

void audioUpdate() {
  VoiceParams next;
  if (updates.consumeLatest(&next)) {
    carrier.setFreq(next.frequency);
    rendered = next;
  }

  int32_t sample =
      carrier.phModIntUnlocked(modulator, rendered.fmDepth);

  audioBlockWrite(sample, sample);
}
```

These are integration snippets: `carrier` and `modulator` are sketch-owned `Osc` instances, requiring `Osc.h` and normal waveform/setup initialization. Initialize live DSP settings before `audioStart()` as well; a snapshot is only applied once consumed.

## Ownership rules

- **One producer and one consumer per queue.** Two audio cores cannot consume the same queue. Multiple control tasks likewise must not publish to it concurrently.
- **Keep snapshots small and self-contained.** Plain numbers and fixed arrays work well. Payloads must be trivially copyable. Copying a pointer does not protect the data it points to or extend its lifetime.
- **Publish after completing related edits.** Audio gets a stable copy; control can then continue editing its own target.
- **Apply accepted values before rendering the affected voice.** Control edits targets; the audio owner edits and advances the live DSP state.
- **Keep application work bounded.** Precompute expensive mappings on the control side. Avoid allocation, table generation, or large buffer clearing when applying snapshots.
- **Keep render state out of control snapshots.** Oscillator phases, filter histories and envelope progress belong to audio. Send explicit reset/start requests instead of copying those live states from control.

The queue only transfers data safely. It does not automatically synchronize existing setters or apply updates to DSP objects.

## Capacity and overload behavior

```cpp
VoiceParams storage[4]{};
AudioSnapshotQueue updates(storage, sizeof(VoiceParams), 4);

VoiceParams largerStorage[8]{};
AudioSnapshotQueue larger(largerStorage, sizeof(VoiceParams), 8);
```

The ordinary, non-template class accepts a storage pointer, payload size in bytes,
and capacity (default four). Capacity must be a power of two, at least two, and
less than 2^31. `isValid()` reports constructor validation: null storage, zero
payload size, invalid capacity or overflowing total size disable the queue.
Publish/consume then return false. The constructor cannot check the actual size
of the supplied buffer: it must hold capacity × payload size bytes.

Storage must outlive the queue and must not be accessed directly while the queue
is active. The payload must be trivially copyable, but this is now the caller's
responsibility rather than a template-enforced check. Both operation arguments
must point to an object of the configured size and must not overlap queue storage.
Null arguments return false. Different types of the same size are not detected.

Storage is supplied by the caller; no runtime allocation occurs. Publication and
consumption use memcpy and atomic counters without retry loops, although copy
cost increases with payload size.

- `publish(&value)` returns true after copying a complete snapshot into a free slot. It returns false when full without waiting or overwriting occupied slots.
- `consumeLatest(&value)` returns true after copying the newest complete published snapshot and releasing the consumed slots. Older snapshots are skipped.
- When no update is available, `consumeLatest(&value)` returns false and leaves the destination unchanged.

Retain the newest desired target on the control side and retry after a failed publication. Do not spin until space appears: allow the control loop and audio renderer to continue normally.

## Triggers and events

A snapshot queue represents the latest desired state, not a lossless stream of events. Several updates can be reduced to one before audio consumes them.

Beat Machine includes a monotonically increasing unsigned trigger serial in every voice snapshot. Control increments it for each requested hit and preserves it in subsequent parameter-only snapshots. The audio owner compares the accepted serial with the last rendered serial:

- An unchanged serial applies parameter changes without retriggering.
- A changed serial requests a new trigger on the audio owner.
- Several pending hits may coalesce into one latest hit. Beat Machine counts this explicitly.

A transient `trigger = true` flag that is cleared in the next dial update can lose a pending trigger when snapshots are skipped. Persistent serials avoid that particular loss of intent, but do not preserve every hit. Use an appropriately designed event FIFO when every note, timestamp or ordering relationship must be retained.

## Platform and validation notes

The queue uses standard C++ acquire/release atomics rather than FreeRTOS or ESP32-specific APIs. Integration builds have passed for ESP32-S3, Pico/RP2040 and Teensy 4.0. Host regressions cover concurrent coherent snapshots, capacity, retry and counter wraparound, plus Beat Machine's parameter/trigger handoff.

Compilation and host tests do not establish hardware timing limits. Beat Machine hardware stress testing is ongoing, with no freezes reported so far. Continue exercising high tempo/density, delay/reverb, dial changes and sound regeneration.
