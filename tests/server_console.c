#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "server_console.h"
#include "server_stats.h"
#include "plates.h"

static unsigned broadcasts;
static char message[300];
int sc_systemChat (int fd, char *text, uint16_t length) {
  assert(fd == 10 || fd == 11);
  assert(plates_fd_active(fd));
  assert(length < sizeof(message));
  memcpy(message,text,length); message[length] = 0; broadcasts++; return 0;
}
bool plates_select (unsigned id) { plate_current = id; return true; }
void plates_select_for_fd (int fd) {
  for (unsigned i = 0; i < MAX_PLAYERS; i++)
    if (player_data[i].client_fd == fd) plate_current = player_plates[i];
}
static void feed (ServerConsole *console, const char *text, FILE *out) {
  server_console_feed(console,text,strlen(text),out);
}
static void output (FILE *file, char *text, size_t capacity) {
  rewind(file); size_t n = fread(text,1,capacity-1,file); text[n] = 0;
  assert(!ferror(file)); fseek(file,0,SEEK_END);
}
int main (void) {
  char text[4096];
  server_stats_reset(); server_stats_format(text,sizeof(text));
  assert(strstr(text,"warming up") && strstr(text,"target 1.00"));
  server_stats_record_tick(0); server_stats_record_tick(-1);
  server_stats_format(text,sizeof(text)); assert(strstr(text,"warming up"));
  server_stats_record_tick(1000000); server_stats_record_tick(2000000);
  server_stats_format(text,sizeof(text));
  assert(strstr(text,"0.67 / 1.00") && strstr(text,"2 ticks, 3.0s"));
  for (unsigned i = 0; i < 60; i++) server_stats_record_tick(1000000);
  server_stats_format(text,sizeof(text));
  assert(strstr(text,"1.00 / 1.00") && strstr(text,"60 ticks, 60.0s"));
  server_stats_record_tick(61000000); server_stats_format(text,sizeof(text));
  assert(strstr(text,"0.50 / 1.00") && strstr(text,"60 ticks, 120.0s"));
  for (unsigned i = 0; i < 60; i++) server_stats_record_tick(500000);
  server_stats_format(text,sizeof(text)); assert(strstr(text,"1.00 / 1.00"));

  for (int i = 0; i < MAX_PLAYERS; i++) player_data[i].client_fd = -1;
  player_data[0].client_fd = 10; strcpy(player_data[0].name,"Alice");
  player_data[1].client_fd = 11; strcpy(player_data[1].name,"Bob");
  player_data[2].client_fd = 12; player_data[2].flags = 0x20;
  strcpy(player_data[2].name,"Loading");
  plates_enabled = true; plate_current = 3;
  player_plates[0] = 0; player_plates[1] = 1;
  ServerConsole console; server_console_init(&console);
  FILE *out = fopen(".tests/console-output.txt","w+b"); assert(out);
  feed(&console,"  /help\r\n\tlist\n/tps\nsay hello from the server with many words\n",out);
  output(out,text,sizeof(text));
  assert(strstr(text,"Console commands:") && strstr(text,"Players online: 2/"));
  assert(strstr(text,"Alice") && strstr(text,"Bob") && !strstr(text,"Loading"));
  assert(strstr(text,"TPS: 1.00 / 1.00"));
  assert(broadcasts == 2 && !strcmp(message,"[Server] hello from the server with many words"));
  assert(plate_current == 3);
  feed(&console,"stop extra\nhelp extra\nsay\nadmin token\n",out);
  assert(!console.stop && broadcasts == 2);
  output(out,text,sizeof(text));
  assert(strstr(text,"Usage: stop") && strstr(text,"Usage: say") && strstr(text,"Unknown console"));
  /* Never execute a truncated prefix, including stop or say. */
  char oversized[300]; memset(oversized,' ',sizeof(oversized)); memcpy(oversized,"stop",4);
  server_console_feed(&console,oversized,sizeof(oversized),out);
  feed(&console,"\nsay bad\001text\n",out);
  const char embedded_nul[] = "say bad\0text\n";
  server_console_feed(&console,embedded_nul,sizeof(embedded_nul)-1,out);
  assert(!console.stop && broadcasts == 2);
  feed(&console,"say recovered\n",out); assert(broadcasts == 4);
  char boundary[COMMAND_MAX_BYTES]; memset(boundary,'x',sizeof(boundary));
  memcpy(boundary,"say ",4);
  server_console_feed(&console,boundary,sizeof(boundary),out);
  feed(&console,"\n",out); assert(broadcasts == 6);
  assert(strlen(message) == COMMAND_MAX_BYTES-4+9);
  feed(&console,"sto",out); assert(!console.stop);
  feed(&console,"p",out); assert(!console.stop);
  server_console_end(&console,out); assert(console.stop && console.closed);
  feed(&console,"say ignored\n",out); assert(broadcasts == 6);
  fclose(out);

  /* Empty/partial pipes must not block, EOF disables input without stopping. */
  int pipes[2], saved_stdin = dup(STDIN_FILENO); assert(saved_stdin >= 0);
  assert(pipe(pipes) == 0 && dup2(pipes[0],STDIN_FILENO) >= 0); close(pipes[0]);
  server_console_init(&console); out = fopen(".tests/console-output.txt","w+b"); assert(out);
  server_console_poll(&console,out); assert(!console.closed);
  assert(write(pipes[1],"tp",2) == 2);
  server_console_poll(&console,out); assert(console.used == 2 && !console.closed);
  assert(write(pipes[1],"s\nlist",6) == 6); close(pipes[1]);
  server_console_poll(&console,out); assert(console.closed && !console.stop);
  output(out,text,sizeof(text)); assert(strstr(text,"TPS:") && strstr(text,"Players online: 2/"));
  assert(dup2(saved_stdin,STDIN_FILENO) >= 0); close(saved_stdin); fclose(out);
  remove(".tests/console-output.txt");
  puts("server console: TPS window, parsing, broadcasts, partial input and EOF passed");
  return 0;
}
