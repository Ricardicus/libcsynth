#include "synth_config.h"

#include <math.h>
#include <stddef.h>

#include "factory_bank.inc"

enum { ORIGINAL_PRESETS = 13 };
_Static_assert(ORIGINAL_PRESETS + sizeof(factoryRecipes) / sizeof(factoryRecipes[0]) ==
               64, "Factory preset count must match the bank");

SynthConfig synthDefaultConfig(void)
{
    SynthConfig config = {.layerCount = 1, .outputEnvelope = {5, 0, 100, 5}};
    config.effects = (SynthEffectsConfig){0, 300, .35, 0, .7, .4};
    for (int i = 0; i < SYNTH_MAX_LAYERS; ++i)
        config.layers[i] = (SynthLayerConfig){.fm = fmDefaultConfig(), .gain = 1.0};
    return config;
}

bool synthConfigValid(const SynthConfig *config)
{
    if (config == NULL || config->layerCount < 1 || config->layerCount > SYNTH_MAX_LAYERS)
        return false;
    if (!synthEffectsConfigValid(&config->effects)) return false;
    if (!synthFilterConfigValid(&config->filters)) return false;
    const SynthEnvelopeConfig *env = &config->outputEnvelope;
    if (env->attackMs < 0 || env->decayMs < 0 || env->releaseMs < 0 ||
        env->sustainPercent < 0 || env->sustainPercent > 100)
        return false;
    for (int i = 0; i < config->layerCount; ++i) {
        const SynthLayerConfig *layer = &config->layers[i];
        if (!fmConfigValid(&layer->fm) || !isfinite(layer->gain) ||
            layer->gain < 0.0 || layer->gain > 1.0 ||
            !isfinite(layer->detuneCents) || layer->detuneCents < -4800.0 ||
            layer->detuneCents > 4800.0)
            return false;
    }
    return true;
}

const char *synthPresetName(int index)
{
    static const char *names[ORIGINAL_PRESETS] = {
        "Classic FM", "Pure sine", "Warm triangle", "Saw lead",
        "Pulse bass", "Electric piano", "Glass bell", "Metal chime",
        "Soft organ", "Wide pad", "Brass", "Space wobble", "Flute"
    };
    if (index < 0 || index >= SYNTH_PRESET_COUNT) return "Custom";
    static const char *graphNames[]={"Graph Tine Duo","Graph Prism Bell","Graph Hollow Reed","Graph Air Choir","Graph Feedback Bass","Graph Glass Cascade","Graph Drawbar Organ","Graph Orbit Texture"};
    if (index>=64) return graphNames[index-64];
    return index < ORIGINAL_PRESETS ? names[index] : factoryRecipes[index - ORIGINAL_PRESETS].name;
}

