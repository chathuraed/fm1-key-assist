/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Integration test: drives the real app/screen.c through the browser simulator API
 * (web/engine/Simulator.c) on the host. Build with tests/run_sim_test.sh.
 *   key_assist_sim_test ka        KEY ASSIST assertions (needs the modified firmware)
 *   key_assist_sim_test regress   deterministic trace; diff stock vs modified output
 *   key_assist_sim_test shot F    write the KEY ASSIST page as a PPM image */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void sim_boot(unsigned clear);
extern void sim_control(unsigned kind, unsigned index, int value);
extern const float *sim_render(void);
extern const char *sim_knobs(void);
extern const uint16_t *sim_pixels(void);
extern unsigned sim_lcd_mask(void);
extern int sim_select(unsigned track, unsigned source);
extern const uint8_t *sim_state(void);
extern uint32_t fm1_millis(void);
extern int groove_midi_local(const uint8_t *message, size_t bytes);

enum { OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC };
enum { BTN = 0, KEY = 1, ENC = 2 };
enum { ACCENT_COLOR = 0x07ff, BACK_COLOR = 0x0000 };

/* screen.c is compiled with -Dgroove_midi_local=ka_spy_midi_local: every note it posts lands here. */
struct event { uint32_t at; uint8_t status, note, velocity; };
static struct event log_[8192];
static unsigned log_count;
int ka_spy_midi_local(const uint8_t *message, size_t bytes)
{
    if (bytes == 3 && (message[0] & 0xe0U) == 0x80U && log_count < sizeof log_ / sizeof log_[0])
        log_[log_count++] = (struct event){fm1_millis(), message[0], message[1], message[2]};
    return groove_midi_local(message, bytes);
}

static unsigned checks, failures;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static float *capture;
static unsigned capture_words, capture_limit;
static void advance(unsigned ms)
{
    uint32_t until = fm1_millis() + ms;
    while ((int32_t)(fm1_millis() - until) < 0) {
        const float *out = sim_render();
        if (capture && capture_words + 256 <= capture_limit) {
            memcpy(capture + capture_words, out, 256 * sizeof *out);
            capture_words += 256;
        }
    }
}
static void press(unsigned kind, unsigned index) { sim_control(kind, index, 1); }
static void release(unsigned kind, unsigned index) { sim_control(kind, index, 0); }
static void tap_button(unsigned button) { press(BTN, button); advance(100); release(BTN, button); advance(150); }
static void turn(unsigned encoder, int delta) { sim_control(ENC, encoder, delta); advance(30); }
static const char *label(unsigned knob) { return sim_knobs() + knob * 32; }
static const char *value(unsigned knob) { return sim_knobs() + knob * 32 + 16; }
static unsigned on_ka_page(void)
{
    return !strcmp(label(0), "ROOT") && !strcmp(label(1), "SCALE") &&
           !strcmp(label(2), "CHORD") && !strcmp(label(3), "MAP");
}
static uint32_t fnv(const void *data, size_t bytes, uint32_t hash)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; ++i) hash = (hash ^ p[i]) * 16777619U;
    return hash;
}
static uint32_t screen_hash(void) { return fnv(sim_pixels(), 240 * 240 * 2, 2166136261U); }
/* Everything below the header strip, so the animated hold bar does not count as a page change. */
static uint32_t body_hash(void) { return fnv(sim_pixels() + 30 * 240, 210 * 240 * 2, 2166136261U); }

