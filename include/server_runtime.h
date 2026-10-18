/* Lifecycle boundaries shared by the process entry point and platform adapters. */
#ifndef H_SERVER_RUNTIME
#define H_SERVER_RUNTIME
#include <stdbool.h>
#include "globals.h"

int server_initialize(int argc, char **argv, int clients[MAX_PLAYERS]);
bool server_save_worlds(void);
int server_listen(void);
int server_accept(int server_fd);
void server_close(int server_fd);
#endif
