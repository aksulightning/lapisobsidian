/* LapisCube HTTP/WebSocket transport. Original code, repository license.
 * Compiled out completely by default. No threads, proxy destinations or TLS
 * dependency: deploy HTTPS/WSS through a reverse proxy for internet use.
 */
#include "webclient.h"
#if defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1
#ifdef ESP_PLATFORM
#error The optional web host requires a desktop server build
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#ifdef _WIN32
#include <winsock2.h>
#define WC_CLOSE closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#define WC_CLOSE close
#endif

#define WC_PEERS 8
#define WC_INPUT 65536u
#define WC_OUTPUT (4u*1024u*1024u)
#define WC_HEADER 8192u
#define WC_TIMEOUT INT64_C(15000000)
typedef struct {
  int fd, state, owned, dead, fragment;
  int64_t progress, write_progress, partial_since;
  size_t input_size, app_size, begin, end, wire_size, wire_sent;
  unsigned char input[WC_INPUT+14], app[WC_INPUT], wire[65546];
  unsigned char *output;
  FILE *file;
} WebPeer;
static WebPeer peers[WC_PEERS];
static int listener = -1;
static const char *root;
static int64_t clock_now;

static int again(void) {
#ifdef _WIN32
  int e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINTR;
#else
  return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR;
#endif
}
static int nonblock(int fd) {
#ifdef _WIN32
  u_long one=1; return ioctlsocket(fd,FIONBIO,&one)==0;
#else
  int f=fcntl(fd,F_GETFL,0); return f>=0 && fcntl(fd,F_SETFL,f|O_NONBLOCK)>=0;
#endif
}
static WebPeer *peer(int fd) {
  for (int i=0;i<WC_PEERS;i++) if (peers[i].state && peers[i].fd==fd) return &peers[i];
  return NULL;
}
static void release(WebPeer *p, int close_fd) {
  if (p->file) fclose(p->file);
  free(p->output);
  if (close_fd) WC_CLOSE(p->fd);
  memset(p,0,sizeof(*p));
}
static void fail(WebPeer *p) {
  if (p->owned) { p->dead=1; shutdown(p->fd,2); }
  else release(p,1);
}
int webclient_has(int fd) { return peer(fd)!=NULL; }
int webclient_disconnect(int fd) {
  WebPeer *p=peer(fd);if(!p)return 0;
  p->owned=0;
  if(p->dead) release(p,1);
  else { p->state=4;p->input_size=p->app_size=0; }
  return 1; /* Transport retains the descriptor until pending replies drain. */
}

