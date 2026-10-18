/* Platform TCP listener and optional local/browser transport acceptance. */
#include "server_runtime.h"
#include "server_config.h"
#include "webclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

int server_listen(void) {
  int server_fd = 0;
  #ifndef LAPIS_SINGLEPLAYER
  // Create server TCP socket
  int opt = 1;
  struct sockaddr_in server_addr;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }
#ifdef _WIN32
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
      (const char*)&opt, sizeof(opt)) < 0) {
#else
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
#endif
    perror("socket options failed");
    exit(EXIT_FAILURE);
  }

  // Bind socket to IP/port
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(server_config.port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  // Listen for incoming connections
  if (listen(server_fd, 5) < 0) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }
  printf("Server listening on port %u...\n", (unsigned)server_config.port);

  #endif /* !LAPIS_SINGLEPLAYER */

  #if (defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1) || defined(LAPIS_SINGLEPLAYER)
  if (!webclient_start()) { fputs("Cannot start optional web host\n",stderr); return -1; }
  #endif

  #ifndef LAPIS_SINGLEPLAYER
  // Make the socket non-blocking
  // This is necessary to not starve the idle task during slow connections
  #ifdef _WIN32
    u_long mode = 1;  // 1 = non-blocking
    if (ioctlsocket(server_fd, FIONBIO, &mode) != 0) {
      fprintf(stderr, "Failed to set non-blocking mode\n");
      exit(EXIT_FAILURE);
    }
  #else
  int flags = fcntl(server_fd, F_GETFL, 0);
  fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
  #endif

  #endif /* !LAPIS_SINGLEPLAYER */

  return server_fd;
}

int server_accept(int server_fd) {
  int client_fd = -1;
#ifndef LAPIS_SINGLEPLAYER
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
#else
  (void)server_fd;
#endif
#if (defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1) || defined(LAPIS_SINGLEPLAYER)
  if (client_fd == -1) client_fd = webclient_accept();
#endif
  if (client_fd != -1) {
      #if defined(_WIN32)
        u_long mode = 1;
        ioctlsocket(client_fd, FIONBIO, &mode);
      #elif !defined(LAPIS_SINGLEPLAYER)
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
      #endif
  }
  return client_fd;
}

void server_close(int server_fd) {
#ifdef LAPIS_SINGLEPLAYER
  (void)server_fd;
#endif
  #ifndef LAPIS_SINGLEPLAYER
  #ifdef _WIN32
  closesocket(server_fd);
  #else
  close(server_fd);
  #endif
  #endif

  #ifdef _WIN32 //cleanup windows socket
    WSACleanup();
  #endif

}