/* Notes sounding right now according to the posted note-on/note-off stream. */
static unsigned sounding(uint8_t out[128])
{
    int depth[128] = {0};
    for (unsigned i = 0; i < log_count; ++i)
        depth[log_[i].note] += (log_[i].status & 0xf0U) == 0x90U && log_[i].velocity ? 1 : -1;
    unsigned n = 0;
    for (unsigned note = 0; note < 128; ++note) if (depth[note] > 0) out[n++] = (uint8_t)note;
    return n;
}
static int balanced(void)
{
    int depth[128] = {0};
    for (unsigned i = 0; i < log_count; ++i) {
        depth[log_[i].note] += (log_[i].status & 0xf0U) == 0x90U ? 1 : -1;
        if (depth[log_[i].note] < 0) return 0;
    }
    for (unsigned note = 0; note < 128; ++note) if (depth[note]) return 0;
    return 1;
}
/* Note-ons since the last clear, in posting order. */
static unsigned note_ons(uint8_t out[64])
{
    unsigned n = 0;
    for (unsigned i = 0; i < log_count && n < 64; ++i)
        if ((log_[i].status & 0xf0U) == 0x90U) out[n++] = log_[i].note;
    return n;
}
static int ons_are(unsigned count, const unsigned *want)
{
    uint8_t got[64];
    unsigned n = note_ons(got), same = n == count;
    for (unsigned i = 0; same && i < count; ++i) same = got[i] == want[i];
    if (!same) {
        printf("  note-ons got:");
        for (unsigned i = 0; i < n; ++i) printf(" %u", got[i]);
        printf("  want:");
        for (unsigned i = 0; i < count; ++i) printf(" %u", want[i]);
        printf("\n");
    }
    return same;
}
static int all_velocity_100(void)
{
    for (unsigned i = 0; i < log_count; ++i)
        if ((log_[i].status & 0xf0U) == 0x90U && log_[i].velocity != 100) return 0;
    return 1;
}

static void fresh(void)
{
    sim_boot(1);
    advance(80);
    log_count = 0;
}
/* Hold SCL until the KEY ASSIST page opens, then let go. */
static void open_ka(void)
{
    press(BTN, SCL); advance(650); release(BTN, SCL); advance(80);
}
static void test_stock_notes_when_off(void)
{
    fresh();
    press(KEY, 9); advance(40);
    CHECK(log_count == 1 && log_[0].status == 0x90 && log_[0].note == 62 && log_[0].velocity == 100);
    release(KEY, 9); advance(40);
    CHECK(log_count == 2 && log_[1].status == 0x80 && log_[1].note == 62);
    CHECK(balanced());
    /* Other pitched keys are untouched too. */
    log_count = 0;
    press(KEY, 0); press(KEY, 26); advance(40);
    unsigned want[] = {53, 79};
    CHECK(ons_are(2, want));
    release(KEY, 0); release(KEY, 26); advance(40);
    CHECK(balanced());
}

static void test_tap_is_stock(void)
{
    fresh();
    uint32_t before = body_hash();
    press(BTN, SCL); advance(300);
    /* Nothing happens while held under 600 ms: the stock action waits for release. */
    CHECK(!on_ka_page());
    CHECK(body_hash() == before);
    release(BTN, SCL); advance(150);
    CHECK(!on_ka_page());
    CHECK(body_hash() != before); /* the stock operator-scaling screen opened */
    printf("  stock SCL tap knobs: %s / %s / %s / %s\n", label(0), label(1), label(2), label(3));
    /* A release at 599 ms is still a tap. */
    fresh();
    press(BTN, SCL); advance(590); release(BTN, SCL); advance(150);
    CHECK(!on_ka_page());
}

static void test_hold_opens_page(void)
{
    fresh();
    CHECK(!on_ka_page());
    press(BTN, SCL);
    advance(300);
    /* Hold bar: the strip at y=27 has grown to roughly half of its 224 px at 300 ms. */
    const uint16_t *px = sim_pixels() + 27 * 240;
    CHECK(px[8 + 20] == ACCENT_COLOR);
    CHECK(px[8 + 200] == BACK_COLOR);
    advance(270);          /* 570 ms: still not open */
    CHECK(!on_ka_page());
    advance(60);           /* 630 ms: open */
    CHECK(on_ka_page());
    CHECK(!strcmp(value(0), "C") && !strcmp(value(1), "OFF") &&
          !strcmp(value(2), "SINGLE") && !strcmp(value(3), "SNAP"));
    CHECK(sim_lcd_mask() == 15); /* simulator dials enabled */
    release(BTN, SCL); advance(100);
    CHECK(on_ka_page()); /* releasing after a hold is not a tap */
    /* SCL tap now leaves the page (back to the music page), and so does HOME. */
    tap_button(SCL);
    CHECK(!on_ka_page());
    open_ka();
    CHECK(on_ka_page());
    tap_button(HOME);
    CHECK(!on_ka_page());
}

