#ifndef SYNTH_CONFIG_H
#define SYNTH_CONFIG_H

#include "fm.h"

#define SYNTH_MAX_LAYERS 8

/* Override for builds needing longer delays; use the same value for all clients. */
#ifndef CSYNTH_ECHO_MAX_DELAY_MS
#define CSYNTH_ECHO_MAX_DELAY_MS 2000
#endif
#if CSYNTH_ECHO_MAX_DELAY_MS < 1 || CSYNTH_ECHO_MAX_DELAY_MS > 60000
#error "CSYNTH_ECHO_MAX_DELAY_MS must be between 1 and 60000"
#endif

typedef struct {
    FmConfig fm;
    double gain;        /* 0..1, mixed with normalization by layer count. */
    double detuneCents; /* -4800..4800; shifts the whole layer's FM chain. */
} SynthLayerConfig;

typedef struct {
    int attackMs;
    int decayMs;
    int sustainPercent;
    int releaseMs;
} SynthEnvelopeConfig;

typedef struct {
    double echoMix;       /* 0..1 */
    double echoDelayMs;   /* 1..CSYNTH_ECHO_MAX_DELAY_MS */
    double echoFeedback;  /* 0..0.95 */
    double reverbMix;     /* 0..1 */
    double reverbRoom;    /* 0..0.95, feedback amount */
    double reverbDamping; /* 0..1, high-frequency absorption */
} SynthEffectsConfig;

typedef struct {
    double lowpassHz;  /* 0 bypasses; otherwise 20..20000 Hz. */
    double highpassHz; /* 0 bypasses; otherwise 20..20000 Hz. */
} SynthFilterConfig;

typedef struct {
    SynthEffectsConfig effects;
    SynthEnvelopeConfig outputEnvelope; /* Shared amplitude ADSR for every note. */
    int layerCount;
    SynthLayerConfig layers[SYNTH_MAX_LAYERS];
    SynthFilterConfig filters; /* Shared tone filters before echo/reverb. */
} SynthConfig;

SynthConfig synthDefaultConfig(void);
#define SYNTH_PRESET_COUNT 72
const char *synthPresetName(int index);
SynthConfig synthPresetConfig(int index);
bool synthEffectsConfigValid(const SynthEffectsConfig *config);
bool synthFilterConfigValid(const SynthFilterConfig *config);
bool synthConfigValid(const SynthConfig *config);

#endif