/* SHA-1 is used only for RFC 6455's public handshake, never authentication. */
static uint32_t rol(uint32_t n, unsigned b) { return (n<<b)|(n>>(32-b)); }
static void accept_key(const char *key, char out[29]) {
  static const char base64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  unsigned char data[128]={0}, digest[20];
  uint32_t h[5]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
  memcpy(data,key,24); memcpy(data+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
  data[60]=128; data[126]=1; data[127]=224; /* 60 bytes, 480 bits */
  for (int off=0;off<128;off+=64) {
    uint32_t w[80],a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for (int i=0;i<16;i++) { const unsigned char *s=data+off+i*4; w[i]=(uint32_t)s[0]<<24|(uint32_t)s[1]<<16|(uint32_t)s[2]<<8|s[3]; }
    for (int i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    for (int i=0;i<80;i++) {
      uint32_t f,k;
      if (i<20) { f=(b&c)|(~b&d); k=0x5a827999u; }
      else if (i<40) { f=b^c^d; k=0x6ed9eba1u; }
      else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdcu; }
      else { f=b^c^d; k=0xca62c1d6u; }
      uint32_t t=rol(a,5)+f+e+k+w[i]; e=d; d=c; c=rol(b,30); b=a; a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
  }
  for (int i=0;i<20;i++) digest[i]=(unsigned char)(h[i/4]>>(24-(i%4)*8));
  for (int i=0,j=0;i<20;i+=3) {
    uint32_t n=(uint32_t)digest[i]<<16;
    if (i+1<20) n|=(uint32_t)digest[i+1]<<8;
    if (i+2<20) n|=digest[i+2];
    out[j++]=base64[n>>18]; out[j++]=base64[(n>>12)&63];
    out[j++]=base64[(n>>6)&63]; out[j++]=i+2<20?base64[n&63]:'=';
  }
  out[28]=0;
}
static int equal(const char *a,const char *b) {
  while (*a && *b) if (tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0;
  return !*a && !*b;
}
static int token(const char *list,const char *value) {
  char word[80];
  while (*list) {
    size_t n=0; while (*list==' ' || *list==',') list++;
    while (*list && *list!=',') { if(n+1>=sizeof(word)) return 0; word[n++]=*list++; }
    while(n && word[n-1]==' ') n--;
    word[n]=0;
    if(equal(word,value)) return 1;
  }
  return 0;
}
static void response(WebPeer *p, int status, const char *reason) {
  p->wire_size=(size_t)snprintf((char *)p->wire,sizeof(p->wire),
    "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",status,reason);
  p->wire_sent=0;p->state=2;p->write_progress=clock_now;
}
static const char *mime(const char *path) {
  const char *ext=strrchr(path,'.'); if(!ext) return NULL;
  if(!strcmp(ext,".html")) return "text/html; charset=utf-8";
  if(!strcmp(ext,".js")) return "text/javascript; charset=utf-8";
  if(!strcmp(ext,".css")) return "text/css; charset=utf-8";
  if(!strcmp(ext,".wasm")) return "application/wasm";
  if(!strcmp(ext,".json")) return "application/json";
  if(!strcmp(ext,".webmanifest")) return "application/manifest+json";
  if(!strcmp(ext,".png")) return "image/png";
  if(!strcmp(ext,".svg")) return "image/svg+xml";
  if(!strcmp(ext,".wav")) return "audio/wav";
  if(!strcmp(ext,".zip")) return "application/zip";
  if(!strcmp(ext,".txt") || !strcmp(ext,".md")) return "text/plain; charset=utf-8";
  return NULL;
}
static int safe_path(const char *s) {
  if(*s++!='/') return 0;
  if(!*s) return 1;
  for(const char *p=s;*p;p++) {
    if(*p=='.' && (p==s || p[-1]=='/')) return 0;
    if(!isalnum((unsigned char)*p) && *p!='/' && *p!='.' && *p!='-' && *p!='_') return 0;
  }
  return 1;
}
static void http(WebPeer *p) {
  char request[WC_HEADER+1],path[256],verb[8],version[16];
  char *host=NULL,*origin=NULL,*key=NULL,*upgrade=NULL,*connection=NULL,*protocol=NULL,*ws_version=NULL;
  if(p->input_size>WC_HEADER) { response(p,431,"Request Header Fields Too Large"); return; }
  memcpy(request,p->input,p->input_size);request[p->input_size]=0;
  if(memchr(request,0,p->input_size)) { response(p,400,"Bad Request"); return; }
  char *end=strstr(request,"\r\n\r\n");if(!end) return;
  size_t consumed=(size_t)(end-request)+4;
  char *line=strstr(request,"\r\n");if(!line) { response(p,400,"Bad Request"); return; }
  *line=0;
  int valid=sscanf(request,"%7s %255s %15s",verb,path,version)==3 && !strcmp(version,"HTTP/1.1");
  line+=2;
  while(valid && line<end) {
    char *next=strstr(line,"\r\n"),*colon=strchr(line,':');
    if(!next || !colon || colon>=next) { valid=0;break; }
    *next=0;*colon++=0;while(*colon==' ' || *colon=='\t') colon++;
    char *tail=next;while(tail>colon && (tail[-1]==' ' || tail[-1]=='\t')) *--tail=0;
    char **field=NULL;
    if(equal(line,"Host")) field=&host;
    else if(equal(line,"Origin")) field=&origin;
    else if(equal(line,"Upgrade")) field=&upgrade;
    else if(equal(line,"Connection")) field=&connection;
    else if(equal(line,"Sec-WebSocket-Key")) field=&key;
    else if(equal(line,"Sec-WebSocket-Version")) field=&ws_version;
    else if(equal(line,"Sec-WebSocket-Protocol")) field=&protocol;
    else if(equal(line,"Transfer-Encoding") || (equal(line,"Content-Length") && strcmp(colon,"0"))) valid=0;
    if(field) { if(*field) valid=0;*field=colon; }
    line=next+2;
  }
  if(!valid || !host || !*host || !safe_path(path)) { response(p,400,"Bad Request");return; }
  if(strcmp(verb,"GET") && strcmp(verb,"HEAD")) { response(p,405,"Method Not Allowed");return; }
  if(!strcmp(path,"/ws")) {
    /* Browsers must come from this origin (or the operator's explicit TLS origin).
     * This is not account authentication: the game keeps its offline login rules. */
    char expected[300];const char *configured=getenv("LAPIS_WEB_ORIGIN");
    snprintf(expected,sizeof(expected),"http://%s",host);
    if(!origin || strcmp(origin,configured?configured:expected)) { response(p,403,"Forbidden");return; }
    if(strcmp(verb,"GET") || !upgrade || !equal(upgrade,"websocket") || !connection || !token(connection,"upgrade") ||
       !key || strlen(key)!=24 || key[22]!='=' || key[23]!='=' || !ws_version || strcmp(ws_version,"13") ||
       !protocol || !token(protocol,"LapisCube")) { response(p,400,"Bad Request");return; }
    for(int i=0;i<22;i++) if(!isalnum((unsigned char)key[i]) && key[i]!='+' && key[i]!='/') { response(p,400,"Bad Request");return; }
    p->output=malloc(WC_OUTPUT);if(!p->output) { response(p,503,"Service Unavailable");return; }
    char accept[29];accept_key(key,accept);
    p->wire_size=(size_t)snprintf((char *)p->wire,sizeof(p->wire),
      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: LapisCube\r\n\r\n",accept);
    p->state=3;p->wire_sent=0;p->write_progress=clock_now;
    memmove(p->input,p->input+consumed,p->input_size-consumed);p->input_size-=consumed;
    return;
  }
  if(upgrade || consumed!=p->input_size) { response(p,400,"Bad Request");return; }
  if(!strcmp(path,"/")) strcpy(path,"/index.html");
  const char *type=mime(path);char filename[1024];
  if(!type || snprintf(filename,sizeof(filename),"%s%s",root,path)>=(int)sizeof(filename)) { response(p,404,"Not Found");return; }
  p->file=fopen(filename,"rb");
  if(!p->file) { response(p,404,"Not Found");return; }
  if(fseek(p->file,0,SEEK_END)) { fail(p);return; }
  long size=ftell(p->file);if(size<0 || size>32*1024*1024) { fail(p);return; }
  rewind(p->file);
  p->wire_size=(size_t)snprintf((char *)p->wire,sizeof(p->wire),
    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n\r\n",type,size);
  p->state=2;p->wire_sent=0;p->write_progress=clock_now;
  if(!strcmp(verb,"HEAD")) { fclose(p->file);p->file=NULL; }
}
static void frames(WebPeer *p) {
  while(p->input_size>=2 && !p->dead) {
    unsigned char *s=p->input;unsigned op=s[0]&15,fin=s[0]&128;
    size_t n=s[1]&127,h=2;
    if((s[0]&112) || !(s[1]&128) || (op!=0 && op!=2 && op!=8 && op!=9 && op!=10)) { fail(p);return; }
    if(n==126) { if(p->input_size<4) return;n=(size_t)s[2]*256+s[3];h=4;if(n<126) { fail(p);return; } }
    else if(n==127) { /* Browser writes are <= 32 KiB; reject unbounded frames. */ fail(p);return; }
    if((op>=8 && (!fin || n>125)) || (op==0 && !p->fragment) || (op==2 && p->fragment)) { fail(p);return; }
    h+=4;if(p->input_size<h+n) return;
    if(op<8 && n>WC_INPUT-p->app_size) return; /* Apply TCP backpressure. */
    if(op==9 && p->wire_size) return;
    for(size_t i=0;i<n;i++) s[h+i]^=s[h-4+(i&3)];
    if(op==8) { fail(p);return; }
    if(op==9) {
      p->wire[0]=138;p->wire[1]=(unsigned char)n;memcpy(p->wire+2,s+h,n);p->wire_size=n+2;p->wire_sent=0;p->write_progress=clock_now;
    } else if(op<8) {
      memcpy(p->app+p->app_size,s+h,n);p->app_size+=n;p->fragment=!fin;
    }
    memmove(s,s+h+n,p->input_size-h-n);p->input_size-=h+n;
    p->partial_since=p->input_size?clock_now:0;
  }
}
int webclient_recv(int fd,void *data,size_t size,int peek) {
  WebPeer *p=peer(fd);if(!p || p->dead) return 0;
  if(size>p->app_size) size=p->app_size;
  if(!size) {
#ifdef _WIN32
    WSASetLastError(WSAEWOULDBLOCK);
#else
    errno=EAGAIN;
#endif
    return -1;
  }
  memcpy(data,p->app,size);
  if(!peek) { memmove(p->app,p->app+size,p->app_size-size);p->app_size-=size; }
  return (int)size;
}
int webclient_send(int fd,const void *data,size_t size) {
  WebPeer *p=peer(fd);if(!p || p->dead || size>WC_OUTPUT-(p->end-p->begin)) { if(p) fail(p);return -1; }
  if(size>WC_OUTPUT-p->end) { memmove(p->output,p->output+p->begin,p->end-p->begin);p->end-=p->begin;p->begin=0; }
  if(!p->wire_size && p->begin==p->end) p->write_progress=clock_now;
  memcpy(p->output+p->end,data,size);p->end+=size;return (int)size;
}
int webclient_accept(void) {
  for(int i=0;i<WC_PEERS;i++) if(peers[i].state==3 && !peers[i].owned && !peers[i].dead) {
    peers[i].owned=1;return peers[i].fd;
  }
  return -1;
}
int webclient_start(void) {
  const char *port=getenv("LAPIS_WEB_PORT");char *end;long number=port?strtol(port,&end,10):8080;
  if(number<1 || number>65535 || (port && (!*port || *end))) return 0;
  root=getenv("LAPIS_WEB_ROOT");if(!root) root="webclient";
  char filename[1024];snprintf(filename,sizeof(filename),"%s/index.html",root);
  FILE *f=fopen(filename,"rb");if(!f) { fprintf(stderr,"Missing web bundle: %s\n",filename);return 0; }fclose(f);
  listener=(int)socket(AF_INET,SOCK_STREAM,0);if(listener<0) return 0;
  int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof(one));
  struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((unsigned short)number);address.sin_addr.s_addr=htonl(INADDR_ANY);
  if(bind(listener,(struct sockaddr *)&address,sizeof(address)) || listen(listener,8) || !nonblock(listener)) { WC_CLOSE(listener);listener=-1;return 0; }
  printf("LapisCube web client: http://localhost:%ld/ (root %s)\n",number,root);return 1;
}
void webclient_poll(int64_t now) {
  if(listener<0) return;
  clock_now=now;
  for(int i=0;i<WC_PEERS;i++) if(!peers[i].state) {
    int fd=(int)accept(listener,NULL,NULL);if(fd<0) break;
    if(!nonblock(fd)) { WC_CLOSE(fd);break; }
    peers[i].fd=fd;peers[i].state=1;peers[i].progress=now;break;
  }
  for(int i=0;i<WC_PEERS;i++) {
    WebPeer *p=&peers[i];if(!p->state || p->dead) continue;
    /* Pending HTTP and stalled output are bounded, including unclaimed upgrades. */
    if(now<p->progress || ((p->state!=3 || !p->owned) && now-p->progress>WC_TIMEOUT) ||
       ((p->wire_size || p->end>p->begin) && now-p->write_progress>WC_TIMEOUT) ||
       (p->partial_since && now-p->partial_since>WC_TIMEOUT)) { fail(p);continue; }
    if((p->state==1 || p->state==3) && p->input_size<sizeof(p->input) && p->app_size<WC_INPUT) {
      int n=recv(p->fd,(char *)p->input+p->input_size,(int)(sizeof(p->input)-p->input_size),0);
      if(n>0) {
        if(!p->input_size && p->state==3) p->partial_since=now;
        p->input_size+=(size_t)n;
      }
      else if(!n || !again()) { fail(p);continue; }
    }
    if(p->state==1) http(p);
    if(!p->state) continue;
    if(p->state==3 && p->owned) frames(p);
    if(!p->state || p->dead) continue;
    if(!p->wire_size) {
      if(p->state==2 && p->file) {
        p->wire_size=fread(p->wire,1,65536,p->file);p->wire_sent=0;
        if(!p->wire_size) { release(p,1);continue; }
      } else if(p->state==2) { release(p,1);continue; }
      else if((p->state==3 || p->state==4) && p->end>p->begin) {
        size_t n=p->end-p->begin;if(n>65535) n=65535;
        p->wire[0]=130;size_t h=2;
        if(n<126) p->wire[1]=(unsigned char)n;
        else { p->wire[1]=126;p->wire[2]=(unsigned char)(n>>8);p->wire[3]=(unsigned char)n;h=4; }
        memcpy(p->wire+h,p->output+p->begin,n);p->begin+=n;
        if(p->begin==p->end) p->begin=p->end=0;
        p->wire_size=n+h;p->wire_sent=0;
      } else if(p->state==4) {
        p->wire[0]=136;p->wire[1]=2;p->wire[2]=3;p->wire[3]=232;
        p->wire_size=4;p->wire_sent=0;p->state=5;
      } else if(p->state==5) { release(p,1);continue; }
    }
    if(p->wire_size) {
#ifdef _WIN32
      int flags=0;
#else
      int flags=MSG_NOSIGNAL;
#endif
      int n=send(p->fd,(const char *)p->wire+p->wire_sent,(int)(p->wire_size-p->wire_sent),flags);
      if(n>0) { p->wire_sent+=(size_t)n;p->progress=p->write_progress=now;if(p->wire_sent==p->wire_size) p->wire_size=p->wire_sent=0; }
      else if(!n || !again()) fail(p);
    }
  }
}
void webclient_stop(void) {
  for(int i=0;i<WC_PEERS;i++) if(peers[i].state) release(&peers[i],1);
  if(listener>=0) WC_CLOSE(listener);
  listener=-1;
}
#endif
