#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "server_config.h"
#include "beta173_rng.h"

static bool failure (char *error, size_t capacity, unsigned line, const char *message) {
  if (error && capacity) snprintf(error,capacity,"server.txt:%u: %s",line,message);
  return false;
}
static char *trim (char *text) {
  while (*text == ' ' || *text == '\t' || *text == '\r') text++;
  size_t n = strlen(text);
  while (n && (text[n-1] == ' ' || text[n-1] == '\t' || text[n-1] == '\r')) text[--n] = 0;
  return text;
}
static bool decimal (const char *text, unsigned min, unsigned max, uint16_t *out) {
  if (!*text) return false;
  unsigned value = 0;
  for (; *text; text++) {
    if (*text < '0' || *text > '9') return false;
    unsigned digit = (unsigned)(*text-'0');
    if (value > max/10 || (value == max/10 && digit > max%10)) return false;
    value = value*10+digit;
  }
  if (value < min) return false;
  *out = (uint16_t)value; return true;
}
static bool boolean (const char *text, bool *out) {
  if (!strcmp(text,"true")) *out = true;
  else if (!strcmp(text,"false")) *out = false;
  else return false;
  return true;
}
static bool message_valid (const char *text) {
  size_t n = strlen(text); if (n > CONFIG_MOTD_MAX) return false;
  for (size_t i = 0; i < n;) {
    unsigned char first = (unsigned char)text[i++];
    if (first < 32 || first == 127) return false;
    if (first < 128) continue;
    unsigned extra; uint32_t value, minimum;
    if (first >= 0xc2 && first <= 0xdf) { extra = 1; value = first&31u; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { extra = 2; value = first&15u; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { extra = 3; value = first&7u; minimum = 0x10000; }
    else return false;
    if (extra > n-i) return false;
    while (extra--) {
      unsigned char byte = (unsigned char)text[i++]; if ((byte&0xc0u) != 0x80u) return false;
      value = (value<<6)|(byte&63u);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}
static bool setting (ServerConfig *c, char *line, unsigned *seen) {
  static const char *const names[] = {"port","motd","gamemode","seed","mirror-horizontal","wheat-growth-seconds","experimental_enable_plates"};
  char *key = trim(line); if (!*key || *key == '#') return true;
  char *value = strchr(key,'='); if (!value) return false;
  *value++ = 0; key = trim(key); value = trim(value);
  unsigned id = 0; while (id < 7 && strcmp(key,names[id])) id++;
  if (id == 7 || (*seen&(1u<<id))) return false;
  *seen |= 1u<<id;
  switch (id) {
    case 0: return decimal(value,1,65535,&c->port);
    case 1:
      if (!message_valid(value)) return false;
      memcpy(c->motd,value,strlen(value)+1); return true;
    case 2: {
      static const char *const modes[] = {"survival","creative","adventure","spectator"};
      for (uint8_t i = 0; i < 4; i++) if (!strcmp(value,modes[i]) || (value[0] == '0'+i && !value[1])) { c->gamemode = i; return true; }
      return false;
    }
    case 3:
      c->seed_set = *value != 0;
      return !c->seed_set || beta173_seed_parse(value,&c->seed);
    case 4: return boolean(value,&c->mirror_horizontal);
    case 5: return decimal(value,1,600,&c->wheat_growth_seconds);
    case 6: return boolean(value,&c->experimental_enable_plates);
    default: return false;
  }
}
static bool create_defaults (const char *path, const ServerConfig *c) {
  /* C11 exclusive creation protects a file created by another process. */
  FILE *file = fopen(path,"wx"); if (!file) return false;
  bool ok = fprintf(file,
    "# Lapis Obsidian - edit and restart the server to apply changes.\n"
    "# Lines beginning with # are comments. Values are literal, without quotes.\n"
    "port=%u\nmotd=%s\ngamemode=%u\n"
    "# Empty seed uses the saved world seed, or the default for a new world.\n"
    "seed=\nmirror-horizontal=false\nwheat-growth-seconds=%u\nexperimental_enable_plates=false\n",
    (unsigned)c->port,c->motd,(unsigned)c->gamemode,(unsigned)c->wheat_growth_seconds) >= 0;
  if (fclose(file)) ok = false;
  return ok;
}
bool server_config_load (const char *path, ServerConfig *out, char *error, size_t capacity) {
  if (!path || !out) return failure(error,capacity,0,"missing configuration path or output");
  ServerConfig next = SERVER_CONFIG_DEFAULTS;
  FILE *file = fopen(path,"rb");
  if (!file && errno == ENOENT) {
    if (create_defaults(path,&next)) { *out = next; return true; }
    /* Reopen if someone else created the configuration in the meantime. */
    if (errno != EEXIST) return failure(error,capacity,0,"cannot create configuration");
    file = fopen(path,"rb");
  }
  if (!file) return failure(error,capacity,0,"cannot open configuration");
  char line[256]; size_t used = 0, total = 0; unsigned number = 1, seen = 0;
  bool ok = true; int ch;
  while ((ch = fgetc(file)) != EOF) {
    if (++total > 8192 || ch == 0) { ok = false; break; }
    if (ch == '\n') {
      line[used] = 0; if (!setting(&next,line,&seen)) { ok = false; break; }
      used = 0; number++;
    } else {
      if (used == sizeof(line)-1) { ok = false; break; }
      line[used++] = (char)ch;
    }
  }
  if (ok && used) { line[used] = 0; ok = setting(&next,line,&seen); }
  if (ferror(file)) ok = false;
  if (fclose(file)) ok = false;
  if (!ok) return failure(error,capacity,number,"invalid, duplicate, unknown or oversized setting; see docs/server-config.md");
  *out = next; return true;
}
bool server_config_arguments (ServerConfig *config, int argc, char **argv, char *error, size_t capacity) {
  if (!config || argc < 1 || !argv) return failure(error,capacity,0,"missing command-line arguments");
  ServerConfig next = *config; bool seed_seen = false, mirror_seen = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i],"--seed") && !seed_seen) {
      if (++i >= argc || !beta173_seed_parse(argv[i],&next.seed))
        return failure(error,capacity,0,"--seed requires a signed 64-bit decimal integer");
      seed_seen = next.seed_set = true;
    } else if ((!strcmp(argv[i],"--mirror-horizontal") || !strcmp(argv[i],"--no-mirror-horizontal")) && !mirror_seen) {
      next.mirror_horizontal = !strcmp(argv[i],"--mirror-horizontal"); mirror_seen = true;
    } else return failure(error,capacity,0,"usage: lapis-obsidian [--seed <integer>] [--mirror-horizontal | --no-mirror-horizontal]");
  }
  *config = next; return true;
}
