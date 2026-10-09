#include "LapisAudio.h"
#include "Audio.h"
#include <math.h>
#include <string.h>

#ifdef CC_BUILD_WEB
#include <emscripten.h>
/* WebAudio buffers hold the same original synthesized PCM as the native port. */
EM_JS(void,PlayPCM,(int voice,const void* samples,int count,int rate,int volume),{
    var ctx=window.AUDIO && AUDIO.context;if(!ctx)return;
    var bank=Module.lapisSounds || (Module.lapisSounds={buffers:[],active:0});
    if(bank.active>=16)return;
    var buffer=bank.buffers[voice];
    if(!buffer) {
        buffer=ctx.createBuffer(1,count,22050);var dst=buffer.getChannelData(0);
        for(var i=0;i<count;i++)dst[i]=HEAP16[(samples>>1)+i]/32768;
        bank.buffers[voice]=buffer;
    }
    var src=ctx.createBufferSource(),gain=ctx.createGain();
    src.buffer=buffer;src.playbackRate.value=Math.max(.05,rate/100);
    gain.gain.value=volume/100;src.connect(gain);gain.connect(ctx.destination);
    bank.active++;src.onended=function(){bank.active--;src.disconnect();gain.disconnect();};src.start();
});
#endif
#define SAMPLES 11025
#ifdef CC_BUILD_WEB
static cc_int16 pcm[8][SAMPLES];
#endif
static struct AudioChunk bank[8];
static int allocated;
static int Init(void) {
    int i,j;cc_int16* samples;double t,signal,envelope;cc_uint32 noise=772;
    if(allocated)return 1;
#ifdef CC_BUILD_WEB
    for(j=0;j<8;j++) { bank[j].data=pcm[j];bank[j].size=SAMPLES*2; }
#else
    if(Audio_AllocChunks(SAMPLES*2,bank,8))return 0;
#endif
    for(j=0;j<8;j++) {
        samples=(cc_int16*)bank[j].data;
        for(i=0;i<SAMPLES;i++) {
            t=(double)i/22050;envelope=(1-t*2)*(1-t*2);noise=noise*1664525u+1013904223u;
            signal=sin(t*6.283185307179586*(j==6?880:261.625565));
            if(j==1)signal=sin(t*6.283185307179586*130.8128);
            if(j==2)signal=.7*sin(t*6.283185307179586*261.625565)+.3*sin(t*6.283185307179586*523.25113);
            if(j==3 || j==4 || j==7)signal=((double)(noise>>16)/32768-1)*(j==4?sin(t*190):1);
            if(j==5)signal=sin(t*6.283185307179586*90)+.25*((double)(noise>>16)/32768-1);
            samples[i]=(cc_int16)(signal*envelope*8000);
        }
    }
    allocated=1;return 1;
}
void LapisAudio_Free(void) {
    if(!allocated)return;
#ifndef CC_BUILD_WEB
    AudioPool_Close();Audio_FreeChunks(bank,8);
#endif
    memset(bank,0,sizeof(bank));allocated=0;
}
void LapisAudio_Play(const struct LapisSoundEvent* s,double x,double y,double z) {
    struct AudioData data;double distance;int voice=3;float gain;
    if(!Audio_SoundsVolume)return;
    distance=sqrt((s->x-x)*(s->x-x)+(s->y-y)*(s->y-y)+(s->z-z)*(s->z-z));
    gain=(float)(1-distance/32)*s->volume;
    if(gain<=0 || !Init())return;
    if(strstr(s->name,"note_block")) {
        voice=0;
        if(strstr(s->name,"bass"))voice=1;
        if(strstr(s->name,"bell") || strstr(s->name,"chime"))voice=2;
        if(strstr(s->name,"snare") || strstr(s->name,"hat"))voice=3;
        if(strstr(s->name,"basedrum"))voice=4;
    } else if(strstr(s->name,"hurt") || strstr(s->name,"death"))voice=5;
    else if(strstr(s->name,"pickup") || strstr(s->name,"experience"))voice=6;
    else if(strstr(s->name,"water") || strstr(s->name,"lava"))voice=7;
    memset(&data,0,sizeof(data));data.chunk=bank[voice];data.channels=1;data.sampleRate=22050;
    data.rate=(int)(s->pitch*100);data.volume=(int)(Audio_SoundsVolume*(gain>1?1:gain));
#ifdef CC_BUILD_WEB
    PlayPCM(voice,data.chunk.data,SAMPLES,data.rate,data.volume);
#else
    AudioPool_Play(&data);
#endif
}
