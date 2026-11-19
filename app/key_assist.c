/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "key_assist.h"

struct key_assist key_assist_state[KA_TRACKS];

static const uint16_t scale_masks[KA_SCALES] = {
    0, 0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x295, 0x4A9, 0x4E9, 0x9AD
};
static const char *const scale_names[KA_SCALES] = {
    "OFF", "MAJOR", "MINOR", "DORIAN", "MIXO", "PENT MAJ", "PENT MIN", "BLUES", "HARM MIN"
};
static const char *const chord_names[KA_CHORDS] = {"SINGLE", "TRIAD", "7TH", "POWER"};
static const char *const map_names[KA_MAPS] = {"SNAP", "WHITE"};
static const char *const root_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* Key 0 is F (F3..G5): white keys sit at 0,2,4,6,7,9,11 within each octave of keys. */
static const int8_t white_index[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};

const char *key_assist_scale_name(unsigned scale) { return scale_names[scale < KA_SCALES ? scale : 0]; }
const char *key_assist_chord_name(unsigned chord) { return chord_names[chord < KA_CHORDS ? chord : 0]; }
const char *key_assist_map_name(unsigned map) { return map_names[map < KA_MAPS ? map : 0]; }
const char *key_assist_root_name(unsigned root) { return root_names[root % 12U]; }
unsigned key_assist_scale_mask(unsigned scale) { return scale < KA_SCALES ? scale_masks[scale] : 0U; }

unsigned key_assist_active(const struct key_assist *a)
{
    return a && (a->scale != KA_OFF || a->chord != KA_SINGLE);
}

int key_assist_encoder_field(unsigned encoder)
{
    return encoder >= 2U && encoder < 2U + KA_FIELDS ? (int)(encoder - 2U) : -1;
}

int key_assist_adjust(unsigned track, unsigned field, int delta)
{
    if (track >= KA_TRACKS || field >= KA_FIELDS) return -1;
    struct key_assist *a = &key_assist_state[track];
    uint8_t *value[KA_FIELDS] = {&a->root, &a->scale, &a->chord, &a->map};
    static const uint8_t count[KA_FIELDS] = {12, KA_SCALES, KA_CHORDS, KA_MAPS};
    int v = (int)*value[field] + delta;
    if (field == KA_ROOT) v = ((v % 12) + 12) % 12;
    else v = v < 0 ? 0 : v > (int)count[field] - 1 ? (int)count[field] - 1 : v;
    *value[field] = (uint8_t)v;
    return 0;
}

/* OFF with a chord selected behaves as MAJOR; OFF with SINGLE is not assisting. */
static unsigned effective_mask(const struct key_assist *a)
{
    return key_assist_scale_mask(a->scale == KA_OFF ? KA_MAJOR : a->scale);
}

unsigned key_assist_in_scale(const struct key_assist *a, unsigned pc)
{
    if (!key_assist_active(a)) return 1;
    return (effective_mask(a) >> ((pc % 12U + 12U - a->root % 12U) % 12U)) & 1U;
}

unsigned key_assist_notes(const struct key_assist *a, unsigned key, unsigned raw,
                          unsigned key0, uint8_t out[KA_MAX_NOTES])
{
    unsigned n = 0;
    if (!a || !out || key >= KA_KEYS) return 0;
    if (!key_assist_active(a)) {
        if (raw < 128U) out[n++] = (uint8_t)raw;
        return n;
    }
    unsigned mask = effective_mask(a), root = a->root % 12U, count = 0;
    uint8_t degree[12];
    for (unsigned s = 0; s < 12U; ++s)
        if ((mask >> s) & 1U) degree[count++] = (uint8_t)s;
    int base;      /* absolute note carrying the root pitch class */
    unsigned step; /* degree number counted up from base */
    if (a->map == KA_WHITE) {
        int white = white_index[key % 12U];
        if (white < 0) return 0;
        step = (key / 12U) * 7U + (unsigned)white;
        base = (int)key0 + (int)((root + 12U - key0 % 12U) % 12U);
    } else {
        unsigned offset = (raw % 12U + 12U - root) % 12U, s = offset;
        while (!((mask >> s) & 1U)) --s; /* bit 0 is set in every scale */
        step = 0;
        while (degree[step] != s) ++step;
        base = (int)raw - (int)offset;
    }
    static const uint8_t chord_steps[KA_CHORDS][KA_MAX_NOTES] = {
        {0}, {0, 2, 4}, {0, 2, 4, 6}, {0}
    };
    static const uint8_t chord_count[KA_CHORDS] = {1, 3, 4, 1};
    unsigned chord = a->chord < KA_CHORDS ? a->chord : KA_SINGLE;
    int note[KA_MAX_NOTES];
    unsigned total = chord_count[chord];
    for (unsigned i = 0; i < total; ++i) {
        unsigned d = step + chord_steps[chord][i];
        note[i] = base + (int)(d / count) * 12 + (int)degree[d % count];
    }
    if (chord == KA_POWER) {
        note[1] = note[0] + 7;
        note[2] = note[0] + 12;
        total = 3;
    }
    for (unsigned i = 0; i < total; ++i)
        if (note[i] >= 0 && note[i] < 128) out[n++] = (uint8_t)note[i];
    return n;
}

unsigned key_assist_gesture(struct ka_gesture *g, unsigned held, uint32_t now)
{
    if (held) {
        if (!g->down) {
            g->down = 1; g->fired = 0; g->since = now;
            return KA_GESTURE_NONE;
        }
        if (!g->fired && (uint32_t)(now - g->since) >= KA_HOLD_MS) {
            g->fired = 1;
            return KA_GESTURE_HOLD;
        }
        return KA_GESTURE_NONE;
    }
    if (!g->down) return KA_GESTURE_NONE;
    unsigned tap = !g->fired;
    g->down = g->fired = 0;
    return tap ? KA_GESTURE_TAP : KA_GESTURE_NONE;
}
