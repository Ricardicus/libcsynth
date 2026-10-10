# libcsynth

A small C11 synth that renders mono float audio into your buffers. There is no
audio device backend and no SDL dependency. Feed the samples to your own audio
system, or render them straight to a file.

The engine supports chords across 128 note pitches, up to eight independent
layers, and an FM chain of up to eight operators in each layer. Waveforms are
sine, square, triangle, saw, pulse, and noise. Each note has a master ADSR;
modulators have their own timbre envelopes. Low-pass and high-pass tone filters run on the mix, followed by echo and reverb.
The 72 factory sounds include flutes, keys, bells, basses, leads, pads, and
some stranger textures.

You can also use a WAV/MP3 sample bank as the source: one recording can cover
the keyboard, or several recordings can cover different pitches. The same
ADSR, layer gain/detune, filters, and effects apply.

Settings can change while notes are held. Each engine has its own state, so
several engines can coexist. The host owns timing and thread synchronization.

## Build

From this folder (or from the repository root once this is a separate repo):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

You need CMake 3.16+ and a C11 compiler. The Unix build links the system math
library. WAV/MP3 decoders are vendored in `third_party/`; there is nothing extra
to install or download. Tests are enabled by default;
use `-DBUILD_TESTING=OFF` for just the library, or `-DCSYNTH_BUILD_TESTS=OFF` to
disable only these tests when embedding it in a larger build.

## Use it

```c
#include "synth.h"

SynthConfig sound = synthPresetConfig(12); /* Flute. */
Synth *engine = synthCreate(48000, &sound);
if (!engine) return;

float samples[256];
synthNoteOn(engine, 60);
synthRender(engine, samples, 256);
/* Hand samples to your audio system. Keep rendering while the note plays. */

sound.effects.reverbMix = 0.25;
synthConfigure(engine, &sound); /* Changes the held note too. */
synthNoteOff(engine, 60);
/* Keep rendering for the release/reverb tail before destroying the engine. */
synthDestroy(engine);
```

Read the [API doc](docs/libcsynth-api.md) for the full interface, settings ranges,
live editing behavior, and threading rules. The [sound guide](presets/README.md)
describes the factory patches.

For an offline example that renders a chord, edits its sound, and releases it:

```sh
cmake -S . -B build -DCSYNTH_BUILD_EXAMPLES=ON
cmake --build build --target csynth_live
./build/csynth_live my-chord.wav
```

## Make an instrument from recordings

Say you have a flute recording at 440 Hz. To play 880 Hz, the engine reads that
recording twice as fast. To play 220 Hz, it reads at half speed. You supply the
recording’s **original/base frequency**, not the note you want to play. The
normal note API still chooses the target pitch.

With several recordings, each note/layer picks the closest base frequency in
semitones. That keeps transposition smaller and usually sounds more natural.
Layers can pick different recordings when their detune settings differ. This
is a source mode for the whole engine: the bank replaces the FM generators,
while layer count, gain, detune, MIDI velocity, master ADSR, filters, echo, and
reverb keep working. FM ratios, waveforms, vibrato, and index envelopes do not
modify recorded samples. New engines still start in FM mode.

### The coordination file

Use a plain-text `.csamples` file next to your recordings:

```text
csynth-samples 1
#              base Hz   gain   loop start   loop end    file
sample         261.63    1.00   0            0           "Piano C4.wav"
sample         440.00    0.85   12000        60000       "Flute A4.wav"
sample         880.00    1.00   0            0           "Piano A5.mp3"
```

Every sample row has those five fields in that order. File paths are quoted;
relative paths are resolved against the map’s folder, not your process’s
working directory. Absolute paths work too. Use forward slashes for portable
paths. Inside quotes, `\\` represents a literal backslash and `\"` a quote.
Blank lines and `#` comments are allowed; a comment can follow a row. Numbers
use Hz and ordinary decimal values; loop points are integer **source frames**.
The header must be the first non-comment line. Lines are limited to 4,094
characters and unescaped filenames to 2,047 characters.

- **Base Hz:** the recording’s known fundamental, from 1 Hz up to half its
  sample rate. You can use fractional pitches such as 261.625565. Nothing
  detects or corrects the pitch for you.
- **Gain:** 0–4, applied to that recording. Start at 1 and balance recordings
  whose levels differ. The engine’s normal layer mixing/headroom still applies.
