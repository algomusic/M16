Use audioBlockWrite() when an ESP32 program uses dual-core audio and audioUpdate() advances arrays of independent,
  stateful voices.

  Typical examples include:

  - Multiple Osc objects
  - Per-voice SVF/SVF2 filters
  - Per-voice envelopes evaluated inside the audio callback
  - Per-voice delays, physical models, or sample players
  - Any polyphonic voice array where different voices can be assigned to different cores

  The standard structure is:

  void audioUpdate() {
    int32_t left = 0;
    int32_t right = 0;

    for (int voice = audioPartitionOffset();
         voice < voiceCount;
         voice += audioPartitionStride()) {
      // Process only the voices owned by this core.
      int32_t sample = oscillators[voice].next();
      left += sample;
      right += sample;
    }

    audioBlockWrite(left, right);
  }

  M16 assigns:

  - Core 0: voices 0, 2, 4…
  - Core 1: voices 1, 3, 5…

  It buffers each partial mix, combines matching blocks, and performs one ordered DMA write.

  ## Post-combine effects

  Any stateful processing that must receive the complete mix should use setAudioPostProcessCallback():

  void processMasterEffects(int32_t& left, int32_t& right) {
    int32_t wetLeft;
    int32_t wetRight;

    effects.reverbStereo(left, right, wetLeft, wetRight);

    left = wetLeft;
    right = wetRight;
  }

  void setup() {
    effects.initReverbSafe();
    setAudioPostProcessCallback(processMasterEffects);
    setIsDualCore(true);
    audioStart();
  }

  Suitable post-combine processing includes:

  - Reverb
  - Master delay
  - Compression or limiting
  - Master filters
  - Distortion or bit crushing
  - Master gain and final clipping
  - Any effect with shared history or feedback
  - Any nonlinear effect that should operate on the summed signal

  Do not run these only under audioIsFinalizerCore() inside audioUpdate(). At that point Core 0 has only its own voice partition; the two partitions have not yet been combined.

  Delay example:

  void processMasterEffects(int32_t& left, int32_t& right) {
    // Save the complete dry mix.
    int32_t dryLeft = left;
    int32_t dryRight = right;

    // Each stereo channel has an independent delay line.
    int32_t delayedLeft = delayLeft.next(dryLeft);
    int32_t delayedRight = delayRight.next(dryRight);

    // Add the wet output to the dry signal and prevent overflow.
    left = clip16(dryLeft + delayedLeft);
    right = clip16(dryRight + delayedRight);
  }

  ## Use i2s_write_samples() when

  Use the simpler writer when:

  - Audio is explicitly single-core with setIsDualCore(false).
  - The callback generates one simple signal without a partitionable voice array.
  - The callback is stateless or already independently safe on both cores.
  - A legacy sketch does not need the block-split architecture.
  - Minimum buffering latency matters more than throughput.

  For stateful single-signal synthesis, explicitly selecting single-core is generally safest:

  void setup() {
    setIsDualCore(false);
    audioStart();
  }

  void audioUpdate() {
    int32_t sample = oscillator.next();
    i2s_write_samples(sample, sample);
  }

  ## Use of reverb example

  #include "M16.h"
  #include "Osc.h"
  #include "FX.h"

  constexpr int VOICE_COUNT = 8;

  int16_t* wavetable;
  Osc voices[VOICE_COUNT];
  FX effects;

  // Runs once on Core 0 after M16 combines both cores' partial mixes.
  void processMasterEffects(int32_t& left, int32_t& right) {
    int32_t wetLeft;
    int32_t wetRight;

    effects.reverbStereoInterp(left, right, wetLeft, wetRight);

    left = wetLeft;
    right = wetRight;
  }

  void setup() {
    Osc::allocateWaveMemory(&wavetable);
    Osc::sawGen(wavetable);

    for (int i = 0; i < VOICE_COUNT; i++) {
      voices[i].setTable(wavetable);
      voices[i].setPitch(48 + i);
    }

    // Allocate and configure reverb before starting audio.
    effects.initReverbSafe();
    effects.setReverbLength(0.5);
    effects.setReverbMix(0.25);

    // M16 calls this after combining the two voice partitions.
    setAudioPostProcessCallback(processMasterEffects);

    setIsDualCore(true);
    audioStart();
  }

  void loop() {
  }

  // Both cores execute audioUpdate(), but each processes only its own voices.
  void audioUpdate() {
    int32_t left = 0;
    int32_t right = 0;

    for (int voice = audioPartitionOffset();
         voice < VOICE_COUNT;
         voice += audioPartitionStride()) {
      int32_t sample = voices[voice].next();

      left += sample >> 3;
      right += sample >> 3;
    }

    // Submit this core's partial mix. M16 combines both partial mixes,
    // calls processMasterEffects(), and then writes the completed block.
    audioBlockWrite(left, right);
  }

  ## Do not use audioBlockWrite() incorrectly

  Avoid these patterns:

  // Wrong: both cores process every oscillator.
  for (int i = 0; i < voiceCount; i++) {
    mix += oscillators[i].next();
  }
  audioBlockWrite(mix, mix);

  This advances every voice twice.

  // Wrong: reverb sees only each core's partial mix.
  reverb.next(mix);
  audioBlockWrite(mix, mix);

  Shared effects must run post-combine.

  // Wrong: only Core 0 applies reverb to its own partition.
  if (audioIsFinalizerCore()) {
    reverb.next(mix);
  }
  audioBlockWrite(mix, mix);

  audioIsFinalizerCore() identifies Core 0, but does not itself combine the partitions.

  ## Practical decision rule

  Use this decision tree:

  Does audioUpdate contain multiple independent voices?
  ├── No
  │   └── Prefer single-core + i2s_write_samples()
  └── Yes
      ├── Running single-core?
      │   └── Either writer works; direct writing has lower buffering latency
      └── Running dual-core?
          ├── Partition voices with audioPartitionOffset/Stride()
          ├── Submit partial mixes with audioBlockWrite()
          └── Put shared/master effects in setAudioPostProcessCallback()

  For substantial polyphonic ESP32 projects, the recommended default is partitioned audioBlockWrite() plus a post-combine callback.

  ## Pattern for interrelated voices

   Use this pattern when the synthesis graph cannot be divided into independent voices—for example, an FM cascade where one oscillator modulates the next—or when simplicity and deterministic state progression are more important than distributing synthesis across both cores.

   void setup() {
    setIsDualCore(false);

    // Initialize synthesis and effects...

    audioStart();
  }

  void loop() {
    // do something
  }

  void audioUpdate() {
    // Run the complete stateful signal chain once.
    int32_t left = generateAudio();
    int32_t right = left;

    processSharedDelay(left, right);
    processSharedReverb(left, right);

    // Batch output frames to reduce I2S driver overhead.
    audioBlockWrite(left, right);
  }
