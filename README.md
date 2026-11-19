# KEY ASSIST for the M-VAVE FM-1

**Scale lock and one-finger chords for the FM-1's 27 keys, built into the stock FM1 Open groovebox.** Hold **SCL** for 600 ms, pick a root, scale, chord type and key mapping on the 240×240 screen, and every key plays in key. No menu-diving, no separate firmware.

> **Status: emulator-verified, not hardware-validated.** The firmware logic, screen, controls and audio path are tested in a host simulator built from the real sources. The `.fwsc` was built with the official toolchain, fits the size limit, and boots and plays in an emulated FM-1 CPU. See [Proof](#proof). It has not been run on a physical FM-1; that is what the hardware request below is for.

- **Replay demo (34 s scripted):** `<REPLAY_URL — set by CI after the first deploy; see REPLAY_URL.txt on the Pages site>`
- **Play it yourself in the emulator:** same page, without `&script=`
- **Firmware:** `fm1_key_assist.fwsc` (SHA-256 and size report beside it)
- **Demo video (45 s):** `<TODO — record following proof/DEMO_STEPS.md>`

## What it does

| Control | Action |
|---|---|
| **Hold SCL ≥ 600 ms** | Opens the KEY ASSIST page. A bar fills along the top edge while you hold. |
| **K1 / K2 / K3 / K4** | Root note (C … B, sharps shown) · scale · chord · key mapping |
| **SELECT** | Switches track. Settings are per track |
| **SCL tap (< 600 ms) or HOME** | Leaves the page. Assist stays on |
| **Note keys** | Play assisted notes on every page |

- **Scales:** OFF, MAJOR, MINOR, DORIAN, MIXOLYDIAN, PENT MAJ, PENT MIN, BLUES, HARM MIN.
- **Chords:** SINGLE, TRIAD (1-3-5 of the scale), 7TH (1-3-5-7), POWER (root, fifth, octave). With scale OFF, chords use MAJOR.
- **Mapping:** SNAP sends an off-scale key to the nearest lower scale tone. WHITE makes the white keys consecutive scale degrees from the root and silences the black keys.
- **LCD:** the current root, scale, chord and mapping, and the four knob labels.
- **Drum tracks** (808, sampler) are never changed. The page says `DRUM TRACK - ASSIST OFF`.
- **Recording:** assisted notes go through the normal MIDI path, so step-record captures the chord.

Three things differ from stock, all on purpose:

1. **SCL now acts on release.** A tap opens the stock operator-scaling page when you let go, so it can be told apart from a hold.
2. **KEY ASSIST settings live in RAM only** and reset at power-off. Nothing is written to flash.
3. **Three stock extras were removed to make room.** M-VAVE's firmware has only 345 bytes of headroom, so a feature this size can only fit by giving something up: the **startup logo**, the **DX7 algorithm diagram** on the edit page (the algorithm number is still a knob), and the **voice-rename screen** (EDIT+REC, and the Actions-menu item). Everything else is unchanged. The size numbers below come from the real toolchain.

## Proof

Everything below is reproducible; commands are in [How to reproduce](#how-to-reproduce).

| Claim | Evidence | Status |
|---|---|---|
| Scale, chord, encoder and gesture logic is correct | 115,038 unit checks (`tests/key_assist_test.c`) | ✅ passes locally |
| The real `screen.c` behaves: tap vs hold, page, four knobs, notes, drum bypass, no hanging notes, no flash writes | 87 checks driving the real firmware code through the simulator | ✅ passes locally |
| One key sounds the right chord | The audio rendered for one assisted key is **bit-identical** to pressing the same 3–4 stock keys (max difference 0) | ✅ passes locally |
| KEY ASSIST off = stock | The same 33-step session replayed on the stock `screen.c` and the modified one: MIDI and knob labels identical on all 33 steps; LCD pixels identical except the four steps that show the operator-scaling page, where the removed algorithm diagram was | ✅ as described |
| The demo script does what it says | 34 s script replayed against the firmware; all 20 notes match | ✅ passes locally |
| Browser simulator builds to WASM | `web-preview` CI job | ⏳ needs the Windows CI run |
| Firmware links with the official toolchain; app image ≤ 453,921 B | Built locally with JieLi 2.5.3 (all 22 files match `sdk/manifest.json`) run under Wine. **App image 453,896 B** (25 B under the limit; stock is 453,576 B). `Package.cs` reports `fits=True` | ✅ built locally; the Windows CI run is still ⏳ |
| The `.fwsc` boots in an emulated FM-1 CPU and the chords are audible | `fm1-emu`'s WASM core ran the packaged `.fwsc` through `proof/demo.script` (34.5 s): audio produced, all six chord segments audible (RMS about 0.10 against 0.00 idle), and the LCD at 10.5 s matches the host render of the KEY ASSIST page pixel for pixel | ✅ passes locally |

**About the size limit.** The 453,921-byte ceiling in `sdk/Package.cs` applies to the *application image inside the package*, which is 453,896 B here. The finished `.fwsc` is 492,084 B; that is larger than the limit by design (M-VAVE's own V15 package is 699,956 B), so the workflow gates the app image and reports both numbers. The margin is only 25 B, so any later change to `screen.c` must be re-measured.

## For donors: how a physical unit gets tested

The software above is finished and testable without hardware. What it still needs is a real FM-1 to confirm it on the device: flashing, key scanning, LCD refresh and audio on the real board.

- **The ask:** one stock M-VAVE FM-1. Nothing else.
- **What you get:** a recorded on-device test within `<N>` days of arrival (flash, KEY ASSIST demo, rollback to M-VAVE's original V15 to prove the unit is left stock-safe), credit in this README, and the results in `HARDWARE_TEST.md`.
- **How to buy it (no cash from the recipient):** order directly to the recipient from the official M-VAVE store on AliExpress or Amazon, and check at checkout that the listing **ships to Sri Lanka**. Choose a tracked service. Prefer a listing that shows duties and taxes included; otherwise confirm Sri Lankan import duty and clearance against the current Sri Lanka Customs guidance before paying. `<TODO: add the live listing URLs and prices checked on the day. None are listed here because they change>`
- **Privacy:** the delivery address is sent by private message only and is never put in this repository.

## How to reproduce

Host tests run on macOS or Linux with only a C compiler:

```bash
# unit tests
cc -std=c11 -Wall -Wextra -Werror -Iapp tests/key_assist_test.c app/key_assist.c -o /tmp/ka_test && /tmp/ka_test
# the real firmware code in the simulator: page, knobs, notes, audio, polyphony
tests/run_sim_test.sh
# the demo script against the firmware
tests/run_sim_test.sh demo proof/demo.script
# zero regression: identical traces from the stock and the modified screen.c
tests/run_sim_test.sh stock > /tmp/stock.txt && tests/run_sim_test.sh regress > /tmp/modified.txt && diff /tmp/stock.txt /tmp/modified.txt
```

The firmware itself needs Windows (JieLi PI32v2 toolchain 2.5.3). The GitHub Actions workflow `.github/workflows/build-fwsc.yml` does the whole build on `windows-latest`; to build by hand, follow `README.upstream.md` (`./sdk/Build.ps1 -FirmwareOnly -Toolchain 'C:\JL\pi32'`). Step-by-step emulator verification: [`proof/DEMO_STEPS.md`](proof/DEMO_STEPS.md).

## Files

- `app/key_assist.c`, `app/key_assist.h`: scale and chord tables, note calculation, knob and gesture logic. No SDK or hardware calls.
- `app/screen.c`: the hooks: gesture on SCL, the KEY ASSIST page, `KA` tag, LEDs, chord-aware note on/off.
- `tests/`: unit test, simulator integration test, emulator check, runner script.
- `proof/`: demo script and verification steps.
- `README.upstream.md`: the original FM1 Open README, unchanged.

## License and credits

Application code: **GPLv3 with the FM1 SDK linking exception** (see `LICENSE.md`); the device engine is M-VAVE's binary SDK under its own terms (`THIRD_PARTY.md`). Retain both when distributing builds. Built on [m-vave/fm1_open](https://github.com/m-vave/fm1_open). The emulator page is a copy of [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) (MIT), built from a pinned commit by CI.
