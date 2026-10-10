#include "LapisAudio.h"
#include "Audio.h"
#include "LapisSoundData.h"
#include <math.h>
#include <string.h>

#define NOTE_SAMPLES 11025
#define NOTE_COUNT 3

#ifdef CC_BUILD_WEB
#include <emscripten.h>
/* Event PCM is embedded, so the first interaction does not wait for a fetch. */
EM_JS(void,PlayPCM,(int voice,const void* samples,int count,int rate,int volume),{
    var ctx=window.AUDIO && AUDIO.context;if(!ctx)return;
    var bank=Module.lapisSounds || (Module.lapisSounds={buffers:[],sources:new Set()});
    if(bank.sources.size>=16)return;
    var buffer=bank.buffers[voice];
    if(!buffer) {
        buffer=ctx.createBuffer(1,count,22050);var dst=buffer.getChannelData(0);
        for(var i=0;i<count;i++)dst[i]=HEAP16[(samples>>1)+i]/32768;
        bank.buffers[voice]=buffer;
    }
    var src=ctx.createBufferSource(),gain=ctx.createGain();
    src.buffer=buffer;src.playbackRate.value=Math.max(.05,rate/100);
    gain.gain.value=volume/100;src.connect(gain);gain.connect(ctx.destination);
    bank.sources.add(src);
    src.onended=function(){bank.sources.delete(src);src.disconnect();gain.disconnect();};src.start();
});
EM_JS(void,FreePCM,(void),{
    var bank=Module.lapisSounds;if(!bank)return;
    bank.sources.forEach(function(src){src.stop();});
    Module.lapisSounds=null;
});
static cc_int16 note_pcm[NOTE_COUNT][NOTE_SAMPLES];
#else
static struct AudioChunk recorded[LAPIS_RECORDED_COUNT];
#endif
static struct AudioChunk notes[NOTE_COUNT];
static int last_variant[LAPIS_SOUND_GROUPS];
static cc_uint32 random_state=772;

cc_uint8 LapisAudio_Material(const char* name) {
    if(!strcmp(name,"air") || strstr(name,"water") || strstr(name,"lava"))return SOUND_NONE;
    if(strstr(name,"glass") || strstr(name,"ice"))return SOUND_GLASS;
    if(!strcmp(name,"snow") || !strcmp(name,"snow_block") || !strcmp(name,"powder_snow"))return SOUND_SNOW;
    if(strstr(name,"wool") || strstr(name,"_bed") ||
       (strstr(name,"carpet") && !strstr(name,"moss")))return SOUND_CLOTH;
    if(strstr(name,"gravel"))return SOUND_GRAVEL;
    if(strstr(name,"sand") && !strstr(name,"sandstone"))return SOUND_SAND;
    if(!strcmp(name,"iron_block") || !strcmp(name,"gold_block") || strstr(name,"copper_block") ||
       !strcmp(name,"netherite_block") || !strcmp(name,"iron_door") || !strcmp(name,"iron_trapdoor") ||
       !strcmp(name,"iron_bars") || !strcmp(name,"chain") || strstr(name,"anvil") ||
       strstr(name,"rail") || strstr(name,"weighted_pressure_plate") || !strcmp(name,"hopper") ||
       !strcmp(name,"cauldron"))return SOUND_METAL;
    if(strstr(name,"grass") || strstr(name,"dirt") || strstr(name,"leaves") ||
       strstr(name,"sapling") || strstr(name,"flower") || strstr(name,"mushroom") ||
       !strcmp(name,"moss_carpet") || !strcmp(name,"farmland") || !strcmp(name,"podzol") ||
       !strcmp(name,"mud") || !strcmp(name,"wheat") || !strcmp(name,"sugar_cane") ||
       !strcmp(name,"fern") || strstr(name,"bush") || !strcmp(name,"vine"))return SOUND_GRASS;
    if(strstr(name,"wood") || strstr(name,"oak") || strstr(name,"chest") ||
       strstr(name,"bookshelf") || strstr(name,"torch") || !strcmp(name,"ladder") ||
       !strcmp(name,"crafting_table") || !strcmp(name,"composter") ||
       !strcmp(name,"note_block") || !strcmp(name,"jukebox"))return SOUND_WOOD;
    return SOUND_STONE;
}

static int InitNote(int voice) {
    int i;cc_int16* samples;double t,signal,envelope;
    if(notes[voice].data)return 1;
#ifdef CC_BUILD_WEB
    notes[voice].data=note_pcm[voice];notes[voice].size=NOTE_SAMPLES*2;
#else
    if(Audio_AllocChunks(NOTE_SAMPLES*2,&notes[voice],1))return 0;
#endif
    samples=(cc_int16*)notes[voice].data;
    for(i=0;i<NOTE_SAMPLES;i++) {
        t=(double)i/LAPIS_SOUND_RATE;envelope=(1-t*2)*(1-t*2);
        signal=sin(t*6.283185307179586*(voice==1?130.8128:261.625565));
        if(voice==2)signal=.7*signal+.3*sin(t*6.283185307179586*523.25113);
        samples[i]=(cc_int16)(signal*envelope*8000);
    }
    return 1;
}

