#ifndef SYNTH_H
#define SYNTH_H

#include "synth_config.h"
#include "samples.h"
#include <stddef.h>
#include <stdint.h>

#define SYNTH_ANALYSIS_SAMPLES 2048

typedef struct Synth Synth;
typedef enum { SYNTH_SOURCE_FM = 0, SYNTH_SOURCE_SAMPLES = 1 } SynthSourceMode;

/* Apply a nonempty bank and enter sample mode; NULL detaches it and enters FM.
 * Engine retains the bank, which becomes immutable, so the caller may destroy
 * its reference immediately afterward. Replacing/detaching may free old data:
 * apply outside the audio callback with exclusive access to the engine.
 * Active voices restart sample playback but keep their ADSR/ownership state.
 * Failure leaves the previous bank and mode intact. */
int synthApplySampleBank(Synth *engine, SynthSampleBank *bank);
/* FM is the default. Switching back to samples requires an attached bank.
 * Does not allocate/free. Active voices keep ADSR, restarting sample cursors. */
int synthSetSourceMode(Synth *engine, SynthSourceMode mode);
SynthSourceMode synthGetSourceMode(const Synth *engine); /* NULL returns FM. */

/* SDL-free engine. Rate: 1000..384000 Hz. NULL config selects defaults.
 * Returns NULL for invalid settings or allocation failure. */
Synth *synthCreate(int sampleRate, const SynthConfig *config);
void synthDestroy(Synth *engine); /* NULL is harmless. */

/* Caller supplies count mono float samples. No allocations or device I/O.
 * NULL engine produces silence; NULL output is ignored.
 * Each instance must have a single owner, or external synchronization. */
void synthRender(Synth *engine, float *samples, size_t count);

/* Apply copied settings to active and future notes, preserving phases.
 * Return 0 on success, -1 for invalid pointers/settings. Failed changes
 * leave the previous patch intact. These calls do not allocate memory. */
int synthConfigure(Synth *engine, const SynthConfig *config);
int synthGetConfig(const Synth *engine, SynthConfig *config);

/* Note numbers 0..127, middle C=60. Invalid notes/NULL engines are ignored.
 * Repeated note-ons do not retrigger a held note; holds aren't counted.
 * Manual and MIDI ownership are independent. MIDI velocity clamps to 0..127;
 * zero is a silent hold, so use synthMidiNoteOff for a note-off. */
void synthNoteOn(Synth *engine, int note);
void synthNoteOff(Synth *engine, int note);
void synthMidiNoteOn(Synth *engine, int note, int velocity);
void synthMidiNoteOff(Synth *engine, int note);

/* Latest rendered output, oldest first. Caller supplies 2048 floats and a
 * valid position pointer. Returns sample rate, or 0 for invalid pointers.
 * Position counts samples rendered since creation. Not a recording queue. */
int synthAudioSnapshot(const Synth *engine, float *samples, uint64_t *samplePosition);

#endif