static void test_encoders_on_page(void)
{
    fresh(); open_ka();
    CHECK(on_ka_page());
    turn(2, 1);  CHECK(!strcmp(value(0), "C#"));      /* K1 root, sharps visible */
    turn(2, -2); CHECK(!strcmp(value(0), "B"));       /* wraps below C */
    turn(2, 1);  CHECK(!strcmp(value(0), "C"));
    turn(3, 1);  CHECK(!strcmp(value(1), "MAJOR"));   /* K2 scale */
    turn(3, 1);  CHECK(!strcmp(value(1), "MINOR"));
    turn(3, 20); CHECK(!strcmp(value(1), "HARM MIN"));
    turn(3, -20); CHECK(!strcmp(value(1), "OFF"));
    turn(4, 1);  CHECK(!strcmp(value(2), "TRIAD"));   /* K3 chord */
    turn(4, 1);  CHECK(!strcmp(value(2), "7TH"));
    turn(4, 1);  CHECK(!strcmp(value(2), "POWER"));
    turn(4, -9); CHECK(!strcmp(value(2), "SINGLE"));
    turn(5, 1);  CHECK(!strcmp(value(3), "WHITE"));   /* K4 key mapping */
    turn(5, -1); CHECK(!strcmp(value(3), "SNAP"));
    /* Encoders 1 and 6 do not leak into other parameters on this page. */
    char before[64]; memcpy(before, sim_knobs(), sizeof before);
    turn(1, 3); turn(6, 3);
    CHECK(on_ka_page() && !memcmp(before, sim_knobs(), sizeof before));
    /* Settings are per track: encoder 0 selects another track with its own defaults. */
    turn(3, 2); CHECK(!strcmp(value(1), "MINOR"));
    turn(0, 1);
    CHECK(on_ka_page() && !strcmp(value(1), "OFF"));
    turn(0, -1);
    CHECK(on_ka_page() && !strcmp(value(1), "MINOR"));
}

static void set_assist(int root_steps, int scale_steps, int chord_steps, int map_steps)
{
    turn(2, root_steps); turn(3, scale_steps); turn(4, chord_steps); turn(5, map_steps);
}

static void test_notes(void)
{
    fresh(); open_ka();
    set_assist(0, 1, 1, 0);                       /* C MAJOR TRIAD SNAP */
    tap_button(SCL);                              /* back to the music page: assist stays on */
    CHECK(!on_ka_page());
    log_count = 0;
    press(KEY, 9); advance(40);                   /* D: D F A */
    unsigned dtri[] = {62, 65, 69};
    CHECK(ons_are(3, dtri) && all_velocity_100());
    release(KEY, 9); advance(40);
    CHECK(balanced());
    log_count = 0;
    press(KEY, 8); advance(40);                   /* C# snaps down to C: C E G */
    unsigned ctri[] = {60, 64, 67};
    CHECK(ons_are(3, ctri));
    release(KEY, 8); advance(40);
    CHECK(balanced());
    /* The same keys play assisted notes while the KEY ASSIST page is open. */
    open_ka();
    log_count = 0;
    press(KEY, 9); advance(40);
    CHECK(ons_are(3, dtri));
    release(KEY, 9); advance(40);
    CHECK(balanced());
    /* 7TH and POWER. */
    turn(4, 1);                                   /* 7TH */
    log_count = 0;
    press(KEY, 14); advance(40);                  /* G7 = G B D F */
    unsigned g7[] = {67, 71, 74, 77};
    CHECK(ons_are(4, g7));
    release(KEY, 14); advance(40);
    turn(4, 1);                                   /* POWER */
    log_count = 0;
    press(KEY, 11); advance(40);                  /* E: E B E */
    unsigned power[] = {64, 71, 76};
    CHECK(ons_are(3, power));
    release(KEY, 11); advance(40);
    CHECK(balanced());
    /* Octave button shifts every chord tone. */
    turn(4, -9); turn(4, 1);                      /* TRIAD */
    tap_button(OCT_UP);
    log_count = 0;
    press(KEY, 9); advance(40);
    unsigned up[] = {74, 77, 81};
    CHECK(ons_are(3, up));
    release(KEY, 9); advance(40);
    CHECK(balanced());
    tap_button(OCT_DOWN);
}

