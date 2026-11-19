/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Host test for app/key_assist.c. Build and run:
 *   cc -std=c11 -Wall -Wextra -Werror -Iapp tests/key_assist_test.c app/key_assist.c -o /tmp/ka_test && /tmp/ka_test */
#include "key_assist.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define F3 53U /* stock note of key 0 at octave 0 */

static unsigned notes(unsigned root, unsigned scale, unsigned chord, unsigned map,
                      unsigned key, uint8_t out[KA_MAX_NOTES])
{
    struct key_assist a = {(uint8_t)root, (uint8_t)scale, (uint8_t)chord, (uint8_t)map};
    return key_assist_notes(&a, key, F3 + key, F3, out);
}
static unsigned raw_notes(unsigned root, unsigned scale, unsigned chord, unsigned map,
                          unsigned raw, uint8_t out[KA_MAX_NOTES])
{
    struct key_assist a = {(uint8_t)root, (uint8_t)scale, (uint8_t)chord, (uint8_t)map};
    return key_assist_notes(&a, 0, raw, F3, out);
}
static int is(const uint8_t *got, unsigned n, unsigned count, const unsigned *want)
{
    if (n != count) return 0;
    for (unsigned i = 0; i < n; ++i) if (got[i] != want[i]) return 0;
    return 1;
}

static void test_masks(void)
{
    /* Built independently of the table from interval lists. */
    static const int major[] = {0, 2, 4, 5, 7, 9, 11, -1}, minor[] = {0, 2, 3, 5, 7, 8, 10, -1},
        dorian[] = {0, 2, 3, 5, 7, 9, 10, -1}, mixo[] = {0, 2, 4, 5, 7, 9, 10, -1},
        pmaj[] = {0, 2, 4, 7, 9, -1}, pmin[] = {0, 3, 5, 7, 10, -1},
        blues[] = {0, 3, 5, 6, 7, 10, -1}, hmin[] = {0, 2, 3, 5, 7, 8, 11, -1};
    const int *list[KA_SCALES] = {0, major, minor, dorian, mixo, pmaj, pmin, blues, hmin};
    CHECK(key_assist_scale_mask(KA_OFF) == 0);
    for (unsigned s = KA_MAJOR; s < KA_SCALES; ++s) {
        unsigned want = 0;
        for (const int *p = list[s]; *p >= 0; ++p) want |= 1U << *p;
        CHECK(key_assist_scale_mask(s) == want);
        CHECK(key_assist_scale_mask(s) & 1U); /* every scale contains its root */
        CHECK(strlen(key_assist_scale_name(s)) > 0);
    }
    CHECK(key_assist_scale_mask(KA_SCALES) == 0);
}

static void test_passthrough(void)
{
    uint8_t out[KA_MAX_NOTES];
    for (unsigned key = 0; key < KA_KEYS; ++key) {
        unsigned want[] = {F3 + key};
        /* Any root or map is ignored while assist is inactive. */
        CHECK(is(out, notes(7, KA_OFF, KA_SINGLE, KA_WHITE, key, out), 1, want));
        CHECK(is(out, notes(0, KA_OFF, KA_SINGLE, KA_SNAP, key, out), 1, want));
    }
    CHECK(!key_assist_active(&key_assist_state[0]));
    struct key_assist a = {0, KA_MAJOR, KA_SINGLE, KA_SNAP};
    CHECK(key_assist_active(&a));
    a.scale = KA_OFF; a.chord = KA_TRIAD;
    CHECK(key_assist_active(&a));
}

