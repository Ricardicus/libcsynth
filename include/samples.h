#ifndef SYNTH_SAMPLES_H
#define SYNTH_SAMPLES_H

#include <stddef.h>

#define SYNTH_SAMPLE_MAX_ENTRIES 128
#define SYNTH_SAMPLE_MAX_BYTES ((size_t)256 * 1024 * 1024)

typedef struct SynthSampleBank SynthSampleBank;

typedef struct {
    const float *samples; /* Finite mono PCM (-16..16). AddPcm copies this buffer. */
    size_t frameCount;
    int sampleRate;       /* Recording's sample rate: 1000..384000. */
    double baseFrequencyHz; /* Known fundamental: 1 Hz..recording Nyquist. */
    double gain;          /* 0..4; 1 leaves the recording's level alone. */
    size_t loopStart, loopEnd; /* Source frames, end exclusive; 0,0 = one-shot. */
} SynthSampleData;

typedef struct {
    size_t frameCount;
    int sampleRate;
    double baseFrequencyHz, gain;
    size_t loopStart, loopEnd;
} SynthSampleInfo;

/* Build/load outside audio processing. NULL error or zero size is fine.
 * Up to 128 entries / 256 MiB of decoded mono PCM per bank.
 * Adding fails without modifying the bank. Applied banks become immutable. */
SynthSampleBank *synthSampleBankCreate(void);
void synthSampleBankDestroy(SynthSampleBank *bank); /* Releases caller's reference. */
int synthSampleBankAddPcm(SynthSampleBank *bank, const SynthSampleData *sample,
                          char *error, size_t errorSize);
int synthSampleBankAddFile(SynthSampleBank *bank, const char *path,
                           double baseFrequencyHz, double gain,
                           size_t loopStart, size_t loopEnd,
                           char *error, size_t errorSize);
SynthSampleBank *synthSampleBankLoad(const char *mapPath, char *error, size_t errorSize);
size_t synthSampleBankCount(const SynthSampleBank *bank);
int synthSampleBankGetInfo(const SynthSampleBank *bank, size_t index, SynthSampleInfo *info);
/* Nearest base pitch in semitones; a tie selects the earlier map entry. */
int synthSampleBankFind(const SynthSampleBank *bank, double frequencyHz, size_t *index);

#endif