- **Loop start/end:** both zero means a one-shot. Otherwise, start is inclusive,
  end is exclusive, and `0 <= start < end <= decoded frame count`. The recording
  plays its intro once, then repeats that region through sustain **and release**.
  ADSR ultimately silences it. For example, 12,000–60,000 at 48 kHz loops the
  region from 0.25 to 1.25 seconds. These are frames, not stereo scalar samples.
- **File:** WAV or MP3, case-insensitive extension. Recordings can have different
  sample rates. They decode to mono floats; multichannel WAV and stereo MP3
  channels are averaged. No normalization is applied.

A one-shot ends when its recording runs out, even if the note is still held:
ADSR multiplies the recording’s own amplitude; it does not remove a recorded
attack or decay. It cannot make a short recording last longer. Use a loop for a sustained
instrument. Choose loop boundaries at matching waveform positions, preferably
zero crossings; this first version does not crossfade loop seams. MP3’s encoder
delay/lossy edges can make precise loops harder, so WAV is a better loop source.
Loop positions refer to the decoder’s output, which you can inspect with
`synthSampleBankGetInfo`.

All entries compete by pitch. There are no velocity zones, round-robin groups,
key-range overrides, or sample crossfades. An exact distance tie chooses the
first map row; repeated base frequencies are accepted, but the first is chosen.

### Load, apply, play

```c
#include "synth.h"
#include <stdio.h>

char error[512];
SynthSampleBank *bank = synthSampleBankLoad("samples/flute.csamples", error, sizeof(error));
if (!bank) {
    fprintf(stderr, "%s\n", error);
    return;
}

SynthConfig sound = synthPresetConfig(1); /* One layer; other settings are yours. */
sound.outputEnvelope = (SynthEnvelopeConfig){20, 100, 80, 300};
Synth *engine = synthCreate(48000, &sound);
if (!engine || synthApplySampleBank(engine, bank) != 0) {
    synthDestroy(engine);
    synthSampleBankDestroy(bank);
    return;
}
synthSampleBankDestroy(bank); /* Engine holds its own reference now. */

float output[256];
synthMidiNoteOn(engine, 69, 100); /* A4, with velocity. */
synthRender(engine, output, 256); /* Keep calling this in your audio callback. */
synthMidiNoteOff(engine, 69);
/* Continue rendering for ADSR release and effect tails. */
synthDestroy(engine);
```

Applying a nonempty bank enters sample mode. To keep the bank but switch the
source, call `synthSetSourceMode(engine, SYNTH_SOURCE_FM)` or
`synthSetSourceMode(engine, SYNTH_SOURCE_SAMPLES)`. To detach/free the engine’s
reference and return to FM, call `synthApplySampleBank(engine, NULL)`.
`synthGetSourceMode` tells you the current mode.

Master ADSR edits use the existing `synthConfigure` call. It also applies live
layer gain/detune and effect edits. When a held note’s detune crosses into a
different recording’s pitch region, that recording restarts. Switching mode
or replacing a bank restarts sample cursors while retaining held-note ownership
and current ADSR. For a clean instrument change, release the notes first; bank
changes do not crossfade. Note-ons after a release restart the recording;
repeating a note-on while it is already held follows the existing no-retrigger
rule. As in FM mode, all 128 note pitches can play independently.

### Supplying your own decoded buffers

If your host already decodes audio, build a bank directly:

```c
SynthSampleBank *bank = synthSampleBankCreate();
SynthSampleData recording = {
    .samples = myMonoBuffer,
    .frameCount = myFrameCount,
    .sampleRate = 44100,
    .baseFrequencyHz = 440.0,
    .gain = 1.0,
    .loopStart = 0,
    .loopEnd = myFrameCount
};
int result = synthSampleBankAddPcm(bank, &recording, error, sizeof(error));
/* If result == 0, the bank has its own copy; myMonoBuffer can be discarded. */
```

Add as many entries as needed, then apply the bank. `synthSampleBankAddFile`
adds a WAV/MP3 directly without a map. `synthSampleBankCount`,
`synthSampleBankGetInfo`, and `synthSampleBankFind` help a host list recordings
or inspect which one a target frequency selects. The public header is
[`samples.h`](include/samples.h), also included by `synth.h`.

### Ownership, timing, and sound quality

A bank is mutable only until its first successful application. After that it
is immutable, and it can be shared by several engines at different output
rates. Each engine has its own playback cursors. Destroying the caller’s bank
reference does not invalidate attached engines; the PCM is freed after the
last reference is released. Build a new bank to edit an applied instrument.
Adding an invalid entry leaves the bank alone. A failed load returns `NULL`
with a path/line error and does not affect an engine; a failed application
leaves its previous bank/mode intact.