static void test_snap(void)
{
    uint8_t out[KA_MAX_NOTES];
    /* C major: C# (61, key 8) snaps down to C (60); in-scale keys are untouched. */
    unsigned c[] = {60}, d[] = {62}, a_[] = {57};
    CHECK(is(out, notes(0, KA_MAJOR, KA_SINGLE, KA_SNAP, 8, out), 1, c));
    CHECK(is(out, notes(0, KA_MAJOR, KA_SINGLE, KA_SNAP, 9, out), 1, d));
    /* A minor: C# (61) is off-scale and snaps to C; A3 (57) stays. */
    CHECK(is(out, notes(9, KA_MINOR, KA_SINGLE, KA_SNAP, 8, out), 1, c));
    CHECK(is(out, notes(9, KA_MINOR, KA_SINGLE, KA_SNAP, 4, out), 1, a_));
    /* Every snapped note is in the scale, never above the pressed key, and within a tone. */
    for (unsigned s = KA_MAJOR; s < KA_SCALES; ++s)
        for (unsigned root = 0; root < 12; ++root)
            for (unsigned key = 0; key < KA_KEYS; ++key) {
                struct key_assist k = {(uint8_t)root, (uint8_t)s, KA_SINGLE, KA_SNAP};
                unsigned n = key_assist_notes(&k, key, F3 + key, F3, out);
                CHECK(n == 1);
                CHECK(out[0] <= F3 + key && F3 + key - out[0] <= 2);
                CHECK(key_assist_in_scale(&k, out[0] % 12U));
            }
    /* A black key is still sounded in SNAP; it snaps to a neighbour. */
    CHECK(notes(0, KA_MAJOR, KA_SINGLE, KA_SNAP, 1, out) == 1);
}

static void test_chords(void)
{
    uint8_t out[KA_MAX_NOTES];
    /* D in C major, stock note 62 = key 9. */
    unsigned dtri[] = {62, 65, 69}, g7[] = {67, 71, 74, 77}, pw[] = {64, 71, 76};
    CHECK(is(out, notes(0, KA_MAJOR, KA_TRIAD, KA_SNAP, 9, out), 3, dtri));
    /* G7 on G (67 = key 14): G B D F. */
    CHECK(is(out, notes(0, KA_MAJOR, KA_SEVENTH, KA_SNAP, 14, out), 4, g7));
    /* Power chord on E (64 = key 11): root, fifth, octave. */
    CHECK(is(out, notes(0, KA_MAJOR, KA_POWER, KA_SNAP, 11, out), 3, pw));
    /* A minor triad on A3 (57 = key 4): A C E. */
    unsigned am[] = {57, 60, 64};
    CHECK(is(out, notes(9, KA_MINOR, KA_TRIAD, KA_SNAP, 4, out), 3, am));
    /* Scale OFF with a chord uses MAJOR: same as C major. */
    CHECK(is(out, notes(0, KA_OFF, KA_TRIAD, KA_SNAP, 9, out), 3, dtri));
    /* Pentatonic minor, A root, TRIAD stacks scale degrees: A D E. */
    unsigned pent[] = {57, 62, 67};
    CHECK(is(out, notes(9, KA_PENT_MIN, KA_TRIAD, KA_SNAP, 4, out), 3, pent));
    /* All chords are ascending, distinct and in range for every input. */
    for (unsigned s = KA_OFF; s < KA_SCALES; ++s)
        for (unsigned c = KA_SINGLE; c < KA_CHORDS; ++c)
            for (unsigned m = 0; m < KA_MAPS; ++m)
                for (unsigned root = 0; root < 12; ++root)
                    for (unsigned key = 0; key < KA_KEYS; ++key) {
                        unsigned n = notes(root, s, c, m, key, out);
                        CHECK(n <= KA_MAX_NOTES);
                        for (unsigned i = 0; i < n; ++i) {
                            CHECK(out[i] < 128);
                            if (i) CHECK(out[i] > out[i - 1]);
                        }
                    }
}

