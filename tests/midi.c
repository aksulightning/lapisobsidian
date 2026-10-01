#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "midi.h"
#include "notes.h"
static MidiSong song;
static const uint8_t simple[] = {
  'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
  'M','T','r','k',0,0,0,19,
  0,0x90,60,100, 96,0x80,60,0, 0,0x90,64,80, 96,64,0, 0,255,47,0
};
int main (int argc, char **argv) {
  assert(!midi_parse(simple,MIDI_FILE_LIMIT+1,&song));
  assert(midi_parse(simple,sizeof(simple),&song));
  assert(song.count == 2 && song.duration_ms == 1000);
  assert(song.notes[0].ms == 0 && song.notes[0].note == 6 && song.notes[0].velocity == 100);
  assert(song.notes[1].ms == 500 && song.notes[1].note == 10);
  for (size_t n = 0; n < sizeof(simple); n++) assert(!midi_parse(simple,n,&song));
  assert(!midi_parse(NULL,0,&song) && !midi_parse(simple,sizeof(simple),NULL));
  uint8_t bad[sizeof(simple)];
  const unsigned offsets[] = {0,7,9,11,12,14,21,23,24,25,40};
  for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
    memcpy(bad,simple,sizeof(bad)); bad[offsets[i]] = 255; assert(!midi_parse(bad,sizeof(bad),&song));
  }
  /* Format 1 conductor track doubles tempo after the first beat. */
  const uint8_t multi[] = {
    'M','T','h','d',0,0,0,6,0,1,0,2,0,96,
    'M','T','r','k',0,0,0,11, 96,255,81,3,15,66,64, 96,255,47,0,
    'M','T','r','k',0,0,0,14, 0,0xc0,33, 96,0x90,48,90, 96,72,90, 0,255,47,0
  };
  assert(midi_parse(multi,sizeof(multi),&song));
  assert(song.count == 2 && song.notes[0].ms == 500 && song.notes[1].ms == 1500 && song.duration_ms == 1500);
  assert(song.notes[0].instrument == NOTE_BASS && song.notes[0].note == 6);
  /* Reject excessive zero-time polyphony, malformed VLQs and missing EOT. */
  uint8_t dense[512]; memcpy(dense,simple,22); size_t at = 22;
  for (unsigned i = 0; i < 33; i++) { dense[at++]=0; dense[at++]=0x90; dense[at++]=60; dense[at++]=100; }
  dense[at++]=0; dense[at++]=255; dense[at++]=47; dense[at++]=0; dense[21]=(uint8_t)(at-22);
  assert(!midi_parse(dense,at,&song));
  memcpy(bad,simple,sizeof(bad)); memset(bad+22,0x80,5); assert(!midi_parse(bad,sizeof(bad),&song));
  static uint8_t many[MIDI_FILE_LIMIT]; memcpy(many,simple,22); at = 22;
  for (unsigned i = 0; i <= MIDI_NOTE_LIMIT; i++) { many[at++]=10; many[at++]=0x90; many[at++]=60; many[at++]=100; }
  many[at++]=0; many[at++]=255; many[at++]=47; many[at++]=0;
  uint32_t length = (uint32_t)(at-22);
  for (unsigned i = 0; i < 4; i++) many[18+i]=(uint8_t)(length>>(24-8*i));
  assert(!midi_parse(many,at,&song)); /* More than 2048 notes, without triggering the burst cap. */
  const uint8_t long_song[] = {
    'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,10,
    0x87,0x84,1,0x90,60,100,0,255,47,0
  };
  assert(!midi_parse(long_song,sizeof(long_song),&song)); /* Beyond ten minutes. */
  if (argc == 2) {
    FILE *f = fopen(argv[1],"rb"); assert(f); size_t n = fread(many,1,sizeof(many),f); assert(!ferror(f)); fclose(f);
    assert(midi_parse(many,n,&song) && song.count == 8 && song.duration_ms == 4000);
  }
  /* Deterministic mutations exercise bounded reads without assuming every mutation is invalid. */
  for (unsigned i = 0; i < 4096; i++) { memcpy(bad,simple,sizeof(bad)); bad[i%sizeof(bad)] ^= (uint8_t)(i*73u); (void)midi_parse(bad,sizeof(bad),&song); }
  puts("MIDI: format 0/1, tempo merge, running status, pitch/program mapping, truncation and malformed/oversized event limits passed");
  return 0;
}
