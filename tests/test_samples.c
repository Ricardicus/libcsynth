#include "synth.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define PI 3.14159265358979323846

static SynthConfig patch(void)
{
    SynthConfig config=synthPresetConfig(1);
    config.outputEnvelope=(SynthEnvelopeConfig){0,0,100,50};
    config.filters=(SynthFilterConfig){0,0};
    config.effects.echoMix=0; config.effects.reverbMix=0;
    return config;
}
static double energy(const float *pcm, size_t count)
{
    double sum=0;
    for (size_t i=0;i<count;++i) { CHECK(isfinite(pcm[i]) && fabs(pcm[i])<=1); sum+=pcm[i]*pcm[i]; }
    return sum;
}
/* Sample sources use exactly the same shared processing as FM, including live edits. */
static void processing(void)
{
    float tone[4800], out[24000]; char error[128];
    for (int i=0;i<4800;++i) tone[i]=(float)sin(2*PI*440*i/48000.0);
    SynthSampleData sample={tone,4800,48000,440,1,0,4800};
    SynthSampleBank *bank=synthSampleBankCreate(); CHECK(bank);
    CHECK(!synthSampleBankAddPcm(bank,&sample,error,sizeof(error)));
    SynthConfig config=patch(); Synth *engine=synthCreate(48000,&config); CHECK(engine);
    CHECK(!synthApplySampleBank(engine,bank)); synthSampleBankDestroy(bank);
    synthNoteOn(engine,69); synthRender(engine,out,24000);
    double dry=energy(out+4800,19200); CHECK(dry>1);
    config.filters.lowpassHz=40;
    CHECK(!synthConfigure(engine,&config)); synthRender(engine,out,24000);
    CHECK(energy(out+4800,19200)<dry*.05);
    config.filters.lowpassHz=0; config.filters.highpassHz=4000;
    CHECK(!synthConfigure(engine,&config)); synthRender(engine,out,24000);
    CHECK(energy(out+4800,19200)<dry*.05);
    config.filters.highpassHz=0;
    config.outputEnvelope=(SynthEnvelopeConfig){100,100,25,100};
    CHECK(!synthConfigure(engine,&config));
    synthNoteOff(engine,69); synthRender(engine,out,24000);
    synthNoteOn(engine,69); synthRender(engine,out,24000);
    CHECK(energy(out,480)<energy(out+4320,480)*.05); /* Attack rises. */
    CHECK(energy(out+19200,4800)<energy(out+4320,480)*2); /* Decay reaches quiet sustain. */
    synthNoteOff(engine,69); synthRender(engine,out,24000);
    CHECK(energy(out,4800)>0 && energy(out+4800,19200)==0);
    CHECK(synthGetSourceMode(engine)==SYNTH_SOURCE_SAMPLES); synthDestroy(engine);

    /* A short one-shot ends; each effect must produce an audible tail afterward. */
    for (int i=0;i<32;++i) tone[i]=.5f;
    sample=(SynthSampleData){tone,32,48000,440,1,0,0};
    bank=synthSampleBankCreate(); CHECK(bank);
    CHECK(!synthSampleBankAddPcm(bank,&sample,error,sizeof(error)));
    for (int effect=0;effect<3;++effect) {
        config=patch(); config.outputEnvelope.releaseMs=0;
        if (effect==1) { config.effects.echoMix=.5; config.effects.echoDelayMs=25; config.effects.echoFeedback=.4; }
        if (effect==2) config.effects.reverbMix=.5;
        engine=synthCreate(48000,&config); CHECK(engine && !synthApplySampleBank(engine,bank));
        synthNoteOn(engine,69); synthRender(engine,out,64); synthNoteOff(engine,69);
        synthRender(engine,out,24000);
        if (!effect) CHECK(energy(out,24000)==0);
        else CHECK(energy(out+1000,23000)>1e-10);
        synthDestroy(engine);
    }
    synthSampleBankDestroy(bank);
}
static int crossings(const float *pcm, size_t count)
{
    int result=0;
    for (size_t i=1;i<count;++i) if (pcm[i-1]<=0 && pcm[i]>0) ++result;
    return result;
}
static void u16(FILE *file, unsigned value) { fputc(value&255,file); fputc((value>>8)&255,file); }
static void u32(FILE *file, uint32_t value) { u16(file,value&65535); u16(file,value>>16); }
static void wav(const char *path)
{
    FILE *file=fopen(path,"wb"); CHECK(file);
    fwrite("RIFF",1,4,file); u32(file,36+80*4); fwrite("WAVEfmt ",1,8,file);
    u32(file,16); u16(file,1); u16(file,2); u32(file,8000); u32(file,32000);
    u16(file,4); u16(file,16); fwrite("data",1,4,file); u32(file,80*4);
    for (int i=0;i<80;++i) { u16(file,16384); u16(file,0); }
    CHECK(!ferror(file)); CHECK(!fclose(file));
}
static void text(const char *path, const char *content)
{
    FILE *file=fopen(path,"wb"); CHECK(file); CHECK(fputs(content,file)>=0); CHECK(!fclose(file));
}
int main(void)
{
    processing();
    char error[1024];
    SynthConfig config=patch();
    SynthSampleBank *bank=synthSampleBankCreate(); CHECK(bank);
    CHECK(synthSampleBankCount(NULL)==0);
    Synth *a=synthCreate(48000,&config), *b=synthCreate(96000,&config), *c=synthCreate(48000,&config);
    CHECK(a && b && c);
    CHECK(synthGetSourceMode(a)==SYNTH_SOURCE_FM);
    CHECK(synthSetSourceMode(a,SYNTH_SOURCE_SAMPLES)==-1);
    CHECK(synthApplySampleBank(a,bank)==-1 && synthGetSourceMode(a)==SYNTH_SOURCE_FM);
    float original[4800];
    for (size_t i=0;i<4800;++i) original[i]=(float)sin(2*PI*440*i/48000.0);
    SynthSampleData sample={original,4800,48000,440,1,0,4800};
    CHECK(!synthSampleBankAddPcm(bank,&sample,error,sizeof(error)));
    memset(original,0,sizeof(original)); /* Bank owns a copy, not this buffer. */
    for (size_t i=0;i<4800;++i) original[i]=(float)sin(2*PI*880*i/48000.0);
    sample.baseFrequencyHz=880;
    CHECK(!synthSampleBankAddPcm(bank,&sample,error,sizeof(error)));
    CHECK(synthSampleBankCount(bank)==2);
    size_t index=999; SynthSampleInfo info;
    CHECK(!synthSampleBankFind(bank,440,&index) && index==0);
    CHECK(!synthSampleBankFind(bank,700,&index) && index==1);
    CHECK(!synthSampleBankFind(bank,sqrt(440*880),&index) && index==0);
    CHECK(synthSampleBankFind(bank,NAN,&index)==-1);
    CHECK(!synthSampleBankGetInfo(bank,1,&info) && info.frameCount==4800 && info.sampleRate==48000);
    CHECK(synthSampleBankGetInfo(bank,2,&info)==-1);
    sample.loopStart=10; sample.loopEnd=10;
    CHECK(synthSampleBankAddPcm(bank,&sample,error,sizeof(error))==-1);
    CHECK(synthSampleBankCount(bank)==2);
    sample.loopStart=0; sample.loopEnd=4800;
    sample.frameCount=SYNTH_SAMPLE_MAX_BYTES/sizeof(float)+1;
    CHECK(synthSampleBankAddPcm(bank,&sample,error,sizeof(error))==-1);
    sample.frameCount=4800; sample.baseFrequencyHz=1e-300;
    CHECK(synthSampleBankAddPcm(bank,&sample,error,sizeof(error))==-1);
    sample.baseFrequencyHz=880; original[0]=NAN;
    CHECK(synthSampleBankAddPcm(bank,&sample,error,sizeof(error))==-1);
    original[0]=0;
    CHECK(!synthApplySampleBank(a,bank) && !synthApplySampleBank(b,bank) && !synthApplySampleBank(c,bank));
    CHECK(synthGetSourceMode(a)==SYNTH_SOURCE_SAMPLES);
    CHECK(synthSampleBankAddPcm(bank,&sample,error,sizeof(error))==-1 && strstr(error,"immutable"));
    synthSampleBankDestroy(bank); /* Three engines keep the recording alive. */
    float x[24000], y[48000];
    synthNoteOn(a,69); synthNoteOn(b,69); synthNoteOn(c,69);
    synthRender(a,x,24000); synthRender(b,y,48000);
    CHECK(abs(crossings(x,24000)-220)<=1);
    for (int i=500;i<24000;++i) CHECK(fabs(x[i]-y[2*i])<1e-6);
    for (size_t offset=0;offset<24000;) {
        size_t count=offset%173+1; if (count>24000-offset) count=24000-offset;
        synthRender(c,y+offset,count); offset+=count;
    }
    CHECK(!memcmp(x,y,sizeof(x))); /* Render block boundaries are irrelevant. */
    config.layers[0].detuneCents=1200;
    CHECK(!synthConfigure(a,&config)); synthRender(a,x,24000);
    CHECK(abs(crossings(x,24000)-440)<=1); /* Live detune + upper multisample. */
    config.layers[0].gain=.5;
    config.outputEnvelope.sustainPercent=50;
    CHECK(!synthConfigure(a,&config)); synthRender(a,x,24000);
    CHECK(energy(x+1000,23000)>.5 && energy(x+1000,23000)<10);
    synthNoteOff(a,69); synthRender(a,x,24000); CHECK(energy(x,2000)>0 && energy(x+3000,21000)==0);
    config=patch(); config.layerCount=2; config.layers[1]=config.layers[0];
    config.layers[1].detuneCents=1200;
    CHECK(!synthConfigure(a,&config)); synthNoteOn(a,69); synthRender(a,x,24000);
    for (int i=500;i<24000;++i)
        CHECK(fabs(x[i]-.05*(sin(2*PI*440*i/48000.0)+sin(2*PI*880*i/48000.0)))<1e-6);
    config.layers[0].fm.operators[0].waveform=WAVE_NOISE;
    config.layers[0].fm.operators[0].ratio=4;
    CHECK(!synthConfigure(a,&config)); synthRender(a,y,24000);
    CHECK(!memcmp(x+500,y+500,(24000-500)*sizeof(float))); /* FM settings don't alter samples. */
    SynthSampleBank *empty=synthSampleBankCreate();
    CHECK(synthApplySampleBank(a,empty)==-1 && synthGetSourceMode(a)==SYNTH_SOURCE_SAMPLES);
    synthSampleBankDestroy(empty);
    CHECK(synthSetSourceMode(a,(SynthSourceMode)99)==-1);
    CHECK(!synthSetSourceMode(a,SYNTH_SOURCE_FM));
    CHECK(!synthSetSourceMode(a,SYNTH_SOURCE_SAMPLES));
    CHECK(!synthApplySampleBank(a,NULL) && synthGetSourceMode(a)==SYNTH_SOURCE_FM);
    CHECK(synthSetSourceMode(a,SYNTH_SOURCE_SAMPLES)==-1);
    synthDestroy(a); synthDestroy(b); synthDestroy(c);

    /* One-shot exhaustion and an intro followed by a held/release loop. */
    float ramp[]={0,1,2,3,4,5};
    sample=(SynthSampleData){ramp,6,1000,440,1,0,0};
    bank=synthSampleBankCreate(); CHECK(!synthSampleBankAddPcm(bank,&sample,error,sizeof(error)));
    config=patch(); a=synthCreate(1000,&config); CHECK(a && !synthApplySampleBank(a,bank));
    synthSampleBankDestroy(bank); synthNoteOn(a,69); synthRender(a,x,20);
    CHECK(energy(x,6)>0 && energy(x+6,14)==0);
    synthDestroy(a);
    sample.loopStart=2; sample.loopEnd=5;
    bank=synthSampleBankCreate(); CHECK(!synthSampleBankAddPcm(bank,&sample,error,sizeof(error)));
    a=synthCreate(1000,&config); CHECK(a && !synthApplySampleBank(a,bank)); synthSampleBankDestroy(bank);
    synthNoteOn(a,69); synthRender(a,x,20);
    for (int i=5;i<20;++i) CHECK(fabs(x[i]-.1*(2+(i-5)%3))<1e-6);
    synthNoteOff(a,69); synthRender(a,x,100); CHECK(energy(x,49)>0 && energy(x+50,50)==0);
    config.layers[0].detuneCents=4800;
    CHECK(!synthConfigure(a,&config)); synthNoteOn(a,127); synthRender(a,x,1000); CHECK(energy(x,1000)>0);
    synthDestroy(a);

    /* Files, relative paths with spaces, stereo averaging, MP3 and rollback. */
    const char *wavePath=SAMPLE_TEST_DIR "/root tone.WAV";
    const char *mapPath=SAMPLE_TEST_DIR "/instrument.csamples";
    wav(wavePath);
    text(mapPath,"# bank\ncsynth-samples 1\nsample 440 1 0 80 \"root tone.WAV\" # relative\n");
    bank=synthSampleBankLoad(mapPath,error,sizeof(error)); CHECK(bank && synthSampleBankCount(bank)==1);
    CHECK(!synthSampleBankGetInfo(bank,0,&info) && info.frameCount==80 && info.sampleRate==8000);
    config=patch(); a=synthCreate(8000,&config); CHECK(a && !synthApplySampleBank(a,bank));
    synthSampleBankDestroy(bank); synthNoteOn(a,69); synthRender(a,x,1000);
    CHECK(fabs(x[900]-.025)<1e-6); /* (left .5 + right 0)/2, then synth gain .1. */
    text(mapPath,"csynth-samples 1\nsample 440 1 0 0 \"missing.wav\"\n");
    CHECK(!synthSampleBankLoad(mapPath,error,sizeof(error)) && strstr(error,":2:"));
    synthRender(a,x,1000); CHECK(fabs(x[900]-.025)<1e-6); /* Failed load leaves old sound alone. */
    const char *bad[]={"", "csynth-samples 2\n", "csynth-samples 1\n",
        "csynth-samples 1\nsample nan 1 0 0 \"root tone.WAV\"\n",
        "csynth-samples 1\nsample 440 1 -1 2 \"root tone.WAV\"\n",
        "csynth-samples 1\nsample 440 1 0 90 \"root tone.WAV\"\n",
        "csynth-samples 1\nsample 440 1 0 0 \"root tone.WAV\" junk\n"};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) { text(mapPath,bad[i]); CHECK(!synthSampleBankLoad(mapPath,error,sizeof(error))); }
    const char *badWav=SAMPLE_TEST_DIR "/broken.wav", *badMp3=SAMPLE_TEST_DIR "/broken.mp3";
    text(badWav,"not a WAV"); text(badMp3,"not an MP3");
    bank=synthSampleBankCreate();
    CHECK(synthSampleBankAddFile(bank,badWav,440,1,0,0,error,sizeof(error))==-1);
    CHECK(synthSampleBankAddFile(bank,badMp3,440,1,0,0,error,sizeof(error))==-1);
    CHECK(synthSampleBankCount(bank)==0);
    remove(badWav); remove(badMp3);
    CHECK(!synthSampleBankAddFile(bank,MP3_TEST_FILE,440,1,0,0,error,sizeof(error)));
    CHECK(!synthSampleBankGetInfo(bank,0,&info) && info.sampleRate==44100 && info.frameCount>4000);
    CHECK(!synthApplySampleBank(a,bank)); synthSampleBankDestroy(bank);
    synthNoteOff(a,69); synthNoteOn(a,69); synthRender(a,x,1000); CHECK(energy(x,1000)>.001);
    synthDestroy(a);
    remove(wavePath); remove(mapPath);
    puts("Sample banks: pitch, rates, looping, one-shots, ADSR, live edits, ownership, WAV/MP3 and maps passed.");
    return 0;
}