static void test_white_map(void)
{
    uint8_t out[KA_MAX_NOTES];
    /* C major, WHITE: key 0 (F) plays the root C and white keys climb by degree. */
    static const struct { unsigned key, note; } cmaj[] = {
        {0, 60}, {2, 62}, {4, 64}, {6, 65}, {7, 67}, {9, 69}, {11, 71},
        {12, 72}, {14, 74}, {16, 76}, {18, 77}, {19, 79}, {21, 81}, {23, 83},
        {24, 84}, {26, 86}
    };
    for (unsigned i = 0; i < sizeof cmaj / sizeof cmaj[0]; ++i) {
        unsigned want[] = {cmaj[i].note};
        CHECK(is(out, notes(0, KA_MAJOR, KA_SINGLE, KA_WHITE, cmaj[i].key, out), 1, want));
    }
    /* Black keys are silent: keys 1,3,5,8,10 of each octave (and 13,15,17,20,22,25). */
    static const unsigned black[] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
    for (unsigned i = 0; i < sizeof black / sizeof black[0]; ++i)
        CHECK(notes(0, KA_MAJOR, KA_SINGLE, KA_WHITE, black[i], out) == 0);
    /* A minor pentatonic: five degrees per octave, the sixth key wraps up an octave. */
    static const unsigned pm[] = {57, 60, 62, 64, 67, 69};
    static const unsigned pm_key[] = {0, 2, 4, 6, 7, 9};
    for (unsigned i = 0; i < 6; ++i) {
        unsigned want[] = {pm[i]};
        CHECK(is(out, notes(9, KA_PENT_MIN, KA_SINGLE, KA_WHITE, pm_key[i], out), 1, want));
    }
    /* WHITE chord on the first key of A minor pentatonic stacks degrees 0,2,4: A D G. */
    unsigned tri[] = {57, 62, 67};
    CHECK(is(out, notes(9, KA_PENT_MIN, KA_TRIAD, KA_WHITE, 0, out), 3, tri));
    /* Sixteen white keys across the 27-key range. */
    unsigned whites = 0;
    for (unsigned key = 0; key < KA_KEYS; ++key)
        whites += notes(0, KA_MAJOR, KA_SINGLE, KA_WHITE, key, out) == 1;
    CHECK(whites == 16);
}

static void test_range(void)
{
    uint8_t out[KA_MAX_NOTES];
    /* Top of range: chord tones above 127 are dropped, never wrapped. */
    unsigned top7[] = {127}, top3[] = {124, 127};
    CHECK(is(out, raw_notes(0, KA_MAJOR, KA_SEVENTH, KA_SNAP, 127, out), 1, top7));
    CHECK(is(out, raw_notes(0, KA_MAJOR, KA_TRIAD, KA_SNAP, 124, out), 2, top3));
    /* Bottom: a snap below note 0 is dropped (F# major, note 0 snaps to -1). */
    CHECK(raw_notes(6, KA_MAJOR, KA_SINGLE, KA_SNAP, 0, out) == 0);
    /* Low chord tones stay positive: C# in D major (raw 1) triad = 1, 4, 7. */
    unsigned low[] = {1, 4, 7};
    CHECK(is(out, raw_notes(2, KA_MAJOR, KA_TRIAD, KA_SNAP, 1, out), 3, low));
    /* Invalid inputs. */
    struct key_assist a = {0, KA_MAJOR, KA_TRIAD, KA_SNAP};
    CHECK(key_assist_notes(0, 0, 60, F3, out) == 0);
    CHECK(key_assist_notes(&a, KA_KEYS, 60, F3, out) == 0);
    CHECK(key_assist_notes(&a, 0, 60, F3, 0) == 0);
}

