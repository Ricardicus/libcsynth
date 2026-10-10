#ifndef FM_H
#define FM_H

#include "oscillator.h"
#include <stdbool.h>

#define FM_MAX_OPERATORS 8

typedef enum {
    FM_INDEX_SUSTAIN,
    FM_INDEX_DECAY,
    FM_INDEX_ADSR
} FmIndexMode;

typedef enum {
    FM_ALGORITHM_CHAIN = 0, FM_ALGORITHM_PAIRS, FM_ALGORITHM_FAN_IN,
    FM_ALGORITHM_FAN_OUT, FM_ALGORITHM_ADDITIVE, FM_ALGORITHM_CUSTOM,
    FM_ALGORITHM_COUNT
} FmAlgorithm;
const char *fmAlgorithmName(FmAlgorithm algorithm);

typedef struct {
    Waveform waveform;
    double pulseWidth;      /* Pulse duty cycle, strictly between 0 and 1. */
    double vibratoRateHz;
    double vibratoDepthCents;
    double rm;             /* Outgoing FM depth: deviation / nominal source Hz. */
    double ratio;          /* Operator's nominal Hz = base Hz * ratio. */
    FmIndexMode indexMode;
    double decayRate;      /* Exponential index decay rate, per second. */
    int attackMs;
    int decayMs;
    int sustainPercent;
    int releaseMs;
    double outputLevel; /* 0..1: audible carrier level in graph modes. */
    double feedback;    /* 0..8: previous output times nominal Hz, frequency feedback. */
} FmOperatorConfig;

typedef struct {
    int operatorCount; /* Active operators; routing is selected by algorithm. */
    /* In legacy chain mode, only the last OP is audible; its index is unused
     * unless feedback is enabled. Graph modes support multiple carriers. */
    FmOperatorConfig operators[FM_MAX_OPERATORS];
    FmAlgorithm algorithm; /* Zero retains the original serial chain. */
    double routing[FM_MAX_OPERATORS][FM_MAX_OPERATORS]; /* [source][destination], 0..1.
        Custom mode only: source must be earlier than destination; feedback is separate. */
} FmConfig;

typedef enum {
    FM_ENV_IDLE,
    FM_ENV_ATTACK,
    FM_ENV_DECAY,
    FM_ENV_SUSTAIN,
    FM_ENV_RELEASE
} FmEnvelopeStage;

/* One instance per voice. All instance operations belong to its rendering
 * thread, or must run while that thread is locked. No SDL dependency. */
typedef struct {
    Oscillator oscillator;
    FmOperatorConfig config;
    FmEnvelopeStage stage;
    double indexEnvelope;
    double stageStart;
    double stagePosition;
    double decayMultiplier;
} FmOperator;

typedef struct {
    int operatorCount;
    FmOperator operators[FM_MAX_OPERATORS];
    bool held;
    FmAlgorithm algorithm;
    double routing[FM_MAX_OPERATORS][FM_MAX_OPERATORS];
    double outputLevels[FM_MAX_OPERATORS], previousOutputs[FM_MAX_OPERATORS];
    bool modulators[FM_MAX_OPERATORS];
    double outputNormalization;
} FmSynth;

FmConfig fmDefaultConfig(void);
bool fmConfigValid(const FmConfig *config);
/* Returns 0 on success, -1 for invalid configuration or sample rate. */
int fmInit(FmSynth *synth, double sampleRate, const FmConfig *config);
/* Reset phases for a fresh voice; retain them when retriggering a tail. */
void fmNoteOn(FmSynth *synth, bool resetPhases);
void fmNoteOff(FmSynth *synth);
/* Longest modulator ADSR release; caller keeps the amplitude tail audible. */
int fmReleaseDurationMs(const FmSynth *synth);
/* The sound-generation entry point: base Hz in, one sample in [-1,1] out.
 * Call once per sample after fmNoteOn. Index envelopes change timbre only;
 * the caller applies the output amplitude envelope and mixes voices. */
float fmNextSample(FmSynth *synth, double baseFrequencyHz);

#endif
