#include "fm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); exit(1); } } while (0)
static FmConfig patch(int count)
{
    FmConfig c=fmDefaultConfig(); c.operatorCount=count;
    for (int i=0;i<count;++i) { c.operators[i].ratio=1+i*.37; c.operators[i].rm=.7; }
    return c;
}
int main(void)
{
    /* Compare the default against the original serial integrator, sample for sample. */
    for (int n=1;n<=8;++n) {
        FmConfig c=patch(n); FmSynth actual; CHECK(!fmInit(&actual,48000,&c)); fmNoteOn(&actual,true);
        Oscillator legacy[8]; double frequencies[8], deviations[8];
        for(int i=0;i<n;++i) { frequencies[i]=220*c.operators[i].ratio; deviations[i]=c.operators[i].rm*frequencies[i]; }
        for (int i=0;i<n;++i) { oscillatorInit(&legacy[i],48000); oscillatorSetWaveform(&legacy[i],WAVE_SINE); }
        for (int sample=0;sample<10000;++sample) {
            float old=0;
            for (int i=0;i<n;++i) {
                double hz=frequencies[i]; if(i>0) hz+=deviations[i-1]*old;
                old=oscillatorNextSample(&legacy[i],hz);
            }
            CHECK(fmNextSample(&actual,220)==old);
        }
    }
    /* Parallel stacks equal the independently rendered, weighted legacy chains. */
    FmConfig c=patch(4); c.algorithm=FM_ALGORITHM_PAIRS;
    c.operators[1].outputLevel=.75; c.operators[3].outputLevel=.25;
    FmSynth graph,a,b; CHECK(!fmInit(&graph,48000,&c)); fmNoteOn(&graph,true);
    FmConfig pair=c; pair.algorithm=FM_ALGORITHM_CHAIN; pair.operatorCount=2;
    CHECK(!fmInit(&a,48000,&pair)); pair.operators[0]=c.operators[2]; pair.operators[1]=c.operators[3];
    CHECK(!fmInit(&b,48000,&pair)); fmNoteOn(&a,true); fmNoteOn(&b,true);
    for (int i=0;i<10000;++i) CHECK(fabs(fmNextSample(&graph,440)-(.75*fmNextSample(&a,440)+.25*fmNextSample(&b,440)))<1e-7);
    /* Multiple source frequencies sum at one destination in the same timestep. */
    c=patch(3); c.algorithm=FM_ALGORITHM_CUSTOM;
    c.routing[0][2]=.4; c.routing[1][2]=.8;
    c.operators[0].outputLevel=0; c.operators[1].outputLevel=0;
    CHECK(!fmInit(&graph,48000,&c)); fmNoteOn(&graph,true);
    Oscillator reference[3]; for (int i=0;i<3;++i) oscillatorInit(&reference[i],48000);
    for (int i=0;i<10000;++i) {
        float x=oscillatorNextSample(&reference[0],440*c.operators[0].ratio);
        float y=oscillatorNextSample(&reference[1],440*c.operators[1].ratio);
        float expected=oscillatorNextSample(&reference[2],440*c.operators[2].ratio+
            .4*440*c.operators[0].ratio*c.operators[0].rm*x+.8*440*c.operators[1].ratio*c.operators[1].rm*y);
        CHECK(fabs(fmNextSample(&graph,440)-expected)<1e-7);
    }
    /* Feedback is delayed frequency modulation, not a phase offset. */
    c=patch(1); c.operators[0].feedback=1.2;
    CHECK(!fmInit(&graph,48000,&c)); fmNoteOn(&graph,true);
    Oscillator oscillator; oscillatorInit(&oscillator,48000); float previous=0;
    for (int i=0;i<10000;++i) {
        previous=oscillatorNextSample(&oscillator,440+440*1.2*previous);
        CHECK(fmNextSample(&graph,440)==previous);
    }
    fmNoteOff(&graph); fmNoteOn(&graph,true); CHECK(graph.previousOutputs[0]==0);
    c=patch(8); c.algorithm=FM_ALGORITHM_CUSTOM; c.routing[0][7]=1;
    CHECK(fmConfigValid(&c)); c.routing[7][0]=.1; CHECK(!fmConfigValid(&c)); c.routing[7][0]=0;
    c.routing[1][1]=.1; CHECK(!fmConfigValid(&c)); c.routing[1][1]=0;
    c.routing[0][7]=NAN; CHECK(!fmConfigValid(&c)); c.routing[0][7]=1;
    c.operators[0].feedback=9; CHECK(!fmConfigValid(&c)); c.operators[0].feedback=8;
    c.operators[0].outputLevel=-1; CHECK(!fmConfigValid(&c)); c.operators[0].outputLevel=1;
    for (int mode=0;mode<FM_ALGORITHM_COUNT;++mode) for (int n=1;n<=8;++n) {
        c=patch(n); c.algorithm=(FmAlgorithm)mode; c.operators[0].feedback=8;
        CHECK(!fmInit(&graph,48000,&c)); fmNoteOn(&graph,true);
        for (int i=0;i<5000;++i) { float x=fmNextSample(&graph,440); CHECK(isfinite(x) && fabs(x)<=1); }
    }
    c=patch(3); c.algorithm=FM_ALGORITHM_CUSTOM; c.routing[0][2]=1;
    c.operators[0].indexMode=FM_INDEX_ADSR; c.operators[0].attackMs=0; c.operators[0].decayMs=0;
    c.operators[0].releaseMs=350;
    CHECK(!fmInit(&graph,48000,&c)); fmNoteOn(&graph,true); CHECK(fmReleaseDurationMs(&graph)==350);
    fmNoteOff(&graph); CHECK(graph.operators[0].stage==FM_ENV_RELEASE);
    puts("Legacy equivalence, parallel stacks, summed modulation, feedback, validation and bounded output passed.");
    return 0;
}
