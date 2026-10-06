#include "synth.h"
#include "envelope.h"
#include "effects.h"
#include "filters.h"
#include "samples_private.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NOTE_COUNT 128

typedef struct {
    FmSynth generator;
    double pitchMultiplier;
    double mixGain;
    size_t sampleIndex;
    double samplePosition, sampleStep;
} LayerVoice;

typedef struct {
    OutputEnvelope envelope;
    double frequency;
    double midiVelocity, velocityGain;
    bool held;
    bool active;
    LayerVoice layers[SYNTH_MAX_LAYERS];
} Voice;

struct Synth {
    Voice voices[NOTE_COUNT];
    unsigned char noteSources[NOTE_COUNT];
    int sampleRate;
    int layerCount;
    SynthConfig config;
    SynthSampleBank *sampleBank;
    SynthSourceMode sourceMode;
    SynthEffects effects;
    SynthFilters filters;
    float analysisSamples[SYNTH_ANALYSIS_SAMPLES];
    int analysisCursor;
    uint64_t samplePosition;
};

static void selectSample(Synth *engine, Voice *voice, LayerVoice *part, bool restart)
{
    if (!engine->sampleBank) return;
    size_t index=0;
    double frequency=voice->frequency*part->pitchMultiplier;
    synthSampleBankFind(engine->sampleBank,frequency,&index);
    if (restart || part->sampleIndex!=index) part->samplePosition=0;
    part->sampleIndex=index;
    const SynthSampleInfo *info=&engine->sampleBank->entries[index].info;
    part->sampleStep=info->sampleRate/(double)engine->sampleRate*frequency/info->baseFrequencyHz;
}
static void restartSamples(Synth *engine)
{
    for (int note=0;note<NOTE_COUNT;++note)
        for (int layer=0;layer<engine->layerCount;++layer)
            selectSample(engine,&engine->voices[note],&engine->voices[note].layers[layer],true);
}
int synthApplySampleBank(Synth *engine, SynthSampleBank *bank)
{
    if (!engine || (bank && !bank->count)) return -1;
    if (bank) sampleBankRetainAndSeal(bank);
    SynthSampleBank *previous=engine->sampleBank;
    engine->sampleBank=bank;
    engine->sourceMode=bank ? SYNTH_SOURCE_SAMPLES : SYNTH_SOURCE_FM;
    restartSamples(engine);
    synthSampleBankDestroy(previous);
    return 0;
}
int synthSetSourceMode(Synth *engine, SynthSourceMode mode)
{
    if (!engine || (mode!=SYNTH_SOURCE_FM && mode!=SYNTH_SOURCE_SAMPLES) ||
        (mode==SYNTH_SOURCE_SAMPLES && !engine->sampleBank)) return -1;
    if (engine->sourceMode!=mode) { engine->sourceMode=mode; restartSamples(engine); }
    return 0;
}
SynthSourceMode synthGetSourceMode(const Synth *engine)
{
    return engine ? engine->sourceMode : SYNTH_SOURCE_FM;
}

void synthRender(Synth *engine, float *samples, size_t count)
{
    if (!samples) return;
    if (!engine) { memset(samples, 0, count * sizeof(*samples)); return; }
    for (size_t i = 0; i < count; ++i) {
        double sample = 0.0;
        for (int note = 0; note < NOTE_COUNT; ++note) {
            Voice *voice = &engine->voices[note];
            if (!voice->active)
                continue;
            double velocity = (engine->noteSources[note] & 1) ? 1.0 :
                              (engine->noteSources[note] & 2) ? voice->midiVelocity : voice->velocityGain;
            double gainStep = 1.0 / (.005 * engine->sampleRate);
            voice->velocityGain += fmax(-gainStep, fmin(gainStep, velocity - voice->velocityGain));
            double amplitude = outputEnvelopeNext(&voice->envelope, engine->sampleRate) * voice->velocityGain;
            for (int layer = 0; layer < engine->layerCount; ++layer) {
                LayerVoice *part = &voice->layers[layer];
                double generated=engine->sourceMode==SYNTH_SOURCE_SAMPLES
                    ? sampleBankNext(engine->sampleBank,part->sampleIndex,&part->samplePosition,part->sampleStep)
                    : fmNextSample(&part->generator,voice->frequency*part->pitchMultiplier);
                sample += 0.1 * amplitude * part->mixGain * generated;
            }
            voice->active = voice->held || voice->envelope.stage != FM_ENV_IDLE;
        }
        sample = filtersNext(&engine->filters, (float)sample);
        sample = effectsNext(&engine->effects, (float)sample);
        samples[i] = (float)fmax(-1.0, fmin(1.0, sample));
        /* Capture the actual mixed output; analysis stays on the main thread. */
        engine->analysisSamples[engine->analysisCursor] = samples[i];
        engine->analysisCursor = (engine->analysisCursor + 1) % SYNTH_ANALYSIS_SAMPLES;
    }
    engine->samplePosition += (uint64_t)count;
}

