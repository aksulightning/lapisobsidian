#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "server_config.h"
#include "commands.h"
#include "packets.h"
#include "tools.h"
#include "varnum.h"
int main (void) {
  int fd[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
  strcpy(server_config.motd,"A \"quoted\" \\ world – hyvä");
  assert(!sc_statusResponse(fd[1]));
  int frame = readVarInt(fd[0]); assert(frame > 0 && frame < 512);
  assert(readVarInt(fd[0]) == 0); int n = readVarInt(fd[0]);
  assert(n > 0 && frame == 1+sizeVarInt((uint32_t)n)+n);
  char json[512]; assert(recv_all(fd[0],json,(size_t)n,false) == n); json[n] = 0;
  assert(strstr(json,"A \\\"quoted\\\" \\\\ world – hyvä"));
  assert(strstr(json,"\"protocol\":772") && !strcmp(json+n-3,"\"}}"));
  server_config.gamemode = 1; PlayerData *p = &player_data[0];
  commands_reset_player(p); assert(commands_gamemode(p) == 1 && commands_abilities(p) == 13);
  server_config.gamemode = 3; commands_reset_player(p); assert(commands_gamemode(p) == 3 && commands_abilities(p) == 7);
  close(fd[0]); close(fd[1]);
  puts("config packets: JSON escaping/framing, Unicode MOTD and default game-mode abilities passed");
  return 0;
}