Load/build banks on a worker or setup thread. Applying, replacing, or detaching
a bank requires exclusive access to the engine and can free memory. Stop/pause
processing or use your host’s synchronization before doing that. It is not an
audio-callback operation. Do not mutate a bank concurrently with applying or
reading it. Once a bank is frozen, engines can read it concurrently. The usual
single-owner rule still applies to each engine. Source-mode toggles and normal
configuration changes allocate/free nothing, but also need the engine’s owner.

`synthRender` never decodes files, allocates memory, or locks. It advances a
fractional cursor for each note/layer and linearly interpolates adjacent PCM
frames. The source-frame increment for each output frame is:

```text
step = (recording sample rate / output sample rate)
     * (note frequency * 2^(layer detune cents / 1200) / recording base frequency)
```

This is ordinary sample-rate transposition: pitch and duration change together,
and formants change too. It is not time stretching or pitch-preserving/formant
processing. Linear interpolation keeps the cost small, but large upward
transpositions can alias. Use recordings across the instrument’s range and
avoid extreme detunes when quality matters.

A bank supports 128 recordings and up to 256 MiB of decoded mono PCM in total.
Recording sample rates must be 1,000–384,000 Hz. PCM must be finite and within
-16–16 (normal audio is around -1–1); out-of-range data is rejected instead of
poisoning the DSP state. Memory is roughly `total frames * sizeof(float)`, plus
a small bank description; sharing a bank does not duplicate its PCM. The
original files are no longer needed during rendering.

The `.synth` format and `SynthConfig` still describe ADSR, layers, and effects;
they do not embed a bank or remember the source mode. Save the map path and
mode separately in your host, and load/apply that bank when restoring a sampled
instrument. A map describes the recordings; a `.synth` file describes their
synth settings.

### Try it without your own recordings

The demo generator uses only Python’s standard library and makes three original
warm tones plus a looping map:

```sh
cmake -S . -B build -DCSYNTH_BUILD_EXAMPLES=ON
cmake --build build
python3 examples/make_sample_demo.py /tmp/csynth-sample-demo
./build/csynth_samples /tmp/csynth-sample-demo/warm.csamples /tmp/sampled-chord.wav
```

