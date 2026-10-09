/* Direct bounds/state tests for the optional transport, including malformed input. */
#define LAPIS_ENABLE_WEBCLIENT 1
#include "../../../src/webclient.c"
#include <assert.h>
#ifdef LAPIS_SANDBOX_SANITIZERS
const char* __asan_default_options(void) { return "detect_leaks=0"; }
const char* __ubsan_default_options(void) { return "halt_on_error=1"; }
#endif
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while(0)
static WebPeer *setup(void) {
  WebPeer *p=&peers[0];release(p,0);p->fd=-1;p->state=3;p->owned=1;
  p->output=malloc(WC_OUTPUT);assert(p->output);return p;
}
static void put(WebPeer *p,unsigned op,int fin,const char *data,size_t n) {
  unsigned char *s=p->input+p->input_size;assert(n<126);
  s[0]=(unsigned char)(op|(fin?128:0));s[1]=(unsigned char)(n|128);
  for(int i=0;i<4;i++)s[2+i]=(unsigned char)(i+1);
  for(size_t i=0;i<n;i++)s[6+i]=(unsigned char)data[i]^s[2+(i&3)];
  p->input_size+=n+6;
}
int main(void) {
  char key[29];accept_key("dGhlIHNhbXBsZSBub25jZQ==",key);
  CHECK(!strcmp(key,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
  CHECK(safe_path("/sounds/dig_stone1.wav"));CHECK(!safe_path("/../server.txt"));
  CHECK(!safe_path("/sounds/.secret"));CHECK(!safe_path("/%2e/file"));CHECK(!safe_path("/x\\file"));
  WebPeer *p=setup();put(p,2,0,"ab",2);put(p,9,1,"ping",4);put(p,0,1,"cd",2);frames(p);
  CHECK(p->app_size==4 && !memcmp(p->app,"abcd",4) && !p->fragment);
  CHECK(p->wire_size==6 && p->wire[0]==138 && !memcmp(p->wire+2,"ping",4));
  unsigned char data[16];CHECK(webclient_recv(-1,data,2,1)==2 && p->app_size==4);
  CHECK(webclient_recv(-1,data,8,0)==4 && p->app_size==0);
  CHECK(webclient_recv(-1,data,1,0)==-1);
  p=setup();p->wire_size=1;put(p,9,1,"test",4);frames(p);CHECK(p->input_size==10);
  p->wire_size=0;frames(p);CHECK(!memcmp(p->wire+2,"test",4)); /* No double unmask. */
  p=setup();put(p,0,1,"bad",3);frames(p);CHECK(p->dead);
  p=setup();put(p,2,0,"x",1);put(p,2,1,"x",1);frames(p);CHECK(p->dead);
  p=setup();put(p,9,0,"x",1);frames(p);CHECK(p->dead);
  p=setup();put(p,2,1,"abc",3);p->app_size=WC_INPUT-1;frames(p);
  CHECK(!p->dead && p->input_size==9);p->app_size=0;frames(p);CHECK(p->app_size==3);
  p=setup();CHECK(webclient_send(-1,"test",4)==4);CHECK(webclient_disconnect(-1)==1 && p->state==4 && p->end==4);
  p=setup();p->end=WC_OUTPUT;CHECK(webclient_send(-1,"x",1)==-1 && p->dead);
  p=setup();p->begin=WC_OUTPUT-8;p->end=WC_OUTPUT;memset(p->output+p->begin,42,8);
  CHECK(webclient_send(-1,"tail",4)==4 && p->begin==0 && p->end==12 && p->output[0]==42);
  /* Deterministic malformed frame corpus with all length/opcode combinations. */
  unsigned rng=772;
  for(unsigned i=0;i<4000;i++) {
    p=setup();p->input_size=i%160;
    for(size_t j=0;j<p->input_size;j++) {rng=rng*1664525u+1013904223u;p->input[j]=(unsigned char)(rng>>24);}
    frames(p);CHECK(p->input_size<=160 && p->app_size<=WC_INPUT && p->wire_size<=sizeof(p->wire));
  }
  release(p,0);printf("web host: %u checks passed\n",checks);return 0;
}
