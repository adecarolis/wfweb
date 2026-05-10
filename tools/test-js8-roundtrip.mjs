// Phase 1 Loop A: encoder → 8-FSK synth → decoder round-trip.
//
// Self-contained: takes a 12-char JS8 message, calls js8_encode to get
// the 79-tone array, synthesizes 8-FSK audio at 12 kHz from those tones,
// pushes the audio through js8_decoder_run, drains js8_decoder_pop,
// asserts the decoded text matches the input.
//
// No JS8Call required. This is the inner-loop dev test for Phase 1
// decoder iteration. Run: node tools/test-js8-roundtrip.mjs

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
globalThis.__filename = __filename;
globalThis.__dirname  = __dirname;

const wasmDir   = path.join(__dirname, "..", "resources", "web-standalone", "wasm");
const wasmBytes = fs.readFileSync(path.join(wasmDir, "js8.wasm"));
const createJS8 = (await import(path.join(wasmDir, "js8.mjs"))).default;
const Module    = await createJS8({ wasmBinary: wasmBytes });

// ─── JS8 Normal-mode tone generation ──────────────────────────────────
//
// Symbol rate = 12000 / JS8A_SYMBOL_SAMPLES = 12000 / 1920 = 6.25 baud.
// 8-FSK with tone spacing equal to baud rate (so tones are orthogonal).
// We park tone 0 at BASE_HZ; tone n at BASE_HZ + n * BAUD_HZ.
//
// JS8 frames begin with JS8A_START_DELAY_MS = 500 ms of silence so the
// decoder has time to settle and pick up sync. We pad the front with
// that, and the back with a similar amount so the slot is comfortably
// inside the decoder's expected 15 s window.

const SAMPLE_RATE = 12000;
const SYMBOL_SAMPLES = 1920;       // JS8A_SYMBOL_SAMPLES
const NUM_SYMBOLS = 79;            // JS8_NUM_SYMBOLS
const START_DELAY_MS = 500;
const BAUD_HZ = SAMPLE_RATE / SYMBOL_SAMPLES;     // 6.25
const BASE_HZ = 1500;                              // tone 0 frequency

function synthesizeAudio(tones) {
  if (tones.length !== NUM_SYMBOLS) {
    throw new Error(`tones must be length ${NUM_SYMBOLS}, got ${tones.length}`);
  }
  const preroll = Math.round(START_DELAY_MS / 1000 * SAMPLE_RATE);
  const payload = NUM_SYMBOLS * SYMBOL_SAMPLES;
  // Pad to 15s slot
  const totalSamples = 15 * SAMPLE_RATE;
  const out = new Float32Array(totalSamples);

  // Continuous-phase 8-FSK: keep a phase accumulator across symbols so
  // we don't get phase discontinuity at symbol boundaries (those would
  // otherwise spread spectral energy and confuse the sync stage).
  let phase = 0;
  for (let s = 0; s < NUM_SYMBOLS; ++s) {
    const tone = tones[s];
    const freq = BASE_HZ + tone * BAUD_HZ;
    const dphi = 2 * Math.PI * freq / SAMPLE_RATE;
    const off = preroll + s * SYMBOL_SAMPLES;
    for (let i = 0; i < SYMBOL_SAMPLES; ++i) {
      out[off + i] = 0.5 * Math.sin(phase);
      phase += dphi;
      if (phase > 2 * Math.PI) phase -= 2 * Math.PI;
    }
  }
  return out;
}

// ─── Encode → tones helper ─────────────────────────────────────────────

function encode(msg, frameType = 0) {
  if (msg.length !== 12) throw new Error("must be 12 chars");
  const msgPtr = Module._malloc(13);
  for (let i = 0; i < 12; ++i) Module.HEAPU8[msgPtr + i] = msg.charCodeAt(i);
  Module.HEAPU8[msgPtr + 12] = 0;
  const tonesPtr = Module._malloc(79 * 4);
  const rc = Module._js8_encode(frameType, msgPtr, tonesPtr);
  let out = null;
  if (rc === 0) {
    out = new Int32Array(79);
    for (let i = 0; i < 79; ++i) out[i] = Module.HEAP32[tonesPtr / 4 + i];
  }
  Module._free(msgPtr);
  Module._free(tonesPtr);
  return out;
}

// ─── Decode helper ─────────────────────────────────────────────────────