static void test_encoders(void)
{
    /* K1..K4 are encoders 2..5; track select (0), the unused 1 and 6 are not mapped. */
    CHECK(key_assist_encoder_field(0) == -1);
    CHECK(key_assist_encoder_field(1) == -1);
    CHECK(key_assist_encoder_field(2) == KA_ROOT);
    CHECK(key_assist_encoder_field(3) == KA_SCALE);
    CHECK(key_assist_encoder_field(4) == KA_CHORD);
    CHECK(key_assist_encoder_field(5) == KA_MAP);
    CHECK(key_assist_encoder_field(6) == -1);
    CHECK(key_assist_encoder_field(7) == -1);

    memset(key_assist_state, 0, sizeof key_assist_state);
    struct key_assist *t = &key_assist_state[2];
    /* K1 root: wraps both ways and reaches the sharps. */
    CHECK(key_assist_adjust(2, KA_ROOT, 1) == 0 && t->root == 1);
    CHECK(strcmp(key_assist_root_name(t->root), "C#") == 0);
    CHECK(key_assist_adjust(2, KA_ROOT, -2) == 0 && t->root == 11);
    CHECK(strcmp(key_assist_root_name(t->root), "B") == 0);
    CHECK(key_assist_adjust(2, KA_ROOT, 1) == 0 && t->root == 0);
    CHECK(key_assist_adjust(2, KA_ROOT, 25) == 0 && t->root == 1);
    CHECK(key_assist_adjust(2, KA_ROOT, -13) == 0 && t->root == 0);
    for (unsigned r = 0; r < 12; ++r) CHECK(strlen(key_assist_root_name(r)) >= 1);
    CHECK(strcmp(key_assist_root_name(3), "D#") == 0);
    CHECK(strcmp(key_assist_root_name(10), "A#") == 0);
    /* K2 scale: clamps at both ends, steps through every scale. */
    CHECK(key_assist_adjust(2, KA_SCALE, -5) == 0 && t->scale == KA_OFF);
    for (unsigned s = 1; s < KA_SCALES; ++s) CHECK(key_assist_adjust(2, KA_SCALE, 1) == 0 && t->scale == s);
    CHECK(key_assist_adjust(2, KA_SCALE, 50) == 0 && t->scale == KA_SCALES - 1);
    /* K3 chord and K4 map: clamp. */
    CHECK(key_assist_adjust(2, KA_CHORD, 9) == 0 && t->chord == KA_POWER);
    CHECK(key_assist_adjust(2, KA_CHORD, -9) == 0 && t->chord == KA_SINGLE);
    CHECK(key_assist_adjust(2, KA_MAP, 3) == 0 && t->map == KA_WHITE);
    CHECK(key_assist_adjust(2, KA_MAP, -3) == 0 && t->map == KA_SNAP);
    CHECK(strcmp(key_assist_chord_name(KA_TRIAD), "TRIAD") == 0);
    CHECK(strcmp(key_assist_map_name(KA_WHITE), "WHITE") == 0);
    /* Settings belong to one track. */
    for (unsigned i = 0; i < KA_TRACKS; ++i)
        if (i != 2) CHECK(key_assist_state[i].root == 0 && key_assist_state[i].scale == 0 &&
                          key_assist_state[i].chord == 0 && key_assist_state[i].map == 0);
    CHECK(key_assist_adjust(KA_TRACKS, KA_ROOT, 1) == -1);
    CHECK(key_assist_adjust(0, KA_FIELDS, 1) == -1);
    memset(key_assist_state, 0, sizeof key_assist_state);
}

static void test_gesture(void)
{
    struct ka_gesture g = {0, 0, 0};
    /* Short tap: nothing while held, TAP once on release. */
    CHECK(key_assist_gesture(&g, 1, 1000) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 1100) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 0, 1150) == KA_GESTURE_TAP);
    CHECK(key_assist_gesture(&g, 0, 1200) == KA_GESTURE_NONE);
    /* Release at 599 ms is still a tap. */
    CHECK(key_assist_gesture(&g, 1, 2000) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 2599) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 0, 2599) == KA_GESTURE_TAP);
    /* Hold: fires exactly at 600 ms, once, and the release is not a tap. */
    CHECK(key_assist_gesture(&g, 1, 3000) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 3599) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 3600) == KA_GESTURE_HOLD);
    CHECK(key_assist_gesture(&g, 1, 3700) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 9000) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 0, 9100) == KA_GESTURE_NONE);
    /* A new press after a hold starts a fresh gesture. */
    CHECK(key_assist_gesture(&g, 1, 10000) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 0, 10050) == KA_GESTURE_TAP);
    /* Millisecond clock wrap: 600 ms after 0xFFFFFFF0 is 0x248. */
    CHECK(key_assist_gesture(&g, 1, 0xFFFFFFF0U) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 0x247U) == KA_GESTURE_NONE);
    CHECK(key_assist_gesture(&g, 1, 0x248U) == KA_GESTURE_HOLD);
    CHECK(key_assist_gesture(&g, 0, 0x300U) == KA_GESTURE_NONE);
    CHECK(KA_HOLD_MS == 600);
}

int main(void)
{
    test_masks();
    test_passthrough();
    test_snap();
    test_chords();
    test_white_map();
    test_range();
    test_encoders();
    test_gesture();
    printf("key_assist_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
