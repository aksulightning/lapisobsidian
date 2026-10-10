/* Exercise real event routing/PCM, without requiring an audio device. */
#include "LapisAudio.h"
#include "LapisSoundData.h"
#include "Audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Audio_SoundsVolume=70;
static struct AudioData played;
static int plays,allocations,fail_alloc;
cc_result Audio_AllocChunks(cc_uint32 size,struct AudioChunk* chunks,int count) {
    assert(count==1);
    if(fail_alloc)return 1;
    chunks[0].data=malloc(size);assert(chunks[0].data);
    chunks[0].size=size;allocations++;return 0;
}
void Audio_FreeChunks(struct AudioChunk* chunks,int count) {
    assert(count==1);free(chunks[0].data);allocations--;
}
void AudioPool_Close(void) { }
cc_result AudioPool_Play(struct AudioData* data) { played=*data;plays++;return 0; }

static void Event(struct LapisSoundEvent* e,const char* name) {
    memset(e,0,sizeof(*e));strcpy(e->name,name);e->volume=1;e->pitch=1;
}
static int InGroup(int group) {
    const struct LapisRecordedGroup* g=&Lapis_RecordedGroups[group];
    const struct LapisRecordedClip* c;int i;
    for(i=0;i<g->count;i++) {
        c=&Lapis_RecordedClips[g->clips[i]];
        if(played.chunk.size==(cc_uint32)c->count*2 &&
           !memcmp(played.chunk.data,c->samples,(size_t)c->count*2))return g->clips[i];
    }
    return -1;
}
int main(void) {
    const struct { const char* name;int group; } cases[]={
        {"minecraft:entity.player.hurt",LAPIS_SOUND_HURT},
        {"minecraft:entity.zombie.death",LAPIS_SOUND_HURT},
        {"minecraft:entity.item.pickup",LAPIS_SOUND_PICKUP},
        {"minecraft:entity.generic.eat",LAPIS_SOUND_EAT},
        {"minecraft:block.wooden_door.open",LAPIS_SOUND_DOOR_OPEN},
        {"minecraft:block.wooden_door.close",LAPIS_SOUND_DOOR_CLOSE},
        {"minecraft:block.lever.click",LAPIS_SOUND_CLICK},
        {"minecraft:item.bucket.fill",LAPIS_SOUND_WATER},
        {"minecraft:item.bucket.empty_lava",LAPIS_SOUND_LAVA},
        {"minecraft:block.lava.extinguish",LAPIS_SOUND_EXTINGUISH},
        {"minecraft:entity.skeleton.shoot",LAPIS_SOUND_SHOOT},
        {"minecraft:entity.creeper.primed",LAPIS_SOUND_EXTINGUISH},
        {"minecraft:entity.firework_rocket.blast",LAPIS_SOUND_IMPACT},
        {"minecraft:block.note_block.snare",LAPIS_SOUND_SNARE},
        {"minecraft:block.note_block.hat",LAPIS_SOUND_HAT},
        {"minecraft:block.note_block.basedrum",LAPIS_SOUND_BASEDRUM}
    };
    struct LapisSoundEvent e;int i,before,previous=-1,selected;
    assert(LapisAudio_Material("iron_block")==SOUND_METAL);
    assert(LapisAudio_Material("iron_ore")==SOUND_STONE);
    assert(LapisAudio_Material("iron_trapdoor")==SOUND_METAL);
    assert(LapisAudio_Material("white_wool")==SOUND_CLOTH);
    assert(LapisAudio_Material("glass_pane")==SOUND_GLASS);
    assert(LapisAudio_Material("ice")==SOUND_GLASS);
    assert(LapisAudio_Material("snow_block")==SOUND_SNOW);
    assert(LapisAudio_Material("snowy_grass_block")==SOUND_GRASS);
    assert(LapisAudio_Material("moss_carpet")==SOUND_GRASS);
    assert(LapisAudio_Material("oak_leaves")==SOUND_GRASS);
    assert(LapisAudio_Material("oak_planks")==SOUND_WOOD);
    assert(LapisAudio_Material("sand")==SOUND_SAND);
    assert(LapisAudio_Material("sandstone")==SOUND_STONE);
    assert(LapisAudio_Material("gravel")==SOUND_GRAVEL);
    assert(LapisAudio_Material("water_3")==SOUND_NONE);
    assert(LapisAudio_Material("unknown")==SOUND_STONE);
    for(i=0;i<(int)(sizeof(cases)/sizeof(cases[0]));i++) {
        Event(&e,cases[i].name);before=plays;LapisAudio_Play(&e,0,0,0);
        assert(plays==before+1 && InGroup(cases[i].group)>=0);
        assert(played.sampleRate==22050 && played.channels==1);
    }
    Event(&e,"minecraft:entity.player.hurt");
    for(i=0;i<20;i++) {
        LapisAudio_Play(&e,0,0,0);selected=InGroup(LAPIS_SOUND_HURT);
        assert(selected>=0 && selected!=previous);previous=selected;
    }
    e.x=16;e.pitch=1.5f;LapisAudio_Play(&e,0,0,0);
    assert(played.volume==35 && played.rate==150);
    before=plays;e.x=32;LapisAudio_Play(&e,0,0,0);assert(plays==before);
    e.x=0;Audio_SoundsVolume=0;LapisAudio_Play(&e,0,0,0);assert(plays==before);Audio_SoundsVolume=70;
    Event(&e,"minecraft:entity.cow.ambient");LapisAudio_Play(&e,0,0,0);assert(plays==before);
    Event(&e,"minecraft:unknown.event");LapisAudio_Play(&e,0,0,0);assert(plays==before);
    Event(&e,"minecraft:block.note_block.harp");e.pitch=2;LapisAudio_Play(&e,0,0,0);
    assert(played.rate==200 && played.chunk.size==22050 && plays==before+1);
    Event(&e,"minecraft:block.note_block.bass");LapisAudio_Play(&e,0,0,0);
    Event(&e,"minecraft:block.note_block.bell");LapisAudio_Play(&e,0,0,0);
    LapisAudio_Free();assert(!allocations);LapisAudio_Free();assert(!allocations);
    fail_alloc=1;before=plays;Event(&e,"minecraft:entity.player.hurt");
    LapisAudio_Play(&e,0,0,0);assert(plays==before && !allocations);
    fail_alloc=0;LapisAudio_Play(&e,0,0,0);assert(plays==before+1);
    LapisAudio_Free();assert(!allocations);
    puts("audio: recorded event routing, variants, distance/pitch/volume, notes and cleanup passed");
    return 0;
}
