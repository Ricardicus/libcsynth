#include "samples_private.h"
#define DRWAV_API static
#define DRWAV_PRIVATE static
#define DRMP3_API static
#define DRMP3_PRIVATE static
#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "dr_wav.h"
#include "dr_mp3.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool extension(const char *path, const char *suffix)
{
    const char *dot=strrchr(path,'.');
    if (!dot) return false;
    while (*dot && *suffix)
        if (tolower((unsigned char)*dot++)!=tolower((unsigned char)*suffix++)) return false;
    return !*dot && !*suffix;
}
int synthSampleBankAddFile(SynthSampleBank *bank, const char *path,
                           double baseFrequencyHz, double gain,
                           size_t loopStart, size_t loopEnd,
                           char *error, size_t errorSize)
{
    if (error && errorSize) error[0]=0;
    if (!bank || !path || !*path) return sampleError(error,errorSize,"Missing bank or file path");
    if (atomic_load(&bank->sealed)) return sampleError(error,errorSize,"Applied sample banks are immutable; build a new bank");
    if (bank->count>=SYNTH_SAMPLE_MAX_ENTRIES) return sampleError(error,errorSize,"Too many samples (maximum 128)");
    bool wavFile=extension(path,".wav"), mp3File=extension(path,".mp3");
    if (!wavFile && !mp3File) return sampleError(error,errorSize,"Expected a .wav or .mp3 file: %s",path);
    drwav wav;
    drmp3 mp3;
    if (wavFile ? !drwav_init_file(&wav,path,NULL) : !drmp3_init_file(&mp3,path,NULL))
        return sampleError(error,errorSize,"Could not open or decode %s",path);
    unsigned channels=wavFile ? wav.channels : mp3.channels;
    unsigned rate=wavFile ? wav.sampleRate : mp3.sampleRate;
    uint64_t frames=wavFile ? wav.totalPCMFrameCount : drmp3_get_pcm_frame_count(&mp3);
    float *mono=NULL;
    int result=-1;
    if (!channels || channels>32 || rate<1000 || rate>384000 || !frames ||
        frames>(SYNTH_SAMPLE_MAX_BYTES-bank->bytes)/sizeof(float)) {
        sampleError(error,errorSize,"Unsupported/empty recording or decoded bank exceeds 256 MiB: %s",path); goto done;
    }
    if (!isfinite(baseFrequencyHz) || baseFrequencyHz<1 || baseFrequencyHz>rate*.5 ||
        !isfinite(gain) || gain<0 || gain>4 ||
        ((loopStart || loopEnd) && (loopStart>=loopEnd || loopEnd>frames))) {
        sampleError(error,errorSize,"Invalid base frequency, gain or loop points: %s",path); goto done;
    }
    mono=malloc((size_t)frames*sizeof(float));
    if (!mono) { sampleError(error,errorSize,"Could not allocate decoded PCM: %s",path); goto done; }
    float block[1024*32];
    for (size_t position=0;position<(size_t)frames;) {
        size_t requested=(size_t)frames-position;
        if (requested>1024) requested=1024;
        uint64_t read=wavFile ? drwav_read_pcm_frames_f32(&wav,requested,block) :
                               drmp3_read_pcm_frames_f32(&mp3,requested,block);
        if (read!=requested || (!wavFile && (mp3.sampleRate!=rate || mp3.channels!=channels))) {
            sampleError(error,errorSize,"Truncated or changing-format recording: %s",path); goto done;
        }
        for (size_t frame=0;frame<requested;++frame) {
            double sum=0;
            for (unsigned channel=0;channel<channels;++channel) sum+=block[frame*channels+channel];
            mono[position+frame]=(float)(sum/channels);
        }
        position+=requested;
    }
    result=sampleBankAppendOwned(bank,mono,(SynthSampleInfo){(size_t)frames,(int)rate,
        baseFrequencyHz,gain,loopStart,loopEnd},error,errorSize);
    if (!result) mono=NULL; /* Ownership moves to the bank only on success. */
done:
    free(mono);
    if (wavFile) drwav_uninit(&wav); else drmp3_uninit(&mp3);
    return result;
}