static void test_white_map(void)
{
    fresh(); open_ka();
    set_assist(0, 1, 0, 1);                       /* C MAJOR SINGLE WHITE */
    log_count = 0;
    press(KEY, 0); advance(40);                   /* key 0 plays the root */
    unsigned c[] = {60};
    CHECK(ons_are(1, c));
    release(KEY, 0); advance(40);
    log_count = 0;
    press(KEY, 1); advance(40);                   /* a black key is silent */
    CHECK(log_count == 0);
    release(KEY, 1); advance(40);
    press(KEY, 2); advance(40);
    unsigned d[] = {62};
    CHECK(ons_are(1, d));
    release(KEY, 2); advance(40);
    CHECK(balanced());
}

static void test_no_hanging_notes(void)
{
    fresh(); open_ka();
    set_assist(0, 1, 1, 0);
    log_count = 0;
    press(KEY, 9); advance(40);
    uint8_t now[128];
    CHECK(sounding(now) == 3);
    turn(4, 1);                                   /* chord changes under the held key */
    unsigned want[] = {62, 65, 69, 72};
    CHECK(sounding(now) == 4 && !memcmp(now, (uint8_t[]){62, 65, 69, 72}, 4));
    (void)want;
    turn(3, 1);                                   /* scale changes under the held key */
    CHECK(sounding(now) == 4);
    turn(2, 3);                                   /* root changes under the held key */
    CHECK(sounding(now) == 4);
    release(KEY, 9); advance(40);
    CHECK(sounding(now) == 0 && balanced());
    /* Leaving the page and changing tracks with keys held leaves nothing sounding. */
    press(KEY, 9); press(KEY, 14); advance(40);
    tap_button(SCL);
    release(KEY, 9); release(KEY, 14); advance(40);
    CHECK(sounding(now) == 0 && balanced());
    press(KEY, 4); advance(40);
    turn(0, 1); advance(40);
    release(KEY, 4); advance(40);
    CHECK(sounding(now) == 0 && balanced());
}

static void test_drum_bypass(void)
{
    fresh(); open_ka();
    set_assist(0, 1, 1, 0);                       /* track 1: C MAJOR TRIAD */
    CHECK(sim_select(0, 2) == 0);                 /* make track 1 a DRUM 808 */
    log_count = 0;
    press(KEY, 0); advance(40);
    CHECK(log_count == 1);                        /* one drum note, never a chord */
    release(KEY, 0); advance(40);
    CHECK(balanced());
    open_ka();
    /* The page says so and its four dials are inert on a drum track. */
    CHECK(!strcmp(label(0), "-") && !strcmp(value(0), "--") && !strcmp(value(3), "--"));
    CHECK(sim_lcd_mask() == 0);
    turn(3, 1);                                   /* turning a dial on a drum track changes nothing */
    CHECK(!strcmp(value(1), "--"));
}

