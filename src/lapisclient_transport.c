#ifdef LAPISCLIENT
/* RFC 6455 transport. No target address, TCP forwarding or game packet API. */
#include "globals.h"
#include "lapisclient.h"
#include "lapisclient_internal.h"
#include "plates.h"
#include "procedures.h"
#include "tools.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
LcPeer lc_peers[16];
static int listener = -1;
static const char *origins;
LcPeer *lc_peer(int fd) {
  if (listener < 0 || fd < 0)
    return NULL;
  for (int i = 0; i < 16; i++)
    if (lc_peers[i].fd == fd)
      return &lc_peers[i];
  return NULL;
}
int lc_is(int fd) { return lc_peer(fd) != NULL; }
static int queue(LcPeer *p, const void *data, size_t n) {
  if (!p || p->closing)
    return 1;
  if (n > LC_OUTPUT - p->queued) {
    p->closing = true;
    p->deadline = get_program_time();
    return 1;
  }
  if (!p->output) {
    p->output = malloc(LC_OUTPUT);
    if (!p->output) {
      p->closing = true;
      return 1;
    }
  }
  memcpy(p->output + p->queued, data, n);
  p->queued += n;
  return 0;
}
int lc_frame(int fd, unsigned opcode, const void *data, size_t n) {
  LcPeer *p = lc_peer(fd);
  if (!p || p->stage == 0 || p->closing || !plates_fd_active(fd))
    return 1;
  uint8_t h[10] = {(uint8_t)(128 | opcode)};
  size_t len = 2;
  if (n < 126)
    h[1] = (uint8_t)n;
  else if (n <= 65535) {
    h[1] = 126;
    h[2] = (uint8_t)(n >> 8);
    h[3] = (uint8_t)n;
    len = 4;
  } else {
    h[1] = 127;
    for (unsigned i = 0; i < 8; i++)
      h[2 + i] = (uint8_t)((uint64_t)n >> (56 - 8 * i));
    len = 10;
  }
  if (!p->output && !(p->output = malloc(LC_OUTPUT))) {
    p->closing = true;
    return 1;
  }
  if (n + len > LC_OUTPUT - p->queued) {
    p->closing = true;
    p->deadline = get_program_time();
    return 1;
  }
  return queue(p, h, len) || queue(p, data, n);
}
int lc_json(int fd, const char *format, ...) {
  char data[4096];
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(data, sizeof(data), format, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof(data))
    return 1;
  return lc_frame(fd, 1, data, (size_t)n);
}
int lc_text(int fd, const char *type, const char *key, const char *text,
            size_t n) {
  char escaped[3100];
  size_t at = 0;
  if (n > 512)
    return 1;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c < 32 || c == '"' || c == '\\') {
      int k = snprintf(escaped + at, sizeof(escaped) - at, "\\u%04x", c);
      at += (size_t)k;
    } else
      escaped[at++] = (char)c;
  }
  escaped[at] = 0;
  return lc_json(fd, "{\"type\":\"%s\",\"%s\":\"%s\"}", type, key, escaped);
}
void lc_fail(LcPeer *p, const char *code, const char *message) {
  if (p->closing)
    return;
  lc_json(p->fd, "{\"type\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}",
          p->stage == 2 ? "login_error" : "error", code, message);
  uint8_t close_code[] = {3, 240}; /* 1008 policy violation */
  lc_frame(p->fd, 8, close_code, 2);
  p->closing = true;
  p->deadline = get_program_time() + 1000000;
}
static void drop(LcPeer *p) {
  int fd = p->fd;
  plates_select_for_fd(fd);
  handlePlayerDisconnect(fd);
  close(fd);
  if (client_count)
    client_count--;
  free(p->output);
  memset(p, 0, sizeof(*p));
  p->fd = -1;
}
int lc_start(void) {
  for (int i = 0; i < 16; i++)
    lc_peers[i].fd = -1;
  const char *port_text = getenv("LAPIS_WS_PORT"),
             *host = getenv("LAPIS_WS_BIND");
  char *end;
  long port = port_text ? strtol(port_text, &end, 10) : 25566;
  if (port_text && (!*port_text || *end || port < 0 || port > 65535)) {
    fputs("Invalid LAPIS_WS_PORT\n", stderr);
    return 0;
  }
  if (!port)
    return 1;
  const char *access = getenv("LAPIS_WS_TOKEN");
  if (access && (strlen(access) < 32 || strlen(access) > 128 ||
                 strspn(access, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRST"
                                "UVWXYZ0123456789_-.~") != strlen(access))) {
    fputs("LAPIS_WS_TOKEN must be 32-128 URL-safe characters\n", stderr);
    return 0;
  }
  origins = getenv("LAPIS_WS_ORIGINS");
  if (!origins)
    origins = "http://localhost:8080,http://127.0.0.1:8080";
  if (!*origins || strchr(origins, '*')) {
    fputs("LAPIS_WS_ORIGINS requires exact origins, no wildcard\n", stderr);
    return 0;
  }
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_port = htons((uint16_t)port)};
  if (inet_pton(AF_INET, host ? host : "127.0.0.1", &address.sin_addr) != 1)
    return 0;
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0)
    return 0;
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
      listen(listener, 16) || fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
    perror("lapisclient listen");
    close(listener);
    listener = -1;
    return 0;
  }
  printf("lapisclient v1 listening on ws://%s:%ld/lapisclient\n",
         host ? host : "127.0.0.1", port);
  return 1;
}
static bool token(const char *list, const char *wanted, bool insensitive) {
  size_t n = strlen(wanted);
  while (*list) {
    while (*list == ' ' || *list == '\t' || *list == ',')
      list++;
    const char *e = strchr(list, ',');
    if (!e)
      e = list + strlen(list);
    const char *tail = e;
    while (tail > list && (tail[-1] == ' ' || tail[-1] == '\t'))
      tail--;
    if ((size_t)(tail - list) == n &&
        !(insensitive ? strncasecmp(list, wanted, n)
                      : strncmp(list, wanted, n)))
      return true;
    list = *e ? e + 1 : e;
  }
  return false;
}
static bool header(char *request, const char *name, char *out, size_t cap) {
  char *p = strstr(request, "\r\n");
  bool found = false;
  size_t n = strlen(name);
  while (p && p[2]) {
    p += 2;
    char *end = strstr(p, "\r\n");
    if (!end)
      return false;
    if (end == p)
      break;
    char *colon = memchr(p, ':', (size_t)(end - p));
    if (!colon)
      return false;
    if ((size_t)(colon - p) == n && !strncasecmp(p, name, n)) {
      if (found)
        return false;
      found = true;
      char *v = colon + 1;
      while (v < end && (*v == ' ' || *v == '\t'))
        v++;
      char *tail = end;
      while (tail > v && (tail[-1] == ' ' || tail[-1] == '\t'))
        tail--;
      size_t size = (size_t)(tail - v);
      if (size >= cap)
        return false;
      memcpy(out, v, size);
      out[size] = 0;
    }
    p = end;
  }
  return found;
}
static void handshake(LcPeer *p) {
  if (p->used > LC_INPUT) {
    p->closing = true;
    return;
  }
  p->input[p->used] = 0;
  char *end = strstr((char *)p->input, "\r\n\r\n");
  if (!end)
    return;
  size_t size = (size_t)(end - (char *)p->input) + 4;
  char origin[1024], key[128], version[32], sub[256], upgrade[64],
      connection[128], host[256];
  bool ok =
      !memcmp(p->input, "GET /lapisclient HTTP/1.1\r\n", 27) &&
      header((char *)p->input, "Host", host, sizeof(host)) && *host &&
      header((char *)p->input, "Origin", origin, sizeof(origin)) && *origin &&
      token(origins, origin, false) &&
      header((char *)p->input, "Sec-WebSocket-Protocol", sub, sizeof(sub)) &&
      token(sub, "lapisclient", false) &&
      header((char *)p->input, "Sec-WebSocket-Version", version,
             sizeof(version)) &&
      !strcmp(version, "13") &&
      header((char *)p->input, "Upgrade", upgrade, sizeof(upgrade)) &&
      !strcasecmp(upgrade, "websocket") &&
      header((char *)p->input, "Connection", connection, sizeof(connection)) &&
      token(connection, "upgrade", true) &&
      header((char *)p->input, "Sec-WebSocket-Key", key, sizeof(key)) &&
      strlen(key) == 24;
  unsigned char decoded[32];
  if (ok) {
    ok = key[22] == '=' && key[23] == '=' &&
         EVP_DecodeBlock(decoded, (unsigned char *)key, 24) == 18;
  }
  if (!ok) {
    const char response[] = "HTTP/1.1 403 Forbidden\r\nConnection: "
                            "close\r\nContent-Length: 0\r\n\r\n";
    queue(p, response, sizeof(response) - 1);
    p->closing = true;
    p->deadline = get_program_time() + 1000000;
    return;
  }
  char source[128], accept[64], response[512];
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned length;
  snprintf(source, sizeof(source), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
           key);
  if (!EVP_Digest(source, strlen(source), digest, &length, EVP_sha1(), NULL)) {
    p->closing = true;
    return;
  }
  EVP_EncodeBlock((unsigned char *)accept, digest, (int)length);
  int n = snprintf(response, sizeof(response),
                   "HTTP/1.1 101 Switching Protocols\r\nUpgrade: "
                   "websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                   "%s\r\nSec-WebSocket-Protocol: lapisclient\r\n\r\n",
                   accept);
  queue(p, response, (size_t)n);
  memmove(p->input, p->input + size, p->used - size);
  p->used -= size;
  p->stage = 1;
  p->deadline = get_program_time() + 10000000;
}
static void frames(LcPeer *p) {
  if (p->used < 2)
    return;
  unsigned op = p->input[0] & 15;
  bool fin = (p->input[0] & 128) != 0;
  size_t n = p->input[1] & 127, at = 2;
  if ((p->input[0] & 112) || !(p->input[1] & 128) ||
      (op != 0 && op != 1 && op != 8 && op != 9 && op != 10)) {
    lc_fail(p, "invalid_frame", "Unsupported WebSocket frame");
    return;
  }
  if (n == 126) {
    if (p->used < 4)
      return;
    n = (size_t)p->input[2] * 256 + p->input[3];
    at = 4;
    if (n < 126) {
      lc_fail(p, "invalid_frame", "Noncanonical length");
      return;
    }
  } else if (n == 127) {
    lc_fail(p, "message_too_large", "Message exceeds 4096 bytes");
    return;
  }
  if (n > LC_INPUT || (op >= 8 && (!fin || n > 125)) ||
      (op < 8 && n > LC_INPUT - p->message_size)) {
    lc_fail(p, "message_too_large", "Message exceeds limit");
    return;
  }
  if (p->used < at + 4 + n)
    return;
  uint8_t *mask = p->input + at, *data = mask + 4;
  for (size_t i = 0; i < n; i++)
    data[i] ^= mask[i % 4];
  if (op == 8) {
    if (n == 1) {
      lc_fail(p, "invalid_frame", "Malformed close frame");
      return;
    }
    if (n >= 2) {
      unsigned status = (unsigned)data[0] * 256 + data[1];
      if (status < 1000 || status >= 5000 || status == 1004 || status == 1005 ||
          status == 1006 || (status >= 1015 && status < 3000) ||
          !lc_utf8(data + 2, n - 2)) {
        lc_fail(p, "invalid_frame", "Invalid close status or UTF-8 reason");
        return;
      }
    }
    uint8_t code[] = {3, 232};
    lc_frame(p->fd, 8, code, 2);
    p->closing = true;
    p->deadline = get_program_time() + 1000000;
  } else if (op == 9)
    lc_frame(p->fd, 10, data, n);
  else if (op == 1 || op == 0) {
    if ((op == 0) != p->fragmented) {
      lc_fail(p, "invalid_frame", "Invalid continuation");
      return;
    }
    memcpy(p->message + p->message_size, data, n);
    p->message_size += n;
    p->fragmented = !fin;
    if (fin) {
      lc_message(p, (char *)p->message, p->message_size);
      p->message_size = 0;
    }
  }
  p->last_rx = get_program_time();
  memmove(p->input, p->input + at + 4 + n, p->used - at - 4 - n);
  p->used -= at + 4 + n;
}
void lc_poll(void) {
  if (listener < 0)
    return;
  if (client_count < MAX_PLAYERS)
    for (int i = 0; i < 16; i++)
      if (lc_peers[i].fd < 0) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0)
          break;
        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
          close(fd);
          break;
        }
        lc_peers[i] = (LcPeer){.fd = fd,
                               .deadline = get_program_time() + 10000000,
                               .last_rx = get_program_time(),
                               .movement_budget = 2};
        client_count++;
        break;
      }
  for (int i = 0; i < 16; i++) {
    LcPeer *p = &lc_peers[i];
    if (p->fd < 0)
      continue;
    plates_select_for_fd(p->fd);
    int64_t now = get_program_time();
    if (!p->closing &&
        ((p->stage < 4 && now > p->deadline) || now - p->last_rx > 30000000))
      lc_fail(p, "timeout", "Session timed out");
    if (!p->closing && p->partial_since && now - p->partial_since > 10000000)
      lc_fail(p, "timeout", "Incomplete message timed out");
    if (!p->closing) {
      ssize_t n =
          recv(p->fd, p->input + p->used, sizeof(p->input) - 1 - p->used, 0);
      if (n == 0) {
        drop(p);
        continue;
      }
      if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        drop(p);
        continue;
      }
      if (n > 0) {
        if (!p->partial_since)
          p->partial_since = now;
        p->used += (size_t)n;
      }
      if (p->stage == 0)
        handshake(p);
      if (p->stage && !p->closing)
        frames(p); /* fairness: one application message per peer */
      if (!p->used && !p->fragmented)
        p->partial_since = 0;
      if (p->used == sizeof(p->input) - 1 && !p->closing)
        lc_fail(p, "message_too_large", "Receive buffer limit");
    }
    if (p->sent < p->queued) {
      ssize_t n =
          send(p->fd, p->output + p->sent, p->queued - p->sent, MSG_NOSIGNAL);
      if (n > 0)
        p->sent += (size_t)n;
      else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
               errno != EINTR) {
        drop(p);
        continue;
      }
    }
    if (p->sent == p->queued)
      p->sent = p->queued = 0;
    if (p->closing && (!p->queued || now >= p->deadline))
      drop(p);
  }
}
#endif
