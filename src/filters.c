#include "filters.h"
#include <math.h>

static void configureStage(SynthFilterStage *stage, double hz, double rate, double fallback)
{
    double cutoff = fmin(hz == 0 ? fallback : hz, .45 * rate);
    stage->targetG = tan(3.14159265358979323846 * cutoff / rate);
    stage->targetWet = hz == 0 ? 0 : 1;
}
void filtersConfigure(SynthFilters *filters, SynthFilterConfig config)
{
    if (!filters || !synthFilterConfigValid(&config)) return;
    configureStage(&filters->highpass, config.highpassHz, filters->sampleRate, 20);
    configureStage(&filters->lowpass, config.lowpassHz, filters->sampleRate, 20000);
}
int filtersInit(SynthFilters *filters, double rate, SynthFilterConfig config)
{
    if (!filters || !isfinite(rate) || rate < 1000 || rate > 384000 ||
        !synthFilterConfigValid(&config)) return -1;
    *filters = (SynthFilters){.sampleRate = rate, .smoothing = 1-exp(-1/(.02*rate))};
    filtersConfigure(filters, config);
    filters->lowpass.g = filters->lowpass.targetG;
    filters->lowpass.wet = filters->lowpass.targetWet;
    filters->highpass.g = filters->highpass.targetG;
    filters->highpass.wet = filters->highpass.targetWet;
    return 0;
}

static double processStage(SynthFilterStage *s, double input, double smoothing, bool highpass)
{
    s->g += smoothing * (s->targetG - s->g);
    s->wet += smoothing * (s->targetWet - s->wet);
    /* Topology-preserving state-variable filter, Q = 1/sqrt(2).
     * Integrator states stay meaningful while the cutoff moves. */
    const double k = 1.4142135623730950488;
    double a1 = 1 / (1 + s->g * (s->g + k));
    double a2 = s->g * a1, a3 = s->g * a2;
    double v3 = input - s->integrator2;
    double v1 = a1 * s->integrator1 + a2 * v3;
    double v2 = s->integrator2 + a2 * s->integrator1 + a3 * v3;
    s->integrator1 = 2 * v1 - s->integrator1;
    s->integrator2 = 2 * v2 - s->integrator2;
    /* Flush tiny states to avoid denormal arithmetic on long release tails. */
    if (fabs(s->integrator1) < 1e-20) s->integrator1 = 0;
    if (fabs(s->integrator2) < 1e-20) s->integrator2 = 0;
    double filtered = highpass ? input - k * v1 - v2 : v2;
    return input + s->wet * (filtered - input);
}
float filtersNext(SynthFilters *filters, float input)
{
    double sample = processStage(&filters->highpass, input, filters->smoothing, true);
    return (float)processStage(&filters->lowpass, sample, filters->smoothing, false);
}