static char *space(char *p) { while (isspace((unsigned char)*p)) ++p; return p; }
static bool real(char **cursor, double *number)
{
    char *p=space(*cursor), *end;
    errno=0; *number=strtod(p,&end);
    if (end==p || errno || !isspace((unsigned char)*end)) return false;
    *cursor=end; return true;
}
static bool frameIndex(char **cursor, size_t *number)
{
    char *p=space(*cursor), *end;
    if (*p=='-') return false;
    errno=0; unsigned long long value=strtoull(p,&end,10);
    if (end==p || errno || value>SIZE_MAX || !isspace((unsigned char)*end)) return false;
    *number=(size_t)value; *cursor=end; return true;
}
static bool quotedPath(char **cursor, char *path, size_t capacity)
{
    char *p=space(*cursor); size_t length=0;
    if (*p++!='"') return false;
    while (*p && *p!='"') {
        char c=*p++;
        if (c=='\\') {
            if (*p!='\\' && *p!='"') return false;
            c=*p++;
        }
        if (length+1>=capacity || c=='\n' || c=='\r') return false;
        path[length++]=c;
    }
    if (*p++!='"' || !length) return false;
    path[length]=0; *cursor=space(p);
    return !**cursor || **cursor=='#';
}
static bool resolve(const char *map, const char *file, char *result, size_t capacity)
{
    bool absolute=file[0]=='/' || file[0]=='\\' || (isalpha((unsigned char)file[0]) && file[1]==':');
    size_t prefix=0;
    if (!absolute)
        for (size_t i=0;map[i];++i) if (map[i]=='/' || map[i]=='\\') prefix=i+1;
    if (prefix+strlen(file)+1>capacity) return false;
    memcpy(result,map,prefix); strcpy(result+prefix,file); return true;
}
SynthSampleBank *synthSampleBankLoad(const char *mapPath, char *error, size_t errorSize)
{
    if (error && errorSize) error[0]=0;
    if (!mapPath) { sampleError(error,errorSize,"Missing sample-map path"); return NULL; }
    FILE *file=fopen(mapPath,"rb");
    if (!file) { sampleError(error,errorSize,"Could not open sample map: %s",mapPath); return NULL; }
    SynthSampleBank *bank=synthSampleBankCreate();
    if (!bank) { fclose(file); sampleError(error,errorSize,"Could not allocate sample bank"); return NULL; }
    char line[4096], cause[512]; size_t lineNumber=0;
    bool header=false;
    while (fgets(line,sizeof(line),file)) {
        ++lineNumber;
        if (!strchr(line,'\n') && !feof(file)) { snprintf(cause,sizeof(cause),"Line too long (maximum 4094 characters)"); goto fail; }
        char *p=space(line);
        if (!*p || *p=='#') continue;
        if (!header) {
            const char *magic="csynth-samples 1";
            size_t length=strlen(magic);
            if (strncmp(p,magic,length) || (p[length] && !isspace((unsigned char)p[length]) && p[length]!='#')) {
                snprintf(cause,sizeof(cause),"Expected csynth-samples 1 header"); goto fail;
            }
            p=space(p+length);
            if (*p && *p!='#') { snprintf(cause,sizeof(cause),"Unexpected text after map header"); goto fail; }
            header=true; continue;
        }
        if (strncmp(p,"sample",6) || !isspace((unsigned char)p[6])) {
            snprintf(cause,sizeof(cause),"Expected a sample row"); goto fail;
        }
        p+=6;
        double frequency, gain; size_t loopStart, loopEnd;
        char path[2048], resolved[4096];
        if (!real(&p,&frequency) || !real(&p,&gain) || !frameIndex(&p,&loopStart) ||
            !frameIndex(&p,&loopEnd) || !quotedPath(&p,path,sizeof(path))) {
            snprintf(cause,sizeof(cause),"Expected sample BASE_HZ GAIN LOOP_START LOOP_END \"file.wav or file.mp3\""); goto fail;
        }
        if (!resolve(mapPath,path,resolved,sizeof(resolved))) { snprintf(cause,sizeof(cause),"Resolved sample path is too long"); goto fail; }
        if (synthSampleBankAddFile(bank,resolved,frequency,gain,loopStart,loopEnd,cause,sizeof(cause))) goto fail;
    }
    if (ferror(file)) { snprintf(cause,sizeof(cause),"Could not read sample map"); goto fail; }
    if (!header || !bank->count) { snprintf(cause,sizeof(cause),"Sample map is empty"); goto fail; }
    fclose(file); return bank;
fail:
    fclose(file); synthSampleBankDestroy(bank);
    sampleError(error,errorSize,"%s:%zu: %s",mapPath,lineNumber,cause); return NULL;
}
