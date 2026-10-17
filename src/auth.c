#define _POSIX_C_SOURCE 200809L
#include "auth.h"
#if LAPIS_ENABLE_EXPERIMENTAL_AUTH
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include "globals.h"
#include "packet_input.h"
#include "packets.h"
#include "procedures.h"
#include "tools.h"
#include "varnum.h"
#include "webclient.h"

#define AUTH_TIMEOUT INT64_C(30000000)
#define RESPONSE_LIMIT 16384u
enum { UNUSED, START, CHALLENGE, VERIFYING, VERIFIED, SUCCESS, CONFIGURED, REJECTED };
typedef struct {
  int fd, phase; int64_t accepted;
  char name[17], response[RESPONSE_LIMIT], url[256];
  uint8_t uuid[16], token[16];
  size_t response_size;
  EVP_CIPHER_CTX *encrypt, *decrypt;
  CURL *request;
} Auth;
static Auth sessions[MAX_PLAYERS];
static EVP_PKEY *server_key;
static unsigned char *public_key;
static int public_key_size;
static CURLM *requests;
static Auth *session(int fd) { for(unsigned i=0;i<MAX_PLAYERS;i++) if(sessions[i].phase && sessions[i].fd==fd) return &sessions[i]; return NULL; }
static void request_clear(Auth *s) {
  if(s->request) { curl_multi_remove_handle(requests,s->request); curl_easy_cleanup(s->request); s->request=NULL; }
}
void auth_forget(int fd) {
  Auth *s=session(fd); if(!s) return;
  request_clear(s); EVP_CIPHER_CTX_free(s->encrypt); EVP_CIPHER_CTX_free(s->decrypt);
  OPENSSL_cleanse(s,sizeof(*s));
}
void auth_shutdown(void) {
  for(unsigned i=0;i<MAX_PLAYERS;i++) if(sessions[i].phase) auth_forget(sessions[i].fd);
  if(requests) curl_multi_cleanup(requests);
  requests=NULL; EVP_PKEY_free(server_key); server_key=NULL;
  OPENSSL_free(public_key); public_key=NULL; public_key_size=0;
  curl_global_cleanup();
}
bool auth_init(void) {
  if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) return false;
  const curl_version_info_data *version=curl_version_info(CURLVERSION_NOW);
  if(!(version->features&CURL_VERSION_ASYNCHDNS)) { fputs("Authentication requires libcurl with asynchronous DNS.\n",stderr); auth_shutdown(); return false; }
  requests=curl_multi_init(); EVP_PKEY_CTX *keygen=EVP_PKEY_CTX_new_id(EVP_PKEY_RSA,NULL);
  bool ok=requests && keygen && EVP_PKEY_keygen_init(keygen)>0 && EVP_PKEY_CTX_set_rsa_keygen_bits(keygen,2048)>0 && EVP_PKEY_keygen(keygen,&server_key)>0;
  EVP_PKEY_CTX_free(keygen);
  if(ok) { public_key_size=i2d_PUBKEY(server_key,&public_key); ok=public_key_size>0 && public_key_size<1024; }
  if(!ok) auth_shutdown();
  return ok;
}
bool auth_accept(int fd,int64_t now) {
  if(!server_key || fd<0 || session(fd)) return false;
  for(unsigned i=0;i<MAX_PLAYERS;i++) if(!sessions[i].phase) { sessions[i]=(Auth){.fd=fd,.phase=START,.accepted=now}; return true; }
  return false;
}
static void reject(Auth *s,const char *message) {
  if(s->phase==REJECTED) return;
  int fd=s->fd; request_clear(s); s->phase=REJECTED;
  /* Login Disconnect uses a JSON string. Other protocol states have a
   * different packet layout; close those connections without a login frame. */
  if(getClientState(fd)!=STATE_LOGIN) return;
  /* These are fixed ASCII messages, so no untrusted JSON escaping is needed. */
  char json[192]; int n=snprintf(json,sizeof(json),"{\"text\":\"%s\"}",message);
  if(n>0 && n<(int)sizeof(json)) {
    writeVarInt(fd,1u+(uint32_t)sizeVarInt((uint32_t)n)+(uint32_t)n);
    writeByte(fd,0); writeVarInt(fd,(uint32_t)n); send_all(fd,json,n);
  }
}
bool auth_encrypted(int fd) { Auth *s=session(fd); return s && s->encrypt; }
bool auth_decrypt(int fd,uint8_t *bytes,size_t size) {
  Auth *s=session(fd); if(!s || !s->decrypt) return true;
  if(size>INT_MAX || s->phase==REJECTED) return false;
  int n=0; return EVP_DecryptUpdate(s->decrypt,bytes,&n,bytes,(int)size)>0 && n==(int)size;
}
ssize_t auth_send(int fd,const void *data,size_t size,ssize_t (*sink)(int,const void *,ssize_t)) {
  Auth *s=session(fd); if(!s || !s->encrypt || size>SSIZE_MAX) return -1;
  const uint8_t *bytes=data; size_t at=0;
  while(at<size) {
    uint8_t encrypted[4096]; size_t n=size-at; if(n>sizeof(encrypted)) n=sizeof(encrypted);
    int count=0;
    if(EVP_EncryptUpdate(s->encrypt,encrypted,&count,bytes+at,(int)n)<=0 || count!=(int)n || sink(fd,encrypted,(ssize_t)n)!=(ssize_t)n) {
      if(s->phase && s->fd==fd) s->phase=REJECTED;
      return -1;
    }
    at+=n;
  }
  return (ssize_t)size;
}
static size_t response_write(char *data,size_t size,size_t count,void *user) {
  Auth *s=user;
  if(size && count>SIZE_MAX/size) return 0;
  size_t n=size*count; if(n>RESPONSE_LIMIT-s->response_size) return 0;
  memcpy(s->response+s->response_size,data,n); s->response_size+=n; return n;
}
static void verified_response(Auth *s,long status,bool transport_ok) {
  if(s->phase!=VERIFYING) return;
  char name[17]; uint8_t uuid[16];
  if(!transport_ok || status!=200 || !auth_profile(s->response,s->response_size,s->name,uuid,name)) {
    reject(s,"Unable to verify your Minecraft session. Sign in and try again."); return;
  }
  memcpy(s->uuid,uuid,16); memcpy(s->name,name,17); s->phase=VERIFIED;
}
void auth_poll(void) {
  if(!requests) return;
  int running=0;
  if(curl_multi_perform(requests,&running)!=CURLM_OK) {
    for(unsigned i=0;i<MAX_PLAYERS;i++) if(sessions[i].phase==VERIFYING) reject(&sessions[i],"Minecraft authentication is unavailable. Try again later.");
    return;
  }
  int left=0; CURLMsg *message;
  while((message=curl_multi_info_read(requests,&left))) if(message->msg==CURLMSG_DONE) {
    Auth *s=NULL; long status=0;
    curl_easy_getinfo(message->easy_handle,CURLINFO_PRIVATE,&s);
    curl_easy_getinfo(message->easy_handle,CURLINFO_RESPONSE_CODE,&status);
    bool ok=message->data.result==CURLE_OK;
    if(s && s->request==message->easy_handle) { request_clear(s); verified_response(s,status,ok); }
  }
}
/* Minecraft's SHA-1 server hash is a signed two's-complement hexadecimal integer. */
static bool server_hash(const uint8_t secret[16],char out[42]) {
  uint8_t digest[EVP_MAX_MD_SIZE]; unsigned size=0;
  EVP_MD_CTX *ctx=EVP_MD_CTX_new();
  bool ok=ctx && EVP_DigestInit_ex(ctx,EVP_sha1(),NULL)>0 && EVP_DigestUpdate(ctx,secret,16)>0 &&
    EVP_DigestUpdate(ctx,public_key,(size_t)public_key_size)>0 && EVP_DigestFinal_ex(ctx,digest,&size)>0 && size==20;
  EVP_MD_CTX_free(ctx); if(!ok) return false;
  bool negative=(digest[0]&128)!=0;
  if(negative) { unsigned carry=1; for(int i=19;i>=0;i--) { unsigned v=(unsigned)(digest[i]^255u)+carry; digest[i]=(uint8_t)v; carry=v>>8; } }
  static const char digits[]="0123456789abcdef"; size_t at=0; bool leading=true;
  if(negative) out[at++]='-';
  for(unsigned i=0;i<40;i++) { unsigned nibble=i%2?digest[i/2]&15u:digest[i/2]>>4; if(leading && !nibble && i!=39) continue; leading=false; out[at++]=digits[nibble]; }
  out[at]=0; return true;
}
static bool verify_start(Auth *s,const uint8_t secret[16]) {
  char hash[42]; if(!server_hash(secret,hash)) return false;
  char request_name[17]; memcpy(request_name,s->name,sizeof(request_name));
  snprintf(s->url,sizeof(s->url),"https://sessionserver.mojang.com/session/minecraft/hasJoined?username=%s&serverId=%s",request_name,hash);
  s->phase=VERIFYING;
#ifdef LAPIS_AUTH_TESTING
  /* Test builds complete this request using auth_test_response. No runtime
   * endpoint override or offline exception exists in production binaries. */
  return true;
#else
  s->request=curl_easy_init(); if(!s->request) return false;
#define OPTION(k,v) do { if(curl_easy_setopt(s->request,k,v)!=CURLE_OK) return false; } while(0)
  OPTION(CURLOPT_URL,s->url); OPTION(CURLOPT_PRIVATE,s);
  OPTION(CURLOPT_WRITEFUNCTION,response_write); OPTION(CURLOPT_WRITEDATA,s);
  OPTION(CURLOPT_PROTOCOLS_STR,"https"); OPTION(CURLOPT_FOLLOWLOCATION,0L);
  OPTION(CURLOPT_SSL_VERIFYPEER,1L); OPTION(CURLOPT_SSL_VERIFYHOST,2L);
  OPTION(CURLOPT_SSLVERSION,(long)CURL_SSLVERSION_TLSv1_2);
  OPTION(CURLOPT_CONNECTTIMEOUT_MS,5000L); OPTION(CURLOPT_TIMEOUT_MS,10000L);
  OPTION(CURLOPT_NOSIGNAL,1L); OPTION(CURLOPT_USERAGENT,"LapisObsidian/experimental-auth");
#undef OPTION
  return curl_multi_add_handle(requests,s->request)==CURLM_OK;
#endif
}
static bool decrypt_rsa(const uint8_t *in,size_t size,uint8_t *out,size_t *out_size) {
  EVP_PKEY_CTX *ctx=EVP_PKEY_CTX_new(server_key,NULL);
  bool ok=ctx && EVP_PKEY_decrypt_init(ctx)>0 && EVP_PKEY_CTX_set_rsa_padding(ctx,RSA_PKCS1_PADDING)>0 && EVP_PKEY_decrypt(ctx,out,out_size,in,size)>0;
  EVP_PKEY_CTX_free(ctx); return ok;
}
bool auth_login_packet(int fd,int packet_id) {
  Auth *s=session(fd); if(!s) return false;
  if(packet_id==0 && s->phase==START) {
    int n=readVarInt(fd); if(n<1 || n>16) goto invalid;
    if(recv_all(fd,s->name,(size_t)n,false)!=n || !auth_name_valid(s->name,(size_t)n)) goto invalid;
    s->name[n]=0; uint8_t untrusted_uuid[16];
    if(recv_all(fd,untrusted_uuid,16,false)!=16 || packet_input_remaining()!=0) goto invalid;
#if defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT==1
    if(webclient_has(fd)) { reject(s,"This server requires an authenticated Minecraft Java client."); return false; }
#endif
    if(RAND_bytes(s->token,sizeof(s->token))!=1) goto invalid;
    s->phase=CHALLENGE;
    uint32_t key_size=(uint32_t)public_key_size;
    writeVarInt(fd,1u+1u+(uint32_t)sizeVarInt(key_size)+key_size+1u+(uint32_t)sizeof(s->token)+1u);
    writeByte(fd,1); writeByte(fd,0); writeVarInt(fd,key_size); send_all(fd,public_key,public_key_size);
    writeByte(fd,sizeof(s->token)); send_all(fd,s->token,sizeof(s->token)); writeByte(fd,1);
    return session(fd)==s && s->phase==CHALLENGE;
  }
  if(packet_id==1 && s->phase==CHALLENGE) {
    uint8_t encrypted_secret[512],encrypted_token[512],secret[512],token[512];
    int a=readVarInt(fd),key_size=EVP_PKEY_get_size(server_key);
    if(a!=key_size || a<=0 || a>(int)sizeof(encrypted_secret) || recv_all(fd,encrypted_secret,(size_t)a,false)!=a) goto invalid;
    int b=readVarInt(fd);
    if(b!=key_size || recv_all(fd,encrypted_token,(size_t)b,false)!=b || packet_input_remaining()!=0) goto invalid;
    size_t secret_size=sizeof(secret),token_size=sizeof(token);
    bool ok=decrypt_rsa(encrypted_token,(size_t)b,token,&token_size) && token_size==sizeof(s->token) && CRYPTO_memcmp(token,s->token,sizeof(s->token))==0 &&
      decrypt_rsa(encrypted_secret,(size_t)a,secret,&secret_size) && secret_size==16;
    if(ok) {
      s->encrypt=EVP_CIPHER_CTX_new(); s->decrypt=EVP_CIPHER_CTX_new();
      ok=s->encrypt && s->decrypt && EVP_EncryptInit_ex(s->encrypt,EVP_aes_128_cfb8(),NULL,secret,secret)>0 &&
        EVP_DecryptInit_ex(s->decrypt,EVP_aes_128_cfb8(),NULL,secret,secret)>0;
      if(ok) ok=verify_start(s,secret);
    }
    OPENSSL_cleanse(secret,sizeof(secret)); OPENSSL_cleanse(token,sizeof(token)); OPENSSL_cleanse(s->token,sizeof(s->token));
    if(!ok) goto invalid;
    return true;
  }
  if(packet_id==3 && s->phase==SUCCESS && packet_input_remaining()==0) {
    s->phase=CONFIGURED; setClientState(fd,STATE_CONFIGURATION); return true;
  }
invalid:
  reject(s,"Invalid Minecraft login or encryption response."); return false;
}
int auth_progress(int fd,int64_t now) {
  Auth *s=session(fd); if(!s || s->phase==REJECTED) return -1;
  if(getClientState(fd)!=STATE_PLAY && (now<s->accepted || now-s->accepted>=AUTH_TIMEOUT)) {
    reject(s,"Minecraft authentication timed out."); return -1;
  }
  if(s->phase==VERIFIED) {
    for(unsigned i=0;i<MAX_PLAYERS;i++) if(player_data[i].client_fd>=0 && !memcmp(player_data[i].uuid,s->uuid,16)) {
      reject(s,"This Minecraft account is already connected."); return -1;
    }
    if(reservePlayerData(fd,s->uuid,s->name)) { reject(s,"Server player storage is full."); return -1; }
    if(sc_loginSuccess(fd,s->uuid,s->name) || session(fd)!=s || s->phase!=VERIFIED) return -1;
    s->phase=SUCCESS;
  }
  return 1;
}
#ifdef LAPIS_AUTH_TESTING
const char *auth_test_url(int fd) { Auth *s=session(fd); return s?s->url:NULL; }
void auth_test_response(int fd,long status,const char *body) {
  Auth *s=session(fd); if(!s) return;
  s->response_size=0; bool ok=response_write((char *)body,1,strlen(body),s)==strlen(body);
  verified_response(s,status,ok);
}
#endif
#else
bool auth_init(void) { return true; }
void auth_shutdown(void) {}
bool auth_accept(int fd,int64_t now) { (void)fd; (void)now; return true; }
void auth_forget(int fd) { (void)fd; }
void auth_poll(void) {}
int auth_progress(int fd,int64_t now) { (void)fd; (void)now; return 1; }
#endif
