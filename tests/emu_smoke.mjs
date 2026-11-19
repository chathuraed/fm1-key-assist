// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// CPU-level check of the built firmware in fm1-emu's WASM core (the one the browser page runs).
//   node tests/emu_smoke.mjs <emu site dir> <package.fwsc> <demo.script> [expected-lcd.json]
// Only calls smoke.mjs / bench.mjs already use (fm1_boot_fwsc, fm1_bench_script, fm1_render, fm1_lcd, ...).
// Fatal: the package does not boot, the CPU halts, no audio, or the chord segments are silent.
// Reported as ::warning:: only: the LCD body of the KEY ASSIST page differs from the host render.
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [, , siteArg, packageArg, scriptArg, expectedArg] = process.argv;
if (!siteArg || !packageArg || !scriptArg) {
  console.error("usage: node tests/emu_smoke.mjs <emu site dir> <package.fwsc> <demo.script> [expected-lcd.json]");
  process.exit(2);
}
const dir = pathToFileURL(resolve(siteArg) + "/");
const { jitImports } = await import(new URL("jit.js", dir));
let x;
const { instance } = await WebAssembly.instantiate(readFileSync(new URL("fm1_web.wasm", dir)), { env: jitImports(() => x) });
x = instance.exports;
x.fm1_jit(1, 16);
x.fm1_ahead(1024);

const bytes = new Uint8Array(readFileSync(packageArg));
const pointer = x.fm1_alloc(bytes.length);
new Uint8Array(x.memory.buffer, pointer, bytes.length).set(bytes);
const failures = [], warnings = [];
const fail = (message) => { failures.push(message); console.error(`::error::${message}`); };
const warn = (message) => { warnings.push(message); console.warn(`::warning::${message}`); };
if (x.fm1_boot_fwsc(pointer, bytes.length, 0, 0, 100) !== 0) {
  console.error(`${packageArg}: not an FM-1 update package`);
  process.exit(1);
}
const script = readFileSync(scriptArg, "utf8").trim();
const scriptBytes = new TextEncoder().encode(script), scriptPointer = x.fm1_alloc(scriptBytes.length);
new Uint8Array(x.memory.buffer, scriptPointer, scriptBytes.length).set(scriptBytes);
if (x.fm1_bench_script(scriptPointer, scriptBytes.length)) { console.error("input script rejected"); process.exit(1); }

// The same timeline as proof/demo.script (emulated milliseconds).
const chordWindows = [[16200, 16900], [17500, 18200], [18800, 19500], [20100, 20800],
                      [22500, 23200], [27200, 27900]];
const idleWindow = [3200, 3900];
const hashAt = 10500;                 // KEY ASSIST page just opened, before the first knob turn
const endMs = 34500;
const lcdHashes = (rows) => {
  const px = new Uint16Array(x.memory.buffer, x.fm1_lcd(), 240 * 240).subarray(30 * 240);
  let native = 2166136261, swapped = 2166136261;
  for (const v of px) {
    const lo = v & 255, hi = v >> 8;
    native = Math.imul(native ^ lo, 16777619) >>> 0; native = Math.imul(native ^ hi, 16777619) >>> 0;
    swapped = Math.imul(swapped ^ hi, 16777619) >>> 0; swapped = Math.imul(swapped ^ lo, 16777619) >>> 0;
  }
  return { native, swapped };
};
const rmsOf = {};
const sums = new Map();
for (const w of [idleWindow, ...chordWindows]) sums.set(w, { sum: 0, n: 0 });
let frames = 0, shot = null, lit = 0, peak = 0;
while (x.fm1_ms() < endMs) {
  const before = x.fm1_ms();
  const have = x.fm1_render(128, 50_000_000);
  frames += have;
  if (have) {
    const left = new Float32Array(x.memory.buffer, x.fm1_out_l(), have);
    for (const w of sums.keys()) if (before >= w[0] && before < w[1]) {
      const acc = sums.get(w);
      for (const v of left) { acc.sum += v * v; acc.n++; }
    }
    for (const v of left) if (Math.abs(v) > peak) peak = Math.abs(v);
  }
  if (!shot && x.fm1_ms() >= hashAt) shot = lcdHashes();
  if (x.fm1_halted()) { fail(`the emulated CPU halted at ${x.fm1_ms()} ms`); break; }
  if (!lit && x.fm1_ms() >= 3500) {
    const lcd = new Uint16Array(x.memory.buffer, x.fm1_lcd(), 240 * 240);
    lit = lcd.reduce((n, v) => n + (v ? 1 : 0), 0);
  }
}
const rms = (w) => { const a = sums.get(w); return a.n ? Math.sqrt(a.sum / a.n) : 0; };
const idle = rms(idleWindow), chords = chordWindows.map(rms);
console.log(`package ${bytes.length} bytes; ${frames} audio frames; ${endMs} ms emulated; peak ${peak.toFixed(3)}; lit pixels ${lit}`);
console.log(`idle rms ${idle.toFixed(4)}; chord-window rms ${chords.map((v) => v.toFixed(4)).join(" ")}`);
if (frames < 10 * 44100) fail(`only ${frames} audio frames over ${endMs} ms`);
if (lit < 1000) fail(`the LCD shows ${lit} lit pixels after boot`);
chords.forEach((v, i) => { if (!(v > 0.003 && v > 3 * idle)) fail(`chord segment ${i + 1} is not audible (rms ${v.toFixed(4)}, idle ${idle.toFixed(4)})`); });
if (expectedArg && shot) {
  const want = JSON.parse(readFileSync(expectedArg, "utf8"));
  const same = shot.native === want.ka_default_native || shot.swapped === want.ka_default_swapped ||
               shot.native === want.ka_default_swapped || shot.swapped === want.ka_default_native;
  console.log(`KEY ASSIST page LCD body at ${hashAt} ms: ${same ? "matches the host render" : "DIFFERS from the host render"}`);
  if (!same) warn("the emulated LCD at 10.5 s does not match the host render of the KEY ASSIST page");
}
const report = { package_bytes: bytes.length, audio_frames: frames, peak, idle_rms: idle, chord_rms: chords, lit_pixels: lit,
                 failures, warnings, passed: failures.length === 0 };
writeFileSync(process.env.EMU_REPORT || "emu-report.json", JSON.stringify(report, null, 2));
process.exit(failures.length ? 1 : 0);
