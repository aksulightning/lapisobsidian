#include "auth.h"
#include <string.h>
bool auth_name_valid(const char *s,size_t n) {
  if(!n || n>16) return false;
  for(size_t i=0;i<n;i++) if(!((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') || (s[i]>='0' && s[i]<='9') || s[i]=='_')) return false;
  return true;
}
#if LAPIS_ENABLE_EXPERIMENTAL_AUTH
/* Small bounded JSON reader. Top-level identity fields cannot be shadowed by
 * nested properties, duplicate keys, embedded NULs or trailing JSON values. */
typedef struct { const char *p,*end; } Json;
static void whitespace(Json *j) { while(j->p<j->end && (*j->p==' ' || *j->p=='\t' || *j->p=='\r' || *j->p=='\n')) j->p++; }
static bool take(Json *j,char c) { whitespace(j); if(j->p==j->end || *j->p!=c) return false; j->p++; return true; }
static int hex(char c) { if(c>='0' && c<='9') return c-'0'; if(c>='a' && c<='f') return c-'a'+10; if(c>='A' && c<='F') return c-'A'+10; return -1; }
static bool string(Json *j,char *out,size_t cap) {
  if(!take(j,'"')) return false;
  size_t n=0;
  while(j->p<j->end) {
    unsigned c=(unsigned char)*j->p++;
    if(c=='"') { if(out) out[n]=0; return true; }
    if(c<32) return false;
    if(c=='\\') {
      if(j->p==j->end) return false;
      c=(unsigned char)*j->p++;
      switch(c) {
        case '"': case '\\': case '/': break;
        case 'b': c=8; break; case 'f': c=12; break; case 'n': c=10; break; case 'r': c=13; break; case 't': c=9; break;
        case 'u':
          c=0;
          for(unsigned i=0;i<4;i++) { if(j->p==j->end) return false; int h=hex(*j->p++); if(h<0) return false; c=c*16+(unsigned)h; }
          if(out && (c==0 || c>127)) return false;
          break;
        default: return false;
      }
    }
    if(out) { if(n+1>=cap || c==0 || c>127) return false; out[n++]=(char)c; }
  }
  return false;
}
static bool value(Json *j,unsigned depth) {
  whitespace(j); if(depth>16 || j->p==j->end) return false;
  if(*j->p=='"') return string(j,NULL,0);
  if(*j->p=='{' || *j->p=='[') {
    bool object=*j->p++=='{'; char close=object?'}':']';
    whitespace(j); if(take(j,close)) return true;
    do { if(object && (!string(j,NULL,0) || !take(j,':'))) return false; if(!value(j,depth+1)) return false; } while(take(j,','));
    return take(j,close);
  }
  static const char *const words[]={"true","false","null"};
  for(unsigned i=0;i<3;i++) { size_t n=strlen(words[i]); if((size_t)(j->end-j->p)>=n && !memcmp(j->p,words[i],n)) { j->p+=n; return true; } }
  if(*j->p=='-') j->p++;
  if(j->p==j->end) return false;
  if(*j->p=='0') j->p++; else { if(*j->p<'1' || *j->p>'9') return false; do { j->p++; } while(j->p<j->end && *j->p>='0' && *j->p<='9'); }
  if(j->p<j->end && *j->p=='.') { j->p++; const char *start=j->p; while(j->p<j->end && *j->p>='0' && *j->p<='9') j->p++; if(j->p==start) return false; }
  if(j->p<j->end && (*j->p=='e' || *j->p=='E')) { j->p++; if(j->p<j->end && (*j->p=='+' || *j->p=='-')) j->p++; const char *start=j->p; while(j->p<j->end && *j->p>='0' && *j->p<='9') j->p++; if(j->p==start) return false; }
  return true;
}
static char lower(char c) { return c>='A' && c<='Z'?(char)(c+('a'-'A')):c; }
bool auth_profile(const char *json,size_t size,const char *claimed,uint8_t uuid[16],char name[17]) {
  if(!json || size>16384) return false;
  Json j={json,json+size}; char id[33]={0}, verified[17]={0}; bool got_id=false,got_name=false;
  if(!take(&j,'{') || take(&j,'}')) return false;
  do {
    char key[128]; if(!string(&j,key,sizeof(key)) || !take(&j,':')) return false;
    if(!strcmp(key,"id")) { if(got_id || !string(&j,id,sizeof(id))) return false; got_id=true; }
    else if(!strcmp(key,"name")) { if(got_name || !string(&j,verified,sizeof(verified))) return false; got_name=true; }
    else if(!value(&j,0)) return false;
  } while(take(&j,','));
  if(!take(&j,'}')) return false;
  whitespace(&j);
  if(j.p!=j.end || !got_id || !got_name || strlen(id)!=32 || !auth_name_valid(verified,strlen(verified)) || strlen(claimed)!=strlen(verified)) return false;
  for(size_t i=0;verified[i];i++) if(lower(verified[i])!=lower(claimed[i])) return false;
  uint8_t result[16]; unsigned nonzero=0;
  for(unsigned i=0;i<16;i++) { int a=hex(id[2*i]),b=hex(id[2*i+1]); if(a<0 || b<0) return false; result[i]=(uint8_t)(a*16+b); nonzero|=result[i]; }
  if(!nonzero) return false;
  memcpy(uuid,result,16); memcpy(name,verified,17); return true;
}
#endif
