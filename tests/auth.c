#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <ctype.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include "auth.h"
#include "globals.h"
#include "packet_input.h"
#include "procedures.h"
#include "tools.h"
#include "varnum.h"

typedef struct { int fd[2]; PacketInput input; EVP_CIPHER_CTX *enc,*dec; uint8_t token[32]; size_t token_size; EVP_PKEY *key; } Peer;
static const char profile[]="{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"PremiumPlayer123\",\"properties\":[{\"name\":\"textures\",\"value\":\"abc\"}]}";
static void put(uint8_t *p,size_t *n,unsigned v) { do { p[(*n)++]=(uint8_t)((v&127u)|(v>127?128u:0u)); v>>=7; } while(v); }
static unsigned get(const uint8_t *p,size_t size,size_t *n) { unsigned v=0; for(unsigned shift=0;shift<35;shift+=7) { assert(*n<size); unsigned b=p[(*n)++]; v|=(b&127u)<<shift; if(!(b&128u)) return v; } abort(); }
static void cipher(EVP_CIPHER_CTX *ctx,uint8_t *p,size_t size,bool encrypt) {
  int n=0; assert(size<4096); if(!ctx) return;
  assert((encrypt?EVP_EncryptUpdate(ctx,p,&n,p,(int)size):EVP_DecryptUpdate(ctx,p,&n,p,(int)size))>0 && n==(int)size);
}
static void open_peer(Peer *p) {
  memset(p,0,sizeof(*p)); assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p->fd));
  assert(!fcntl(p->fd[1],F_SETFL,O_NONBLOCK)); packet_input_reset(&p->input,p->fd[1]);
  assert(auth_accept(p->fd[1],1000)); setClientState(p->fd[1],STATE_LOGIN);
}
static void close_peer(Peer *p) {
  auth_forget(p->fd[1]); setClientState(p->fd[1],STATE_NONE);
  close(p->fd[0]); close(p->fd[1]); EVP_CIPHER_CTX_free(p->enc); EVP_CIPHER_CTX_free(p->dec); EVP_PKEY_free(p->key);
}
static bool send_packet(Peer *p,unsigned id,const uint8_t *payload,size_t size,bool fragmented) {
  uint8_t bytes[2048]; size_t n=0; put(bytes,&n,(unsigned)size+1); put(bytes,&n,id); memcpy(bytes+n,payload,size); n+=size;
  cipher(p->enc,bytes,n,true);
  if(fragmented) {
    for(size_t i=0;i<n;i++) { assert(send(p->fd[0],bytes+i,1,0)==1); assert(packet_input_poll(&p->input,2000+(int64_t)i)==(i==n-1?1:0)); }
  } else { assert(send(p->fd[0],bytes,n,0)==(ssize_t)n); assert(packet_input_poll(&p->input,2000)==1); }
  recv_count=1; int packet=readVarInt(p->fd[1]); assert(packet==(int)id);
  bool ok=auth_login_packet(p->fd[1],packet); bool complete=packet_input_end();
  return ok && complete;
}
static size_t read_packet(Peer *p,uint8_t *data) {
  uint8_t prefix[5]; size_t n=0;
  do { assert(recv(p->fd[0],prefix+n,1,0)==1); cipher(p->dec,prefix+n,1,false); } while((prefix[n++]&128) && n<5);
  size_t at=0; unsigned length=get(prefix,n,&at); assert(length<2048);
  size_t read=0; while(read<length) { ssize_t count=recv(p->fd[0],data+read,length-read,0); assert(count>0); cipher(p->dec,data+read,(size_t)count,false); read+=(size_t)count; }
  return read;
}
static void challenge(Peer *p) {
  uint8_t payload[64]={0},data[2048]; size_t n=0; put(payload,&n,16); memcpy(payload+n,"PremiumPlayer123",16); n+=16; n+=16;
  assert(send_packet(p,0,payload,n,true));
  size_t size=read_packet(p,data),at=0; assert(get(data,size,&at)==1 && get(data,size,&at)==0);
  unsigned key_size=get(data,size,&at); assert(key_size<=size-at);
  const unsigned char *key=data+at; p->key=d2i_PUBKEY(NULL,&key,(long)key_size); assert(p->key); at+=key_size;
  p->token_size=get(data,size,&at); assert(p->token_size==16 && p->token_size<=size-at); memcpy(p->token,data+at,p->token_size); at+=p->token_size;
  assert(at+1==size && data[at]==1);
}
static size_t encrypt_rsa(Peer *p,const uint8_t *data,size_t n,uint8_t *out) {
  EVP_PKEY_CTX *ctx=EVP_PKEY_CTX_new(p->key,NULL); size_t size=512;
  assert(ctx && EVP_PKEY_encrypt_init(ctx)>0 && EVP_PKEY_CTX_set_rsa_padding(ctx,RSA_PKCS1_PADDING)>0 && EVP_PKEY_encrypt(ctx,out,&size,data,n)>0);
  EVP_PKEY_CTX_free(ctx); return size;
}
static bool respond(Peer *p,bool correct) {
  uint8_t secret[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},encrypted[512],payload[1100]; size_t at=0;
  size_t n=encrypt_rsa(p,secret,16,encrypted); put(payload,&at,(unsigned)n); memcpy(payload+at,encrypted,n); at+=n;
  if(!correct) p->token[0]^=1;
  n=encrypt_rsa(p,p->token,p->token_size,encrypted); put(payload,&at,(unsigned)n); memcpy(payload+at,encrypted,n); at+=n;
  bool ok=send_packet(p,1,payload,at,true);
  if(ok) {
    /* Independently derive the session-service hash using a BIGNUM, including
     * Java's signed SHA-1 representation (no unsigned/zero-padded shortcut). */
    unsigned char *der=NULL; int der_size=i2d_PUBKEY(p->key,&der); assert(der_size>0);
    uint8_t input[1024],hash[EVP_MAX_MD_SIZE]; unsigned hash_size=0;
    memcpy(input,secret,16); memcpy(input+16,der,(size_t)der_size); OPENSSL_free(der);
    assert(EVP_Digest(input,16+(size_t)der_size,hash,&hash_size,EVP_sha1(),NULL)>0 && hash_size==20);
    BIGNUM *number=BN_bin2bn(hash,20,NULL); assert(number);
    if(hash[0]&128) {
      BIGNUM *modulus=BN_new(); assert(modulus && BN_one(modulus) && BN_lshift(modulus,modulus,160) && BN_sub(number,number,modulus)); BN_free(modulus);
    }
    char *hex=BN_bn2hex(number); BN_free(number); assert(hex);
    char expected[42]; size_t start=hex[0]=='-'?1:0,used=0;
    if(start) expected[used++]='-';
    while(hex[start]=='0' && hex[start+1]) start++;
    while(hex[start]) { expected[used++]=(char)tolower((unsigned char)hex[start++]); } expected[used]=0; OPENSSL_free(hex);
    const char *url=auth_test_url(p->fd[1]),*server_id=strstr(url,"&serverId=");
    assert(server_id && !strcmp(server_id+10,expected));
    p->enc=EVP_CIPHER_CTX_new(); p->dec=EVP_CIPHER_CTX_new();
    assert(p->enc && p->dec && EVP_EncryptInit_ex(p->enc,EVP_aes_128_cfb8(),NULL,secret,secret)>0 && EVP_DecryptInit_ex(p->dec,EVP_aes_128_cfb8(),NULL,secret,secret)>0);
  }
  return ok;
}
int main(void) {
  uint8_t uuid[16]; char name[17];
  assert(auth_profile(profile,strlen(profile),"PremiumPlayer123",uuid,name)); assert(strlen(name)==16);
  static const char *const bad[]={
    "{}", "{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"WrongName\"}",
    "{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"PremiumPlayer123\",\"name\":\"PremiumPlayer123\"}",
    "{\"id\":\"00000000000000000000000000000000\",\"name\":\"PremiumPlayer123\"}",
    "{\"properties\":{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"PremiumPlayer123\"}}",
    "{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"PremiumPlayer123\"}null",
    "{\"id\":\"1234567890abcdef1234567890abcdef\",\"name\":\"PremiumPlayer123\\u0000\"}"
  };
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(!auth_profile(bad[i],strlen(bad[i]),"PremiumPlayer123",uuid,name));
  for(size_t i=0;i<strlen(profile);i++) assert(!auth_profile(profile,i,"PremiumPlayer123",uuid,name));
  for(unsigned i=0;i<MAX_PLAYERS;i++) player_data[i].client_fd=-1;
  for(unsigned i=0;i<MAX_PLAYERS;i++) client_states[i*2]=-1;
  assert(auth_init()); Peer p; open_peer(&p); challenge(&p); assert(respond(&p,true));
  assert(auth_encrypted(p.fd[1])); assert(player_data_count==0);
  uint8_t data[2048]; assert(recv(p.fd[0],data,sizeof(data),MSG_DONTWAIT)<0); /* No success before verification. */
  auth_test_response(p.fd[1],200,profile); assert(auth_progress(p.fd[1],3000)==1);
  size_t size=read_packet(&p,data),at=0; assert(get(data,size,&at)==2); assert(!memcmp(data+at,uuid,16)); at+=16;
  assert(get(data,size,&at)==16 && !memcmp(data+at,"PremiumPlayer123",16)); at+=16;
  assert(get(data,size,&at)==0 && at==size && player_data_count==1);
  assert(!memcmp(player_data[0].uuid,uuid,16) && player_name_length(player_data[0].name)==16);
  assert(send_packet(&p,3,data,0,true) && getClientState(p.fd[1])==STATE_CONFIGURATION);
  Peer duplicate; open_peer(&duplicate); challenge(&duplicate); assert(respond(&duplicate,true));
  auth_test_response(duplicate.fd[1],200,profile); assert(auth_progress(duplicate.fd[1],3000)<0);
  assert(player_data[0].client_fd==p.fd[1] && player_data_count==1); close_peer(&duplicate);
  /* Replay of the acknowledgement cannot jump through the login state machine. */
  assert(!send_packet(&p,3,data,0,false)); close_peer(&p); player_data[0].client_fd=-1;
  open_peer(&p); challenge(&p); assert(!respond(&p,false)); assert(auth_progress(p.fd[1],3000)<0); close_peer(&p);
  open_peer(&p); assert(!send_packet(&p,3,data,0,false)); close_peer(&p);
  open_peer(&p); assert(auth_progress(p.fd[1],30001000)<0); close_peer(&p);
  static const long statuses[]={204,401,429,500};
  for(unsigned i=0;i<sizeof(statuses)/sizeof(statuses[0]);i++) {
    open_peer(&p); challenge(&p); assert(respond(&p,true)); auth_test_response(p.fd[1],statuses[i],profile);
    assert(auth_progress(p.fd[1],3000)<0); close_peer(&p);
  }
  open_peer(&p); challenge(&p); assert(respond(&p,true)); auth_test_response(p.fd[1],200,"not json"); assert(auth_progress(p.fd[1],3000)<0); close_peer(&p);
  open_peer(&p); challenge(&p); assert(respond(&p,true)); assert(auth_progress(p.fd[1],30001000)<0); close_peer(&p);
  auth_shutdown(); puts("auth: real RSA/AES handshake, fragmented encrypted frames, verified UUID, 16-character names, duplicate sessions, ordering, malformed profiles and fail-closed timeouts passed");
}
