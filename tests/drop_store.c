#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "drop_store.h"
#include "durable.h"
#include "items.h"
#include "doors.h"
#include "signs.h"
#include "farming.h"
#include "circuits.h"
#include "procedures.h"
#include "registries.h"
#include "serialize.h"
#include "tools.h"
#include "worldgen.h"

static int pair[2];
static unsigned total(void) { unsigned n=0; for(unsigned i=0;i<ITEM_ENTITY_LIMIT;i++) n+=items_at(i)->count; return n; }
static void drain(void) { uint8_t b[65536]; while(recv(pair[0],b,sizeof(b),MSG_DONTWAIT)>0) {} }
static void reload(void) {
  block_changes_count=0; assert(!initSerializer()); assert(items_load("drops.bin"));
  player_data[0].client_fd=pair[1];
}
static void wait_child(pid_t child,int expected) { int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==expected); }
int main(void) {
  char dir[]=".tests/drop-world.XXXXXX"; assert(mkdtemp(dir) && !chdir(dir));
  memset(player_data,0,sizeof(player_data)); for(unsigned i=0;i<MAX_PLAYERS;i++) player_data[i].client_fd=-1;
  memset(block_changes,255,sizeof(block_changes)); block_changes_count=0; assert(!initSerializer());
  assert(items_load("drops.bin") && doors_load(NULL) && signs_load(NULL) && farming_load(NULL) && circuits_load(NULL));
  assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair)); PlayerData *p=&player_data[0];
  assert(drop_store_begin());
  p->client_fd=pair[1]; p->uuid[0]=1; p->x=0; p->y=201; p->z=0; p->health=20; p->flags=0;
  p->inventory_items[0]=I_dirt; p->inventory_count[0]=32;
  assert(!makeBlockChange(0,200,0,B_stone)); assert(!makeBlockChange(1,201,0,B_dirt)); assert(drop_store_commit()); drain();
  pid_t mining=fork(); assert(mining>=0);
  if(!mining) {
    assert(drop_store_begin()); handlePlayerAction(p,2,1,201,0);
    assert(getBlockAt(1,201,0)==B_air && total()==1); assert(items_save()); _exit(76);
  }
  wait_child(mining,76); assert(drop_store_recover(".",false)); reload(); drain();
  assert(getBlockAt(1,201,0)==B_dirt && total()==0 && p->inventory_count[0]==32);
  /* Crash at creation boundaries: only source written, both source/registry
   * written, and completed commit. Recovery must preserve stack conservation. */
  for(int stage=0;stage<4;stage++) {
    pid_t child=fork(); assert(child>=0);
    if(!child) {
      assert(drop_store_begin()); assert(items_drop_slot(p,0,true));
      uint8_t b; assert(recv(pair[0],&b,1,MSG_DONTWAIT)<0); /* No acknowledged spawn yet. */
      if(stage>=1) writePlayerDataToDisk();
      if(stage>=2) assert(items_save());
      if(stage==3) assert(drop_store_commit());
      _exit(77);
    }
    wait_child(child,77); assert(drop_store_recover(".",false)); assert(drop_store_recover(".",false)); reload(); drain();
    if(stage<3) assert(p->inventory_count[0]==32 && total()==0);
    else assert(p->inventory_count[0]==0 && total()==32);
  }
  uint64_t id=items_at(0)->persistent_id; assert(id);
  assert(drop_store_begin()); items_tick(100000); assert(drop_store_commit()); drain();
  uint32_t age=items_at(0)->age_ms,delay=items_at(0)->delay_ms; reload();
  assert(items_at(0)->persistent_id==id && items_at(0)->age_ms==age && items_at(0)->delay_ms==delay && !items_at(0)->viewers);
  /* Interrupted pickup restores the player and drop together, even if both
   * files had reached disk. Replaying recovery twice must be harmless. */
  for(int stage=0;stage<3;stage++) {
    pid_t child=fork(); assert(child>=0);
    if(!child) {
      assert(drop_store_begin()); items_tick(2000000); assert(total()==0 && p->inventory_count[0]==32);
      writePlayerDataToDisk(); if(stage>=1) assert(items_save());
      if(stage==2) assert(drop_store_commit());
      _exit(78);
    }
    wait_child(child,78); assert(drop_store_recover(".",false)); assert(drop_store_recover(".",false)); reload(); drain();
    if(stage<2) assert(total()==32 && p->inventory_count[0]==0);
    else assert(total()==0 && p->inventory_count[0]==32);
  }
  assert(drop_store_begin()); assert(items_spawn(I_dirt,5,0,201,0,0));
  for(unsigned i=0;i<36;i++) { p->inventory_items[i]=I_dirt; p->inventory_count[i]=64; }
  p->inventory_count[0]=62; items_tick(1000000); assert(total()==3 && p->inventory_count[0]==64);
  assert(drop_store_commit()); drain(); reload(); assert(total()==3 && p->inventory_count[0]==64);
  /* Failed commit keeps its recovery copy and withholds outgoing packets. */
  pid_t child=fork(); assert(child>=0);
  if(!child) {
    assert(drop_store_begin()); assert(items_spawn(I_dirt,1,10,201,10,500));
    assert(!mkdir("world.bin.tmp",0700)); assert(!drop_store_commit()); _exit(79);
  }
  wait_child(child,79); assert(!rmdir("world.bin.tmp")); assert(drop_store_recover(".",false)); reload(); assert(total()==3); drain();
  /* Registry corruption is rejected before replacing any world state. */
  uint8_t *saved=NULL; size_t n=0; assert(durable_read("drops.bin",&saved,&n,8192) && saved && n>=68);
  for(size_t cut=0;cut<n;cut+=7) { assert(durable_replace("bad.bin",saved,cut)); assert(!items_load("bad.bin")); }
  uint8_t *bad=malloc(n); assert(bad); memcpy(bad,saved,n);
  bad[8]=2; durable_put32(bad+n-4,durable_crc(bad,n-4)); assert(durable_replace("bad.bin",bad,n)); assert(!items_load("bad.bin"));
  memcpy(bad,saved,n); durable_put32(bad+40,UINT32_C(0x7fc00000)); durable_put32(bad+n-4,durable_crc(bad,n-4));
  assert(durable_replace("bad.bin",bad,n)); assert(!items_load("bad.bin"));
  free(bad); free(saved); assert(items_load("drops.bin") && total()==3);
  /* A corrupt recovery journal must not partially restore anything. */
  assert(durable_replace("drops.journal","broken",6)); assert(!drop_store_recover(".",false));
  assert(durable_remove("drops.journal")); reload(); assert(total()==3);
  close(pair[0]); close(pair[1]); puts("persistent drops: restart, identity/age/delay, partial pickup, crash recovery, delayed acknowledgements, failed writes and corrupt data passed");
}
