#ifndef H_NOTES
#define H_NOTES
#include <stdint.h>
enum { NOTE_HARP, NOTE_BASSDRUM, NOTE_SNARE, NOTE_HAT, NOTE_BASS, NOTE_INSTRUMENTS };
uint8_t notes_instrument (int x, int y, int z);
void notes_play (int x, int y, int z, uint8_t instrument, uint8_t note, uint8_t velocity);
#endif