/* Render `ms` of audio while the given keys are held; returns frames captured. */
static unsigned render_keys(float *dest, unsigned capacity_words, const unsigned *keys, unsigned count, unsigned ms)
{
    capture = dest; capture_words = 0; capture_limit = capacity_words;
    for (unsigned i = 0; i < count; ++i) press(KEY, keys[i]);
    advance(ms);
    for (unsigned i = 0; i < count; ++i) release(KEY, keys[i]);
    advance(200);
    capture = NULL;
    return capture_words / 2;
}
static double peak_of(const float *data, unsigned words, int *finite)
{
    double peak = 0;
    for (unsigned i = 0; i < words; ++i) {
        if (!isfinite(data[i])) *finite = 0;
        if (fabs(data[i]) > peak) peak = fabs(data[i]);
    }
    return peak;
}
static double max_difference(const float *a, const float *b, unsigned words)
{
    double worst = 0;
    for (unsigned i = 0; i < words; ++i) if (fabs((double)a[i] - b[i]) > worst) worst = fabs((double)a[i] - b[i]);
    return worst;
}
static float audio_ref[44100 * 2 * 2], audio_kai[44100 * 2 * 2];
static void audio_case(const char *name, int scale_steps, int chord_steps, unsigned assist_key,
                       const unsigned *stock_keys, unsigned stock_count, unsigned expected_notes)
{
    /* Reference: assist off, the chord's notes pressed as ordinary keys. */
    fresh();
    unsigned ref_frames = render_keys(audio_ref, sizeof audio_ref / sizeof audio_ref[0], stock_keys, stock_count, 1500);
    uint8_t ref_notes[64], n_ref = (uint8_t)note_ons(ref_notes);
    /* KEY ASSIST: one key. */
    fresh(); open_ka(); set_assist(0, scale_steps, chord_steps, 0); tap_button(SCL);
    log_count = 0;
    unsigned kai_frames = render_keys(audio_kai, sizeof audio_kai / sizeof audio_kai[0], &assist_key, 1, 1500);
    uint8_t kai_notes[64], n_kai = (uint8_t)note_ons(kai_notes);
    int finite = 1;
    double peak = peak_of(audio_kai, kai_frames * 2, &finite), diff = max_difference(audio_ref, audio_kai, ref_frames * 2);
    printf("  %-8s one key -> %u notes, peak %.3f, max difference from %u stock keys: %g\n",
           name, n_kai, peak, stock_count, diff);
    CHECK(n_ref == expected_notes && n_kai == expected_notes);
    CHECK(ref_frames > 60000 && ref_frames == kai_frames);
    CHECK(finite);
    CHECK(peak > 0.001 && peak < 4.0);            /* audible FM output, no runaway level */
    CHECK(diff == 0.0);                           /* bit-identical to pressing those stock keys */
}
static void test_polyphony_and_audio(void)
{
    static const unsigned triad[] = {9, 12, 16};          /* D F A */
    static const unsigned seventh[] = {14, 18, 21, 24};   /* G B D F */
    static const unsigned power[] = {11, 18, 23};         /* E B E */
    audio_case("TRIAD", 1, 1, 9, triad, 3, 3);
    audio_case("7TH", 1, 2, 14, seventh, 4, 4);
    audio_case("POWER", 1, 3, 11, power, 3, 3);

    /* Silence check: with nothing held the engine is quiet, so the assertions above are not vacuous. */
    fresh();
    int finite = 1;
    unsigned quiet = render_keys(audio_ref, 256 * 40, NULL, 0, 300);
    CHECK(quiet > 0 && peak_of(audio_ref, quiet * 2, &finite) < 0.0001);

    /* Polyphony: eight keys of a 7TH chord = 32 notes requested. No crash, finite output, clean release. */
    fresh(); open_ka(); set_assist(0, 1, 2, 0); tap_button(SCL);
    static float audio_c[44100 * 2 * 3];
    static const unsigned keys[] = {0, 2, 4, 6, 7, 9, 11, 12};
    log_count = 0;
    unsigned frames = render_keys(audio_c, sizeof audio_c / sizeof audio_c[0], keys, 8, 1500);
    double peak_c = peak_of(audio_c, frames * 2, &finite);
    CHECK(finite && peak_c > 0.001 && peak_c < 8.0);
    CHECK(log_count >= 8 && balanced());
    printf("  polyphony: 8 keys x 7TH = %u note events, peak %.3f, all released\n", log_count, peak_c);
}

static void test_no_flash_writes(void)
{
    /* Changing every KEY ASSIST setting is RAM-only: the sim storage revision must not move. */
    extern uint32_t sim_flash_revision(void);
    fresh();
    uint32_t before = sim_flash_revision();
    open_ka();
    set_assist(5, 4, 2, 1); set_assist(-9, -9, -9, -1);
    press(KEY, 9); advance(40); release(KEY, 9); advance(40);
    tap_button(SCL);
    CHECK(sim_flash_revision() == before);
}


/* Replays proof/demo.script (the fm1-emu ?script= grammar: ms:id press, ms:id^ release, ms:eN:clicks)
 * against the real firmware and checks the whole performance: pages, settings and every note sent. */
