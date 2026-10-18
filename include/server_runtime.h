/* Lifecycle boundaries shared by the process entry point and platform adapters. */
#ifndef H_SERVER_RUNTIME
#define H_SERVER_RUNTIME
#include <stdbool.h>
#include "globals.h"

/* Call before accepting clients: recover saves, select world state and mark
 * connection slots unused. Returns EXIT_SUCCESS only when startup is complete. */
int server_initialize(int argc, char **argv, int clients[MAX_PLAYERS]);
/* Shutdown-only: save loaded worlds and players, then release world services.
 * False reports a sidecar save failure; this is not an atomic world snapshot. */
bool server_save_worlds(void);
/* Singleplayer uses a placeholder listener; accepted IDs may be virtual. */
int server_listen(void);
/* Poll once; -1 means no accepted client. Close clients via disconnectClient. */
int server_accept(int server_fd);
void server_close(int server_fd);
#endif
