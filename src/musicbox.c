#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "musicbox.h"
#include "midi.h"
#include "notes.h"
#include "packets.h"
#include "registries.h"
#include "tools.h"
#include "worldgen.h"

static char directory[200], songs[MUSICBOX_SONG_LIMIT][64];
static unsigned song_count;
static uint64_t clock_us;
typedef struct { int16_t x,z; uint8_t y,selected; uint64_t expires,next_menu,next_play; } Selection;
static Selection selections[MAX_PLAYERS];
typedef struct { MidiSong *song; uint64_t elapsed_us; uint32_t cursor; int16_t x,z; uint8_t y; } Playback;
static Playback playing[MUSICBOX_PLAYER_LIMIT];
static int player_index (const PlayerData *p) { for (int i = 0; i < MAX_PLAYERS; i++) if (p == &player_data[i]) return i; return -1; }
static bool near (const PlayerData *p, int x, int y, int z) {
  if (!p || p->client_fd < 0 || !p->health || (p->flags&0x22) || commands_gamemode(p) == 3) return false;
  if (x < -32768 || x > 32767 || z < -32768 || z > 32767 || y < 0 || y > 255) return false;
  int dx = (int)p->x-x, dy = (int)p->y-y, dz = (int)p->z-z;
  return dx >= -6 && dx <= 6 && dy >= -6 && dy <= 6 && dz >= -6 && dz <= 6 && getBlockAt(x,y,z) == B_jukebox;
}
static CommandResult reply (PlayerData *p, CommandResult result, const char *text) {
  sc_systemChat(p->client_fd,(char *)text,(uint16_t)strlen(text)); return result;
}
static bool filename (const char *name) {
  size_t n = strlen(name); if (n < 5 || n >= 64 || strcmp(name+n-4,".mid")) return false;
  for (size_t i = 0; i < n-4; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ')) return false;
  }
  return true;
}
static void add_song (const char *name) {
  if (!filename(name)) return;
  unsigned at = 0; while (at < song_count && strcmp(songs[at],name) < 0) at++;
  if (at < song_count && !strcmp(songs[at],name)) return;
  if (at == MUSICBOX_SONG_LIMIT) return;
  if (song_count < MUSICBOX_SONG_LIMIT) song_count++;
  for (unsigned i = song_count-1; i > at; i--) memcpy(songs[i],songs[i-1],64);
  strcpy(songs[at],name);
}
void musicbox_shutdown (void) {
  for (unsigned i = 0; i < MUSICBOX_PLAYER_LIMIT; i++) { free(playing[i].song); memset(&playing[i],0,sizeof(playing[i])); }
}
bool musicbox_init (const char *folder) {
  if (!folder || strlen(folder) >= sizeof(directory)) return false;
  musicbox_shutdown(); memset(selections,0,sizeof(selections)); song_count = 0;
  memmove(directory,folder,strlen(folder)+1);
#ifdef _WIN32
  char pattern[208]; snprintf(pattern,sizeof(pattern),"%s/*.mid",directory);
  WIN32_FIND_DATAA entry; HANDLE h = FindFirstFileA(pattern,&entry);
  if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
  do { if (!(entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) add_song(entry.cFileName); } while (FindNextFileA(h,&entry));
  bool ok = GetLastError() == ERROR_NO_MORE_FILES; FindClose(h); return ok;
#else
  DIR *dir = opendir(directory); if (!dir) return errno == ENOENT;
  errno = 0; struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (filename(entry->d_name)) {
      struct stat st;
      if (fstatat(dirfd(dir),entry->d_name,&st,AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= MIDI_FILE_LIMIT) add_song(entry->d_name);
    }
    errno = 0;
  }
  bool ok = errno == 0; if (closedir(dir)) ok = false; return ok;
#endif
}
void musicbox_reset_player (PlayerData *p) { int slot = player_index(p); if (slot >= 0) memset(&selections[slot],0,sizeof(selections[slot])); }
void musicbox_menu (PlayerData *p, int x, int y, int z) {
  int slot = player_index(p); if (slot < 0 || !near(p,x,y,z)) return;
  Selection *s = &selections[slot]; if (clock_us < s->next_menu) return;
  *s = (Selection){(int16_t)x,(int16_t)z,(uint8_t)y,1,clock_us+60000000,clock_us+1000000,s->next_play};
  reply(p,COMMAND_OK,"Musicbox: /music <number> to play, /music stop to stop. Selection lasts 60 seconds.");
  if (!song_count) reply(p,COMMAND_OK,"No songs. Add .mid files to songs/ and ask an admin to run /music reload.");
  for (unsigned i = 0; i < song_count; i++) { char line[96]; snprintf(line,sizeof(line),"/music %u - %s",i+1,songs[i]); reply(p,COMMAND_OK,line); }
}
static MidiSong *load_song (unsigned index) {
  if (index >= song_count) return NULL;
  char path[268]; snprintf(path,sizeof(path),"%s/%s",directory,songs[index]);
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES || (attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) return NULL;
  FILE *f = fopen(path,"rb");
#else
  int fd = open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
  if (fd < 0) return NULL;
  struct stat st;
  if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > MIDI_FILE_LIMIT) { close(fd); return NULL; }
  FILE *f = fdopen(fd,"rb"); if (!f) { close(fd); return NULL; }
#endif
  if (!f) return NULL;
  uint8_t *data = malloc(MIDI_FILE_LIMIT); MidiSong *song = malloc(sizeof(*song));
  if (!data || !song) { free(data); free(song); fclose(f); return NULL; }
  size_t n = fread(data,1,MIDI_FILE_LIMIT,f); int extra = fgetc(f);
  bool ok = !ferror(f) && extra == EOF; if (fclose(f)) ok = false;
  if (!ok || !midi_parse(data,n,song)) { free(song); song = NULL; }
  free(data); return song;
}
CommandResult musicbox_command (PlayerData *p, int argc, char *const argv[]) {
  int slot = player_index(p); if (slot < 0) return COMMAND_INVALID;
  if (argc != 2) return reply(p,COMMAND_USAGE,"Right-click a musicbox, then /music <number|stop>. Admin: /music reload.");
  if (!strcmp(argv[1],"reload")) {
    if (!commands_is_admin(p)) return reply(p,COMMAND_DENIED,"Administrator permission required.");
    if (!musicbox_init(directory[0] ? directory : "songs")) return reply(p,COMMAND_DENIED,"Could not read the songs directory.");
    return reply(p,COMMAND_OK,"Song list reloaded; playback stopped. Right-click the musicbox again.");
  }
  Selection *s = &selections[slot];
  if (!s->selected || clock_us > s->expires || !near(p,s->x,s->y,s->z)) return reply(p,COMMAND_DENIED,"Right-click a nearby musicbox first.");
  Playback *box = NULL, *available = NULL;
  for (unsigned i = 0; i < MUSICBOX_PLAYER_LIMIT; i++) {
    Playback *b = &playing[i];
    if (!b->song) available = b;
    else if (b->x == s->x && b->y == s->y && b->z == s->z) box = b;
  }
  if (!strcmp(argv[1],"stop")) {
    if (box) { free(box->song); memset(box,0,sizeof(*box)); }
    return reply(p,COMMAND_OK,"Musicbox stopped.");
  }
  if (!argv[1][0] || strlen(argv[1]) > 2) return reply(p,COMMAND_USAGE,"Choose a song number from the musicbox menu.");
  unsigned number = 0;
  for (const char *c = argv[1]; *c; c++) { if (*c < '0' || *c > '9') return reply(p,COMMAND_USAGE,"Invalid song number."); number = number*10u+(unsigned)(*c-'0'); }
  if (!number || number > song_count) return reply(p,COMMAND_USAGE,"Song number is not in the menu.");
  if (clock_us < s->next_play) return reply(p,COMMAND_DENIED,"Wait one second between musicbox actions.");
  s->next_play = clock_us+1000000;
  if (!box) box = available;
  if (!box) return reply(p,COMMAND_DENIED,"Both musicbox playback slots are busy.");
  MidiSong *song = load_song(number-1);
  if (!song) return reply(p,COMMAND_DENIED,"Unsupported, malformed or oversized MIDI file; playback unchanged.");
  free(box->song); *box = (Playback){song,0,0,s->x,s->z,s->y};
  return reply(p,COMMAND_OK,"Musicbox playing.");
}
void musicbox_block_changed (int x, int y, int z) {
  bool affected = false;
  for (unsigned i = 0; i < MUSICBOX_PLAYER_LIMIT; i++)
    if (playing[i].song && playing[i].x == x && playing[i].y == y && playing[i].z == z) affected = true;
  for (unsigned i = 0; i < MAX_PLAYERS; i++)
    if (selections[i].selected && selections[i].x == x && selections[i].y == y && selections[i].z == z) affected = true;
  if (!affected) return;
  if (getBlockAt(x,y,z) == B_jukebox) return;
  for (unsigned i = 0; i < MUSICBOX_PLAYER_LIMIT; i++) {
    Playback *b = &playing[i];
    if (b->song && b->x == x && b->y == y && b->z == z) { free(b->song); memset(b,0,sizeof(*b)); }
  }
  for (unsigned i = 0; i < MAX_PLAYERS; i++) if (selections[i].selected && selections[i].x == x && selections[i].y == y && selections[i].z == z) selections[i].selected = 0;
}
void musicbox_tick (int64_t elapsed_us) {
  if (elapsed_us <= 0) return;
  uint64_t dt = (uint64_t)elapsed_us;
  if (dt > UINT64_C(3600000000)) dt = UINT64_C(3600000000);
  clock_us += dt;
  for (unsigned i = 0; i < MUSICBOX_PLAYER_LIMIT; i++) {
    Playback *b = &playing[i]; if (!b->song) continue;
    if (getBlockAt(b->x,b->y,b->z) != B_jukebox) { free(b->song); memset(b,0,sizeof(*b)); continue; }
    b->elapsed_us += dt; uint64_t ms = b->elapsed_us/1000; unsigned sent = 0;
    while (b->cursor < b->song->count && b->song->notes[b->cursor].ms <= ms) {
      const MidiNote *n = &b->song->notes[b->cursor++];
      /* Bound packet bursts and skip stale notes after a slow generation frame. */
      if (ms-n->ms <= 100 && sent++ < 32) notes_play(b->x,b->y,b->z,n->instrument,n->note,n->velocity);
    }
    if (b->cursor == b->song->count && ms >= b->song->duration_ms) { free(b->song); memset(b,0,sizeof(*b)); }
  }
}
