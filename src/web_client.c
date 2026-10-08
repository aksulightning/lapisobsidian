/* Optional HTTP/WebSocket listener and transport for the protocol-772 client.
 * RFC 6455 framing. No proxy, third-party assets, TLS library or worker thread.
 * All reads/writes are nonblocking; bounded queues isolate slow browsers. */
#include "web_client.h"
#if defined(LAPIS_OBSIDIAN_WEB_CLIENT) && LAPIS_OBSIDIAN_WEB_CLIENT == 1
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/inet.h"
#elif defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include "globals.h"
#include "packet_input.h"
#include "web_assets.h"

#define WEB_INPUT (PACKET_INPUT_LIMIT + 14u)
#define WEB_OUTPUT (8u * 1024u * 1024u)
#define WEB_STAGE 16384u
#define WEB_TIMEOUT INT64_C(15000000)
enum { UNKNOWN, HTTP, WEBSOCKET, CLOSING };
typedef struct {
  bool used, failed, fragmented;
  int fd, state;
  int64_t started, partial_since, output_since, now;
  size_t raw_size, data_size, message_size, staged, out_pos, out_size, capacity;
  unsigned char raw[WEB_INPUT], data[WEB_INPUT], stage[WEB_STAGE];
  unsigned char *out;
} WebClient;
int web_client_listen(const char *address, uint16_t port) {
  int fd = (int)socket(AF_INET,SOCK_STREAM,0);
  if (fd < 0) return -1;
  int opt = 1;
  struct sockaddr_in bind_address;
  memset(&bind_address,0,sizeof(bind_address));
  bind_address.sin_family = AF_INET;
  bind_address.sin_addr.s_addr = inet_addr(address); /* Configuration is validated. */
  bind_address.sin_port = htons(port);
#ifdef _WIN32
  if (setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(const char *)&opt,sizeof(opt)) < 0) goto fail;
#else
  if (setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt)) < 0) goto fail;
#endif
  if (bind(fd,(struct sockaddr *)&bind_address,sizeof(bind_address)) < 0 || listen(fd,5) < 0) goto fail;
#ifdef _WIN32
  u_long nonblocking = 1;
  if (ioctlsocket(fd,FIONBIO,&nonblocking) != 0) goto fail;
#else
  int flags = fcntl(fd,F_GETFL,0);
  if (flags < 0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0) goto fail;
#endif
  return fd;
fail:
#ifdef _WIN32
  { int error = WSAGetLastError(); closesocket(fd); WSASetLastError(error); }
#else
  { int error = errno; close(fd); errno = error; }
