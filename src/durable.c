#define _POSIX_C_SOURCE 200809L
#include "durable.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

void durable_put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
uint32_t durable_get32(const uint8_t *p) { uint32_t v=0; for (unsigned i=0;i<4;i++) v|=(uint32_t)p[i]<<(8*i); return v; }
void durable_put64(uint8_t *p, uint64_t v) { for (unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
uint64_t durable_get64(const uint8_t *p) { uint64_t v=0; for (unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
uint32_t durable_crc(const void *data, size_t size) {
  const uint8_t *p=data; uint32_t crc=UINT32_MAX;
  for(size_t i=0;i<size;i++) { crc^=p[i]; for(unsigned b=0;b<8;b++) crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u))); }
  return ~crc;
}
static bool regular(const char *path, bool missing) {
  struct stat st;
#if defined(_WIN32) || defined(ESP_PLATFORM)
  if (stat(path,&st)) return missing && errno==ENOENT;
#else
  if (lstat(path,&st)) return missing && errno==ENOENT;
#endif
  return S_ISREG(st.st_mode);
}
bool durable_read(const char *path, uint8_t **data, size_t *size, size_t limit) {
  *data=NULL; *size=0;
  if(!regular(path,true)) return false;
  FILE *f=fopen(path,"rb"); if(!f) return errno==ENOENT;
  bool ok=fseek(f,0,SEEK_END)==0; long n=ok?ftell(f):-1;
  if(n<0 || (uint64_t)n>limit || fseek(f,0,SEEK_SET)) ok=false;
  uint8_t *p=ok?malloc((size_t)n+1):NULL; if(!p) ok=false;
  if(ok && (fread(p,1,(size_t)n,f)!=(size_t)n || fgetc(f)!=EOF || ferror(f))) ok=false;
  if(fclose(f)) ok=false;
  if(!ok) { free(p); return false; }
  *data=p; *size=(size_t)n; return true;
}
static bool sync_fd(int fd) {
#if defined(__EMSCRIPTEN__) || defined(ESP_PLATFORM)
  (void)fd; return true; /* IDBFS syncfs is the browser durability boundary. */
#elif defined(_WIN32)
  return _commit(fd)==0;
#else
  return fsync(fd)==0;
#endif
}
static bool sync_directory(const char *path) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || defined(ESP_PLATFORM)
  (void)path; return true;
#else
  char parent[256]; size_t n=strlen(path); if(n>=sizeof(parent)) return false;
  memcpy(parent,path,n+1); char *slash=strrchr(parent,'/');
  if(slash) { if(slash==parent) slash[1]=0; else *slash=0; } else strcpy(parent,".");
  int fd=open(parent,O_RDONLY); if(fd<0) return false;
  bool ok=fsync(fd)==0; if(close(fd)) ok=false; return ok;
#endif
}
bool durable_sync(const char *path) {
  if(!regular(path,true)) return false;
  FILE *f=fopen(path,"rb"); if(!f) return errno==ENOENT;
  bool ok=sync_fd(fileno(f)); if(fclose(f)) ok=false;
  return ok && sync_directory(path);
}
bool durable_replace(const char *path, const void *data, size_t size) {
  char tmp[256]; if(snprintf(tmp,sizeof(tmp),"%s.tmp",path)>=(int)sizeof(tmp)) return false;
  if(!regular(path,true) || !regular(tmp,true)) return false;
  FILE *f=fopen(tmp,"wb"); if(!f) return false;
  bool ok=fwrite(data,1,size,f)==size && fflush(f)==0 && sync_fd(fileno(f));
  if(fclose(f)) ok=false;
  if(ok) {
#ifdef _WIN32
    ok=MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    ok=rename(tmp,path)==0;
#endif
  }
  return ok && sync_directory(path);
}
bool durable_remove(const char *path) {
  if(!regular(path,true)) return false;
  if(remove(path) && errno!=ENOENT) return false;
  return sync_directory(path);
}
