#include "samples_private.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sampleError(char *error, size_t size, const char *format, ...)
{
    if (error && size) {
        va_list args; va_start(args,format); vsnprintf(error,size,format,args); va_end(args);
    }
    return -1;
}
SynthSampleBank *synthSampleBankCreate(void)
{
    SynthSampleBank *bank=calloc(1,sizeof(*bank));
    if (bank) { atomic_init(&bank->references,1); atomic_init(&bank->sealed,false); }
    return bank;
}
void sampleBankRetainAndSeal(SynthSampleBank *bank)
{
    atomic_store(&bank->sealed,true);
    atomic_fetch_add_explicit(&bank->references,1,memory_order_relaxed);
}
void synthSampleBankDestroy(SynthSampleBank *bank)
{
    if (!bank || atomic_fetch_sub_explicit(&bank->references,1,memory_order_acq_rel)!=1) return;
    for (size_t i=0;i<bank->count;++i) free(bank->entries[i].pcm);
    free(bank);
}
static int validate(const SynthSampleBank *bank, const float *pcm, SynthSampleInfo info,
                    char *error, size_t errorSize)
{
    if (!bank || !pcm) return sampleError(error,errorSize,"Missing bank or PCM buffer");
    if (atomic_load(&bank->sealed)) return sampleError(error,errorSize,"Applied sample banks are immutable; build a new bank");
    if (bank->count>=SYNTH_SAMPLE_MAX_ENTRIES) return sampleError(error,errorSize,"Too many samples (maximum 128)");
    if (!info.frameCount || info.frameCount>(SYNTH_SAMPLE_MAX_BYTES-bank->bytes)/sizeof(float))
        return sampleError(error,errorSize,"Empty sample or decoded bank exceeds 256 MiB");
    if (info.sampleRate<1000 || info.sampleRate>384000 || !isfinite(info.baseFrequencyHz) ||
        info.baseFrequencyHz<1 || info.baseFrequencyHz>info.sampleRate*.5 ||
        !isfinite(info.gain) || info.gain<0 || info.gain>4)
        return sampleError(error,errorSize,"Invalid sample rate, base frequency, or gain");
    if ((info.loopStart || info.loopEnd) &&
        (info.loopStart>=info.loopEnd || info.loopEnd>info.frameCount))
        return sampleError(error,errorSize,"Loop must satisfy start < end <= frame count (or both zero)");
    for (size_t i=0;i<info.frameCount;++i)
        if (!isfinite(pcm[i]) || fabs(pcm[i])>16) return sampleError(error,errorSize,"PCM must be finite and within -16..16");
    return 0;
}
int sampleBankAppendOwned(SynthSampleBank *bank, float *pcm, SynthSampleInfo info,
                          char *error, size_t errorSize)
{
    if (validate(bank,pcm,info,error,errorSize)) return -1;
    bank->entries[bank->count++]=(SampleEntry){pcm,info,log2(info.baseFrequencyHz)};
    bank->bytes+=info.frameCount*sizeof(float);
    return 0;
}
int synthSampleBankAddPcm(SynthSampleBank *bank, const SynthSampleData *sample,
                          char *error, size_t errorSize)
{
    if (error && errorSize) error[0]=0;
    if (!sample) return sampleError(error,errorSize,"Missing sample description");
    SynthSampleInfo info={sample->frameCount,sample->sampleRate,sample->baseFrequencyHz,
                          sample->gain,sample->loopStart,sample->loopEnd};
    if (validate(bank,sample->samples,info,error,errorSize)) return -1;
    float *copy=malloc(info.frameCount*sizeof(float));
    if (!copy) return sampleError(error,errorSize,"Could not allocate sample PCM");
    memcpy(copy,sample->samples,info.frameCount*sizeof(float));
    bank->entries[bank->count++]=(SampleEntry){copy,info,log2(info.baseFrequencyHz)};
    bank->bytes+=info.frameCount*sizeof(float);
    return 0;
}
size_t synthSampleBankCount(const SynthSampleBank *bank) { return bank ? bank->count : 0; }
int synthSampleBankGetInfo(const SynthSampleBank *bank, size_t index, SynthSampleInfo *info)
{
    if (!bank || !info || index>=bank->count) return -1;
    *info=bank->entries[index].info; return 0;
}
int synthSampleBankFind(const SynthSampleBank *bank, double frequencyHz, size_t *index)
{
    if (!bank || !bank->count || !index || !isfinite(frequencyHz) || frequencyHz<=0) return -1;
    double pitch=log2(frequencyHz), best=INFINITY;
    *index=0;
    for (size_t i=0;i<bank->count;++i) {
        double distance=fabs(pitch-bank->entries[i].logFrequency);
        if (distance<best-1e-12) { best=distance; *index=i; }
    }
    return 0;
}
float sampleBankNext(const SynthSampleBank *bank, size_t index, double *position, double step)
{
    const SampleEntry *entry=&bank->entries[index];
    const SynthSampleInfo *info=&entry->info;
    double p=*position;
    if (info->loopEnd && p>=info->loopEnd)
        p=info->loopStart+fmod(p-info->loopStart,(double)(info->loopEnd-info->loopStart));
    if (p>=info->frameCount) return 0;
    size_t first=(size_t)p, second=first+1;
    if (info->loopEnd && second>=info->loopEnd) second=info->loopStart;
    else if (second>=info->frameCount) second=first;
    double fraction=p-(double)first;
    double value=(1-fraction)*entry->pcm[first]+fraction*entry->pcm[second];
    p+=step;
    if (info->loopEnd && p>=info->loopEnd)
        p=info->loopStart+fmod(p-info->loopStart,(double)(info->loopEnd-info->loopStart));
    *position=p;
    return (float)(value*info->gain);
}
