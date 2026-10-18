/* Framed socket IO with optional authentication and durable output deferral. */
#include "plates.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef ESP_PLATFORM
  #include "lwip/sockets.h"
  #include "lwip/netdb.h"
  #include "esp_timer.h"
#else
  #ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
  #else
    #include <sys/socket.h>
    #include <arpa/inet.h>
  #endif
  #include <unistd.h>
  #include <time.h>
  #ifndef CLOCK_MONOTONIC
    #define CLOCK_MONOTONIC 1
  #endif
#endif

#include "globals.h"
#include "varnum.h"
#include "procedures.h"
#include "tools.h"
#include "packet_input.h"
#include "drop_store.h"
#include "auth.h"
#include "webclient.h"

// Helps notice misread packets and clean up after errors
uint64_t total_bytes_received = 0;

ssize_t recv_all (int client_fd, void *buf, size_t n, uint8_t require_first) {
  ssize_t framed;
  if (packet_input_read(client_fd,buf,n,&framed)) {
    if (framed > 0) total_bytes_received += (uint64_t)framed;
    return framed;
  }
  char *p = buf;
  size_t total = 0;

  // Track time of last meaningful network update
  // Used to handle timeout when client is stalling
  int64_t last_update_time = get_program_time();

  // If requested, exit early when first byte not immediately available
  if (require_first) {
    ssize_t r = recv(client_fd, p, 1, MSG_PEEK);
    if (r <= 0) {
      if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0; // no first byte available yet
      }
      return -1; // error or connection closed
    }
  }

  // Busy-wait (with task yielding) until we get exactly n bytes
  while (total < n) {
    ssize_t r = recv(client_fd, p + total, n - total, 0);
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // handle network timeout
        if (get_program_time() - last_update_time > NETWORK_TIMEOUT_TIME) {
          disconnectClient(&client_fd, -1);
          return -1;
        }
        task_yield();
        continue;
      } else {
        total_bytes_received += total;
        return -1; // real error
      }
    } else if (r == 0) {
      // connection closed before full read
      total_bytes_received += total;
      return total;
    }
    total += r;
    last_update_time = get_program_time();
  }

  total_bytes_received += total;
  return total; // got exactly n bytes
}

static ssize_t send_transport (int client_fd, const void *buf, ssize_t len) {
  // Treat any input buffer as *uint8_t for simplicity
  const uint8_t *p = (const uint8_t *)buf;
  ssize_t sent = 0;

  // Track time of last meaningful network update
  // Used to handle timeout when client is stalling
  int64_t last_update_time = get_program_time();

  // Busy-wait (with task yielding) until all data has been sent
  while (sent < len) {
    #ifdef _WIN32
      ssize_t n = send(client_fd, p + sent, len - sent, 0);
    #else
      ssize_t n = send(client_fd, p + sent, len - sent, MSG_NOSIGNAL);
    #endif
    if (n > 0) { // some data was sent, log it
      sent += n;
      last_update_time = get_program_time();
      continue;
    }
    if (n == 0) { // connection was closed, treat this as an error
      errno = ECONNRESET;
      return -1;
    }
    // not yet ready to transmit, try again
    #ifdef _WIN32 //handles windows socket timeout
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
    #else
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
    #endif
      // handle network timeout
      if (get_program_time() - last_update_time > NETWORK_TIMEOUT_TIME) {
        disconnectClient(&client_fd, -2);
        return -1;
      }
      task_yield();
      continue;
    }
    return -1; // real error
  }

  return sent;
}

ssize_t send_all (int client_fd, const void *buf, ssize_t len) {
  /* Defence in depth for world-scoped broadcasts from inherited packet code. */
  if (!plates_fd_active(client_fd)) return len;
  if (len < 0) return -1;
  int queued = drop_store_buffer(client_fd,buf,(size_t)len);
  if (queued) return queued < 0 ? -1 : len;
  #if (defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1) || defined(LAPIS_SINGLEPLAYER)
  if (webclient_has(client_fd)) return len < 0 ? -1 : webclient_send(client_fd,buf,(size_t)len);
  #endif
#if LAPIS_ENABLE_EXPERIMENTAL_AUTH
  if(auth_encrypted(client_fd)) return auth_send(client_fd,buf,(size_t)len,send_transport);
#endif
  return send_transport(client_fd,buf,len);
}

void discard_all (int client_fd, size_t remaining, uint8_t require_first) {
  while (remaining > 0) {
    size_t recv_n = remaining > MAX_RECV_BUF_LEN ? MAX_RECV_BUF_LEN : remaining;
    ssize_t received = recv_all(client_fd, recv_buffer, recv_n, require_first);
    if (received <= 0) return;
    if ((size_t)received > remaining) return;
    remaining -= received;
    require_first = false;
  }
}
