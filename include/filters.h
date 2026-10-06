#ifndef FILTERS_H
#define FILTERS_H
#include "synth_config.h"

typedef struct {
    double g, targetG, wet, targetWet;
    double integrator1, integrator2;
} SynthFilterStage;
typedef struct {
    double sampleRate, smoothing;
    SynthFilterStage lowpass, highpass;
} SynthFilters;

/* Two 12 dB/oct Butterworth tone filters. HP -> LP, zero cutoff bypasses.
 * Live cutoff/bypass changes smooth over 20 ms and retain filter state.
 * Cutoffs clamp internally to 45% of the sample rate. No allocation/locking. */
int filtersInit(SynthFilters *filters, double sampleRate, SynthFilterConfig config);
void filtersConfigure(SynthFilters *filters, SynthFilterConfig config);
float filtersNext(SynthFilters *filters, float input);
#endif