[`sample_player.c`](examples/sample_player.c) renders a sampled chord, changes
ADSR/detune while it plays, and releases the notes. WAV/MP3 decoding uses the
vendored [dr_libs](https://github.com/mackron/dr_libs); its version and licence
are in [`third_party/README.md`](third_party/README.md).

## Link from another CMake project

With a source checkout:

```cmake
set(CSYNTH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/libcsynth libcsynth-build)
add_executable(my_player player.c)
target_link_libraries(my_player PRIVATE csynth::csynth)
```

Or install and use its CMake package:

```sh
cmake --install build --prefix /your/install/folder
```

```cmake
find_package(csynth CONFIG REQUIRED)
add_executable(my_player player.c)
target_link_libraries(my_player PRIVATE csynth::csynth)
```

Pass `-DCMAKE_PREFIX_PATH=/your/install/folder` when configuring your project.
The package carries the include paths and math dependency for you.

## What's in here

- `include/`: public C headers.
- `src/`: engine, DSP, presets, sample playback, and file/map loading.
- `presets/factory/`: all 72 shipped sound files.
- `docs/`: API documentation.
- `examples/`: FM/sample WAV renderers and a sample-bank demo generator.
- `tests/`: engine, FM, envelopes, effects, presets, and sample-bank tests.
- `third_party/`: vendored WAV/MP3 decoders and their licence.
- `cmake/`: installed-package configuration.

Echo capacity defaults to 2,000 ms. Builds that need longer echoes can define
`CSYNTH_ECHO_MAX_DELAY_MS` (1–60,000) consistently for the library and its clients.
The echo buffer is allocated at engine creation, never during live edits. Its
memory use is approximately `sampleRate * maxDelayMs / 1000 * sizeof(float)`
bytes per engine. Changing this option changes the accepted configuration range.

## FM routing and feedback

The original serial chain still works exactly as before: `algorithm =
FM_ALGORITHM_CHAIN` is zero, old presets load in that mode, and feedback starts
at zero. There are now six algorithms, selected per layer:

| Algorithm | Connections and audible operators |
| --- | --- |
| `FM_ALGORITHM_CHAIN` | OP1 → OP2 → … → OPn; only OPn is audible, at unity level. |
| `FM_ALGORITHM_PAIRS` | OP1 → OP2, OP3 → OP4, etc.; each pair's carrier is audible. An unmatched last operator is also audible. |
| `FM_ALGORITHM_FAN_IN` | Every earlier operator modulates OPn; OPn is audible. |
| `FM_ALGORITHM_FAN_OUT` | OP1 modulates every later operator; those operators are audible. With one operator it is audible itself. |
| `FM_ALGORITHM_ADDITIVE` | No inter-operator modulation; every operator is an audible carrier. |
| `FM_ALGORITHM_CUSTOM` | The routing matrix chooses connections; any operator can also contribute to the output. |

For graph modes, `operators[i].outputLevel` ranges from 0 to 1. The output is
`sum(level[i] * wave[i]) / max(1, sum(level[i]))` over that algorithm's audible
carriers. Muting a carrier is allowed, including muting all of them. Legacy
chain mode ignores output levels to keep old sounds identical.

`routing[source][destination]` ranges from 0 to 1 and is used in Custom mode.
Only earlier operators may feed later ones: `source < destination`. All
backward/diagonal entries must be zero, even for unused slots. This fixed
ordering avoids cycles and makes rendering deterministic without allocations
or graph traversal on the audio thread. A modulator can feed several carriers,
and several modulators can feed the same carrier. An operator may be both
audible and a modulator. Reorder operators to express a different DAG.

```c
SynthConfig patch = synthDefaultConfig();
FmConfig *fm = &patch.layers[0].fm;
fm->operatorCount = 4;
fm->algorithm = FM_ALGORITHM_CUSTOM;
fm->routing[0][2] = 1.0; // OP1 and OP2 modulate OP3.
fm->routing[1][2] = 0.5;
fm->routing[0][3] = 0.25; // OP1 also modulates OP4.
fm->operators[0].outputLevel = 0;
fm->operators[1].outputLevel = 0;
fm->operators[2].outputLevel = 1;
fm->operators[3].outputLevel = 0.5;
fm->operators[0].feedback = 0.3;
synthConfigure(engine, &patch); // While your render thread is excluded.
```

This engine uses **frequency modulation**, including feedback. For operator
`i`, with nominal frequency `f_i`, index envelope `e_i[n]`, FM depth `rm_i`,
feedback `b_i`, and connection `r[j][i]`:

```text
F_i[n] = f_i + sum(j < i, r[j][i] * f_j * rm_j * e_j[n] * y_j[n])
              + f_i * b_i * e_i[n] * y_i[n-1]
y_i[n] = waveform_i(phase_i[n])
phase_i[n+1] = phase_i[n] + 2*pi*F_i[n]/sampleRate
```

Vibrato scales the instantaneous frequency before integration. Self-feedback
uses the previous output, so there is always one sample of delay. It is not a
phase offset. Feedback ranges from 0 to 8 (deviation / nominal Hz); zero turns
it off. The source's Sustain/Decay/ADSR index envelope shapes outgoing FM and
feedback, but does not fade an audible carrier's level. Master ADSR still
shapes note volume. A fresh note resets feedback history; tail retriggers and
live configurations retain oscillator phases and existing feedback history.

High feedback/depth can alias. This version does not add oversampling,
band-limited waveforms, arbitrary cyclic routing, or per-carrier amplitude
envelopes. Start with modest feedback and adjust by ear. Route/algorithm edits
can change the waveform abruptly, like existing FM-depth changes.

The eight **Graph** presets (indices 64–71) demonstrate these features; the
original 64 preset indices keep their names and positions. Of those sounds,
28 now use graph routing for finer attack and sustain detail; see the
[factory sound guide](presets/README.md#routing-refresh). New `.synth` exports
use **version 3**: each layer has an `algorithm` row, eight `route` rows, and
each `op` row appends output level and feedback. Readers still accept v1/v2
as legacy chains with output level 1 and feedback 0. Older library builds do
not read v3, so rebuild clients when updating these public config structs.

### Refreshed factory sounds

28 established patches now use the graph routing and feedback features for
independent attack harmonics, shared pad modulation, and a little extra texture.
The bank still has 72 sounds with the same names and program numbers. See the
[factory sound guide](presets/README.md#routing-refresh) for what changed.
Factory `.synth` files and compiled presets carry the same settings, so SDL
and the Logic plugin load the same sounds from this shared library.