#endif
  return -1;
}
static WebClient clients[MAX_PLAYERS];
static WebClient *lookup(int fd) {
  for (unsigned i = 0; i < MAX_PLAYERS; i++)
    if (clients[i].used && clients[i].fd == fd) return &clients[i];
  return NULL;
}
void web_client_forget(int fd) {
  WebClient *c = lookup(fd);
  if (c) { free(c->out); memset(c,0,sizeof(*c)); }
}
void web_client_reset(int fd, int64_t now) {
  web_client_forget(fd);
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (!clients[i].used) {
    clients[i].used = true; clients[i].fd = fd;
    clients[i].started = clients[i].now = now; return;
  }
}
int web_client_active(int fd) {
  WebClient *c = lookup(fd);
  return c && (c->state == WEBSOCKET || c->state == CLOSING);
}
static bool again(void) {
#ifdef _WIN32
  int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
static int socket_read(int fd, void *p, size_t n, int flags) {
#ifdef _WIN32
  return recv(fd,(char *)p,(int)n,flags);
#else
  return (int)recv(fd,p,n,flags);
#endif
}
static bool queue(WebClient *c, const void *p, size_t n) {
  if (c->failed) return false;
  size_t pending = c->out_size-c->out_pos;
  if (n > WEB_OUTPUT-pending) { c->failed = true; return false; }
  if (c->out_pos) { memmove(c->out,c->out+c->out_pos,pending); c->out_size = pending; c->out_pos = 0; }
  if (c->out_size+n > c->capacity) {
    size_t size = c->capacity ? c->capacity : WEB_STAGE;
    while (size < c->out_size+n) size *= 2;
    unsigned char *out = realloc(c->out,size);
    if (!out) { c->failed = true; return false; }
    c->out = out; c->capacity = size;
  }
  if (!pending) c->output_since = c->now;
  memcpy(c->out+c->out_size,p,n); c->out_size += n;
  return true;
}
static bool frame(WebClient *c, unsigned opcode, const void *p, size_t n) {
  unsigned char header[4] = {(unsigned char)(128u|opcode),0,0,0};
  size_t h = 2;
  if (n < 126) header[1] = (unsigned char)n;
  else { header[1] = 126; header[2] = (unsigned char)(n>>8); header[3] = (unsigned char)n; h = 4; }
  return queue(c,header,h) && queue(c,p,n);
}
static bool stage_flush(WebClient *c) {
  if (!c->staged) return true;
  bool ok = frame(c,2,c->stage,c->staged); c->staged = 0; return ok;
}
ssize_t web_client_send(int fd, const void *buffer, size_t size) {
  WebClient *c = lookup(fd);
  if (!c || c->failed || c->state != WEBSOCKET) return -1;
  const unsigned char *p = buffer; size_t remaining = size;
  while (remaining) {
    size_t n = WEB_STAGE-c->staged;
    if (n > remaining) n = remaining;
    memcpy(c->stage+c->staged,p,n); c->staged += n; p += n; remaining -= n;
    if (c->staged == WEB_STAGE && !stage_flush(c)) return -1;
  }
  return (ssize_t)size;
}
static int flush(WebClient *c) {
  if (!stage_flush(c)) return -1;
  if (c->out_pos == c->out_size) return 1;
  if (c->now-c->output_since >= WEB_TIMEOUT) return -1;
  size_t n = c->out_size-c->out_pos;
  if (n > 65536) n = 65536;
#ifdef _WIN32
  int sent = send(c->fd,(const char *)c->out+c->out_pos,(int)n,0);
#else
  int sent = (int)send(c->fd,c->out+c->out_pos,n,MSG_NOSIGNAL);
#endif
  if (sent < 0) return again() ? 0 : -1;
  if (!sent) return -1;
  c->out_pos += (size_t)sent; c->output_since = c->now;
  if (c->out_pos != c->out_size) return 0;
  c->out_pos = c->out_size = 0; return 1;
}

/* SHA-1 is used only for the public WebSocket handshake, never authentication.
 * Its input here is exactly a 24-byte key plus the RFC's 36-byte GUID. */
static uint32_t rotate(uint32_t n, unsigned bits) { return (n<<bits)|(n>>(32-bits)); }
static void accept_key(const char *key, char output[29]) {
  unsigned char bytes[128] = {0}, digest[20];
  memcpy(bytes,key,24); memcpy(bytes+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
  bytes[60] = 128; bytes[126] = 1; bytes[127] = 224; /* 60 * 8 */
  uint32_t hash[5] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
  for (unsigned block = 0; block < 2; block++) {
    uint32_t w[80];
    for (unsigned i = 0; i < 16; i++) {
      const unsigned char *p = bytes+block*64+i*4;
      w[i] = ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
    }
    for (unsigned i = 16; i < 80; i++) w[i] = rotate(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=hash[0], b=hash[1], d=hash[3], e=hash[4], v=hash[2];
    for (unsigned i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b&v)|(~b&d); k = 0x5a827999; }
      else if (i < 40) { f = b^v^d; k = 0x6ed9eba1; }
      else if (i < 60) { f = (b&v)|(b&d)|(v&d); k = 0x8f1bbcdc; }
      else { f = b^v^d; k = 0xca62c1d6; }
      uint32_t next = rotate(a,5)+f+e+k+w[i]; e=d; d=v; v=rotate(b,30); b=a; a=next;
    }
    hash[0]+=a; hash[1]+=b; hash[2]+=v; hash[3]+=d; hash[4]+=e;
  }
  for (unsigned i = 0; i < 20; i++) digest[i] = (unsigned char)(hash[i/4]>>(24-8*(i%4)));
  const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (unsigned i = 0, j = 0; i < 20; i += 3, j += 4) {
    uint32_t n = (uint32_t)digest[i]<<16;
    if (i+1 < 20) n |= (uint32_t)digest[i+1]<<8;
    if (i+2 < 20) n |= digest[i+2];
    output[j]=alphabet[n>>18]; output[j+1]=alphabet[(n>>12)&63];
    output[j+2]=alphabet[(n>>6)&63]; output[j+3]=i+2 < 20 ? alphabet[n&63] : '=';
  }
  output[28] = 0;
}
static bool equal(const char *a, const char *b) {
  while (*a && *b) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
  return *a == *b;
}
static bool token(const char *value, const char *wanted) {
  char copy[256];
  if (strlen(value) >= sizeof(copy)) return false;
  strcpy(copy,value);
  char *part = copy;
  while (part) {
    char *next = strchr(part,','); if (next) *next++ = 0;
    while (*part == ' ' || *part == '\t') part++;
    char *end = part+strlen(part); while (end > part && (end[-1] == ' ' || end[-1] == '\t')) *--end=0;
    if (equal(part,wanted)) return true;
    part = next;
  }
  return false;
}
static bool header_value(char **dest, char *value) {
  if (*dest) return false; /* Reject ambiguous duplicate handshake fields. */
  *dest = value; return true;
}
static int response(WebClient *c, const char *status, const char *type, const void *body, size_t size) {
  char header[512];
  int n = snprintf(header,sizeof(header),"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; connect-src 'self'; frame-ancestors 'none'\r\n\r\n",status,type,size);
  c->state = CLOSING;
  if (n < 0 || (size_t)n >= sizeof(header) || !queue(c,header,(size_t)n) || !queue(c,body,size)) return -1;
  return 0;
}
static int http(WebClient *c) {
  if (c->raw_size > 4096 || memchr(c->raw,0,c->raw_size)) return -1;
  c->raw[c->raw_size] = 0;
  char *request = (char *)c->raw, *end = strstr(request,"\r\n\r\n");
  if (!end) return 0;
  size_t consumed = (size_t)(end-request)+4;
  char *line = strstr(request,"\r\n"); *line = 0; line += 2;
  char *host=NULL, *origin=NULL, *key=NULL, *upgrade=NULL, *connection=NULL, *version=NULL;
  while (line < end) {
    char *next = strstr(line,"\r\n"); if (!next) return -1; *next=0;
    char *colon = strchr(line,':'); if (!colon) return -1; *colon++=0;
    while (*colon == ' ' || *colon == '\t') colon++;
    char *tail = colon+strlen(colon); while (tail > colon && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail=0;
    char **field = NULL;
    if (equal(line,"Host")) field=&host;
    else if (equal(line,"Origin")) field=&origin;
    else if (equal(line,"Sec-WebSocket-Key")) field=&key;
    else if (equal(line,"Sec-WebSocket-Version")) field=&version;
    else if (equal(line,"Upgrade")) field=&upgrade;
    else if (equal(line,"Connection")) field=&connection;
    if (field && !header_value(field,colon)) return -1;
    line=next+2;
  }
  if (!host || !*host) return -1;
  if (!strcmp(request,"GET /ws HTTP/1.1")) {
    const char *authority = origin && !strncmp(origin,"http://",7) ? origin+7 :
                            origin && !strncmp(origin,"https://",8) ? origin+8 : NULL;
    if (!authority || !equal(authority,host)) return response(c,"403 Forbidden","text/plain","Same-origin WebSocket required.\n",31);
    if (!key || strlen(key) != 24 || strcmp(key+22,"==") || !strchr("AQgw",key[21]) ||
        !version || strcmp(version,"13") || !upgrade || !equal(upgrade,"websocket") ||
        !connection || !token(connection,"upgrade")) return -1;
    for (unsigned i=0;i<22;i++) if (!isalnum((unsigned char)key[i]) && key[i]!='+' && key[i]!='/') return -1;
    char accepted[29], reply[192]; accept_key(key,accepted);
    int n=snprintf(reply,sizeof(reply),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accepted);
    if (n < 0 || (size_t)n >= sizeof(reply) || !queue(c,reply,(size_t)n)) return -1;
    c->state=WEBSOCKET;
    memmove(c->raw,c->raw+consumed,c->raw_size-consumed); c->raw_size-=consumed;
    c->partial_since = c->raw_size ? c->now : 0;
    return 1;
  }
  struct Asset { const char *request, *type; const unsigned char *data; size_t size; };
  static const struct Asset assets[] = {
    {"GET / HTTP/1.1","text/html; charset=utf-8",web_index_html,sizeof(web_index_html)},
    {"GET /style.css HTTP/1.1","text/css; charset=utf-8",web_style_css,sizeof(web_style_css)},
    {"GET /protocol.mjs HTTP/1.1","text/javascript; charset=utf-8",web_protocol_mjs,sizeof(web_protocol_mjs)},
    {"GET /renderer.mjs HTTP/1.1","text/javascript; charset=utf-8",web_renderer_mjs,sizeof(web_renderer_mjs)},
    {"GET /catalog.mjs HTTP/1.1","text/javascript; charset=utf-8",web_catalog_mjs,sizeof(web_catalog_mjs)},
    {"GET /client.mjs HTTP/1.1","text/javascript; charset=utf-8",web_client_mjs,sizeof(web_client_mjs)}
  };
  for (unsigned i=0;i<sizeof(assets)/sizeof(assets[0]);i++) if (!strcmp(request,assets[i].request))
    return response(c,"200 OK",assets[i].type,assets[i].data,assets[i].size);
  return response(c,"404 Not Found","text/plain","Not found.\n",11);
}
static int websocket(WebClient *c) {
  size_t used=0;
  while (c->raw_size-used >= 2) {
    unsigned char *p=c->raw+used; unsigned op=p[0]&15u; bool fin=(p[0]&128u)!=0;
    size_t n=p[1]&127u, header=6;
    if ((p[0]&112u) || !(p[1]&128u) || n==127) return -1;
    if (n==126) {
      if (c->raw_size-used < 4) break;
      n=(size_t)p[2]*256+p[3]; header=8;
      if (n<126) return -1;
    }
    if (n > PACKET_INPUT_LIMIT+3u || (op>=8 && (!fin || n>125))) return -1;
    if (c->raw_size-used < header+n) break;
    for (size_t i=0;i<n;i++) p[header+i]^=p[header-4+i%4];
    if (op==8) {
      if (n==1) return -1;
      /* Reply with a normal close, never reflect unvalidated close text. */
      const unsigned char normal[2]={3,232};
      if (!stage_flush(c) || !frame(c,8,normal,2)) return -1;
      c->state=CLOSING; c->raw_size=0; return 0;
    } else if (op==9) {
      if (!stage_flush(c) || !frame(c,10,p+header,n)) return -1;
    } else if (op!=10) {
      if ((op!=2 && op!=0) || (op==0 && !c->fragmented) || (op==2 && c->fragmented)) return -1;
      if (n > PACKET_INPUT_LIMIT+3u-c->message_size || n > sizeof(c->data)-c->data_size) return -1;
      memcpy(c->data+c->data_size,p+header,n); c->data_size+=n;
      c->message_size+=n; c->fragmented=!fin;
      if (fin) c->message_size=0;
    }
    used+=header+n;
  }
  if (used) { memmove(c->raw,c->raw+used,c->raw_size-used); c->raw_size-=used; }
  if (!c->raw_size && !c->fragmented) c->partial_since=0;
  return 1;
}
int web_client_poll(int fd, int64_t now) {
  WebClient *c=lookup(fd); if (!c) return 1; c->now=now;
  if (c->failed || now<c->started) return -1;
  int sent=flush(c); if (sent<0) return -1;
  if (c->state==CLOSING) return sent ? -1 : 0;
  if ((c->state!=WEBSOCKET && now-c->started>=WEB_TIMEOUT) ||
      (c->partial_since && now-c->partial_since>=WEB_TIMEOUT)) return -1;
  if (c->state==UNKNOWN) {
    char peek[4]; int n=socket_read(fd,peek,4,MSG_PEEK);
    if (n<0) return again() ? 0 : -1;
    if (!n) return -1;
    if (memcmp(peek,"GET ",(size_t)n)) return -1;
    if (n<4) return 0;
    c->state=HTTP;
  }
  if (c->raw_size==sizeof(c->raw)-1) return -1;
  int n=socket_read(fd,c->raw+c->raw_size,sizeof(c->raw)-1-c->raw_size,0);
  if (!n) return -1;
  if (n<0 && !again()) return -1;
  if (n>0) {
    if (!c->partial_since) c->partial_since=now;
    c->raw_size+=(size_t)n;
  }
  if (c->state==HTTP) return http(c);
  return websocket(c);
}
ssize_t web_client_receive(int fd, void *buffer, size_t size, int flags) {
  WebClient *c=lookup(fd);
  if (!c || c->failed) return -1;
  if (!c->data_size) {
    errno=EAGAIN;
#ifdef _WIN32
    WSASetLastError(WSAEWOULDBLOCK);
#endif
    return -1;
  }
  if (size>c->data_size) size=c->data_size;
  memcpy(buffer,c->data,size);
  if (!(flags&MSG_PEEK)) { memmove(c->data,c->data+size,c->data_size-size); c->data_size-=size; }
  return (ssize_t)size;
}
#endif
