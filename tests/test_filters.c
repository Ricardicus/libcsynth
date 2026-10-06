#include "filters.h"
#include "synth.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); exit(1); } } while(0)

static double gain(SynthFilterConfig config, double hz)
{
    SynthFilters filters; CHECK(!filtersInit(&filters,48000,config));
    double in=0,out=0;
    for (int i=0;i<48000;++i) {
        float x=(float)sin(2*3.141592653589793*hz*i/48000);
        float y=filtersNext(&filters,x); CHECK(isfinite(y));
        if (i>=24000) { in+=x*x; out+=y*y; }
    }
    return sqrt(out/in);
}
int main(void)
{
    CHECK(fabs(gain((SynthFilterConfig){0,0},1000)-1)<1e-6);
    CHECK(gain((SynthFilterConfig){1000,0},100)>.99);
    CHECK(gain((SynthFilterConfig){1000,0},10000)<.01);
    CHECK(gain((SynthFilterConfig){0,1000},100)<.011);
    CHECK(gain((SynthFilterConfig){0,1000},10000)>.99);
    CHECK(fabs(gain((SynthFilterConfig){1000,0},1000)-sqrt(.5))<1e-5);
    CHECK(fabs(gain((SynthFilterConfig){0,1000},1000)-sqrt(.5))<1e-5);
    CHECK(gain((SynthFilterConfig){5000,200},1000)>.99);
    SynthFilterConfig invalid={NAN,100}; CHECK(!synthFilterConfigValid(&invalid));
    invalid=(SynthFilterConfig){19,100}; CHECK(!synthFilterConfigValid(&invalid));
    SynthFilters filters; CHECK(filtersInit(&filters,0,(SynthFilterConfig){0,0})==-1);
    /* Rapid sweeps and bypass transitions must stay finite at different rates. */
    const int rates[]={1000,8000,44100,48000,192000};
    for (int r=0;r<5;++r) {
        CHECK(!filtersInit(&filters,rates[r],(SynthFilterConfig){0,0}));
        for (int i=0;i<rates[r];++i) {
            if (i%79==0) filtersConfigure(&filters,(SynthFilterConfig){
                i%3==0 ? 0 : i%2==0 ? 20 : 20000,
                i%5==0 ? 0 : i%2==0 ? 20000 : 20});
            double y=filtersNext(&filters,(float)(.5*sin(i*.01)));
            CHECK(isfinite(y) && fabs(y)<2);
        }
    }
    /* A toggle starts a fade rather than abruptly removing the signal. */
    CHECK(!filtersInit(&filters,48000,(SynthFilterConfig){0,0}));
    for (int i=0;i<4800;++i) filtersNext(&filters,.5f);
    filtersConfigure(&filters,(SynthFilterConfig){0,20000});
    CHECK(filtersNext(&filters,.5f)>.49);
    /* Verify the whole engine applies live filters to a held note. */
    SynthConfig sound=synthPresetConfig(1); sound.outputEnvelope=(SynthEnvelopeConfig){0,0,100,0};
    Synth *engine=synthCreate(48000,&sound); CHECK(engine); synthNoteOn(engine,69);
    float block[4800]; synthRender(engine,block,4800);
    sound.filters.lowpassHz=100; CHECK(!synthConfigure(engine,&sound));
    /* Let a full-range coefficient sweep settle before measuring rejection. */
    for (int i=0;i<6;++i) synthRender(engine,block,4800);
    double energy=0; for(int i=0;i<4800;++i) energy+=block[i]*block[i]; CHECK(energy<.1);
    sound.filters=(SynthFilterConfig){0,0}; CHECK(!synthConfigure(engine,&sound));
    synthRender(engine,block,4800); synthRender(engine,block,4800);
    energy=0; for(int i=0;i<4800;++i) energy+=block[i]*block[i]; CHECK(energy>20);
    synthDestroy(engine);
    puts("LP/HP passbands, -3 dB cutoff, rejection, bypass, live sweeps and engine integration passed.");
    return 0;
}