function decode(samples, submode = 0) {
  const dec = Module._js8_decoder_new(submode);
  if (!dec) return [];

  // Push in chunks so the C-side ring doesn't truncate
  const sPtr = Module._malloc(samples.length * 4);
  const f32 = new Float32Array(Module.HEAPF32.buffer, sPtr, samples.length);
  f32.set(samples);
  Module._js8_decoder_push(dec, sPtr, samples.length);
  Module._free(sPtr);

  Module._js8_decoder_run(dec);

  const results = [];
  while (true) {
    const sp = Module._js8_decoder_pop(dec);
    if (!sp) break;
    let end = sp;
    while (Module.HEAPU8[end] !== 0) ++end;
    const json = new TextDecoder().decode(Module.HEAPU8.subarray(sp, end));
    Module._js8_free_string(sp);
    try { results.push(JSON.parse(json)); } catch {}
  }
  Module._js8_decoder_free(dec);
  return results;
}

// ─── AWGN — matches virtualrig's noise model (Int16 RMS, 0..1000) ─────
//
// Virtualrig adds Gaussian noise at a configurable Int16 RMS to keep
// audio in the same numeric scale the decoder sees from real LAN UDP
// rigs. Our float audio gets scaled by 32767 inside js8_decoder_run.
// To inject equivalent noise here we work at float RMS = n/32767.
//
// Box-Muller transform — fast enough for a 180k-sample buffer.

function addAWGN(audio, rmsInt16) {
  if (rmsInt16 <= 0) return audio;
  const rmsFloat = rmsInt16 / 32767;
  const out = new Float32Array(audio.length);
  for (let i = 0; i < audio.length; i += 2) {
    const u1 = Math.max(Math.random(), 1e-12);
    const u2 = Math.random();
    const r  = Math.sqrt(-2 * Math.log(u1));
    const z0 = r * Math.cos(2 * Math.PI * u2);
    const z1 = r * Math.sin(2 * Math.PI * u2);
    out[i]     = audio[i]     + rmsFloat * z0;
    if (i + 1 < audio.length) out[i + 1] = audio[i + 1] + rmsFloat * z1;
  }
  return out;
}

// ─── Test cases ────────────────────────────────────────────────────────

const cases = [
  { msg: "HELLOWK1FMab", frameType: 0 },
  { msg: "CQK1FMEM85en", frameType: 0 },
];

// SNR sweep — noise levels matching virtualrig's --noise flag scale.
// 0 = silent, 50 = quiet band, 200 = moderate, 500 = noisy, 1000 = very
// noisy. Phase 1 gate is "≥80% at noise=200 / 100% on clean bus".
const noiseLevels = [0, 50, 100, 200, 350, 500, 750, 1000, 2000];

console.log("\n┌─────────────────────────────────────────────────────────────────────┐");
console.log("│ Loop A — encoder → 8-FSK synth + AWGN → decoder round-trip          │");
console.log("└─────────────────────────────────────────────────────────────────────┘\n");

let allPass = true;
let cleanPass = true;
let noise200Hits = 0;
let noise200Total = 0;

for (const { msg, frameType } of cases) {
  const tones = encode(msg, frameType);
  if (!tones) { console.log(`encode("${msg}") FAILED`); allPass = false; continue; }
  const cleanAudio = synthesizeAudio(tones);
  console.log(`──── "${msg}" ────`);
  for (const noise of noiseLevels) {
    // Average over 3 trials per (msg, noise) for stochastic stability
    const trials = noise === 0 ? 1 : 3;
    let hits = 0;
    let snrSum = 0, snrN = 0;
    for (let t = 0; t < trials; ++t) {
      const audio = addAWGN(cleanAudio, noise);
      const decoded = decode(audio);
      const match = decoded.find(d => d && d.event === "decoded" &&
                                       d.text && d.text.replace(/\s+/g, "") === msg);
      if (match) { ++hits; snrSum += match.snr; ++snrN; }
    }
    const rate = hits / trials;
    const avgSnr = snrN > 0 ? (snrSum / snrN).toFixed(1) : "—";
    const bar = "█".repeat(Math.round(rate * 10)) + "░".repeat(10 - Math.round(rate * 10));
    console.log(`  noise=${noise.toString().padStart(4)}  ${bar}  ${(rate*100).toFixed(0).padStart(3)}%  avg-SNR=${avgSnr}`);
    if (noise === 0   && rate < 1.0)  cleanPass = false;
    if (noise === 200) { noise200Hits += hits; noise200Total += trials; }
  }
}

console.log("");
const noise200Rate = noise200Total > 0 ? noise200Hits / noise200Total : 0;
console.log(`Phase 1 gate (per PLAN.md):`);
console.log(`  100%  on clean bus       — ${cleanPass ? "PASS" : "FAIL"}`);
console.log(`  ≥80%  at noise=200       — ${(noise200Rate*100).toFixed(0)}% (${noise200Hits}/${noise200Total})  ${noise200Rate >= 0.8 ? "PASS" : "FAIL"}`);

const gateOk = cleanPass && noise200Rate >= 0.8;
console.log(gateOk ? "\nLOOP A SWEEP: PASS" : "\nLOOP A SWEEP: FAIL");
process.exit(gateOk ? 0 : 1);
