#ifndef H_MIDI
#define H_MIDI
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MIDI_FILE_LIMIT 65536
#define MIDI_NOTE_LIMIT 2048
#define MIDI_TRACK_LIMIT 16
#define MIDI_DURATION_MS 600000
typedef struct { uint32_t ms; uint8_t note,velocity,instrument,reserved; } MidiNote;
typedef struct { uint32_t count,duration_ms; MidiNote notes[MIDI_NOTE_LIMIT]; } MidiSong;
bool midi_parse (const uint8_t *data, size_t size, MidiSong *out);
#endif