static void describe(char *out, size_t size)
{
    if (on_ka_page())
        snprintf(out, size, "KEY ASSIST  %s %s %s %s", value(0), value(1), value(2), value(3));
    else
        snprintf(out, size, "page knobs: %s / %s / %s / %s", label(0), label(1), label(2), label(3));
}
struct script_event { unsigned ms; int kind; unsigned index; int value; };
static int by_time(const void *a, const void *b)
{
    const struct script_event *x = a, *y = b;
    return x->ms != y->ms ? (x->ms > y->ms ? 1 : -1) : (int)(x - y) < 0 ? -1 : 1;
}
static void test_demo_script(const char *path)
{
    static char text[1 << 16];
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL cannot open %s\n", path); ++failures; return; }
    size_t n = fread(text, 1, sizeof text - 1, f); fclose(f); text[n] = 0;
    static struct script_event events[512];
    unsigned count = 0, last_ms = 0;
    for (char *token = strtok(text, ",\n \r"); token && count < 512; token = strtok(NULL, ",\n \r")) {
        char *colon = strchr(token, ':');
        if (!colon) continue;
        unsigned ms = (unsigned)strtoul(token, NULL, 10);
        const char *rest = colon + 1;
        struct script_event e = {ms, 0, 0, 0};
        if (rest[0] == 'e') {
            e.kind = ENC; e.index = (unsigned)strtoul(rest + 1, NULL, 10);
            const char *c = strchr(rest, ':'); e.value = c ? atoi(c + 1) : 0;
        } else {
            unsigned id = (unsigned)strtoul(rest, NULL, 10);
            e.value = rest[strlen(rest) - 1] == '^' ? 0 : 1;
            if (id < 14) { e.kind = BTN; e.index = id; } else { e.kind = KEY; e.index = id - 14; }
        }
        events[count++] = e;
        if (ms > last_ms) last_ms = ms;
    }
    qsort(events, count, sizeof events[0], by_time);
    CHECK(count > 40 && last_ms >= 30000 && last_ms <= 45000);
    fresh();
    char state[128], previous[128] = "";
    for (unsigned i = 0; i < count; ++i) {
        while (fm1_millis() < events[i].ms) advance(10);
        sim_control((unsigned)events[i].kind, events[i].index, events[i].value);
        advance(10);
        describe(state, sizeof state);
        if (strcmp(state, previous)) { printf("  %5u ms  %s\n", events[i].ms, state); strcpy(previous, state); }
    }
    advance(500);
    uint8_t got[64];
    unsigned total = note_ons(got);
    static const unsigned want[] = {62,  62, 65, 69,  65, 69, 72,  67, 71, 74,  69, 72, 76,
                                    62, 65, 69,  62, 69, 74,  62};
    printf("  notes sent:");
    for (unsigned i = 0; i < total; ++i) printf(" %u", got[i]);
    printf("\n");
    CHECK(total == sizeof want / sizeof want[0]);
    for (unsigned i = 0; i < total && i < sizeof want / sizeof want[0]; ++i) CHECK(got[i] == want[i]);
    uint8_t held[128];
    CHECK(sounding(held) == 0 && balanced());
    CHECK(!on_ka_page());
    CHECK(!strcmp(label(0), "LEVEL")); /* back on the ordinary music page */
}

