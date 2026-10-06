#include "synth.h"
#include <stdint.h>
#include <stdio.h>

static void u16(FILE *file, unsigned value)
{
    fputc((int)(value & 255),file); fputc((int)((value >> 8) & 255),file);
}
static void u32(FILE *file, uint32_t value)
{
    u16(file,value & 65535); u16(file,value >> 16);
}
static void render(FILE *file, Synth *engine, size_t frames)
{
    float samples[256];
    while (frames) {
        size_t count=frames>256 ? 256 : frames;
        synthRender(engine,samples,count);
        for (size_t i=0;i<count;++i) {
            int16_t sample=(int16_t)(samples[i]*32767);
            u16(file,(uint16_t)sample);
        }
        frames-=count;
    }
}
int main(int argc, char **argv)
{
    if (argc!=3) { fprintf(stderr,"Usage: %s instrument.csamples output.wav\n",argv[0]); return 1; }
    const char *path=argv[2];
    char error[512];
    SynthSampleBank *bank=synthSampleBankLoad(argv[1],error,sizeof(error));
    if (!bank) { fprintf(stderr,"%s\n",error); return 1; }
    const unsigned rate=48000, frames=rate*3;
    SynthConfig sound=synthPresetConfig(1);
    sound.outputEnvelope=(SynthEnvelopeConfig){10,80,75,400};
    sound.effects.echoMix=.12; sound.effects.echoDelayMs=250;
    sound.effects.reverbMix=.15;
    Synth *engine=synthCreate((int)rate,&sound);
    if (!engine || synthApplySampleBank(engine,bank)) {
        fprintf(stderr,"Couldn't apply the sample bank.\n");
        synthDestroy(engine); synthSampleBankDestroy(bank); return 1;
    }
    synthSampleBankDestroy(bank); /* The engine retains its own reference. */
    FILE *file=fopen(path,"wb");
    if (!file) { perror(path); synthDestroy(engine); return 1; }
    fwrite("RIFF",1,4,file); u32(file,36+frames*2); fwrite("WAVEfmt ",1,8,file);
    u32(file,16); u16(file,1); u16(file,1); u32(file,rate); u32(file,rate*2);
    u16(file,2); u16(file,16); fwrite("data",1,4,file); u32(file,frames*2);
    synthNoteOn(engine,60); synthNoteOn(engine,64); synthNoteOn(engine,67);
    render(file,engine,rate);
    sound.layers[0].detuneCents=7;
    sound.outputEnvelope.sustainPercent=65;
    sound.outputEnvelope.releaseMs=600;
    sound.effects.echoMix=.2;
    int result=synthConfigure(engine,&sound);
    render(file,engine,rate);
    synthNoteOff(engine,60); synthNoteOff(engine,64); synthNoteOff(engine,67);
    render(file,engine,rate);
    if (ferror(file)) result=-1;
    if (fclose(file)) result=-1;
    synthDestroy(engine);
    if (!result) printf("Wrote %s (sampled chord, live detune/ADSR edit, release).\n",path);
    return result==0 ? 0 : 1;
}
