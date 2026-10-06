#ifndef SAMPLES_PRIVATE_H
#define SAMPLES_PRIVATE_H
#include "samples.h"
#include <stdbool.h>
#include <stdatomic.h>

typedef struct {
    float *pcm;
    SynthSampleInfo info;
    double logFrequency;
} SampleEntry;
struct SynthSampleBank {
    atomic_uint references;
    atomic_bool sealed;
    size_t count, bytes;
    SampleEntry entries[SYNTH_SAMPLE_MAX_ENTRIES];
};
int sampleError(char *error, size_t size, const char *format, ...);
int sampleBankAppendOwned(SynthSampleBank *bank, float *pcm, SynthSampleInfo info,
                          char *error, size_t errorSize);
void sampleBankRetainAndSeal(SynthSampleBank *bank);
float sampleBankNext(const SynthSampleBank *bank, size_t index, double *position, double step);
#endif
