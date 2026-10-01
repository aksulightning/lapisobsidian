/* Bounded Standard MIDI File 0/1 reader. No synthesis or external runtime. */
#include <string.h>
#include "midi.h"
#include "notes.h"
typedef struct {
  const uint8_t *data; size_t at,end; uint64_t tick;
  uint32_t tempo; uint8_t running,status,a,b,meta; bool ended;
} Track;
static uint32_t be32 (const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static bool vlq (Track *t, uint32_t *out) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 4; i++) {
    if (t->at == t->end) return false;
    uint8_t b = t->data[t->at++]; n = (n<<7)|(b&127u);
    if (!(b&128u)) { *out = n; return true; }
  }
  return false;
}
static bool next (Track *t) {
  uint32_t delta; if (!vlq(t,&delta) || t->at == t->end) return false;
  if (t->tick > UINT32_MAX-delta) return false;
  t->tick += delta; t->tempo = 0; t->meta = 0; t->a = t->b = 0;
  uint8_t status = t->data[t->at];
  if (status&128u) t->at++;
  else { status = t->running; if (status < 0x80 || status >= 0xf0) return false; }
  t->status = status;
  if (status < 0xf0) {
    t->running = status;
    unsigned bytes = (status&0xe0u) == 0xc0 ? 1u : 2u;
    if (t->end-t->at < bytes || t->data[t->at] > 127) return false;
    t->a = t->data[t->at++];
    if (bytes == 2) { t->b = t->data[t->at++]; if (t->b > 127) return false; }
    return true;
  }
  t->running = 0;
  if (status != 0xff && status != 0xf0 && status != 0xf7) return false;
  if (status == 0xff) { if (t->at == t->end) return false; t->meta = t->data[t->at++]; if (t->meta > 127) return false; }
  uint32_t len; if (!vlq(t,&len) || len > t->end-t->at) return false;
  if (status == 0xff && t->meta == 0x51) {
    if (len != 3) return false;
    t->tempo = (uint32_t)t->data[t->at]<<16 | (uint32_t)t->data[t->at+1]<<8 | t->data[t->at+2];
    if (!t->tempo) return false;
  }
  if (status == 0xff && t->meta == 0x2f && (len != 0 || t->at != t->end)) return false;
  t->at += len; return true;
}
bool midi_parse (const uint8_t *data, size_t size, MidiSong *out) {
  if (!out) return false;
  out->count = out->duration_ms = 0;
  if (!data || size < 14 || size > MIDI_FILE_LIMIT || memcmp(data,"MThd",4) || be32(data+4) != 6) return false;
  unsigned format = (unsigned)data[8]*256+data[9], count = (unsigned)data[10]*256+data[11];
  unsigned division = (unsigned)data[12]*256+data[13];
  if (format > 1 || !count || count > MIDI_TRACK_LIMIT || (format == 0 && count != 1) || !division || (division&0x8000u)) return false;
  Track tracks[MIDI_TRACK_LIMIT] = {{0}}; size_t at = 14;
  for (unsigned i = 0; i < count; i++) {
    if (size-at < 8 || memcmp(data+at,"MTrk",4)) return false;
    uint32_t len = be32(data+at+4); at += 8;
    if (len > size-at) return false;
    tracks[i].data = data; tracks[i].at = at; tracks[i].end = at+len; at += len;
    if (!next(&tracks[i])) return false;
  }
  if (at != size) return false;
  uint8_t program[16] = {0}; uint64_t tick = 0, micros = 0, fraction = 0;
  uint32_t tempo = 500000, bucket = UINT32_MAX, bucket_notes = 0;
  unsigned processed = 0;
  for (;;) {
    unsigned pick = count;
    for (unsigned i = 0; i < count; i++) if (!tracks[i].ended && (pick == count || tracks[i].tick < tracks[pick].tick)) pick = i;
    if (pick == count) break;
    if (++processed > MIDI_FILE_LIMIT) return false;
    Track *t = &tracks[pick];
    uint64_t elapsed = (t->tick-tick)*tempo+fraction;
    micros += elapsed/division; fraction = elapsed%division; tick = t->tick;
    if (micros > (uint64_t)MIDI_DURATION_MS*1000) return false;
    uint32_t ms = (uint32_t)(micros/1000);
    if (t->tempo) tempo = t->tempo;
    if ((t->status&0xf0u) == 0xc0) program[t->status&15u] = t->a;
    if ((t->status&0xf0u) == 0x90 && t->b) {
      if (out->count == MIDI_NOTE_LIMIT) return false;
      if (ms/50 != bucket) { bucket = ms/50; bucket_notes = 0; }
      if (++bucket_notes > 32) return false;
      uint8_t instrument = program[t->status&15u] >= 32 && program[t->status&15u] < 40 ? NOTE_BASS : NOTE_HARP;
      if ((t->status&15u) == 9) instrument = t->a == 35 || t->a == 36 ? NOTE_BASSDRUM : t->a == 38 || t->a == 40 ? NOTE_SNARE : NOTE_HAT;
      /* Fold by octaves into the client's two-octave F#3..F#5 range. */
      int note = t->a;
      while (note < 54) note += 12;
      while (note > 78) note -= 12;
      out->notes[out->count++] = (MidiNote){ms,(uint8_t)(note-54),t->b,instrument,0};
    }
    out->duration_ms = ms;
    if (t->status == 0xff && t->meta == 0x2f) t->ended = true;
    else if (!next(t)) return false;
  }
  return out->count != 0;
}
