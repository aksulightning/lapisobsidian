#include "drop_store.h"
#include "durable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILES 7u
#define FILE_LIMIT (1024u*1024u)
#define JOURNAL_LIMIT (FILES*(FILE_LIMIT+4u)+20u)
static const char *const names[FILES]={"world.bin","drops.bin","doors.bin","signs.bin","circuits.bin","farming.bin","players.bin"};
static bool paths(const char *root, bool shared, char out[FILES][256], char journal[256]) {
  for(unsigned i=0;i<FILES;i++) {
    const char *dir=i==6 && shared?"plates":root;
    if(snprintf(out[i],256,"%s/%s",dir,names[i])>=256) return false;
  }
  return snprintf(journal,256,"%s/drops.journal",root)<256;
}
bool drop_store_recover(const char *root, bool shared) {
  char files[FILES][256], journal[256]; uint8_t *data=NULL; size_t size=0;
  if(!paths(root,shared,files,journal) || !durable_read(journal,&data,&size,JOURNAL_LIMIT)) return false;
  if(!data) return true;
#if !LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
  fprintf(stderr,"%s needs recovery with experimental drops enabled.\n",journal);
  free(data); return false;
#else
  bool ok=size>=20 && !memcmp(data,"LAPUNDO1",8) && durable_get32(data+8)==FILES &&
    durable_get32(data+12)==(shared?1u:0u) && durable_get32(data+size-4)==durable_crc(data,size-4);
  size_t offsets[FILES]={0}, sizes[FILES]={0}, at=16;
  /* Validate the entire journal before changing even one save file. */
  for(unsigned i=0;ok && i<FILES;i++) {
    if(at>size-4 || size-4-at<4) { ok=false; break; }
    uint32_t n=durable_get32(data+at); at+=4;
    offsets[i]=at; sizes[i]=n;
    if(n==UINT32_MAX) continue;
    if(n>FILE_LIMIT || n>size-4-at) { ok=false; break; }
    at+=n;
  }
  if(at!=size-4) ok=false;
  for(unsigned i=0;ok && i<FILES;i++) {
    if(i==6 && !shared) continue;
    ok=sizes[i]==UINT32_MAX?durable_remove(files[i]):durable_replace(files[i],data+offsets[i],sizes[i]);
  }
  if(ok) ok=durable_remove(journal);
  free(data);
  if(ok) fprintf(stderr,"Recovered interrupted drop transaction: %s\n",root);
  else fprintf(stderr,"Cannot recover %s; preserve the save directory for repair.\n",journal);
  return ok;
#endif
}

