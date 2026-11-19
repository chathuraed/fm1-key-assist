/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_KEY_ASSIST_H
#define FM1_KEY_ASSIST_H
/* KEY ASSIST: per-track scale lock and one-finger chords. Pure logic: no SDK,
 * hardware or allocation, so it builds for the device, the browser simulator
 * and host unit tests. All state is RAM-only and is never written to Flash. */
#include <stdint.h>

enum ka_scale { KA_OFF, KA_MAJOR, KA_MINOR, KA_DORIAN, KA_MIXOLYDIAN,
                KA_PENT_MAJ, KA_PENT_MIN, KA_BLUES, KA_HARM_MIN, KA_SCALES };
enum ka_chord { KA_SINGLE, KA_TRIAD, KA_SEVENTH, KA_POWER, KA_CHORDS };
/* SNAP: an off-scale key sounds the nearest lower scale tone.
 * WHITE: white keys are consecutive scale degrees from the root; black keys are silent. */
enum ka_map { KA_SNAP, KA_WHITE, KA_MAPS };
/* Knob order on the page: K1..K4. */
enum ka_field { KA_ROOT, KA_SCALE, KA_CHORD, KA_MAP, KA_FIELDS };
enum { KA_MAX_NOTES = 4, KA_TRACKS = 8, KA_KEYS = 27 };
/* Hold time on SCL that opens the page. */
enum { KA_HOLD_MS = 600 };

struct key_assist { uint8_t root, scale, chord, map; }; /* root 0..11, C = 0 */
extern struct key_assist key_assist_state[KA_TRACKS];    /* all zero = off */

const char *key_assist_scale_name(unsigned scale);
const char *key_assist_chord_name(unsigned chord);
const char *key_assist_map_name(unsigned map);
const char *key_assist_root_name(unsigned root);          /* "C", "C#", ... "B" */
/* 12-bit mask, bit n = semitone n above the root; 0 for KA_OFF/invalid. */
unsigned key_assist_scale_mask(unsigned scale);
/* Assist changes what a key plays: scale != OFF or chord != SINGLE. */
unsigned key_assist_active(const struct key_assist *a);

/* Knob turn. Root wraps 0..11; scale, chord and map clamp. Returns 0 on success,
 * -1 for an invalid track or field. */
int key_assist_adjust(unsigned track, unsigned field, int delta);
/* Encoder index as delivered by fm1_controls (2..5 = K1..K4) to a field; -1 otherwise. */
int key_assist_encoder_field(unsigned encoder);

/* key 0..26; raw = the stock note for the key; key0 = the stock note of key 0.
 * Writes 0..KA_MAX_NOTES MIDI notes (each 0..127, ascending, distinct) to out and
 * returns the count. Scale OFF with chord SINGLE returns exactly {raw}. With scale
 * OFF and a chord selected, MAJOR is used. Notes outside 0..127 are dropped. */
unsigned key_assist_notes(const struct key_assist *a, unsigned key, unsigned raw,
                          unsigned key0, uint8_t out[KA_MAX_NOTES]);
/* Pitch-class test used for LEDs and the strip: 1 when pc (0..11, C = 0) is in scale. */
unsigned key_assist_in_scale(const struct key_assist *a, unsigned pc);

/* SCL gesture. Feed it every control poll. The result is returned once per gesture:
 * TAP on release before KA_HOLD_MS, HOLD when KA_HOLD_MS elapses while still down. */
enum { KA_GESTURE_NONE, KA_GESTURE_TAP, KA_GESTURE_HOLD };
struct ka_gesture { uint8_t down, fired; uint32_t since; };
unsigned key_assist_gesture(struct ka_gesture *g, unsigned held, uint32_t now);
#endif