SynthConfig synthPresetConfig(int index)
{
    SynthConfig c = synthDefaultConfig();
    if (index>=64 && index<SYNTH_PRESET_COUNT) {
        int recipe=index-64;
        FmConfig *fm=&c.layers[0].fm;
        fm->operatorCount=recipe==7 ? 8 : recipe==6 ? 6 : recipe==5 ? 5 : 4;
        fm->algorithm=recipe==0 || recipe==1 ? FM_ALGORITHM_PAIRS : recipe==2 ? FM_ALGORITHM_FAN_IN :
            recipe==3 ? FM_ALGORITHM_FAN_OUT : recipe==6 ? FM_ALGORITHM_ADDITIVE : FM_ALGORITHM_CUSTOM;
        for (int i=0;i<fm->operatorCount;++i) {
            FmOperatorConfig *op=&fm->operators[i];
            op->ratio=1; op->rm=.6; op->outputLevel=0;
            op->indexMode=FM_INDEX_ADSR; op->attackMs=5; op->decayMs=500; op->sustainPercent=25; op->releaseMs=250;
        }
        c.outputEnvelope=(SynthEnvelopeConfig){8,450,70,350}; c.effects.reverbMix=.12;
        switch (recipe) {
        case 0:
            fm->operators[0].ratio=3; fm->operators[0].rm=1.5; fm->operators[0].sustainPercent=8;
            fm->operators[1].outputLevel=1; fm->operators[2].ratio=7; fm->operators[2].rm=.3;
            fm->operators[3].outputLevel=.45; fm->operators[3].ratio=2;
            c.outputEnvelope=(SynthEnvelopeConfig){3,1200,35,450}; break;
        case 1:
            fm->operators[0].ratio=2.71; fm->operators[0].rm=1.4;
            fm->operators[1].outputLevel=1; fm->operators[2].ratio=3.14;
            fm->operators[3].ratio=1.5; fm->operators[3].outputLevel=.6;
            c.outputEnvelope=(SynthEnvelopeConfig){1,1800,10,900}; c.effects.reverbMix=.25; break;
        case 2:
            fm->operators[0].ratio=2; fm->operators[0].rm=.25;
            fm->operators[1].ratio=3; fm->operators[1].rm=.12;
            fm->operators[2].ratio=5; fm->operators[2].rm=.05; fm->operators[3].outputLevel=1; break;
        case 3:
            fm->operators[0].ratio=.5; fm->operators[0].rm=.45;
            for (int i=1;i<4;++i) { fm->operators[i].ratio=i==3 ? 2 : 1; fm->operators[i].outputLevel=i==3 ? .3 : 1; fm->operators[i].vibratoDepthCents=5*i; }
            c.outputEnvelope=(SynthEnvelopeConfig){400,600,85,1000}; c.effects.reverbMix=.3; break;
        case 4:
            fm->routing[0][1]=1; fm->routing[1][3]=.5; fm->routing[2][3]=.3;
            fm->operators[0].ratio=.5; fm->operators[0].feedback=1.25; fm->operators[0].rm=1.8;
            fm->operators[2].ratio=2; fm->operators[3].outputLevel=1;
            c.outputEnvelope=(SynthEnvelopeConfig){2,350,65,120}; c.filters.lowpassHz=3500; break;
        case 5:
            fm->routing[0][2]=.5; fm->routing[1][2]=1; fm->routing[2][3]=.7; fm->routing[2][4]=.35;
            fm->operators[0].ratio=5.43; fm->operators[1].ratio=2.13;
            fm->operators[3].outputLevel=1; fm->operators[4].ratio=2; fm->operators[4].outputLevel=.4;
            c.outputEnvelope=(SynthEnvelopeConfig){2,1400,20,750}; c.effects.echoMix=.15; break;
        case 6:
            for (int i=0;i<6;++i) { fm->operators[i].ratio=i+1; fm->operators[i].outputLevel=1.0/(i+1); }
            c.outputEnvelope=(SynthEnvelopeConfig){10,0,100,100}; c.effects.reverbMix=.08; break;
        case 7:
            for (int i=0;i<4;++i) { fm->routing[i][i+4]=.5; fm->operators[i].ratio=.5+i*.37; fm->operators[i].feedback=.12*i; fm->operators[i+4].ratio=1+i*.003; fm->operators[i+4].outputLevel=1; }
            c.outputEnvelope=(SynthEnvelopeConfig){600,700,80,1200}; c.effects.echoMix=.2; c.effects.reverbMix=.3; break;
        }
        return c;
    }
    if (index >= ORIGINAL_PRESETS && index < 64) {
        const FactoryRecipe *recipe = &factoryRecipes[index - ORIGINAL_PRESETS];
        c.outputEnvelope = recipe->envelope;
        c.effects = recipe->effects;
        c.layerCount = recipe->layerCount;
        for (int l = 0; l < c.layerCount; ++l) {
            const SynthLayerConfig *source = &recipe->layers[l];
            c.layers[l].gain = source->gain;
            c.layers[l].detuneCents = source->detuneCents;
            c.layers[l].fm.operatorCount = source->fm.operatorCount;
            c.layers[l].fm.algorithm = source->fm.algorithm;
            for (int from=0;from<FM_MAX_OPERATORS;++from)
                for (int to=0;to<FM_MAX_OPERATORS;++to)
                    c.layers[l].fm.routing[from][to]=source->fm.routing[from][to];
            for (int op = 0; op < source->fm.operatorCount; ++op) {
                c.layers[l].fm.operators[op] = source->fm.operators[op];
                if (source->fm.algorithm==FM_ALGORITHM_CHAIN)
                    c.layers[l].fm.operators[op].outputLevel=1;
            }
        }
        return c;
    }
    FmConfig *f = &c.layers[0].fm;
    FmOperatorConfig *m = &f->operators[0], *carrier = &f->operators[1];
    switch (index) {
    case 1: case 2: case 3: case 4:
        f->operatorCount = 1;
        m->waveform = index == 1 ? WAVE_SINE : index == 2 ? WAVE_TRIANGLE :
                      index == 3 ? WAVE_SAWTOOTH : WAVE_PULSE;
        if (index == 3) {
            c.outputEnvelope = (SynthEnvelopeConfig){8, 180, 85, 120};
            c.effects.reverbMix = .08;
            c.layers[0].gain = .85;
        }
        if (index == 4) {
            m->ratio = 0.5;
            c.outputEnvelope = (SynthEnvelopeConfig){5, 220, 70, 90};
            c.layers[0].gain = .8;
        }
        break;
    case 5:
        c.effects.reverbMix = .12;
        c.outputEnvelope = (SynthEnvelopeConfig){5, 900, 20, 350};
        m->ratio = 2; m->rm = 2.5; m->indexMode = FM_INDEX_ADSR;
        m->attackMs = 2; m->decayMs = 650; m->sustainPercent = 12; m->releaseMs = 350;
        break;
    case 6: case 7:
        c.effects.reverbMix = .18;
        c.effects.reverbDamping = .45;
        c.outputEnvelope = (SynthEnvelopeConfig){2, 2200, 0, 1200};
        m->ratio = index == 6 ? 3.5 : 7.13; m->rm = index == 6 ? 1.8 : 3;
        m->indexMode = FM_INDEX_ADSR; m->attackMs = 0; m->decayMs = 1800;
        m->sustainPercent = 0; m->releaseMs = 1200;
        break;
    case 8:
        c.outputEnvelope = (SynthEnvelopeConfig){12, 0, 100, 100};
        c.effects.reverbMix = .14;
        c.layerCount = 3;
        for (int i = 0; i < 3; ++i) {
            c.layers[i].fm.operatorCount = 1;
            c.layers[i].fm.operators[0].ratio = i == 0 ? 1 : i == 1 ? 2 : 4;
            c.layers[i].gain = i == 0 ? 1 : 0.6;
        }
        break;
    case 9:
        c.effects.reverbMix = .24;
        c.effects.reverbDamping = .65;
        c.outputEnvelope = (SynthEnvelopeConfig){700, 900, 75, 1000};
        c.layerCount = 3;
        for (int i = 0; i < 3; ++i) {
            c.layers[i].detuneCents = (i - 1) * 9;
            FmOperatorConfig *op = &c.layers[i].fm.operators[0];
            op->rm = 0.8; op->indexMode = FM_INDEX_ADSR;
            op->attackMs = 700; op->decayMs = 900; op->sustainPercent = 65; op->releaseMs = 1000;
            c.layers[i].fm.operators[1].vibratoDepthCents = 5;
        }
        break;
    case 10:
        c.effects.reverbMix = .1;
        c.outputEnvelope = (SynthEnvelopeConfig){100, 250, 80, 180};
        m->rm = 3; m->indexMode = FM_INDEX_ADSR; m->attackMs = 100;
        m->decayMs = 250; m->sustainPercent = 55; m->releaseMs = 180;
        carrier->waveform = WAVE_TRIANGLE;
        break;
    case 11:
        c.outputEnvelope = (SynthEnvelopeConfig){25, 350, 80, 450};
        c.effects = (SynthEffectsConfig){.18, 340, .35, .2, .75, .55};
        m->ratio = 0.5; m->rm = 5; m->vibratoRateHz = 2; m->vibratoDepthCents = 300;
        carrier->vibratoRateHz = 4; carrier->vibratoDepthCents = 25;
        break;
    case 12:
        c.layerCount = 2;
        c.outputEnvelope = (SynthEnvelopeConfig){45, 180, 88, 220};
        m->ratio = 2; m->rm = .12; m->indexMode = FM_INDEX_ADSR;
        m->attackMs = 35; m->decayMs = 160; m->sustainPercent = 35; m->releaseMs = 160;
        carrier->vibratoRateHz = 5.2; carrier->vibratoDepthCents = 9;
        c.layers[1].fm.operatorCount = 1;
        c.layers[1].fm.operators[0].waveform = WAVE_NOISE;
        c.layers[1].gain = .025;
        c.effects.reverbMix = .12;
        c.effects.reverbRoom = .65;
        c.effects.reverbDamping = .65;
        break;
    default: break;
    }
    return c;
}

bool synthFilterConfigValid(const SynthFilterConfig *c)
{
    return c && isfinite(c->lowpassHz) && isfinite(c->highpassHz) &&
        (c->lowpassHz == 0 || (c->lowpassHz >= 20 && c->lowpassHz <= 20000)) &&
        (c->highpassHz == 0 || (c->highpassHz >= 20 && c->highpassHz <= 20000));
}

bool synthEffectsConfigValid(const SynthEffectsConfig *c)
{
    return c && isfinite(c->echoMix) && c->echoMix >= 0 && c->echoMix <= 1 &&
        isfinite(c->echoDelayMs) && c->echoDelayMs >= 1 && c->echoDelayMs <= CSYNTH_ECHO_MAX_DELAY_MS &&
        isfinite(c->echoFeedback) && c->echoFeedback >= 0 && c->echoFeedback <= .95 &&
        isfinite(c->reverbMix) && c->reverbMix >= 0 && c->reverbMix <= 1 &&
        isfinite(c->reverbRoom) && c->reverbRoom >= 0 && c->reverbRoom <= .95 &&
        isfinite(c->reverbDamping) && c->reverbDamping >= 0 && c->reverbDamping <= 1;
}
