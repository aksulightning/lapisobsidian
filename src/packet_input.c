#include <errno.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#elif defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif
#include "packet_input.h"
#include "webclient.h"

/* One shared frame, no per-client payload allocation or per-packet heap churn. */
static uint8_t frame[PACKET_INPUT_LIMIT+3u];
static size_t cursor, end;
static int active_fd = -1;
static bool failed;
static bool would_block (void) {
#ifdef _WIN32
  int error = WSAGetLastError(); return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
static int receive (int fd, size_t size, int flags) {
#if defined(LAPIS_ENABLE_WEBCLIENT) && LAPIS_ENABLE_WEBCLIENT == 1
  if (webclient_has(fd)) return webclient_recv(fd,frame,size,flags == MSG_PEEK);
#endif
#ifdef _WIN32
  return recv(fd,(char *)frame,(int)size,flags);
#else
  return (int)recv(fd,frame,size,flags);
#endif
}
void packet_input_reset (PacketInput *input, int fd) { *input = (PacketInput){.fd=fd}; }
int packet_input_poll (PacketInput *input, int64_t now) {
  if (!input || input->fd < 0 || now < 0 || active_fd >= 0) return -1;
  if (input->pending && (now < input->started || now-input->started >= PACKET_INPUT_TIMEOUT_US)) return -1;
  /* Peek a protocol length prefix, at most three bytes. */
  int n = receive(input->fd,3,MSG_PEEK);
  if (n < 0) return would_block() ? 0 : -1;
  if (!n) return -1;
  if (!input->pending) { input->pending = true; input->started = now; }
  uint32_t length = 0; unsigned prefix = 0;
  for (; prefix < (unsigned)n; prefix++) {
    uint8_t byte = frame[prefix]; length |= (uint32_t)(byte&127u)<<(prefix*7u);
    if (!(byte&128u)) { prefix++; break; }
  }
  if (frame[prefix-1]&128u) return prefix == 3 ? -1 : 0;
  if (!length || length > PACKET_INPUT_LIMIT) return -1;
  size_t total = prefix+length;
  n = receive(input->fd,total,MSG_PEEK);
  if (n < 0) return would_block() ? 0 : -1;
  if (!n) return -1;
  if ((size_t)n < total) return 0;
  /* This is the sole socket reader: the complete peeked frame can be consumed
   * without waiting. A short/error read closes the connection, never spins. */
  n = receive(input->fd,total,0);
  if (n < 0 || (size_t)n != total) return -1;
  input->pending = false; active_fd = input->fd; cursor = prefix; end = total; failed = false;
  return 1;
}
bool packet_input_read (int fd, void *buffer, size_t count, ssize_t *result) {
  if (active_fd < 0) return false;
  if (fd != active_fd || failed || count > end-cursor) {
    failed = true; *result = -1; errno = EINVAL; return true;
  }
  memcpy(buffer,frame+cursor,count); cursor += count; *result = (ssize_t)count; return true;
}
size_t packet_input_remaining (void) { return active_fd >= 0 && !failed ? end-cursor : 0; }
bool packet_input_end (void) {
  bool ok = active_fd >= 0 && !failed && cursor == end;
  active_fd = -1; cursor = end = 0; failed = false; return ok;
}
