/* Versioned drop file encoding, validation and durable replacement. */
#include "items_internal.h"
#include "durable.h"
#include "protocol.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"Drop storage requires IEEE binary32 floats");

/* Portable drop format: 32-byte header, 32-byte records, trailing CRC32.
 * No pointers, connection IDs, padding or viewer bits enter this file. */
#define DROP_HEADER 32u
#define DROP_RECORD 32u
#define DROP_MAX (DROP_HEADER + ITEM_ENTITY_LIMIT*DROP_RECORD + 4u)
const char *items_path(void) { return items_context()->path; }
bool items_load(const char *path) {
  ItemsContext *ctx = items_context();
  memset(ctx,0,sizeof(*ctx)); ctx->next_id=1;
  if(!path) return true;
  if(strlen(path)>=sizeof(ctx->path)) return false;
  uint8_t *data=NULL; size_t n=0;
  if(!durable_read(path,&data,&n,DROP_MAX)) return false;
#if !LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
  /* Never let a transient build spend inventory while a stale registry waits
   * to resurrect it. An operator must explicitly archive the registry. */
  if(data) { free(data); fprintf(stderr,"%s requires LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1 (or archive it explicitly).\n",path); return false; }
  return true;
#else
  strcpy(ctx->path,path);
  if(!data) return items_save();
  bool ok=n>=DROP_HEADER+4 && !memcmp(data,"LAPDROP1",8) &&
    durable_get32(data+8)==1 && durable_get32(data+12)==LAPIS_PROTOCOL_VERSION &&
    durable_get32(data+16)==ITEM_ENTITY_LIMIT && durable_get32(data+20)<=ITEM_ENTITY_LIMIT;
  unsigned count=ok?durable_get32(data+20):0;
  if(ok) ok=n==DROP_HEADER+(size_t)count*DROP_RECORD+4 && durable_get32(data+n-4)==durable_crc(data,n-4);
  uint64_t next=ok?durable_get64(data+24):0;
  if(!next) ok=false;
  for(unsigned i=0;ok && i<count;i++) {
    const uint8_t *r=data+DROP_HEADER+i*DROP_RECORD; DroppedItem *d=&ctx->items[i];
    d->persistent_id=durable_get64(r);
    uint32_t y=durable_get32(r+8), v=durable_get32(r+12);
    memcpy(&d->y,&y,4); memcpy(&d->velocity,&v,4);
    unsigned x=(unsigned)r[16]|((unsigned)r[17]<<8), z=(unsigned)r[18]|((unsigned)r[19]<<8);
    d->x=(int16_t)(x<32768? (int)x : (int)x-65536); d->z=(int16_t)(z<32768? (int)z : (int)z-65536);
    d->item=(uint16_t)((unsigned)r[20]|((unsigned)r[21]<<8)); d->count=r[22];
    d->age_ms=durable_get32(r+24); d->delay_ms=durable_get32(r+28);
    ok=d->persistent_id && d->persistent_id<next && !r[23] && isfinite(d->y) &&
      d->y>=0 && d->y<=255.25f && isfinite(d->velocity) && d->velocity>=-16 && d->velocity<=0 &&
      items_valid(d->item,d->count,d->x,(int)d->y,d->z) && d->age_ms<ITEM_LIFETIME_MS && d->delay_ms<=ITEM_LIFETIME_MS;
    for(unsigned j=0;ok && j<i;j++) if(ctx->items[j].persistent_id==d->persistent_id) ok=false;
  }
  free(data);
  if(!ok) { memset(ctx->items,0,sizeof(ctx->items)); fprintf(stderr,"Invalid drop registry: %s\n",path); return false; }
  ctx->next_id=next; return true;
#endif
}
bool items_save(void) {
#if LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM
  ItemsContext *ctx = items_context();
  if(!ctx->path[0]) return true;
  uint8_t data[DROP_MAX]={0}; memcpy(data,"LAPDROP1",8);
  durable_put32(data+8,1); durable_put32(data+12,LAPIS_PROTOCOL_VERSION);
  durable_put32(data+16,ITEM_ENTITY_LIMIT); durable_put64(data+24,ctx->next_id);
  unsigned count=0;
  for(unsigned i=0;i<ITEM_ENTITY_LIMIT;i++) {
    const DroppedItem *d=&ctx->items[i]; if(!d->count) continue;
    uint8_t *r=data+DROP_HEADER+count++*DROP_RECORD; uint32_t y,v;
    memcpy(&y,&d->y,4); memcpy(&v,&d->velocity,4);
    durable_put64(r,d->persistent_id); durable_put32(r+8,y); durable_put32(r+12,v);
    r[16]=(uint8_t)d->x; r[17]=(uint8_t)((uint16_t)d->x>>8);
    r[18]=(uint8_t)d->z; r[19]=(uint8_t)((uint16_t)d->z>>8);
    r[20]=(uint8_t)d->item; r[21]=(uint8_t)(d->item>>8); r[22]=d->count;
    durable_put32(r+24,d->age_ms); durable_put32(r+28,d->delay_ms);
  }
  durable_put32(data+20,count); size_t n=DROP_HEADER+(size_t)count*DROP_RECORD;
  durable_put32(data+n,durable_crc(data,n));
  return durable_replace(ctx->path,data,n+4);
#else
  return true;
#endif
}
