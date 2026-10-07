#ifndef H_SERVER_CONSOLE
#define H_SERVER_CONSOLE
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "commands.h"

typedef struct {
  char line[COMMAND_MAX_BYTES+1];
  size_t used;
  bool rejected, closed, stop;
} ServerConsole;

void server_console_init (ServerConsole *console);
void server_console_feed (ServerConsole *console, const char *data, size_t length, FILE *out);
void server_console_end (ServerConsole *console, FILE *out);
void server_console_poll (ServerConsole *console, FILE *out);
#endif
