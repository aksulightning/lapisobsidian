/* Transport regression tests: real nonblocking sockets, no browser/runtime. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "web_client.h"
#include "packet_input.h"

static int pair[2];
static int64_t now=1;
static void connect_client(void) {
  assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
  for(unsigned i=0;i<2;i++) assert(fcntl(pair[i],F_SETFL,O_NONBLOCK)==0);
  web_client_reset(pair[1],now);
}
static void finish(void) { web_client_forget(pair[1]); close(pair[0]); close(pair[1]); now++; }
static void put(const void *p,size_t n) { assert(send(pair[0],p,n,0)==(ssize_t)n); }
static size_t drain(unsigned char *p,size_t size) {
  size_t n=0; ssize_t result;
  while(n<size && (result=recv(pair[0],p+n,size-n,0))>0) n+=(size_t)result;
  return n;
}
static void upgrade(const char *origin) {
  char request[512];
  int n=snprintf(request,sizeof(request),"GET /ws HTTP/1.1\r\nHost: localhost:25565\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nOrigin: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",origin);
  put(request,(size_t)n);
}
static void opened(void) {
  connect_client(); upgrade("http://localhost:25565");
  assert(web_client_poll(pair[1],now)==1);
  assert(web_client_poll(pair[1],now)==1);
  unsigned char reply[512]={0}; size_t n=drain(reply,sizeof(reply)-1);
  assert(n && strstr((char *)reply,"101 Switching Protocols"));
  assert(strstr((char *)reply,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}
static size_t masked(unsigned char *out,unsigned op,const unsigned char *data,size_t size) {
  assert(size<126); out[0]=(unsigned char)op; out[1]=(unsigned char)(128u|size);
  const unsigned char mask[4]={3,11,29,41}; memcpy(out+2,mask,4);
  for(size_t i=0;i<size;i++)out[6+i]=data[i]^mask[i%4];
  return size+6;
}
int main(void) {
  unsigned char frame[256], reply[2048], data[32]; ssize_t got;
  connect_client(); put("GE",2); assert(web_client_poll(pair[1],now)==0);
  const char get[]="T / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  put(get,sizeof(get)-1); assert(web_client_poll(pair[1],now)==0);
  assert(web_client_poll(pair[1],now)==-1);
  memset(reply,0,sizeof(reply)); assert(drain(reply,sizeof(reply)-1)>0);
  assert(strstr((char *)reply,"200 OK")); assert(strstr((char *)reply,"Content-Security-Policy:")); finish();

  connect_client(); const unsigned char native[]={0x47,0,0x84,6}; put(native,sizeof(native));
  assert(web_client_poll(pair[1],now)==1 && !web_client_active(pair[1]));
  assert(recv(pair[1],data,sizeof(native),0)==(ssize_t)sizeof(native)); assert(!memcmp(data,native,sizeof(native))); finish();

  connect_client(); upgrade("http://attacker.example"); assert(web_client_poll(pair[1],now)==0);
  assert(web_client_poll(pair[1],now)==-1); memset(reply,0,sizeof(reply)); drain(reply,sizeof(reply)-1);
  assert(strstr((char *)reply,"403 Forbidden")); finish();

  opened(); PacketInput input; packet_input_reset(&input,pair[1]);
  /* One Minecraft packet spread across TCP reads and fragmented WS frames. */
  const unsigned char prefix[]={3,0x1b}, payload[]={7,9};
  size_t n=masked(frame,2,prefix,sizeof(prefix)); put(frame,3);
  assert(web_client_poll(pair[1],now)==1); assert(packet_input_poll(&input,now)==0);
  put(frame+3,n-3); assert(web_client_poll(pair[1],now)==1); assert(packet_input_poll(&input,now)==0);
  const unsigned char ping[]={4,5}; n=masked(frame,0x89,ping,sizeof(ping)); put(frame,n);
  assert(web_client_poll(pair[1],now)==1); assert(web_client_poll(pair[1],now)==1);
  assert(drain(reply,sizeof(reply))==4 && reply[0]==0x8a && reply[1]==2 && reply[2]==4 && reply[3]==5);
  n=masked(frame,0x80,payload,sizeof(payload)); put(frame,n);
  assert(web_client_poll(pair[1],now)==1); assert(packet_input_poll(&input,now)==1);
  assert(packet_input_read(pair[1],data,3,&got) && got==3);
  assert(data[0]==0x1b && data[1]==7 && data[2]==9 && packet_input_end());
  assert(web_client_send(pair[1],prefix,2)==2); assert(web_client_send(pair[1],payload,2)==2);
  assert(web_client_poll(pair[1],now)==1); assert(drain(reply,sizeof(reply))==6);
  assert(reply[0]==0x82 && reply[1]==4 && !memcmp(reply+2,"\3\33\7\11",4));
  n=masked(frame,0x88,NULL,0); put(frame,n); assert(web_client_poll(pair[1],now)==0);
  assert(web_client_poll(pair[1],now)==-1); assert(drain(reply,sizeof(reply))==4 && reply[0]==0x88); finish();

  opened(); const unsigned char unmasked[]={0x82,1,0}; put(unmasked,sizeof(unmasked));
  assert(web_client_poll(pair[1],now)==-1); finish();
  opened(); const unsigned char huge[]={0x82,0xfe,0x7f,0xff}; put(huge,sizeof(huge));
  assert(web_client_poll(pair[1],now)==-1); finish();
  opened(); n=masked(frame,0x80,payload,2); put(frame,n);
  assert(web_client_poll(pair[1],now)==-1); finish();
  opened(); put("\202",1); assert(web_client_poll(pair[1],now)==1);
  assert(web_client_poll(pair[1],now+15000000)==-1); finish();
  connect_client(); assert(web_client_poll(pair[1],now+15000000)==-1); finish();
  /* Bounded output: a stalled browser cannot allocate without a limit. */
  opened(); unsigned char large[16384]={0}; unsigned count=0;
  while(web_client_send(pair[1],large,sizeof(large))>0)assert(++count<=512);
  assert(count>0 && web_client_poll(pair[1],now)==-1); finish();
  puts("web client transport: passed"); return 0;
}
