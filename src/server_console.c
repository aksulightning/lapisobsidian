#include <errno.h>
#include <string.h>
#include "server_console.h"
#include "server_stats.h"
#include "packets.h"
#include "plates.h"

#if defined(_WIN32)
  #include <windows.h>
  #include <conio.h>
#elif !defined(ESP_PLATFORM)
  #include <poll.h>
  #include <unistd.h>
#endif

static bool online (const PlayerData *player) {
  return player->client_fd >= 0 && !(player->flags & 0x20);
}
static void execute (ServerConsole *console, FILE *out) {
  if (console->rejected) {
    fputs("Invalid console line: use at most 256 printable ASCII bytes.\n",out);
    return;
  }
  console->line[console->used] = 0;
  char *command = console->line;
  while (*command == ' ' || *command == '\t') command++;
  if (*command == '/') command++;
  char *args = command;
  while (*args && *args != ' ' && *args != '\t') args++;
  if (*args) *args++ = 0;
  while (*args == ' ' || *args == '\t') args++;
  size_t n = strlen(args);
  while (n && (args[n-1] == ' ' || args[n-1] == '\t')) args[--n] = 0;
  if (!*command) return;
  if (!strcmp(command,"say")) {
    if (!*args) { fputs("Usage: say <message>\n",out); return; }
    char message[COMMAND_MAX_BYTES+16];
    snprintf(message,sizeof(message),"[Server] %s",args);
    unsigned original_plate = plate_current;
    for (int i = 0; i < MAX_PLAYERS; i++) if (online(&player_data[i])) {
      /* send_all filters traffic to the active Plate. */
      plates_select_for_fd(player_data[i].client_fd);
      sc_systemChat(player_data[i].client_fd,message,(uint16_t)strlen(message));
    }
    plates_select(original_plate);
    fprintf(out,"%s\n",message);
  } else if (!strcmp(command,"help") || !strcmp(command,"tps") ||
             !strcmp(command,"list") || !strcmp(command,"stop")) {
    if (*args) { fprintf(out,"Usage: %s\n",command); return; }
    if (!strcmp(command,"help")) {
      fputs("Console commands: help, tps, list, say <message>, stop\n",out);
    } else if (!strcmp(command,"tps")) {
      char message[160]; server_stats_format(message,sizeof(message));
      fprintf(out,"%s\n",message);
    } else if (!strcmp(command,"list")) {
      unsigned count = 0;
      for (int i = 0; i < MAX_PLAYERS; i++) if (online(&player_data[i])) count++;
      fprintf(out,"Players online: %u/%u\n",count,(unsigned)MAX_PLAYERS);
      for (int i = 0; i < MAX_PLAYERS; i++) if (online(&player_data[i]))
        fprintf(out,"  %.*s\n",16,player_data[i].name);
    } else {
      fputs("Stopping server...\n",out); console->stop = true;
    }
  } else fputs("Unknown console command. Use help.\n",out);
}
void server_console_init (ServerConsole *console) {
  memset(console,0,sizeof(*console));
  #ifdef ESP_PLATFORM
  console->closed = true;
  #endif
}
void server_console_feed (ServerConsole *console, const char *data, size_t length, FILE *out) {
  for (size_t i = 0; i < length && !console->closed && !console->stop; i++) {
    unsigned char ch = (unsigned char)data[i];
    if (ch == '\n' || ch == '\r') {
      execute(console,out); console->used = 0; console->rejected = false;
    } else if (console->used == COMMAND_MAX_BYTES ||
               (ch != '\t' && (ch < 32 || ch > 126))) console->rejected = true;
    else if (!console->rejected) console->line[console->used++] = (char)ch;
  }
  fflush(out);
}
void server_console_end (ServerConsole *console, FILE *out) {
  if (console->closed) return;
  if (!console->stop && (console->used || console->rejected)) execute(console,out);
  console->closed = true; fflush(out);
}
void server_console_poll (ServerConsole *console, FILE *out) {
  if (console->closed || console->stop) return;
  /* Bound work per iteration so a busy pipe cannot starve server ticks. */
  for (unsigned i = 0; i < COMMAND_MAX_BYTES; i++) {
    char ch;
    #if defined(_WIN32)
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD type = GetFileType(input), available, got;
    if (type == FILE_TYPE_CHAR) {
      if (!_kbhit()) return;
      int key = _getch();
      if (key == 0 || key == 224) { (void)_getch(); continue; }
      if (key == '\b') {
        if (console->used && !console->rejected) { console->used--; fputs("\b \b",out); fflush(out); }
        continue;
      }
      ch = (char)key;
      if (ch == '\r') fputc('\n',out); else fputc(ch,out);
    } else {
      if (type == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(input,NULL,0,NULL,&available,NULL)) { server_console_end(console,out); return; }
        if (!available) return;
      } else if (type != FILE_TYPE_DISK) { server_console_end(console,out); return; }
      if (!ReadFile(input,&ch,1,&got,NULL) || !got) { server_console_end(console,out); return; }
    }
    #elif !defined(ESP_PLATFORM)
    struct pollfd input = {STDIN_FILENO,POLLIN,0};
    int ready = poll(&input,1,0);
    if (!ready || (ready < 0 && errno == EINTR)) return;
    if (ready < 0 || (input.revents & (POLLERR|POLLNVAL))) { server_console_end(console,out); return; }
    ssize_t got = read(STDIN_FILENO,&ch,1);
    if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (got <= 0) { server_console_end(console,out); return; }
    #else
    (void)out; return;
    #endif
    server_console_feed(console,&ch,1,out);
    if (console->stop) return;
  }
}
