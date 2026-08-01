# M16 Audio Library Analysis

## Overview

M16 is a 16-bit audio synthesis library for ESP8266, ESP32, and RP2040 (Raspberry Pi Pico) microcontrollers using I2S audio DACs/ADCs. Created by Andrew R. Brown (2021), it's inspired by the 8-bit Mozzi audio library by Tim Barrass.

**License:** Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License

## Supported Platforms

| Platform | Audio Method | Cores | Notes |
|----------|-------------|-------|-------|
| ESP8266 | Timer ISR | 1 | Fixed I2S pins, limited RAM (~50KB heap) |
| ESP32 | FreeRTOS dual-core | 2 | Configurable pins, PSRAM auto-detection, internal DAC support |
| ESP32-S2 | FreeRTOS single-core | 1 | Internal DAC support (GPIO17/18), no PSRAM on most boards |
| ESP32-S3/C3 | FreeRTOS | 1-2 | No internal DAC, external I2S only |
| RP2040 (Pico) | Multicore + I2S | 2 | Cooperative scheduling on Core 0 |
| RP2035/RP2350 (Pico 2) | Multicore + I2S | 2 | Detected via updated `IS_RP2040()` macro |

## M16 Library Summary                                                                                                     
                                                                                                                          
  What it is: A 16-bit audio synthesis library for ESP8266, ESP32, and RP2040 microcontrollers using I2S DACs.            
                                                                                                                          
  Core Components:                                                                                                        
  - M16.h - Core system with I2S audio output, dual-core support, PSRAM management, and utility functions                 
  - Osc.h - Band-limited wavetable oscillator with FM, ring mod, morphing, and spread                                     
  - Env.h - AHDSR envelope generator with timing in milliseconds                                                          
  - SVF.h - State variable filter (LPF/HPF/BPF/Notch with resonance)                                                      
  - EMA.h - Simple single-pole IIR filter (low CPU)                                                                       
  - Del.h - Delay line with feedback and filtering                                                                        
  - All.h - Allpass filter for reverb diffusion                                                                           
  - Comb.h - Comb filter for metallic resonances                                                                          
  - Samp.h - Sample playback with granular features                                                                       
  - FX.h - Effects: reverb, chorus, compression, distortion variants, wave shaping                                        
  - Phys.h - Physical modelling synthesis: Karplus-Strong pluck and digital waveguide
  - Arp.h - MIDI arpeggiator                                                                                              
  - Seq.h - Step sequencer with euclidean rhythms                                                                         
                                                                                                                          
  Key Architecture Points:                                                                                                
  - Requires audioUpdate() function in every sketch ending with i2s_write_samples(left, right) or audioBlockWrite(L, R)  
  - ESP32 external I2S can run the audio callback on one dedicated core or as a pipelined two-core voice partition
  - Polyphonic sketches with independent voice arrays use audioPartitionOffset/Stride + audioBlockWrite
  - Stateful master effects use setAudioPostProcessCallback() so they process the combined mix exactly once
  - Serial/state-dependent graphs (FM cascades, shared feedback networks) use setIsDualCore(false) + audioBlockWrite
  - 16.16 fixed-point math for efficiency                                                                                 
  - PSRAM auto-detection for large buffers                                                                                
  - Band-limited waveforms in 3 frequency bands to reduce aliasing    

## Core Architecture

### Audio System (M16.h)

**Platform Detection Macros (inlined in M16.h — no separate Hardware_defines.h):**
```cpp
IS_ESP8266()    // defined(ESP8266)
IS_ESP32()      // defined(ESP32)
IS_ESP32S2()    // defined(CONFIG_IDF_TARGET_ESP32S2)
IS_ESP32C3()    // defined(CONFIG_IDF_TARGET_ESP32C3)
IS_RP2040()     // ARDUINO_ARCH_RP2040 | RP2035 | RP2350
IS_CAPABLE()    // IS_ESP32() || IS_RP2040()  — platforms with atomics/PSRAM/dual-core features
```

**Key Constants:**
- `SAMPLE_RATE`: Default 44100Hz (configurable via `setSampleRate()`)
- `TABLE_SIZE`: Default 4096 samples (1024 on ESP8266), definable before include
- `FULL_TABLE_SIZE`: 3x TABLE_SIZE for band-limited waveforms (low/mid/high)
- `MAX_16` / `MIN_16`: 32767 / -32767 (16-bit range)

**Audio Callback Pattern:**
```cpp
void setup() {
  audioStart();  // Initialize I2S and start audio tasks
}

void audioUpdate() {  // Called continuously from audio tasks
  int16_t sample = osc.next();
  i2s_write_samples(sample, sample);  // Left, Right
}
```

**Thread Safety:**
- ESP32 external I2S runs `audioUpdate()` on both cores when `isDualCore=true` (the default), or only on Core 0 after `setIsDualCore(false)`
- ESP32 internal DAC uses a single audio task (avoids buffer interleaving)
- `M16_ATOMIC_GUARD(lock, code)` macro for non-blocking critical sections
- `M16_ATOMIC_GUARD_BLOCKING(lock, code)` for blocking critical sections. **Note:** Preferred for deterministic filtering (`SVF2`, `Bob`) and delays (`BBD`, `Del`) to prevent 1-sample hold artifacts.

**Audio-Frame Clock (`audioFrameCount()`):**
- Global monotonic counter advanced exactly once per output frame, incremented inside the per-frame write paths (`i2s_write_samples` on every platform, and the `audioBlockWrite` finaliser; the block-split Core-1 branch is skipped to avoid double-counting). On dual-core external I2S both cores advance the shared atomic for alternate frames, so the combined rate equals the output frame rate.
- It is the correct time base for time-based generators because, unlike `micros()`, it does **not** freeze while the audio task fills the DMA ring in a burst, and unlike a per-call counter it does not assume a particular tick rate. `Env` reads it so envelope slopes are smooth at audio rate and correctly timed whether advanced at control rate or per sample.
- `uint32_t` — wraps after ~27 h at 44.1 kHz; consumers must use unsigned deltas. Atomic on ESP32/RP2040, `volatile` on ESP8266.

**Dedicated Audio Core Pattern (Recommended for Serial/State-Dependent Chains):**