/* Deterministic trace for stock-vs-modified differential comparison. */
static void trace(const char *what)
{
    printf("%-28s screen=%08x knobs=%08x events=%u\n", what, screen_hash(),
           fnv(sim_knobs(), 128, 2166136261U), log_count);
}
static void regress(void)
{
    fresh(); trace("boot");
    press(KEY, 9); advance(40); trace("key down"); release(KEY, 9); advance(40); trace("key up");
    for (unsigned i = 0; i < log_count; ++i) printf("  %02x %u %u\n", log_[i].status, log_[i].note, log_[i].velocity);
    tap_button(SCL); trace("SCL tap 1");
    tap_button(SCL); trace("SCL tap 2");
    press(KEY, 5); advance(40); release(KEY, 5); advance(40); trace("key on scaling page");
    tap_button(SCL); trace("SCL tap 3");
    tap_button(EDIT); trace("EDIT");
    tap_button(EDIT); trace("EDIT again");
    tap_button(ENV); trace("ENV");
    tap_button(LFO); trace("LFO");
    tap_button(FX); trace("FX");
    tap_button(SEQ); trace("SEQ");
    turn(2, 1); trace("SEQ K1");
    tap_button(HOME); trace("HOME");
    tap_button(ARP); trace("ARP on");
    press(KEY, 9); advance(60); release(KEY, 9); advance(60); trace("ARP key");
    tap_button(ARP); trace("ARP off");
    tap_button(SAVE); trace("SAVE");
    tap_button(HOME); trace("HOME");
    press(BTN, GLO); advance(100); press(KEY, 3); advance(50); release(KEY, 3); release(BTN, GLO); advance(200); trace("GLO + key");
    tap_button(OCT_UP); press(KEY, 9); advance(40); release(KEY, 9); advance(40); trace("OCT up key");
    tap_button(OCT_DOWN);
    sim_select(0, 2); advance(40); trace("track 1 -> DRUM");
    press(KEY, 0); advance(40); release(KEY, 0); advance(40); trace("drum key");
    tap_button(SCL); trace("SCL tap on drum");
    tap_button(SCL); trace("SCL tap on drum 2");
    for (unsigned i = 0; i < log_count; ++i) printf("  %02x %u %u\n", log_[i].status, log_[i].note, log_[i].velocity);
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror("shot"); return; }
    fprintf(f, "P6\n240 240\n255\n");
    const uint16_t *px = sim_pixels();
    for (unsigned i = 0; i < 240 * 240; ++i) {
        unsigned v = px[i];
        fputc((int)(((v >> 11) & 31U) * 255U / 31U), f);
        fputc((int)(((v >> 5) & 63U) * 255U / 63U), f);
        fputc((int)((v & 31U) * 255U / 31U), f);
    }
    fclose(f);
}
/* path: the KEY ASSIST page (D DORIAN TRIAD WHITE, key held); path.home.ppm: the music page with the KA tag. */
static void shot(const char *path)
{
    fresh(); open_ka();
    set_assist(2, 3, 1, 1);
    press(KEY, 9); advance(100);
    write_ppm(path);
    release(KEY, 9); tap_button(SCL); advance(100);
    char home[512]; snprintf(home, sizeof home, "%s.home.ppm", path);
    write_ppm(home);
}

/* Reference for the CPU-level emulator check: the body (rows 30..239) of the freshly opened KEY ASSIST page,
 * hashed as native and as byte-swapped RGB565 (the emulator's LCD byte order is not assumed). */
static void lcd_hash(void)
{
    fresh(); open_ka(); advance(200);
    const uint16_t *px = sim_pixels() + 30 * 240;
    uint32_t native = 2166136261U, swapped = 2166136261U;
    for (unsigned i = 0; i < 210 * 240; ++i) {
        uint8_t lo = (uint8_t)(px[i] & 255U), hi = (uint8_t)(px[i] >> 8);
        native = (native ^ lo) * 16777619U; native = (native ^ hi) * 16777619U;
        swapped = (swapped ^ hi) * 16777619U; swapped = (swapped ^ lo) * 16777619U;
    }
    printf("{\"ka_default_native\":%u,\"ka_default_swapped\":%u}\n", native, swapped);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "regress")) { regress(); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "lcdhash")) { lcd_hash(); return 0; }
    if (argc >= 3 && !strcmp(argv[1], "shot")) { shot(argv[2]); return 0; }
    if (argc >= 3 && !strcmp(argv[1], "demo")) {
        test_demo_script(argv[2]);
        printf("demo script: %u checks, %u failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    test_stock_notes_when_off();
    test_tap_is_stock();
    test_hold_opens_page();
    test_encoders_on_page();
    test_notes();
    test_white_map();
    test_no_hanging_notes();
    test_drum_bypass();
    test_polyphony_and_audio();
    test_no_flash_writes();
    printf("key_assist_sim_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
