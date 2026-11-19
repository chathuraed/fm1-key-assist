# KEY ASSIST — 5-step emulator demo verification

Run these in order in Chrome with sound on. The replay plays `proof/demo.script` (34 s of emulated time, plus about 4 s of boot) through `fm1-emu`, so every step has a time stamp. Times are emulated milliseconds from boot. The same script is replayed against the firmware source by `tests/run_sim_test.sh demo`, which prints the timeline below.

**Replay URL** (filled in by CI after the first deploy; see `REPLAY_URL.txt` on the Pages site):
`https://<owner>.github.io/<repo>/emu/?package=<url-encoded https://<owner>.github.io/<repo>/fm1_key_assist.fwsc>&name=KEY%20ASSIST&script=%2F<repo>%2Femu%2Fdemo.script`

`?script=` only loads from the emulator page's own origin, so this works on the copy of `fm1-emu` that CI publishes beside the firmware, not on the public `joakimeriksson.github.io/fm1-emu` page. On the public page you can still use `?package=<our .fwsc URL>` and play by hand.

| # | Step | Time | What you must see and hear | Acceptance criterion |
|---|------|------|----------------------------|----------------------|
| 1 | **Clean boot, assist off** | 0–4.6 s | (There is no startup logo in this build.) The 240×240 LCD boots to the 8-track music page (`PIANO 1`, `303 SAW`, … `808 KIT`). At 4.0 s one key plays a single D with an FM piano tone. | 2 Zero-regression stock boot |
| 2 | **Short SCL tap is the stock action** | 5.5 s, 7.0 s, 8.0 s | Tap 1 opens the stock DX7 operator-scaling page (knob labels `L DEPTH / R DEPTH / BREAKPT`). Tap 2 shows `L CURVE / R CURVE / RATE SC`. HOME returns to the music page. KEY ASSIST does **not** open. | 3 (tap) |
| 3 | **600 ms hold opens KEY ASSIST** | 9.0 s → 9.6 s | A cyan hold bar fills along the top edge for 600 ms, then the `KEY ASSIST` page appears with `T1`, `C OFF`, `SINGLE / SNAP TO SCALE`, and knobs `ROOT / SCALE / CHORD / MAP`. Releasing SCL at 9.9 s leaves the page open. | 3 (hold) |
| 4 | **All four knobs update the LCD** | 10.8–15.1 s | K1: `C#` (10.8 s) then `D` (11.5 s). K2: `MAJOR` → `MINOR` → `DORIAN` (12.3, 12.9, 13.5 s). K3: `TRIAD` (14.3 s). K4: `WHITE` (15.1 s). The title reads `D DORIAN` and the subtitle `TRIAD / WHITE KEYS`. | 3 (knobs), `#` root |
| 5 | **One key = one chord, then back to stock** | 16.0–33.8 s | Single keys sound Dm (16.0 s), F (17.3 s), G (18.6 s), Am (19.9 s) triads. After SCL tap at 21.5 s the music page is back and a key still plays Dm (assist stays on). After K3 → `POWER`, the key at 27.0 s sounds D–A–D. Knobs are reset at 28.8–31.2 s, the page is left at 32.0 s, and the key at 33.0 s sounds a plain D again; assist is off. | 4 Audio and polyphony |

## Expected notes (checked automatically)

Every note the script sends, in order, as MIDI note numbers (D4 = 62):

```
62                  step 1: plain D
62 65 69            step 5: Dm   (D F A)   key 14
65 69 72            step 5: F    (F A C)   key 18
67 71 74            step 5: G    (G B D)   key 20
69 72 76            step 5: Am   (A C E)   key 21
62 65 69            music page, assist still on
62 69 74            POWER (D A D)
62                  assist off again: plain D
```

`tests/run_sim_test.sh demo` asserts exactly this list, checks that nothing is left sounding, and checks that the firmware ends on the ordinary music page.

## Recording (45 s)

1. Open the replay URL in Chrome, click once on the page so audio may start, and begin screen+audio capture (QuickTime "New Screen Recording" with a loopback device, or OBS with "Desktop Audio").
2. Stop after the key at 33.8 s plus the closing second (about 38 s of emulated time plus load); trim to 45 s.
3. Attach the video and the CI run link to the README.

## Automated evidence (fill in after CI)

| Check | Where it runs | Result |
|-------|---------------|--------|
| 115,038 unit checks, scale/chord/encoder/gesture logic | `tests/key_assist_test.c` | ✅ passed locally (macOS, clang `-Werror`); CI: ⏳ |
| 87 integration checks driving the real `screen.c` | `tests/run_sim_test.sh` | ✅ passed locally; CI: ⏳ |
| Demo script replayed against the firmware | `tests/run_sim_test.sh demo` | ✅ passed locally (25 checks); CI: ⏳ |
| Stock-vs-modified trace diff (33 steps) | `tests/run_sim_test.sh stock` / `regress` | ✅ identical locally; CI: ⏳ |
| Browser simulator WASM build `-Werror` | `web-preview` job | ⏳ needs CI (Windows) |
| Firmware links, app image ≤ 453,921 B | `firmware` job | ⏳ needs CI (Windows) |
| `.fwsc` boots in `fm1-emu`, chords audible | `emu-site` job (`tests/emu_smoke.mjs`) | ⏳ needs CI |