#if LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
#include "items.h"
#include "plates.h"
#include "doors.h"
#include "signs.h"
#include "farming.h"
#include "circuits.h"
#include "tools.h"
#include "serialize.h"
static bool active, failed;
static unsigned original_plate;
static char transaction_files[FILES][256], transaction_journal[256];
/* One bounded process-wide output spool. Packet fragments are coalesced by fd. */
#define OUTPUT_LIMIT (16u*1024u*1024u)
static uint8_t *output;
static size_t output_size, output_capacity, last_header;
bool drop_store_active(void) { return active; }
bool drop_store_begin(void) {
  if(active) return false;
  if(!items_path()[0]) return true; /* In-memory unit fixtures do not open storage. */
  char root[128]; strcpy(root,items_path()); char *slash=strrchr(root,'/');
  if(slash) *slash=0; else strcpy(root,".");
  if(!paths(root,plates_enabled,transaction_files,transaction_journal)) return false;
  uint8_t *old=NULL; size_t old_size;
  if(!durable_read(transaction_journal,&old,&old_size,JOURNAL_LIMIT)) return false;
  if(old) { free(old); return false; } /* Never overwrite an unrecovered transaction. */
  uint8_t *copies[FILES]={0}; size_t sizes[FILES]={0}, total=20+4*FILES;
  bool ok=true;
  for(unsigned i=0;ok && i<FILES;i++) {
    if(i==6 && !plates_enabled) continue;
    ok=durable_read(transaction_files[i],&copies[i],&sizes[i],FILE_LIMIT);
    total+=sizes[i];
  }
  uint8_t *data=ok?malloc(total):NULL; if(!data) ok=false;
  if(ok) {
    memcpy(data,"LAPUNDO1",8); durable_put32(data+8,FILES); durable_put32(data+12,plates_enabled?1u:0u);
    size_t at=16;
    for(unsigned i=0;i<FILES;i++) {
      durable_put32(data+at,copies[i]?(uint32_t)sizes[i]:UINT32_MAX); at+=4;
      if(copies[i]) { memcpy(data+at,copies[i],sizes[i]); at+=sizes[i]; }
    }
    durable_put32(data+at,durable_crc(data,at));
    ok=durable_replace(transaction_journal,data,at+4);
  }
  free(data); for(unsigned i=0;i<FILES;i++) free(copies[i]);
  if(!ok) return false;
  active=true; failed=false; original_plate=plate_current; output_size=0; last_header=SIZE_MAX;
  return true;
}
int drop_store_buffer(int fd, const void *data, size_t size) {
  if(!active) return 0;
  if(fd<0 || !size) return 1;
  if(failed || size>OUTPUT_LIMIT-8 || output_size>OUTPUT_LIMIT-8-size) { failed=true; return -1; }
  bool append=last_header!=SIZE_MAX && durable_get32(output+last_header)==(uint32_t)fd;
  size_t needed=output_size+size+(append?0:8);
  if(needed>output_capacity) {
    size_t capacity=output_capacity?output_capacity*2:65536;
    if(capacity<needed) capacity=needed;
    if(capacity>OUTPUT_LIMIT) capacity=OUTPUT_LIMIT;
    void *p=realloc(output,capacity); if(!p) { failed=true; return -1; }
    output=p; output_capacity=capacity;
  }
  if(!append) { last_header=output_size; durable_put32(output+output_size,(uint32_t)fd); durable_put32(output+output_size+4,0); output_size+=8; }
  durable_put32(output+last_header+4,durable_get32(output+last_header+4)+(uint32_t)size);
  memcpy(output+output_size,data,size); output_size+=size; return 1;
}
bool drop_store_commit(void) {
  if(!active) return true;
  unsigned current=plate_current;
  if(failed || !plates_select(original_plate)) return false;
  /* All inherited eager writes are covered by the undo journal. Replace the
   * full world/player image here and check errors even if an eager writer did
   * not propagate one. Sidecar writers are synced before deleting the journal. */
  size_t size=sizeof(block_changes)+(plates_enabled?0:sizeof(player_data));
  uint8_t *world=malloc(size); bool ok=world!=NULL;
  if(ok) {
    memcpy(world,block_changes,sizeof(block_changes));
    if(!plates_enabled) memcpy(world+sizeof(block_changes),player_data,sizeof(player_data));
    ok=durable_replace(transaction_files[0],world,size);
  }
  free(world);
  if(ok && plates_enabled) ok=durable_replace(transaction_files[6],player_data,sizeof(player_data));
  if(ok) ok=items_save() && doors_save() && signs_save() && farming_save() && circuits_save();
  for(unsigned i=0;ok && i<FILES;i++) if(i!=6 || plates_enabled) ok=durable_sync(transaction_files[i]);
  if(ok) ok=durable_remove(transaction_journal);
  if(!ok) { fprintf(stderr,"Drop transaction save failed; restart to recover %s.\n",transaction_journal); return false; }
  active=false;
  for(size_t at=0;at<output_size;) {
    int fd=(int)durable_get32(output+at); size_t n=durable_get32(output+at+4); at+=8;
    plates_select_for_fd(fd); (void)send_all(fd,output+at,(ssize_t)n); at+=n;
  }
  output_size=0;
  return plates_select(current);
}
#else
bool drop_store_active(void) { return false; }
bool drop_store_begin(void) { return true; }
bool drop_store_commit(void) { return true; }
int drop_store_buffer(int fd,const void *data,size_t size) { (void)fd; (void)data; (void)size; return 0; }
#endif