For synthesis graphs whose stages depend on one another (for example, Drone Machine's FM cascade), the graph cannot be divided into independent voice partitions. Run the complete stateful graph once on Core 0 and leave Core 1 for the Arduino loop, UI, MIDI, and system tasks:

```cpp
void setup() {
  setIsDualCore(false);       // must be called before audioStart()
  // Initialise oscillators and effects here.
  audioStart();
}

void audioUpdate() {
  // Runs once: FM cascade, shared delays, filters, chorus, reverb, etc.
  int32_t mod = modulator.next();
  int32_t signal = carrier.phMod(mod, 2.0f);
  int32_t left = signal + delayL.next(signal);
  int32_t right = signal + delayR.next(signal);

  // In single-core mode this is an efficient block writer, not a core combiner.
  audioBlockWrite(left, right);
}
```
Do not merely guard synthesis with `audioIsFinalizerCore()` while leaving dual-core mode enabled. Both audio tasks must still reach `audioBlockWrite()`, and the unused producer adds unnecessary scheduling and synchronization. Explicitly call `setIsDualCore(false)`.

This pattern ensures deterministic state progression and removes audio-core lock contention. Its limitation is CPU capacity: the entire graph must fit within one core's sample budget. `audioBlockWrite()` reduces I2S driver overhead by writing blocks instead of individual frames.

**Block-Split Dual-Core Voice Partitioning (Parallel Chains):**

On dual-core ESP32, `audioUpdate()` runs simultaneously on both cores. Whether partitioning is needed depends on what is inside `audioUpdate()`:

- **Serial or shared-state chain** — use the dedicated-core pattern above.
- **Voice array loop** — use the partition API. Both cores would otherwise run the full loop, advancing every voice's state twice per frame: doubled frequency, corrupted filter state, audible crackle.

**Rule:** if `audioUpdate()` loops over `Osc[]`, `SVF[]`, `Env[]`, `Samp[]`, or any other per-voice stateful array, use the partition API.

**Block-split API:**
```cpp
void audioUpdate() {
  int32_t mix = 0;
  // Each core processes its interleaved voice subset (Core 0: 0,2,…  Core 1: 1,3,…)
  for (int i = audioPartitionOffset(); i < voices; i += audioPartitionStride()) {
    mix += filter[i].nextLPF(osc[i].next()) * env[i].getValue() >> 15;
  }
  // Submit only this core's partial voice mix.
  audioBlockWrite(mix, mix);
}
```

`audioBlockWrite()` accumulates `M16_BLOCK_SIZE` samples (default 32) per core in a two-slot pipeline. Core 1 can render block N+1 while Core 0 combines, post-processes, and writes block N. Sequence tags identify each slot generation so stale task notifications cannot satisfy a later rendezvous. Define `M16_BLOCK_SIZE` before `#include "M16.h"` to override; power-of-2 values are recommended.

Core 0 waits at most 100 ms for Core 1. If Core 1 misses that deadline, M16 emits Core 0's partial block and attempts to resynchronize rather than wedging the output permanently. DMA writes also have a 100 ms timeout. Diagnostics are available through:

```cpp
uint32_t syncMisses = audioBlockSyncTimeoutCount();
uint32_t dmaMisses  = audioDmaWriteTimeoutCount();
```

`audioIsFinalizerCore()` still identifies Core 0, but it must **not** be used to run master effects on `mix` before `audioBlockWrite()`: at that point `mix` contains only Core 0's voice partition.

**Post-combine master effects:**

Reverb, master delay, compression, chorus, global filters, distortion, and other shared/stateful effects must process the complete mix through `setAudioPostProcessCallback()`:

```cpp
FX fx;
BBD delayL, delayR;

void processMasterEffects(int32_t& left, int32_t& right) {
  left = clip16(left + delayL.next(left));
  right = clip16(right + delayR.next(right));

  int32_t wetL, wetR;
  fx.reverbStereoInterp(left, right, wetL, wetR);
  left = wetL;
  right = wetR;
}

void setup() {
  fx.initReverbSafe();
  setAudioPostProcessCallback(processMasterEffects);
  setIsDualCore(true);
  audioStart();
}
```

The callback runs on Core 0 for every combined output frame, immediately before formatting and DMA output. It defaults to `nullptr`, so sketches without master processing require no callback. Per-voice effects with one independent instance per voice remain inside the partitioned voice loop.

On ESP8266 and RP2040 the partition helpers are no-ops (`offset=0`, `stride=1`, `isFinalizerCore=true`) and `audioBlockWrite()` falls through to `i2s_write_samples()` — sketches using the partition API compile and run correctly unchanged on all platforms.

The legacy `i2s_write_samples()` path remains available for simple or legacy sketches. In dual-core external-I2S mode its two producers can reach the driver in nondeterministic order; the reorder buffer that addresses this is disabled by default. New stateful ESP32 sketches should therefore prefer either the dedicated-core block pattern or the partitioned block pattern rather than relying on two legacy writers.

- Use `volatile` and atomic operations for shared state

**Sample Reorder Buffer (disabled by default, dual-core external I2S only):**
Even with atomic phase advance, the order in which the two audio cores reach `i2s_channel_write()` is non-deterministic — adjacent samples can be swapped, producing audible discontinuities under high-modulation FM and percussive transients (confirmed in `examples/FrequencyModulation` and Beat Machine).

The reorder buffer (`M16_REORDER_BUFFER_ENABLE`) is an MPSC ring + single drainer task that restores deterministic sample order at the DAC. It is **disabled by default** as of 2026-05-30 because:
1. The drainer task (max priority, Core 0) writes 4 bytes at a time; when DMA has space it returns immediately without blocking → IDLE0 is starved → Task Watchdog fires after ~5 s in lightweight audio loops.
2. It causes audible crackle in `reverbStereo` and `reverbStereo2`. `M16_ATOMIC_GUARD` + output caching correctly handles dual-core ordering for shared-state FX without the drainer.

For new block-partitioned sketches, the preferred fix for shared/master effects is `setAudioPostProcessCallback()`, which runs the effect once on the ordered combined mix. Non-blocking guards plus output caching remain relevant to legacy dual-producer code that calls the effect from both callbacks.

```cpp
// Enable the reorder buffer (advanced use only — see WDT caveat above):
#define M16_REORDER_BUFFER_ENABLE 1
#include "M16.h"
```

**Pre-claim fix (sign-flip spikes):** Even with the reorder ring, a race existed in sample playback (`Samp::next()`): the spinlock serialised phase advance (correct order) but the reorder sequence number was claimed *after* the lock released — the other core could claim an earlier seq# and swap adjacent samples. Fix: `m16_claimReorderSeq()` is called *inside* the spinlock before release, atomically pairing sample order with output sequence order. `Samp::next()` and `Samp::nextStereo()` both call this. Result: zero-crossing sign-flip spikes eliminated on dual-core external I2S.

Tuning: `M16_REORDER_RING_SIZE` (default 16, must be power of 2). At 44.1 kHz a 16-frame ring adds ≤ 360 µs output latency. Cost: ~192 B RAM, ~680 B flash, one extra FreeRTOS task pinned to core 0 at max priority. No effect on internal DAC, single-core (`setIsDualCore(false)`), ESP32-S2/-C3, or ESP8266 paths — those skip the drainer entirely.

**RP2040 (Pico / Pico 2) Audio Model:**

The RP2040 uses cooperative (not preemptive) dual-core scheduling, which is fundamentally different from the ESP32 FreeRTOS model:

- `audioStart()` initialises I2S output and launches `picoAudioCallback()` on Core 1. Core 1 runs a tight `audioUpdate()` loop independently.
- Core 0 produces audio by calling `audioLoop()` from `loop()`. Without this call, Core 0 produces no samples and UI/MIDI responsiveness improves at the cost of halved audio throughput.
- I2S writes from both cores are serialised via `picoI2SMutex`. Oscillator phase advance is coordinated via mutex-protected callbacks registered by `Osc.h`.

**Required `loop()` pattern on RP2040 in dual-core mode:**
```cpp
void loop() {
  audioLoop();          // REQUIRED on RP2040 dual-core — produces Core 0 samples
  // UI, MIDI, sequencing here (runs between audio samples)
}
```

**Voice arrays and partitioning on RP2040:** The block-split partition API (`audioPartitionOffset`, `audioPartitionStride`, `audioBlockWrite`) is implemented as no-ops on RP2040 — both cores still run the full voice loop. For polyphonic sketches with voice arrays, call `setIsDualCore(false)` before `audioStart()` to restrict audio to Core 1 only and avoid double-advancing per-voice state:
```cpp
void setup() {
  setIsDualCore(false);  // Core 1 audio only — safe for voice array sketches
  audioStart();
}
```

**Audio input on RP2040:** Call `audioInputStart()` after `audioStart()` to initialise the separate I2S input instance. Uses BCLK+4 to avoid pin conflicts with output.

**Internal DAC Output:**
- Available on ESP32 (GPIO25/26) and ESP32-S2 (GPIO17/18) via `useInternalDAC()`
- Uses ESP-IDF `dac_continuous` driver with `DAC_CHANNEL_MODE_ALTER` for stereo
- 8-bit output (16-bit internally, converted at output stage)
- Buffered: 256-byte accumulation buffer, 8 DMA descriptors (~23ms depth)
- Partial-write retry loop ensures no samples are dropped at DMA seams
- ESP32 (dual-core): single audio task on core 0 at max priority, `loop()` on core 1
- ESP32-S2 (single-core): single audio task at priority 2, `dac_continuous_write()` blocking yields to `loop()`
- `audioLoop()` is a no-op on ESP32 — only meaningful for RP2040
- Known limitation: original ESP32's internal DAC may produce low-level artifacts with complex reverb signals due to DAC hardware characteristics; use external I2S DAC for best reverb quality

**Memory Management:**
- PSRAM auto-detection on ESP32 with `isPSRAMAvailable()`
- `psramAllocSafe()` and `psramAllocInt16()` for safe allocation with 10% headroom
- Automatic fallback to regular RAM if PSRAM unavailable

---

## Component Classes

### Osc.h - Wavetable Oscillator

**Purpose:** Band-limited wavetable oscillator with various waveforms, modulation, and spread/detuning.

**Key Features:**
- 16.16 fixed-point phase accumulator for efficiency
- Band-limited waveforms in 3 segments (low <208Hz, mid <831Hz, high >831Hz)
- Thread-safe phase increment with atomic operations on dual-core systems
- `phase_increment_fractional` loaded with `__ATOMIC_ACQUIRE` in all ESP32/RP2040 hot paths
  (`next`, `next2`, `phMod`, `phModInt`) — synchronizes-with `setFreq()`'s `__ATOMIC_RELEASE`
  stores, preventing stale `bandPtr` reads (wrong wavetable band) at note onset
- Anti-aliasing depth cap on every phMod variant: `depth_max = 9000 / (freq × cmRatio)`
  (Chowning sideband-vs-Nyquist limit). Cached in `setFreq()`/`setCMRatio()`, applied
  as a per-call clamp on `modIndex`. Cached values are `volatile` for cross-core visibility.
- Per-carrier `_pairLock` spinlock for atomically paired modulator+carrier advance (see FM methods)
- Dual-core phase synchronization for RP2040
- Sample and hold mode for stepped random output at oscillator frequency

**Waveform Generators (Static):**
- `sinGen()` - Sine wave (starts at 0, sine phase)
- `cosGen()` - Cosine wave (starts at MAX_16, cosine phase)
- `sawGen()` - Band-limited sawtooth (sine phase, peaks early)
- `sqrGen()` - Band-limited square (sine phase, peaks early)
- `triGen()` - Band-limited triangle (cosine phase, starts at MAX_16)
- `noiseGen()`, `noiseGen(grainSize)` - White noise, optionally with sample-and-hold grain
- `brownNoiseGen()`, `pinkNoiseGen()` - Brownian and pink noise
- `crackleGen()` - Sparse impulse noise

**Key Methods:**
```cpp
osc.setTable(waveTable);        // Set external wavetable
osc.setFreq(440.0f);            // Set frequency in Hz
osc.setPitch(69);               // Set MIDI pitch
osc.setPhase(0.5f);             // Set phase 0.0-1.0
osc.setSpread(0.01f);           // Detuning for thickness
osc.setPulseWidth(0.3f);        // PWM 0.05-0.95
osc.setNoise(true);             // Enable non-looping noise (random index on phase wrap)
osc.setSandH(true);             // Sample and hold: pick random sample once per period
int16_t held = osc.getSandHValue(); // Read the current held S&H value without advancing phase
osc.setCMRatio(2.0f);           // C:M ratio for FM anti-aliasing depth cap (default 1.0)
osc.disableAntiAlias();         // Disable depth cap entirely (feedback FM, intentional aliasing)

int16_t sample = osc.next();    // Get next sample (fast)
int16_t sample = osc.next2();   // Get next sample (interpolated, higher quality)
```

**Modulation:**
- `phMod(modulator, modIndex)` - Phase modulation (FM synthesis), scalar modulator value
- `phMod(modOsc, modIndex)` - FM with atomically paired modulator advance **(dual-core safe)**
- `phModInt(modulator, scaledIndex)` - FM with pre-scaled integer mod index (faster, no per-sample float multiply)
- `phModInt(modOsc, scaledIndex)` - Integer FM with atomically paired modulator advance **(dual-core safe)**
- `phMod2(modulator, modIndex)` - 2x oversampled FM (higher quality)
- `ringMod(audioIn)` - Ring modulation
- `feedback(modIndex)` - Self-feedback FM
- `nextMorph(secondTable, amount)` - Wavetable morphing (noise-aware: uses random index for noise morph target, S&H-aware when both flags set)
- `nextWTrans(secondTable, windowSize, dual, invert)` - Window transform

**FM Dual-Core Safety:**

When calling FM on a dual-core system (ESP32, RP2040), using the scalar overloads with an explicit `.next()` call introduces a race condition: `modOsc.next()` and `carrier.phModInt()` are two independent atomic operations that the other core can interleave, pairing the wrong modulator value with the wrong carrier phase and producing FM discontinuities.

Use the `Osc&` overloads to avoid this:
```cpp
// Unsafe on dual-core — two separate atomic ops, can be interleaved:
carrier.phModInt(modOsc.next(), scaledIndex);

// Safe — modulator and carrier advance atomically under _pairLock:
carrier.phModInt(modOsc, scaledIndex);
```

The `Osc&` overloads use a per-carrier `_pairLock` spinlock (`std::atomic<bool>`) with hold time of ~2 atomic fetch_add operations. On single-core platforms (ESP8266, ESP32-S2, ESP32-C3) the lock compiles away entirely via the `M16_ATOMIC_GUARD_BLOCKING` macro.

The scalar overloads remain valid when the modulator value comes from a non-Osc source (audio input, pre-computed buffer, one modulator fanned to multiple carriers).

**FM Anti-Aliasing Depth Cap:**

Every phMod variant (`phMod`, `phModInt`, `phMod2`, `phModMorph`, `phModWTrans` and their `Osc&` overloads) clamps `modIndex` against an anti-aliasing cap derived from the Chowning sideband-vs-Nyquist limit:

```
depth_max = 9000 / (freq × cmRatio)
```

Equivalently, `M × depth ≤ 9000 Hz` where `M` is the modulator frequency — the peak instantaneous frequency deviation cannot exceed 9 kHz. The constant 9000 was empirically tuned against measured aliasing thresholds across a freq×ratio grid; it is a hard limiter (never exceeds any measured threshold).

Configure the ratio with `setCMRatio(float)`:

```cpp
carrier.setCMRatio(2.0f);              // C:M = 1:2 → depth_max = 9000 / (freq × 2)
int16_t s = carrier.phMod(modOsc, modIndex);
```

The cap is recomputed inside `setFreq()` and `setCMRatio()` and stored as `_cachedDepthMax` (float) plus `_cachedDepthMaxScaled` (pre-scaled int32 for `phModInt`), so the per-sample cost is one compare.

**Behavior when `setCMRatio()` is never called:**
- Default `_cmRatio = 1.0`, `_cmRatioSet = false`.
- The `Osc&` overloads (`phMod(Osc&, ...)`, `phModInt(Osc&, ...)`) auto-derive `depth_max = 9000 / modOsc.getFreq()` and pre-clamp before delegating to the scalar version. The inner scalar then re-clamps with the cached default cap, giving a conservative double-clamp = `9000 / max(carrierFreq, modOsc.freq)`. Strictly safe for anti-aliasing but over-restrictive when the modulator is below the carrier (sub-1 ratio). Calling `setCMRatio()` explicitly disables the inner over-clamp and applies the formula exactly.
- Scalar overloads (modulator passed as `int16_t`) cannot auto-derive — they always use the cached cap with `cmRatio = 1.0` until `setCMRatio()` is called.

**When to call `setCMRatio()`:**
- Whenever the C:M ratio changes (preset load, encoder/pot move, FM-cascade pitch reassignment).
- After `setFreq()`/`setPitch()` calls automatically refresh the cap using the cached `_cmRatio`, so re-calling `setCMRatio()` on every pitch change is **not** required — only when the ratio itself changes.

**Disabling the cap — `disableAntiAlias()`:**

For feedback FM (self-modulation), intentional aliasing, or bit-crush-style effects the cap is counterproductive. Call `disableAntiAlias()` once in `setup()` to pin `_cachedDepthMax` to 9999 and prevent `setFreq()`/`setCMRatio()` from resetting it:

```cpp
osc.disableAntiAlias();  // cap frozen — subsequent setFreq() calls leave it untouched
```

This affects only the oscillator instance it is called on; other instances are unaffected. It cannot be reversed — if you need selective bypass, use a separate oscillator instance.

---

### Env.h - AHDSR Envelope Generator

**Purpose:** Attack-Hold-Decay-Sustain-Release envelope with timing in milliseconds.

**States:** 0=complete, 1=attack, 2=hold, 3=decay, 4=sustain, 5=release

**Timing model (audio-frame clock):** The envelope is a function of the global audio-frame clock (`audioFrameCount()` in M16.h), not `micros()` and not the `next()` call count. This is the root-cause fix for the envelope "zipper" — a `micros()`-based envelope freezes within a DMA-fill burst (one value per ~11.6 ms DMA buffer), and a per-`next()`-call counter only keeps time if `next()` is called once per audio frame. The frame clock advances once per output frame on every platform, so the envelope is smooth at audio rate **and** correctly timed regardless of how it is driven.

`next()` and `getValue()` are now **equivalent** — both evaluate the envelope at the current frame. Two supported patterns, both correct with no code changes:
- **Control rate + audio rate (canonical):** advance/trigger in `loop()` (`start()`, `startRelease()`), read per audio sample with `getValue()` in `audioUpdate()`. Smoothness comes from the per-sample `getValue()` reading the live clock.
- **Per sample:** call `next()` (or `getValue()`) directly in `audioUpdate()`.

The cache-the-value-at-control-rate anti-pattern (`v = env.next()` in `loop()`, reuse stale `v` per sample) re-introduces a zipper — read `getValue()` per sample instead.

**Thread Safety:** `envVal`, `envState`, and the `releaseTriggered` note-off latch are `std::atomic` for dual-core consistency. The entire pre-release contour (attack/hold/decay/sustain and the deterministic AD/AR releases when `sustain == 0`) is a pure function of frames-since-start, so concurrent evaluation from both audio cores (single-voice sketches call `getValue()` on both cores) is safe. Only `startRelease()` (asynchronous note-off) writes a release snapshot, published under the atomic flag.

**Key Methods:**
```cpp
env.setAttack(10);      // Attack time in ms
env.setHold(0);         // Hold time in ms
env.setDecay(100);      // Decay time in ms
env.setSustain(0.5);    // Sustain level 0.0-1.0
env.setRelease(200);    // Release time in ms
env.setMaxLevel(1.0);   // Peak level 0.0-1.0 (gain control)

env.start();            // Trigger envelope
env.startRelease();     // Begin release phase (note off)
uint16_t val = env.getValue();  // Evaluate at current audio frame (call per sample)
uint16_t val = env.next();      // Equivalent to getValue()
```

**Special Features:**
- `setDecayRepeats(n)` - Repeat decay phase (claps, guiro); applies when `sustain == 0`
- `setResetOnStart(true)` - Reset to 0 on start (consistent drum attacks)
- Exponential curves for natural decay/release

**Caveats from the audio-frame timing change:**
- `setValue()` is transient — overwritten on the next `getValue()`/`next()` (the clock-driven evaluator is authoritative).
- `getStartTime()` returns the audio-frame index at note start, not a `micros()` timestamp.
- `getAttack()`/`getRelease()` return milliseconds (durations are stored in ms; same external values as before).
- Per-sample `getValue()` runs the evaluator (a few float ops) rather than an atomic load — negligible for single/few voices, a small per-voice-per-sample cost for large block-split polyphony.

---

### SVF.h - State Variable Filter

**Purpose:** Multi-mode resonant filter with simultaneous LPF, HPF, BPF, and Notch outputs.

**Safe Frequency Range:** 40Hz to 21% of sample rate (~9200Hz at 44.1kHz)

**Key Methods:**
```cpp
svf.setFreq(1000);          // Cutoff in Hz (40-9200)
svf.setNormalisedCutoff(0.5); // Cutoff 0.0-1.0 (non-linear mapping)
svf.setRes(0.8);            // Resonance 0.3-0.84

int16_t lp = svf.nextLPF(input);  // Low-pass
int16_t hp = svf.nextHPF(input);  // High-pass
int16_t bp = svf.nextBPF(input);  // Band-pass
int16_t notch = svf.nextNotch(input);  // Notch
int16_t mix = svf.nextFiltMix(input, 0.5);  // LPF-BPF-HPF crossfade

svf.reset();  // Clear state for consistent attacks
```

**Thread Safety:** Uses atomic loads for filter coefficients and non-blocking try-lock (`_svfLock`) on filter state (`low`, `band`, `high`) for dual-core systems. If lock is held by another core, returns previous output (1-sample hold, inaudible at 44.1kHz). Same pattern as Bob.h.

---

### EMA.h - Simple Low/High Pass Filter

**Purpose:** Minimal CPU overhead single-pole IIR filter (exponential moving average).

```cpp
ema.setFreq(5000);          // Cutoff in Hz
ema.setCutoff(0.5);         // Normalized 0.0-1.0
int16_t lp = ema.nextLPF(input);
int16_t hp = ema.nextHPF(input);
```

---

### Del.h - Delay Line

**Purpose:** Audio delay with feedback, filtering, and PSRAM support.

**Key Methods:**
```cpp
del.setMaxDelayTime(500);   // Max buffer in ms (allocates memory)
del.setTime(200);           // Current delay in ms
del.setLevel(0.8);          // Output level 0.0-1.0
del.setFeedback(true);      // Enable feedback
del.setFeedbackLevel(0.5);  // Feedback amount 0.0-1.0
del.setFiltered(2);         // Smoothing 0-4 (higher = duller)

int16_t out = del.next(input);  // Process sample
int16_t read = del.read();      // Read without writing
del.write(sample);              // Write without reading
```

---

### All.h - Allpass Filter

**Purpose:** Schroeder allpass filter for reverb diffusion and phase effects.

**First-order allpass:** `y[n] = -g*x[n] + x[n-D] + g*y[n-D]`

```cpp
all.setDelayTime(50);       // Delay in ms
all.setFeedbackLevel(0.7);  // Feedback 0.0-1.0
int16_t out = all.next(input);

// Second-order allpass (for phaser/resonator effects)
all.setSecondOrderFreq(1000, 2.0);  // Center freq, Q
int16_t out = all.secondOrder(input);
```

---

### Comb.h - Comb Filter

**Purpose:** Comb filter with feedforward and feedback paths for metallic resonances.

**Formula:** `y[n] = a*x[n] + b*x[n-D] + c*y[n-D]`

```cpp
comb.setDelayTime(10);          // Delay in ms
comb.setInputLevel(1.0);        // Input gain
comb.setFeedforwardLevel(0.7);  // Feedforward
comb.setFeedbackLevel(0.5);     // Feedback
int16_t out = comb.next(input);
```

---

### Samp.h - Sample Playback

**Purpose:** Mono/stereo sample playback with variable speed, looping, envelope, and granular features.

**Key Features:**
- 32.32 fixed-point phase for long audio support
- Thread-safe spinlock for 64-bit phase on ESP32
- Shared envelope table for memory efficiency
- Zero-crossing detection for click-free loops
- Linear interpolation for smooth pitch shifting
- Edge fades for granular synthesis
- Reverse stereo fix in `nextStereo()` (decrements phase when `reverse=true`)
- Near-zero smoothing: IIR blend below a configurable amplitude threshold to reduce quantisation click artifacts at zero crossings

**Key Methods:**
```cpp
Samp::initSharedEnvelope(2048, 0.8, 0);  // size, curve, type (0=Gauss, 1=cos, 2=linear)

samp.setTable(buffer, frameCount, sampleRate, numChannels);
samp.setStart(0);
samp.setEnd(10000);
samp.setSpeed(1.0);             // Playback speed
samp.setFreq(880);              // Pitch-based speed
samp.setBasePitch(69);          // Reference MIDI pitch (default 60); used by setPitch()
samp.setPitch(72);              // Playback pitch in MIDI note numbers (adjusts speed relative to basePitch)
samp.setLoopingOn();
samp.setReverse(true);
samp.setInterpolation(true);    // Linear interpolation
samp.setEdgeFade(true);         // Granular click reduction
samp.setEnvPhaseOffset(0.25);   // Stagger envelope for multi-voice
samp.setNearZeroSmooth(true);         // Enable near-zero IIR smoothing (default off)
samp.setNearZeroSmooth(true, 512);    // With custom threshold (default 1024)

samp.start();
int16_t mono = samp.next();
bool playing = samp.nextStereo(leftOut, rightOut);  // Thread-safe stereo
```

**Flash-Stored Samples (`loadFromFlash`):**

Loads an IMA ADPCM-compressed sample from PROGMEM flash, decodes and upsamples it into PSRAM (or internal RAM), then configures the `Samp` instance ready for playback. No SD card required at runtime.

```cpp
// In sketch flash — generated offline via Wav::compress() + Wav::exportToHeader():
#include "snare_adpcm.h"   // defines SNARE_DATA[], SNARE_DATA_SIZE

Wav wav;
Samp samp;

samp.loadFromFlash(wav, SNARE_DATA, SNARE_DATA_SIZE);
samp.setBasePitch(69);   // match the original sample's pitch
```

**`bufRate` in `loadFromFlash()`:**
- **Internal DAC**: the DAC driver runs `audioUpdate()` at `2×SAMPLE_RATE` (88200 Hz). `loadFromFlash()` detects `_useInternalDAC` and sets `bufRate = SAMPLE_RATE/2` to compensate.
- **External I2S (any core count)**: SAMP_LOCK serialises all `next()` calls to exactly `SAMPLE_RATE` calls/sec total. `bufRate = SAMPLE_RATE` is always correct — the lock neutralises the dual-core double-advance that was previously suspected.

Flash cost: ~3 KB per 0.5s sample (compressed). PSRAM/RAM cost: ~43 KB decoded at 44100 Hz. See `examples/SampleDataPlay` for the full workflow.

---

### Wav.h - WAV Loader

**Purpose:** SD-card WAV loading and streaming helpers for sample playback.

**Key Features:**
- Bit-depth conversion (8/16/24/32-bit PCM and 32-bit float)
- Optional PSRAM allocation on ESP32
- External buffer support to avoid fragmentation
- Persistent streaming handle: `openForStreaming()`, `readFramesFast()`, `readFramesWrapFast()`
- Streaming math helpers: `normalizeFrame()`, `normalizeEpoch()`, `computeHeadroom()`, `writeChunkToRing()`

---

### FX.h - Effects Processor

**Purpose:** Collection of DSP effects including distortion, reverb, chorus, and compression.

**Distortion:**
```cpp
fx.softClip(sample, 3.0);       // Default (tube-style)
fx.softClipAtan(sample, 3.0);   // Warm atan
fx.softClipCubic(sample, 3.0);  // Bright polynomial
fx.softClipTanh(sample, 3.0);   // Balanced
fx.softClipTube(sample, 3.0);   // Asymmetric tube
fx.softClipFold(sample, 2.0);   // Foldback
fx.softClipInt(sample, 2048);   // Integer-only (fast)
fx.waveFold(sample, 2.0);       // Wave folding
fx.overdrive(sample, 2.0);      // Filtered overdrive
```

**Bit Crusher:**
```cpp
fx.bitCrush(sample, 8);         // Bit depth (1-16)
fx.bitCrush(sample, 6, 4);      // With sample rate reduction
fx.bitCrushF(sample, 0.5);      // Normalized amount
```

**Compression:**
```cpp
fx.setCompression(0.5, 4.0, 5.0, 100.0);  // threshold, ratio, attack_ms, release_ms
fx.compression(sample);          // Mono
fx.compressionL(sample);         // Stereo left
fx.compressionR(sample);         // Stereo right
```

**Reverb:**
```cpp
fx.initReverbSafe();            // Call in setup() to pre-allocate
fx.setReverbLength(0.8);        // Decay 0.0-1.0
fx.setReverbMix(0.3);           // Wet amount 0.0-1.0
fx.setDampening(0.3);           // HF absorption 0.0-1.0
fx.setReverbSize(4.0);          // Memory multiplier

int16_t mono = fx.reverb(input);
fx.reverbStereo(inL, inR, outL, outR);
fx.reverbStereo2(inL, inR, outL, outR);  // With allpass preprocessing
fx.reverbStereoInterp(inL, inR, outL, outR);  // Half-rate (CPU saver)
```

**Reverb Implementation Notes:**
- Two reverb paths: optimized (inlined buffers, power-of-2 wrap) for sizes ≤ REV_BUF_SIZE (1024 samples), and legacy Del-based path for larger sizes
- Both paths include: input HPF (prevents DC accumulation), dampening lowpass, soft limiting (prevents hard clipping in feedback), and Hadamard mixing matrix
- Legacy path disables Del's built-in smoothing filter (`setFiltered(0)`) to avoid double-filtering with explicit dampening

**Dual-core thread safety for reverb functions:**
- `reverbStereo`: uses `M16_ATOMIC_GUARD` (non-blocking try-lock) + `reverbCacheL`/`reverbCacheR` output cache. If the lock is taken by the other core, returns the cached previous output. Both core outputs are coherent; no DMA ordering artifacts.
- `reverbStereo2`: same non-blocking guard + cache. Adds allpass preprocessing (`All` instances) processed *inside* the lock so `All`'s unprotected buffers are never touched by two cores simultaneously. Runs at half sample rate (toggle) with IIR smoothing (`>> 2`) to halve CPU cost.
- `reverbStereoInterp`: half-rate toggle + IIR smoothing; no allpass. Can trigger Task Watchdog if used in a very lightweight audio loop (Core 0 never blocks long enough for IDLE0).
- **Do not use `M16_ATOMIC_GUARD_BLOCKING` for reverb** — both cores serialise but process in lock-acquisition order, not DMA-slot order → out-of-order samples → audible crackle.

**Chorus:**
```cpp
fx.setChorusMix(0.5);           // Dry/wet blend 0.0 (dry) – 1.0 (wet); default 0.5
fx.setChorusDepth(0.5);         // Internal dry/delayed balance 0.0-1.0
fx.setChorusWidth(0.3);         // LFO pitch depth 0.0-1.0
fx.setChorusRate(0.5);          // LFO Hz
fx.setChorusFeedback(0.3);      // Feedback 0.0-1.0
fx.setChorusDelayTime(30);      // Base delay ms (20-40)

int16_t mono = fx.chorus(input);
fx.chorusStereo(inL, inR, outL, outR);
```

**Chorus initialisation:** All public chorus setters (`setChorusMix`, `setChorusDepth`, `setChorusWidth`, `setChorusRate`, `setChorusDelayTime`) auto-initialise the chorus buffers on first call. Calling any setter in `setup()` before `audioStart()` is sufficient — no separate init call needed. (`setChorusFeedback` is excluded from auto-init because it is called internally by `initChorus()` itself.)

**`setChorusMix` vs `setChorusDepth`:** These control different things. `setChorusMix` is the top-level dry/wet blend between the unprocessed input and the full chorus output — at 0.0 the effect is bypassed, at 1.0 it is fully wet. `setChorusDepth` controls the internal constant-power balance between the dry and delayed paths *within* the chorus processing; it affects the character of the chorus sound (thin vs thick). Both can be set independently.

**Wave Shaping:**
```cpp
fx.setShapeTable(table, size);           // Custom table
fx.setShapeTableSoftClip(5.0);           // Generate soft clip
fx.setShapeTableSigmoidCurve(0.5);       // Generate S-curve
fx.setShapeTableJitter(1000);            // Generate grainy
int16_t out = fx.waveShaper(input, 0.8); // Apply with amount
```

**Karplus-Strong (Pluck):**
```cpp
fx.pluck(noiseInput, 440.0, 0.99);  // input, freq, depth
```

**Smoothing:**
```cpp
fx.smooth(sample, 0.1);
fx.smoothL(sample, 0.1);
fx.smoothR(sample, 0.1);
fx.smoothStereo(inL, inR, outL, outR, 0.1);
```

---

### Phys.h - Physical Modelling Synthesis

**Purpose:** Karplus-Strong plucked string and digital waveguide synthesis with configurable excitation position.

**Key Features:**
- Karplus-Strong pluck algorithm with band-limited feedback averaging
- Digital waveguide model with dual delay lines (left/right traveling waves)
- Configurable excitation position (normalised 0.0-1.0 relative to delay length)
- Non-blocking thread safety via `M16_ATOMIC_GUARD` (dual-core safe)
- Clipped delay lines to prevent runaway feedback growth
- Lazy buffer allocation (1500 samples, ~34ms at 44.1kHz)

**Key Methods:**
```cpp
Phys phys;

// Allpass in feedback path (default: delay 0, feedback 0 — no effect)
phys.setStiffnessTime(0.3);   // 30% of max allpass delay
phys.setStiffnessFeeback(0.5);  // allpass feedback level 0.0-1.0

// Karplus-Strong pluck
phys.setPluckPosition(0.5);    // Excite at midpoint of string (0.0=read head, 1.0=write head)
int16_t out = phys.pluck(audioIn, freq, depth);

// Digital waveguide
phys.setWgPosition(0.3);       // Excite at 30% from read head
int16_t out = phys.waveguide(audioIn, freq, depth);
```

**Parameters:**
- `audioIn` — Excitation signal (noise burst, impulse, oscillator)
- `freq` — Fundamental frequency in Hz (determines delay line length)
- `depth` — Feedback level 0.0-1.0. Higher = longer decay (use 0.96-0.995 for pluck)
- `position` — Normalised 0.0-1.0. 0.0 = at read head (far end), 1.0 = at write head (default)

**Allpass Filter in Feedback Loop:**
- First-order Schroeder allpass (`All.h`) placed at the end of the feedback chain
- Default delay 0ms and feedback 0 — no effect on sound
- `setStiffnessTime(float stiffness)` — normalised 0.0-1.0, proportion of max allpass delay
- `setStiffnessFeeback(float level)` — sets feedback level 0.0-1.0 for both
- Adds phase colouring to the resonance without changing the pitch

**Excitation Position:**
- Recomputed automatically when pitch (delay length) changes
- `setPluckPosition()` / `setWgPosition()` control where the combined excitation+feedback signal enters the delay line relative to the read head
- Default 0.0 (at read head) — bright, harmonically rich
- 0.5 — emphasises odd harmonics, classic guitar-like tone
- 1.0 — at write head (original behaviour)

**Thread Safety:** Uses `M16_ATOMIC_GUARD` (non-blocking try-lock). If the lock is held by the other core, returns the cached previous output (1-sample hold, inaudible at 44.1kHz). Same pattern as SVF.h and Bob.h.

---

### Arp.h - Arpeggiator

**Purpose:** MIDI note arpeggiator with various patterns.

**Directions:** `ARP_ORDER`, `ARP_UP`, `ARP_UP_DOWN`, `ARP_DOWN`, `ARP_RANDOM`, `ARP_RANDOM2`

```cpp
int notes[] = {60, 64, 67, 72};
Arp arp(notes, 4, 2, ARP_UP_DOWN);  // values, count, octaves, direction

arp.setValues(notes, 4);
arp.setDirection(ARP_UP);
arp.setRange(3);            // Octave range
arp.start();

int pitch = arp.next();     // Get next note
int pitch = arp.again();    // Repeat last
double ms = arp.calcStepDelta(120, 4);  // BPM, subdivision -> ms
```

---

### Seq.h - Step Sequencer

**Purpose:** Integer step sequencer for patterns.

```cpp
int pattern[] = {60, 0, 64, 0, 67, 0, 72, 0};
Seq seq(pattern, 8, 4);     // values, size, stepDiv

seq.setSequence(pattern, 8);
seq.setStepValue(0, 72);
seq.setRandom(true);
seq.euclideanGen(100, 5, 0);    // value, hits, rotate
seq.randWalkGen(60, 3, 48, 72); // start, maxDev, min, max

int val = seq.next();
int val = seq.skip(2);      // Advance by N
double ms = Seq::calcStepDelta(120, 4, 2);  // BPM, slice, div
```

---

### SVF2.h - Higher-Quality State Variable Filter

**Purpose:** Alternative to SVF.h using 64-bit arithmetic with gain compensation at high resonance. Higher CPU cost than SVF but avoids clipping artefacts at high Q.

**Thread Safety:** Uses `M16_ATOMIC_GUARD_BLOCKING` for deterministic dual-core operation, ensuring Core 1 waits for fresh state rather than using a 1-sample hold.

**Key Methods:**
```cpp
svf2.setFreq(1000);             // Cutoff in Hz (0–10kHz)
svf2.setNormalisedCutoff(0.5);  // Cutoff 0.0–1.0
svf2.setRes(0.8);               // Resonance 0.01–1.0 (gain-compensated)

int16_t lp = svf2.nextLPF(input);
int16_t hp = svf2.nextHPF(input);
int16_t bp = svf2.nextBPF(input);
int16_t notch = svf2.nextNotch(input);
int16_t lp = svf2.currentLPF();   // Last computed LP output (no advance)
svf2.reset();
```

Use SVF2 when high resonance causes clipping with SVF, or when audio quality is more important than minimal CPU.

---

### Bob.h - Moog Ladder Filter

**Purpose:** 4-pole Moog-style lowpass ladder filter with tanh saturation and anti-runaway state clamping. Drop-in resonant lowpass with a warmer character than SVF.

**Key Methods:**
```cpp
bob.setFreq(1000);   // Cutoff in Hz
bob.setRes(0.8);     // Resonance 0.0–1.0
int16_t out = bob.next(input);
bob.reset();
```

**Thread Safety:** Same non-blocking try-lock pattern as SVF.h — if the lock is held by the other core, returns the previous output (1-sample hold).

Uses a tanh lookup table (`softTanh`) for saturation and clamps internal state against numerical runaway at high resonance settings.

---

### BBD.h - Bucket Brigade Delay

**Purpose:** Analog BBD / tape delay emulation. Fixed 4096-sample buffer; delay time is controlled by adjusting the simulated clock (scan rate), not the buffer length — giving the characteristic pitch-shift artefact when delay time changes.

**Thread Safety:** Hardened with `M16_ATOMIC_GUARD_BLOCKING` to eliminate "fuzz" (sample-hold artifacts) caused by dual-core contention.

**Parameter slewing:** All three time/level setters use target+slew to prevent buffer-seam clicks. When a parameter changes instantly, the ring buffer still holds content written at the old value; one full delay period later the read head encounters the seam, producing a click. Slewing ensures transitions complete before the buffer wraps:

- `setScanRate()` / `setTime()` — sets `_targetScanRate`; `next()` slews `scanRate` by 32 fp-units/sample (~42ms full range). Produces a smooth tape-like pitch shift rather than a click.
- `setLevel()` — sets `_targetDelayLevel`; slews by 4 units/sample (~6ms full range).
- `setFeedbackLevel()` — sets `_targetFeedbackLevel`; slews by 4 units/sample (~6ms full range).

The `empty()` method and the 4-parameter constructor both snap live values directly to their targets (bypassing slew) so the first audio output is always correct.

**Key Methods:**
```cpp
bbd.setTime(200);        // Delay in ms (~31–9000 ms range)
bbd.setScanRate(1.0);    // BBD clock rate multiplier (lower = darker, longer)
bbd.setLevel(0.8);
bbd.setFeedback(true);
bbd.setFeedbackLevel(0.5);
bbd.setFiltered(2);      // Output smoothing 0–4

int16_t out = bbd.next(input);
int16_t r   = bbd.read();
bbd.write(sample);
```

Includes soft saturation for analog warmth and sample-and-hold output between clock ticks. Drop-in replacement for `Del`.

---

### Verb.h - Freeverb-Style Reverb

**Purpose:** Freeverb algorithm (parallel comb filters into series allpass filters) as a standalone class. Offers a different reverb character to the Schroeder-style reverb in FX.h.

**Key Methods:**
```cpp
verb.setHighQuality(true);  // 8 combs + 4 allpass (true) or 4+2 (false); call BEFORE init()
verb.init();                // Allocate buffers (PSRAM-aware); call in setup()
verb.setRoomSize(0.8);      // 0.0–1.0
verb.setDamping(0.3);       // HF absorption 0.0–1.0
verb.setWetMix(0.5);
verb.setStereoWidth(1.0);

int16_t mono = verb.next(input);
verb.nextStereo(inL, inR, outL, outR);  // Thread-safe via atomic guard
```

Delay times are scaled to `SAMPLE_RATE`. Not recommended for ESP8266 (insufficient RAM). `init()` must be called before `next()`.

---

### Sync.h - GPIO Sync Clock

**Purpose:** Send and receive analogue sync clock pulses compatible with Korg Volca, Teenage Engineering Pocket Operators, and similar gear (default 2 PPQN, configurable).

**Key Methods:**
```cpp
Sync sync(outPin, inPin);
sync.setPPQN(2);           // Pulses per quarter note
sync.setOutBpm(120.0f);

// In loop():
if (sync.pulseOnTime(millis()))  sync.startPulse();
if (sync.pulseOffTime(millis())) sync.endPulse();

bool beat = sync.receivePulse(millis());
float bpm = sync.getInBpm();
```

Output requires a voltage divider (~2:1) to reduce 3.3 V GPIO to the ~1.4 V line level expected by Volca/PO inputs.

---

### TLV.h - TLV320AIC3104 Codec Driver

**Purpose:** I2C configuration driver for the TLV320AIC3104 stereo audio codec. Enables the chip as an I2S slave at 44.1 kHz or 48 kHz; no MCLK required (PLL locked to BCLK).

**Key Methods:**
```cpp
TLV tlv(sdaPin, sclPin);       // Default SDA=21, SCL=22
tlv.seti2cPins(sda, scl);
bool ok = tlv.begin(SAMPLE_RATE);  // Enables headphone + line out, stereo line in
```

**Important pin conflict:** The default I2S DIN is GPIO21, which is also the default I2C SDA. Move I2S DIN to GPIO19 (or another free pin) when using the TLV codec alongside ADC input. Includes automatic I2C bus recovery (9-clock pulse + STOP) and hardware reset if a reset pin is wired.

Must call `audioStart()` before `tlv.begin()` so I2S clocks are running when the codec initialises.

---

### MIDI16.h - MIDI I/O

**Purpose:** Lightweight MIDI send/receive over hardware serial. Uses Serial2 on ESP32 (pins configurable), default Serial on ESP8266. Hardcoded 31250 baud.

On ESP32, a high-priority FreeRTOS clock task can be started with `beginClockTask()`. When active, the task owns all Serial2 reads and routes bytes through internal SPSC lock-free ring buffers. This decouples clock timestamp capture and outgoing clock send from `loop()` scheduling jitter.

**Key Methods:**
```cpp
MIDI16 midi(rxPin, txPin);     // ESP32 defaults: rx=37, tx=38

// ESP32 clock task (optional but recommended for timing accuracy)
midi.beginClockTask(coreID, priority);  // default core=1, priority=5
midi.endClockTask();
midi.setClockSendBpm(bpm);   // start/update outgoing clock from clock task; bpm=0 stops
midi.stopClockSend();

// Send (always available, uses Serial2.write directly)
midi.sendNoteOn(channel, pitch, velocity);
midi.sendNoteOff(channel, pitch, velocity);
midi.sendControlChange(channel, cc, value);
midi.sendClock();   // 24 PPQN (manual send, use setClockSendBpm for task-based send)
midi.sendStart();  midi.sendContinue();  midi.sendStop();

// Receive — drain all available messages each loop iteration
uint8_t status;
while ((status = midi.read()) != 0) {
  int ch = midi.getChannel();
  int d1 = midi.getData1();
  int d2 = midi.getData2();
}

// Tempo from incoming MIDI clock
int16_t bpm = midi.clockToBpm();        // 8-sample rolling average with hysteresis
int16_t bpm = midi.getBpm();            // last computed BPM, no side effects
long    us  = midi.calcTempoDelta(120); // µs between clock pulses at given BPM
```

**CC Coalescing (automatic):**
When a CC message is assembled, `read()` peeks ahead in the buffer and drains any subsequent CC messages for the same channel+controller, keeping only the final value. This collapses an entire slider sweep (up to 128 messages) into a single `read()` call, eliminating lag at 31250 baud. Use the drain-loop pattern (`while (midi.read() != 0)`) — a timer-gated single read per loop iteration defeats this optimisation.

**BPM Tracking (`clockToBpm()`):**
- **8-sample rolling average** over inter-pulse deltas for a balance of responsiveness and stability.
- **0.75 BPM hysteresis**: integer output only updates when the computed value differs from the last reported value by more than 0.75 BPM, preventing oscillation between adjacent integers.
- **Stop/restart detection**: a gap of >1 second resets the history buffer. The next valid pulse pre-fills all history slots with the current delta so the first post-restart reading is accurate rather than 8× too high.
- **3ms burst guard**: deltas below 3ms are discarded to prevent drain-loop artefacts from inflating BPM readings.
- **Hardware timestamps (with clock task)**: when `beginClockTask()` is active, clock byte timestamps are captured at read time in the task (not at `clockToBpm()` call time), giving accuracy independent of `loop()` blocking.

**Clock Send Jitter (with clock task):**
`setClockSendBpm()` runs outgoing clock pulses from the clock task using adaptive sleep: the task sleeps for most of the inter-pulse interval then busy-waits the final 1ms for ~50µs send accuracy, compared to ±1ms with a fixed `vTaskDelay(1)`. Interval advance is cumulative (not reset to `micros()`) so no long-term BPM drift.

**Internal SPSC Ring Buffers (ESP32 clock task):**
- `_rxBuf[64]` — all incoming bytes forwarded from Serial2 to `read()` via the ring
- `_tsBuf[32]` — one `micros()` timestamp per 0xF8 byte, pushed before the byte enters `_rxBuf` so the timestamp is always available when `clockToBpm()` is called
- Both rings are Single-Producer (clock task) / Single-Consumer (`loop()`) with volatile write indices — no mutex required

**Status Constants:**
```cpp
MIDI16::noteOn            // 0x90
MIDI16::noteOff           // 0x80
MIDI16::controlChange     // 0xB0
MIDI16::pitchBend         // 0xE0
MIDI16::clock             // 0xF8
MIDI16::start             // 0xFA
MIDI16::cont              // 0xFB
MIDI16::stop              // 0xFC
```

---

### Mic.h - I2S Audio Input

**Purpose:** Reads stereo samples from an I2S MEMS microphone. Uses the `rx_handle` I2S channel defined in M16.h on ESP32; a separate I2S input instance on RP2040. Not implemented for ESP8266.

**Key Methods:**
```cpp
int16_t l = mic.nextLeft();
int16_t r = mic.nextRight();
```

Internally maintains a 256-sample circular input buffer with deinterlaced left/right channels. On RP2040 call `audioInputStart()` before using. On ESP32 the rx channel is initialised alongside tx in `audioStart()`.

---

## Utility Functions (M16.h)

**Pitch Conversion:**
```cpp
float freq = mtof(69);          // MIDI -> Hz (fast lookup)
float midi = ftom(440.0);       // Hz -> MIDI
float freq = intervalFreq(440, 7);  // Interval transposition
int pitch = pitchQuantize(61, scaleArray, 0);  // Snap to scale
```

**Panning (constant-power):**
```cpp
float leftGain = panLeft(0.3);   // 0.0=left, 1.0=right — output clamped to [0,1]
float rightGain = panRight(0.3); // output clamped to [0,1]
```

**Math Utilities:**
```cpp
int32_t clipped = clip16(largeValue);   // Clamp to 16-bit
int16_t clipped = clip(val, -1000, 1000);
float mapped = floatMap(x, 0, 1, 0, 100);
float smoothed = slew(current, target, 0.1);
float sig = sigmoid(0.7);       // S-curve 0.0-1.0
float cos = cosr(step, 16, 8);  // Cosine LFO
float ms = bpmToMs(120);        // BPM -> ms per beat
```

**Random (ISR-safe):**
```cpp
int r = rand(100);              // 0 to 99 (fast xorshift)
int r = audioRand(100);         // ISR-safe (xoshiro128**)
int r = gaussRand(100);         // Gaussian approx
int r = gaussRand3(100);        // Fast 3-sample gaussian
int r = audioRandGauss(100, 3); // ISR-safe gaussian
float r = chaosRand(1.0);       // Chaotic attractor
audioRandSeed(micros());        // Seed from time
```

---

## Default I2S Pin Configurations

| Platform | BCLK | WS/LRCLK | DOUT | DIN |
|----------|------|----------|------|-----|
| ESP8266 | GPIO15 | GPIO2 | GPIO3 (RX) | - |
| ESP32 | GPIO16 | GPIO17 | GPIO18 | GPIO21 |
| RP2040 | GPIO16 | GPIO17 | GPIO18 | GPIO19 |

Configure with: `seti2sPins(bck, ws, dout, din);` before `audioStart()`

---

## Required Program Structure

**ESP32 / ESP8266:**
```cpp
#include "M16.h"
#include "Osc.h"

Osc osc;

void setup() {
  Serial.begin(115200);
  setIsDualCore(false);  // safe default for one stateful oscillator chain
  osc.sinGen();
  osc.setPitch(69);
  audioStart();
}

void loop() {
  // Non-audio-rate tasks (UI, MIDI, sequencing)
}

void audioUpdate() {  // REQUIRED - called at sample rate from audio task(s)
  int16_t sample = osc.next();
  audioBlockWrite(sample, sample);  // efficient single-core block output
}
```

For independent polyphonic voices on dual-core ESP32, replace the single-core selection with `setIsDualCore(true)`, partition the voice loop using `audioPartitionOffset()` / `audioPartitionStride()`, submit partial mixes with `audioBlockWrite()`, and register shared effects through `setAudioPostProcessCallback()`.

**RP2040 (Pico / Pico 2) — dual-core mode:**
```cpp
#include "M16.h"
#include "Osc.h"

Osc osc;

void setup() {
  Serial.begin(115200);
  osc.sinGen();
  osc.setPitch(69);
  audioStart();  // launches Core 1 audio loop
}

void loop() {
  audioLoop();  // REQUIRED on RP2040 - Core 0 produces samples here
  // UI, MIDI, sequencing between samples
}

void audioUpdate() {  // called from both cores via audioLoop() and Core 1 callback
  int16_t sample = osc.next();
  i2s_write_samples(sample, sample);
}
```

For polyphonic RP2040 sketches with voice arrays, call `setIsDualCore(false)` in `setup()` before `audioStart()` and omit the `audioLoop()` call — audio runs on Core 1 only, `loop()` is free for UI.

---

## Performance Considerations

1. **ESP32 audio architecture:** Choose one of two explicit patterns:
   - Serial/shared-state graph: `setIsDualCore(false)` + complete DSP chain + `audioBlockWrite()`.
   - Independent polyphonic voices: `setIsDualCore(true)` + `audioPartitionOffset()` / `audioPartitionStride()` + `audioBlockWrite()` + `setAudioPostProcessCallback()` for master effects.
   Do not advance the same stateful object from both audio tasks. For FM between independent oscillator pairs, use the `phMod(Osc&, ...)` / `phModInt(Osc&, ...)` overloads.

2. **RP2040 dual-core:** `audioLoop()` must be called from `loop()` for Core 0 to produce audio. Omitting it gives Core 0 fully to UI/MIDI but halves throughput. Voice array sketches must use `setIsDualCore(false)` — the partition API is not implemented on RP2040 and both cores would otherwise run the full voice loop.

3. **Memory:** Use PSRAM for large buffers (delays, reverb). Check `isPSRAMAvailable()`.

4. **Fixed-point:** Many classes use 10-bit (0-1024) or 15-bit fixed-point. Shift operations (`>>10`, `>>15`) are faster than division.

5. **Filter stability:** SVF safe up to 21% of sample rate. Higher frequencies can cause instability.

6. **Avoid in audioUpdate():**
   - `Serial.print()` (not ISR-safe)
   - `malloc()`/`new` (use pre-allocation)
   - `rand()` (use `audioRand()`)
   - Floating-point heavy operations when avoidable

7. **Pre-initialization:** Call `fx.initReverbSafe()` and similar in `setup()` to avoid allocation in audio callback.

8. **Internal DAC limitations:** The 8-bit internal DAC works well for oscillators, filters, and delays. Complex reverb signals may exhibit low-level artifacts on the original ESP32's DAC hardware. For reverb-heavy patches, use an external I2S DAC. The ESP32-S2's internal DAC has better low-level characteristics.

9. **Waveform phase alignment for morphing:** When using `nextMorph()`, waveforms should be phase-aligned to avoid cancellation during crossfade. `triGen()`, `cosGen()` are cosine-phase (start at MAX_16). `sinGen()`, `sqrGen()`, `sawGen()` are sine-phase (start at 0, peak early). For morphing across all standard waveforms, use `sinGen()` as the base since sqr/saw are also sine-phase, and tri peaks early enough to blend smoothly. `nextMorph()` supports noise-aware morphing — when `setNoise(true)`, the morph target reads random table positions via `audioRand()`. When `setSandH(true)` is also set, the noise morph target holds a single random value per period instead.

10. **Block pipeline balance:** In dual-core block mode, Core 1 can render ahead while Core 0 performs post-combine processing. If master effects are substantially more expensive than a voice partition, assign fewer voices to Core 0 manually or reduce effect cost. Monitor `audioBlockSyncTimeoutCount()` and `audioDmaWriteTimeoutCount()` when diagnosing gaps.

---

## File Summary

| File | Purpose | Key Class |
|------|---------|-----------|
| M16.h | Core system, I2S, dual-core audio, utilities | - |
| Osc.h | Band-limited wavetable oscillator | `Osc` |
| Env.h | AHDSR envelope | `Env` |
| SVF.h | State variable filter (LPF/HPF/BPF/Notch) | `SVF` |
| SVF2.h | Higher-quality SVF with 64-bit math and gain compensation | `SVF2` |
| EMA.h | Simple single-pole IIR filter | `EMA` |
| Bob.h | Moog ladder 4-pole lowpass filter | `Bob` |
| Del.h | Delay line with feedback and filtering | `Del` |
| BBD.h | Bucket brigade delay emulation | `BBD` |
| All.h | Allpass filter | `All` |
| Comb.h | Comb filter | `Comb` |
| Samp.h | Sample playback | `Samp` |
| Wav.h | WAV file loading and streaming | - |
| FX.h | Effects: reverb, chorus, compression, distortion | `FX` |
| Phys.h | Physical modelling: Karplus-Strong pluck and waveguide | `Phys` |
| Verb.h | Freeverb-style reverb (standalone) | `Verb` |
| Arp.h | MIDI arpeggiator | `Arp` |
| Seq.h | Step sequencer | `Seq` |
| Sync.h | GPIO sync clock (Korg/TE Pocket Operator standard) | `Sync` |
| MIDI16.h | MIDI send/receive over hardware serial | `MIDI16` |
| Mic.h | I2S audio input (MEMS microphone) | - |
| TLV.h | TLV320AIC3104 I2C codec driver | `TLV` |

---

## Default Internal DAC Pin Configurations

| Platform | Left Channel | Right Channel | Notes |
|----------|-------------|---------------|-------|
| ESP32 | GPIO25 | GPIO26 | Hardware-fixed, no configuration needed |
| ESP32-S2 | GPIO17 | GPIO18 | Hardware-fixed, no configuration needed |

Enable with `useInternalDAC()` before `audioStart()`. Not available on ESP32-S3, C3, or other chips without internal DAC.

---

## Version Notes

- Primary development: 2021-2025
- ESP32 Arduino Core: Supports both V2 (commented) and V3+ I2S APIs
- RP2040 support: Added with dual-core cooperative scheduling
- Pico 2 (RP2035/RP2350) detection folded into `IS_RP2040()` macro
- Internal DAC support: ESP32 and ESP32-S2 via `dac_continuous` driver (ESP-IDF V5+)
- SVF.h: Added non-blocking try-lock for dual-core thread safety on filter state
- FX.h: Legacy Del reverb path upgraded with soft limiting, input HPF, dampening; Del built-in filtering disabled for reverb delays
- M16.h: Removed per-sample `yield()` from audio callback (4-9% CPU savings); partial-write retry on DAC output
- Osc.h (Apr 2025): Fixed memory ordering bug — `phase_increment_fractional` loads in `next()`, `next2()`, `phMod()`, `phModInt()` changed from `__ATOMIC_RELAXED` to `__ATOMIC_ACQUIRE` to correctly pair with `setFreq()`'s `__ATOMIC_RELEASE` stores; `modDepthScale` marked `volatile`; added `phMod(Osc&, float)` and `phModInt(Osc&, int32_t)` overloads with per-carrier `_pairLock` spinlock for dual-core-safe paired modulator+carrier advance
- Osc.h (May 2026): Replaced ad-hoc `modDepthScale` Nyquist-headroom taper with the Chowning-derived depth cap `depth_max = 9000 / (freq × cmRatio)` applied across all phMod variants (`phMod`, `phModInt`, `phMod2`, `phModMorph`, `phModWTrans` and their `Osc&` overloads); added `setCMRatio(float)` setter; the `Osc&` overloads auto-derive ratio from the modulator's frequency when `setCMRatio()` has not been called explicitly
 - M16.h (May 2026): Added sample reorder buffer for dual-core external I2S (`M16_REORDER_BUFFER_ENABLE`). MPSC ring + single drainer task pinned to core 0 restores deterministic sample order at the DAC. **Disabled by default as of 2026-05-30** — the drainer task starves IDLE0 (→ Task Watchdog) in lightweight audio loops and causes crackle in reverb functions. Enable with `#define M16_REORDER_BUFFER_ENABLE 1` before `#include "M16.h"`. The preferred dual-core fix for shared-state FX is `M16_ATOMIC_GUARD` + output cache. Implementation plan: `DOCS/REORDER_BUFFER_PLAN.md`.
- M16.h (May 2026): Replaced experimental per-sample split-core API with block-based dual-core voice partitioning. New API: `audioPartitionOffset()`, `audioPartitionStride()`, `audioIsFinalizerCore()`, `audioBlockWrite()`. Block size is configurable via `M16_BLOCK_SIZE` (default 32); design rationale: `DOCS/BLOCK_SPLIT_PLAN.md`.
- Library Hardening (May 2026): Migrated `SVF2.h` and `BBD.h` to blocking locks (`M16_ATOMIC_GUARD_BLOCKING`) to eliminate 1-sample hold artifacts (fuzz) on dual-core systems. Migrated `Env.h` to `std::atomic`. Documented "Dedicated Audio Core" pattern for serial synthesis chains to eliminate lock contention and state corruption. Resolved I2S/I2C pin conflicts for ESP32-S3 hardware.
- M16.h (May 2026): Inlined `Hardware_defines.h` platform detection macros directly into M16.h — the separate file is removed. Added `IS_ESP32S2()`, `IS_ESP32C3()`, and `IS_CAPABLE()` (`IS_ESP32() || IS_RP2040()`) macros. `panLeft()`/`panRight()` outputs now clamped to [0.0, 1.0].
- M16.h (May 2026): Reorder buffer pre-claim fix — `m16_claimReorderSeq()` claims the output sequence number *inside* the SAMP_LOCK spinlock rather than after release, atomically pairing sample computation order with reorder ring slot assignment. Eliminates sign-flip spikes at zero crossings caused by the other core stealing an earlier seq# between lock release and seq claim. `Samp::next()` and `Samp::nextStereo()` both call `m16_claimReorderSeq()` before releasing SAMP_LOCK on dual-core ESP32.
- Samp.h (May 2026): Added `loadFromFlash(Wav&, const uint8_t*, uint32_t)` for flash-stored IMA ADPCM samples (no SD card at runtime). Fixed `bufRate` logic: internal DAC requires `SAMPLE_RATE/2` (driver runs `audioUpdate()` at 2×rate); external I2S always uses `SAMPLE_RATE` (SAMP_LOCK serialises to 44100 calls/sec regardless of core count). Added `setBasePitch(float)` / `setPitch(float)` for pitch-relative playback speed. Added `setNearZeroSmooth(bool, int16_t threshold=1024)` — amplitude-gated IIR blend at zero crossings to reduce quantisation click artifacts on internal DAC playback.
- MIDI16.h (May 2026): Added optional FreeRTOS clock task (`beginClockTask()`) that owns all Serial2 reads on ESP32, routing bytes through internal SPSC lock-free ring buffers. Clock byte timestamps captured at read time for accurate `clockToBpm()` independent of `loop()` blocking. Outgoing clock send moved into the same task via `setClockSendBpm()`; adaptive sleep (sleep until 1ms before deadline, then busy-wait) reduces send jitter from ±1ms to ~±50µs. Added CC coalescing in `handleChannelRead()` — same-channel/controller CC messages are collapsed to the final value on each `read()` call, eliminating slider lag at 31250 baud. BPM tracking improvements: 8-sample rolling average (down from 16), 0.75 BPM hysteresis on integer output, stop/restart detection with history pre-fill for accurate first post-restart reading, and 3ms burst guard against drain-loop artefacts. Added `getBpm()` for side-effect-free BPM reads.
- FX.h (May 2026): Fixed dual-core crackle in `reverbStereo` and `reverbStereo2`. Root cause: `M16_ATOMIC_GUARD_BLOCKING` serialised both cores but allowed them to write DMA samples out of slot order → audible discontinuities. Fix: switched to `M16_ATOMIC_GUARD` (non-blocking try-lock) + `reverbCacheL`/`reverbCacheR` output cache — losing core returns cached previous output, which is audibly correct. `reverbStereo2` additionally moves `All` allpass processing inside the guard (All has no internal locking) and adds half-rate toggle + IIR smoothing for CPU efficiency. Confirmed fix: both functions run cleanly with dual-core enabled and `M16_REORDER_BUFFER_ENABLE 0`.
- Osc.h (Jun 2026): Added `disableAntiAlias()` — pins `_cachedDepthMax` to 9999 and sets `_antiAliasDisabled` flag so subsequent `setFreq()`/`setCMRatio()` calls do not reset the cap. Intended for feedback FM (self-modulation noise), intentional aliasing, and bit-crush effects where the Chowning depth cap is counterproductive. Per-instance; does not affect other oscillators. Added `_antiAliasDisabled` guard to both `setFreq()` and `setCMRatio()` depth-cap recalculation paths.
- BBD.h (Jun 2026): Extended parameter slewing to `delayLevel` and `feedbackLevel` in addition to the existing `scanRate` slew. All three setters (`setLevel`, `setFeedbackLevel`, `setScanRate`/`setTime`) now write only to target variables; `next()` slews live values toward targets at 4 units/sample (level/feedback, ~6ms full range) or 32 fp-units/sample (scan rate, ~42ms). Prevents buffer-seam clicks when any of these parameters are adjusted during playback. Also lowered `softSaturate` threshold from 24000 to 20000 to ensure 4:1 compression handles worst-case feedback inputs without hitting the hard-clip guard.
- FX.h (May 2026): Added `setChorusMix(float)` — top-level dry/wet blend for `chorusStereo()` (0.0 = dry, 1.0 = wet, default 0.5). Blend is applied as a final stage inside `chorusStereo()` after the internal depth/normalisation processing, matching the pattern of `reverbMix` in `reverbStereo`. All public chorus setters (`setChorusMix`, `setChorusDepth`, `setChorusWidth`, `setChorusRate`, `setChorusDelayTime`) now auto-call `initChorus()` on first use, so calling any setter in `setup()` before `audioStart()` pre-allocates chorus buffers and eliminates the startup click caused by lazy init inside the audio callback. `setChorusFeedback` is excluded from auto-init (it is called from within `initChorus()` and would recurse).
- M16.h + Env.h (Jun 2026): Fixed envelope "zipper"/stepped slopes by giving time-based generators a real audio-frame clock. Added global `audioFrameCount()` / `m16AdvanceAudioFrame()` (atomic on ESP32/RP2040, `volatile` on ESP8266), advanced once per output frame in `i2s_write_samples` (all platforms) and the `audioBlockWrite` finaliser (Core-1 block-split branch skipped to avoid double-count). Rewrote `Env.h` to evaluate the envelope as a function of this clock instead of `micros()` (which freezes within a DMA-fill burst → ~11.6 ms staircase) or a per-`next()`-call counter (which only kept time at per-sample call rates, breaking the control-rate `next()`-in-`loop()` pattern). `Env::next()` and `getValue()` are now equivalent clock evaluators; the pre-release contour is a pure function of frames-since-start (concurrent dual-core evaluation safe), and only `startRelease()` writes an async snapshot under the new `releaseTriggered` atomic latch. Decay-repeats are clock-derived. Both the control-rate (`getValue()` per sample) and per-sample (`next()`/`getValue()` in `audioUpdate`) patterns are now smooth and correctly timed with no sketch changes. Behavior changes: `setValue()` is transient (overwritten by the evaluator), `getStartTime()` returns a frame index, durations stored in ms. Refactored the cache-the-value examples (`Envelope`, `FM_spread`, `PluckArpeggio`, `Sync`) to read `getValue()` per sample in `audioUpdate()`; examples that already read `getValue()`/`next()` per sample are smooth unchanged.
- M16.h (Jul 2026): Added `setAudioPostProcessCallback()` for master processing after dual-core partial mixes are combined. Reverb, master delay, chorus, compression, global filters, distortion, and final gain now have a defined full-mix stage instead of being applied to Core 0's partial mix. Updated `Polyphony` and `SequenceVoices` examples to use the callback.
- M16.h (Jul 2026): Converted dual-core `audioBlockWrite()` to a two-slot, sequence-tagged pipeline. Core 1 renders block N+1 while Core 0 combines, post-processes, and writes block N. Added bounded 100 ms Core-1 rendezvous and DMA-write timeouts so a failed producer or driver call cannot wedge audio permanently. Added `audioBlockSyncTimeoutCount()` and `audioDmaWriteTimeoutCount()` diagnostics. The pipeline adds approximately 512 bytes of static RAM at the default block size.
- Beat Machine integration (Jul 2026): Migrated from two complete shared-state callbacks to partitioned voice rendering, post-combine master effects, pipelined block output, and workload-aware voice ownership. Hardware testing eliminated permanent stalls and recurring silent underruns.