Synth *synthCreate(int sampleRate, const SynthConfig *config)
{
    SynthConfig defaults;
    if (!config) { defaults = synthDefaultConfig(); config = &defaults; }
    if (sampleRate < 1000 || sampleRate > 384000 || !synthConfigValid(config)) return NULL;
    Synth *engine = calloc(1, sizeof(*engine));
    if (!engine) return NULL;
    engine->sampleRate = sampleRate;
    engine->layerCount = config->layerCount;
    engine->config = *config;
    if (filtersInit(&engine->filters, sampleRate, config->filters) != 0) {
        free(engine); return NULL;
    }
    if (effectsInit(&engine->effects, sampleRate, config->effects) != 0) {
        free(engine); return NULL;
    }
    for (int note = 0; note < NOTE_COUNT; ++note)
        engine->voices[note].frequency = 440.0 * pow(2.0, (note - 69) / 12.0);
    for (int note = 0; note < NOTE_COUNT; ++note) {
        outputEnvelopeConfigure(&engine->voices[note].envelope, config->outputEnvelope);
        for (int layer = 0; layer < engine->layerCount; ++layer) {
            LayerVoice *part = &engine->voices[note].layers[layer];
            const SynthLayerConfig *settings = &config->layers[layer];
            fmInit(&part->generator, engine->sampleRate, &settings->fm);
            for (int op = 0; op < part->generator.operatorCount; ++op)
                part->generator.operators[op].oscillator.noiseState =
                    (uint32_t)(note + 1) * 0x9e3779b9u ^ (uint32_t)(layer + 1) * 0x85ebca6bu ^ (uint32_t)(op + 1);
            part->pitchMultiplier = exp2(settings->detuneCents / 1200.0);
            part->mixGain = settings->gain / engine->layerCount;
        }
    }
    return engine;
}

