#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "server_config.h"

static const char *path = ".tests/config-unit.txt";
static char error[200];
static void write_bytes (const char *text, size_t n) {
  FILE *f = fopen(path,"wb"); assert(f && fwrite(text,1,n,f) == n); assert(!fclose(f));
}
static void rejects (const char *text, size_t n) {
  ServerConfig c = SERVER_CONFIG_DEFAULTS, before = c;
  write_bytes(text,n); assert(!server_config_load(path,&c,error,sizeof(error)));
  assert(!memcmp(&c,&before,sizeof(c)) && strstr(error,"server.txt:"));
}
int main (void) {
  remove(path); ServerConfig c;
  assert(server_config_load(path,&c,error,sizeof(error)));
  assert(c.port == PORT && c.gamemode == GAMEMODE && !c.seed_set && c.seed == INITIAL_WORLD_SEED);
  assert(c.web_port == 8080 && !strcmp(c.web_address,"0.0.0.0"));
  assert(!c.experimental_enable_plates && !c.mirror_horizontal && c.wheat_growth_seconds == 30 && !strcmp(c.motd,"Lapis Obsidian"));
  assert(server_config_load(path,&c,error,sizeof(error))); /* generated file round-trip */
  const char valid[] = " # comment\r\n port = 65535 \r\nmotd=My \"world\" \\ #1 = hyvä\n"
    "web-address=127.0.0.1\nweb-port=65535\ngamemode = creative\nseed=-9223372036854775808\nmirror-horizontal=true\nwheat-growth-seconds=600\nexperimental_enable_plates=true";
  write_bytes(valid,sizeof(valid)-1); assert(server_config_load(path,&c,error,sizeof(error)));
  assert(c.web_port == 65535 && !strcmp(c.web_address,"127.0.0.1"));
  assert(c.port == 65535 && c.gamemode == 1 && c.seed == UINT64_C(0x8000000000000000) && c.seed_set);
  assert(c.experimental_enable_plates && c.mirror_horizontal && c.wheat_growth_seconds == 600 && !strcmp(c.motd,"My \"world\" \\ #1 = hyvä"));
  const char *bad[] = {"web-port=0","web-port=-1","web-port=65536","web-port=6553600000000000000000","web-port=80x","web-port=80\nweb-port=81",
    "web-address=","web-address=localhost","web-address=::1","web-address=256.0.0.1","web-address=127.1","web-address=127.0.0.1:80",
    "web-address=127.0.0.1.2","web-address=127..0.1","web-address=127.0.0.","web-address=01.2.3.4","web-address=127.0.0.1/8",
    "web-address=127.0.0.1\nweb-address=0.0.0.0","experimental_enable_plates=yes","experimental_enable_plates=true\nexperimental_enable_plates=false","port=0","port=-1","port=65536","port=9999999999999999999999","port=2x","port=1\nport=2",
    "gamemode=4","gamemode=builder","mirror-horizontal=1","seed=9223372036854775808","seed=-9223372036854775809",
    "wheat-growth-seconds=0","wheat-growth-seconds=601","unknown=true","port 25565","motd=a\tb", "motd=\xc0\xaf",
    "motd=\xed\xa0\x80","motd=\xf4\x90\x80\x80","motd=\xe2\x82"};
  for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) rejects(bad[i],strlen(bad[i]));
  const char nul[] = "port=1234\0motd=hidden"; rejects(nul,sizeof(nul)-1);
  char big[8193]; memset(big,'#',sizeof(big)); rejects(big,256);
  memset(big,'\n',sizeof(big)); rejects(big,sizeof(big));
  char motd[128] = "motd="; memset(motd+5,'a',121); rejects(motd,126);
  write_bytes(motd,125); assert(server_config_load(path,&c,error,sizeof(error)) && strlen(c.motd) == 120);
  const char minimum[] = "port=1\nweb-port=1\nweb-address=255.255.255.255\ngamemode=3\nseed=\nwheat-growth-seconds=1\n";
  write_bytes(minimum,sizeof(minimum)-1); assert(server_config_load(path,&c,error,sizeof(error)));
  assert(c.web_port == 1 && !strcmp(c.web_address,"255.255.255.255"));
  assert(c.port == 1 && c.gamemode == 3 && !c.seed_set && c.wheat_growth_seconds == 1);
  char *args[] = {"lapis-obsidian","--seed","-17","--mirror-horizontal"};
  assert(server_config_arguments(&c,4,args,error,sizeof(error)) && c.seed == UINT64_MAX-16 && c.seed_set && c.mirror_horizontal);
  char *off[] = {"lapis-obsidian","--no-mirror-horizontal"};
  assert(server_config_arguments(&c,2,off,error,sizeof(error)) && !c.mirror_horizontal);
  ServerConfig before = c;
  char *dup[] = {"lapis-obsidian","--seed","1","--seed","2"};
  assert(!server_config_arguments(&c,5,dup,error,sizeof(error)) && !memcmp(&c,&before,sizeof(c)));
  char *missing[] = {"lapis-obsidian","--seed"}; assert(!server_config_arguments(&c,2,missing,error,sizeof(error)));
  char *unknown[] = {"lapis-obsidian","--wat"}; assert(!server_config_arguments(&c,2,unknown,error,sizeof(error)));
  write_bytes("\nport=bad\n",10); assert(!server_config_load(path,&c,error,sizeof(error)) && strstr(error,"server.txt:2:"));
  remove(path); assert(!mkdir(path,0700)); assert(!server_config_load(path,&c,error,sizeof(error))); assert(!rmdir(path));
  assert(!server_config_load(".tests/no-config-parent/server.txt",&c,error,sizeof(error)));
  puts("server config: defaults, exclusive creation, bounds, UTF-8, malformed files, duplicates, atomic loads and CLI precedence passed");
  return 0;
}