static int GroupFor(const char* name) {
    if(strstr(name,"note_block")) {
        if(strstr(name,".basedrum"))return LAPIS_SOUND_BASEDRUM;
        if(strstr(name,".snare"))return LAPIS_SOUND_SNARE;
        if(strstr(name,".hat"))return LAPIS_SOUND_HAT;
        return -1;
    }
    if(strstr(name,".extinguish") || strstr(name,".primed"))return LAPIS_SOUND_EXTINGUISH;
    if(strstr(name,"lava"))return LAPIS_SOUND_LAVA;
    if(strstr(name,"water") || strstr(name,"bucket") || strstr(name,".splash"))return LAPIS_SOUND_WATER;
    if(strstr(name,".hurt") || strstr(name,".death"))return LAPIS_SOUND_HURT;
    if(strstr(name,".pickup") || strstr(name,"experience"))return LAPIS_SOUND_PICKUP;
    if(strstr(name,".eat") || strstr(name,".burp"))return LAPIS_SOUND_EAT;
    if(strstr(name,"door") || strstr(name,"chest") || strstr(name,"fence_gate"))
        return strstr(name,"close")?LAPIS_SOUND_DOOR_CLOSE:LAPIS_SOUND_DOOR_OPEN;
    if(strstr(name,"button") || strstr(name,"lever") || strstr(name,"pressure_plate"))return LAPIS_SOUND_CLICK;
    if(strstr(name,".shoot"))return LAPIS_SOUND_SHOOT;
    if(strstr(name,".blast") || strstr(name,".explode"))return LAPIS_SOUND_IMPACT;
    /* Do not impersonate animal voices or unknown events with generic noise. */
    return -1;
}

void LapisAudio_Free(void) {
#ifdef CC_BUILD_WEB
    FreePCM();
#else
    int i;
    AudioPool_Close();
    for(i=0;i<LAPIS_RECORDED_COUNT;i++)if(recorded[i].data)Audio_FreeChunks(&recorded[i],1);
    for(i=0;i<NOTE_COUNT;i++)if(notes[i].data)Audio_FreeChunks(&notes[i],1);
    memset(recorded,0,sizeof(recorded));
#endif
    memset(notes,0,sizeof(notes));
    memset(last_variant,0,sizeof(last_variant));random_state=772;
}

void LapisAudio_Play(const struct LapisSoundEvent* s,double x,double y,double z) {
    struct AudioData data;
    const struct LapisRecordedGroup* group;
    const struct LapisRecordedClip* clip;
    double distance;float gain;int family,variant,voice,note=-1;
    if(!Audio_SoundsVolume)return;
    distance=sqrt((s->x-x)*(s->x-x)+(s->y-y)*(s->y-y)+(s->z-z)*(s->z-z));
    gain=(float)(1-distance/32)*s->volume;
    if(gain<=0)return;
    family=GroupFor(s->name);
    if(family<0) {
        if(!strstr(s->name,"note_block"))return;
        note=0;
        if(strstr(s->name,".bass"))note=1;
        if(strstr(s->name,".bell") || strstr(s->name,".chime"))note=2;
    }
    memset(&data,0,sizeof(data));
    if(note>=0) {
        if(!InitNote(note))return;
        data.chunk=notes[note];voice=LAPIS_RECORDED_COUNT+note;
    } else {
        group=&Lapis_RecordedGroups[family];
        random_state=random_state*1664525u+1013904223u;
        variant=(int)((random_state>>16)%(cc_uint32)group->count);
        if(group->count>1 && variant==last_variant[family])variant=(variant+1)%group->count;
        last_variant[family]=variant;voice=group->clips[variant];clip=&Lapis_RecordedClips[voice];
#ifdef CC_BUILD_WEB
        data.chunk.data=(void*)clip->samples;data.chunk.size=(cc_uint32)clip->count*2;
#else
        if(!recorded[voice].data) {
            if(Audio_AllocChunks((cc_uint32)clip->count*2,&recorded[voice],1))return;
            memcpy(recorded[voice].data,clip->samples,(size_t)clip->count*2);
        }
        data.chunk=recorded[voice];
#endif
    }
    data.channels=1;data.sampleRate=LAPIS_SOUND_RATE;
    data.rate=(int)(s->pitch*100);data.volume=(int)((float)Audio_SoundsVolume*(gain>1?1:gain));
    if(data.rate<5)data.rate=5;
    if(data.volume<=0)return;
#ifdef CC_BUILD_WEB
    PlayPCM(voice,data.chunk.data,(int)(data.chunk.size/2),data.rate,data.volume);
#else
    AudioPool_Play(&data);
#endif
}