int synthConfigure(Synth *engine, const SynthConfig *config)
{
    if (!engine || !synthConfigValid(config)) return -1;
    filtersConfigure(&engine->filters, config->filters);
    effectsConfigure(&engine->effects, config->effects);
    int previousLayers = engine->layerCount;
    engine->layerCount = config->layerCount;
    for (int note = 0; note < NOTE_COUNT; ++note) {
        Voice *voice = &engine->voices[note];
        outputEnvelopeConfigure(&voice->envelope, config->outputEnvelope);
        for (int layer = 0; layer < engine->layerCount; ++layer) {
            LayerVoice *part = &voice->layers[layer];
            FmSynth previous = part->generator;
            bool existing = layer < previousLayers;
            const SynthLayerConfig *settings = &config->layers[layer];
            fmInit(&part->generator, engine->sampleRate, &settings->fm);
            for (int op = 0; op < part->generator.operatorCount; ++op)
                part->generator.operators[op].oscillator.noiseState =
                    (uint32_t)(note + 1) * 0x9e3779b9u ^ (uint32_t)(layer + 1) * 0x85ebca6bu ^ (uint32_t)(op + 1);
            if (voice->active) {
                fmNoteOn(&part->generator, true);
                if (!voice->held)
                    fmNoteOff(&part->generator);
            }
            if (existing) {
                for (int i = 0; i < previous.operatorCount && i < part->generator.operatorCount; ++i) {
                    FmOperator *op = &part->generator.operators[i];
                    const FmOperator *old = &previous.operators[i];
                    op->oscillator.noiseState = old->oscillator.noiseState;
                    op->oscillator.phase = old->oscillator.phase;
                    op->oscillator.vibratoPhase = old->oscillator.vibratoPhase;
                    if (i < previous.operatorCount - 1 && i < part->generator.operatorCount - 1 &&
                        op->config.indexMode == old->config.indexMode) {
                        op->stage = old->stage;
                        op->stageStart = old->stageStart;
                        op->stagePosition = old->stagePosition;
                        op->indexEnvelope = op->stage == FM_ENV_SUSTAIN && op->config.indexMode == FM_INDEX_ADSR
                            ? op->config.sustainPercent / 100.0 : old->indexEnvelope;
                    }
                }
            }
            part->pitchMultiplier = exp2(settings->detuneCents / 1200.0);
            part->mixGain = settings->gain / engine->layerCount;
            if (!existing || settings->detuneCents!=engine->config.layers[layer].detuneCents)
                selectSample(engine,voice,part,!existing);
        }
    }
    engine->config = *config;
    return 0;
}

void synthDestroy(Synth *engine)
{
    if (!engine) return;
    effectsDestroy(&engine->effects);
    synthSampleBankDestroy(engine->sampleBank);
    free(engine);
}

int synthGetConfig(const Synth *engine, SynthConfig *config)
{
    if (!engine || !config) return -1;
    *config = engine->config;
    return 0;
}

int synthAudioSnapshot(const Synth *engine, float *samples, uint64_t *position)
{
    if (!engine || !samples || !position) return 0;
    int first = SYNTH_ANALYSIS_SAMPLES - engine->analysisCursor;
    memcpy(samples, engine->analysisSamples + engine->analysisCursor, (size_t)first * sizeof(*samples));
    memcpy(samples + first, engine->analysisSamples, (size_t)engine->analysisCursor * sizeof(*samples));
    *position = engine->samplePosition;

    return engine->sampleRate;
}
static void noteOn(Synth *engine, int note, unsigned char source, int velocity)
{
    if (engine == NULL || note < 0 || note >= NOTE_COUNT)
        return;
    Voice *voice = &engine->voices[note];
    if (source == 2) voice->midiVelocity = fmax(0, fmin(127, velocity)) / 127.0;
    engine->noteSources[note] |= source;
    if (!voice->held) {
        bool reset = voice->envelope.level == 0;
        outputEnvelopeOn(&voice->envelope);
        for (int layer = 0; layer < engine->layerCount; ++layer) {
            LayerVoice *part = &voice->layers[layer];
            fmNoteOn(&part->generator, reset);
            if (engine->sourceMode==SYNTH_SOURCE_SAMPLES) part->samplePosition=0;
        }
    }
    voice->held = true;
    voice->active = true;
}

static void noteOff(Synth *engine, int note, unsigned char source)
{
    if (engine == NULL || note < 0 || note >= NOTE_COUNT)
        return;
    Voice *voice = &engine->voices[note];
    engine->noteSources[note] &= (unsigned char)~source;
    if (voice->held && !engine->noteSources[note]) {
        voice->held = false;
        outputEnvelopeOff(&voice->envelope);
        for (int layer = 0; layer < engine->layerCount; ++layer) {
            LayerVoice *part = &voice->layers[layer];
            fmNoteOff(&part->generator);
        }
    }
}

void synthNoteOn(Synth *engine, int note) { noteOn(engine, note, 1, 127); }
void synthNoteOff(Synth *engine, int note) { noteOff(engine, note, 1); }
void synthMidiNoteOn(Synth *engine, int note, int velocity) { noteOn(engine, note, 2, velocity); }
void synthMidiNoteOff(Synth *engine, int note) { noteOff(engine, note, 2); }
