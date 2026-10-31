// SSTV codec worker (resources/web-shared/sstv-worker.js) under node.
//
// Loads the real module worker with a shim for the worker globals (self,
// postMessage, a fetch that serves file: URLs), so the Asyncified sstv.wasm
// it decodes with is the one the browser gets. Images are encoded by the
// worker itself ('encode', the TX path), resampled to the rig's rate,
// optionally mixed with calibrated white noise, and fed back as 20 ms Int16
// chunks the way handleAudioData does.  Checks:
//   - clean Robot 36, Martin 1, Scottie 1 and PD-120 at 48 kHz decode
//     completely with the mode read from the VIS header and a small error;
//   - Robot 36 at 8 kHz (older rigs) decodes completely;
//   - a 0.1 % sound-card clock error does not slant the image (each line is
//     re-anchored on its sync pulse);
//   - a 1 s gap and two images back to back give two complete images;
//   - 'reset' half-way through an image drops it and the next one decodes
//     clean, with no row of the old image leaking through;
//   - a 20 ms audio frame inserted or dropped mid-picture (a TX underrun, a
//     lost packet) garbles a few rows and the rest follows the signal again
//     (the vendored crate's re-lock patch);
//   - config {force} (the START button) decodes the lines from the next
//     sample in the pinned mode, and the next picture by its header;
//   - white noise at +12 dB SNR (in 2.5 kHz) for Scottie 1 and +15 dB for
//     Robot 36 (its 9 ms syncs make it the harder one; +12 dB is marginal)
//     through the worker's quadrature front end still reads the VIS and
//     delivers every row; the raw path is run at +15 dB for the table only
//     (it needs ~27 dB), and +12 / +9 / +6 dB are measured and printed.
//   `--sweep [mode]` (env SSTV_SWEEP_SNR, SSTV_SWEEP_FE, SSTV_IQ_SMOOTH_MS)
//   prints the SNR table alone, for tuning the front end.
// Exit status 0 when everything passes.  Takes ~60 s.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const WORKER = path.resolve(here, '..', 'resources', 'web-shared', 'sstv-worker.js');
const CODEC = path.resolve(here, '..', 'resources', 'web-standalone', 'wasm', 'sstv-codec.mjs');

// ---- worker shim ----------------------------------------------------------
const realFetch = globalThis.fetch;
globalThis.fetch = async (u) => {
    const url = new URL(u);
    if (url.protocol !== 'file:') return realFetch(u);
    const body = fs.readFileSync(url);
    return new Response(body, { status: 200, headers: { 'content-length': String(body.length), 'content-type': 'application/octet-stream' } });
};
globalThis.self = globalThis;
globalThis.SSTV_CODEC_URL = pathToFileURL(CODEC).href;   // the worker's './wasm/' is the deployed layout
const replies = [];
globalThis.postMessage = (m) => replies.push(m);
await import(pathToFileURL(WORKER).href);

const tick = () => new Promise(r => setImmediate(r));
async function until(pred, ms = 10000) {
    const t0 = Date.now();
    while (!pred()) { if (Date.now() - t0 > ms) throw new Error('timeout'); await tick(); }
}
await until(() => replies.some(m => m.type === 'ready'));
const MODES = replies.find(m => m.type === 'ready').modes;
const modeByName = (n) => MODES.find(m => m.name === n);
async function send(msg) { await self.onmessage({ data: msg }); await tick(); }
function take(type) { const out = replies.filter(m => m.type === type); for (let i = replies.length - 1; i >= 0; i--) if (replies[i].type === type) replies.splice(i, 1); return out; }

// ---- signal ---------------------------------------------------------------
let seed = 0x9e3779b9;
function rand() { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }
function gaussian() { let u = 0; while (u === 0) u = rand(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * rand()); }

function testImage(mode) {
    const rgb = new Uint8Array(mode.w * mode.h * 3);
    for (let y = 0; y < mode.h; y++) for (let x = 0; x < mode.w; x++) {
        const i = (y * mode.w + x) * 3;
        const bar = Math.floor(x * 8 / mode.w);                 // colour bars on top
        if (y < mode.h / 3) { rgb[i] = (bar & 4) ? 220 : 30; rgb[i + 1] = (bar & 2) ? 220 : 30; rgb[i + 2] = (bar & 1) ? 220 : 30; }
        else { rgb[i] = (x * 255 / (mode.w - 1)) | 0; rgb[i + 1] = (y * 255 / (mode.h - 1)) | 0; rgb[i + 2] = 255 - ((x * 255 / (mode.w - 1)) | 0); }
    }
    return rgb;
}

async function encode(mode, rgb, rate) {
    await send({ type: 'encode', mode: mode.id, rgb, rate });
    await until(() => replies.some(m => m.type === 'encoded'));
    return take('encoded')[0];
}

// Linear resample Int16 from `from` Hz to `to` Hz (the TX path is 12 kHz,
// the rig's RX rate is 48 kHz or 8 kHz). `clockPpm` stretches the result
// to mimic a sound card whose clock is off.
function resample(int16, from, to, clockPpm = 0) {
    const ratio = from / (to * (1 + clockPpm / 1e6));
    const n = Math.floor(int16.length / ratio);
    const out = new Int16Array(n);
    for (let i = 0; i < n; i++) {
        const p = i * ratio, k = Math.floor(p), f = p - k;
        const a = int16[k] || 0, b = int16[k + 1] ?? a;
        out[i] = a + (b - a) * f;
    }
    return out;
}

// Signal at amplitude 0.5 plus white noise; SNR referenced to 2.5 kHz.
function withNoise(int16, rate, snrDb, { lead = 1, tail = 2 } = {}) {
    const n = Math.round(lead * rate) + int16.length + Math.round(tail * rate);
    const x = new Float32Array(n);
    for (let i = 0; i < int16.length; i++) x[Math.round(lead * rate) + i] = int16[i] / 32768 * 0.5;
    if (snrDb < 90) {
        const ps = 0.125;
        const sd = Math.sqrt(ps / Math.pow(10, snrDb / 10) * (rate / 2) / 2500);
        for (let k = 0; k < n; k++) x[k] += sd * gaussian();
    }
    let peak = 0;
    for (const v of x) peak = Math.max(peak, Math.abs(v));
    const g = peak > 0.95 ? 0.95 / peak : 1;
    const out = new Int16Array(n);
    for (let k = 0; k < n; k++) out[k] = Math.round(x[k] * g * 32767);
    return out;
}

// Feed in 20 ms chunks, yielding so the Asyncified decoder consumes as it
// would in a browser; returns the rows / image / done messages seen.
async function feed(int16, rate, { onChunk } = {}) {
    const chunk = Math.round(rate / 50);
    for (let p = 0; p < int16.length; p += chunk) {
        await send({ type: 'pcm', int16: int16.slice(p, Math.min(p + chunk, int16.length)) });
        if (onChunk) await onChunk(p / rate);
    }
    await tick();
}

function errorOf(rows, rgb, w) {
    let err = 0, n = 0;
    for (const r of rows) for (let i = 0; i < r.rgb.length; i++) { err += Math.abs(r.rgb[i] - rgb[r.index * w * 3 + i]); n++; }
    return n ? err / n : NaN;
}

// ---- the run ----------------------------------------------------------------
let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }
const table = [];

async function run(label, modeName, { rate = 48000, snrDb = 99, frontend = 'iq', clockPpm = 0, maxErr = 6, minRows = 1.0, assert = true } = {}) {
    const mode = modeByName(modeName);
    const rgb = testImage(mode);
    const tx = await encode(mode, rgb, 12000);
    const audio = withNoise(resample(tx.int16, 12000, rate, clockPpm), rate, snrDb);
    await send({ type: 'config', rate, pinMode: -1, frontend, iqSmoothMs: Number(process.env.SSTV_IQ_SMOOTH_MS) || 0 });
    take('row'); take('image'); take('done');
    const t0 = Date.now();
    await feed(audio, rate);
    await until(() => replies.some(m => m.type === 'done'), 5000).catch(() => {});
    const dt = (Date.now() - t0) / 1000;
    const images = take('image'), rows = take('row'), done = take('done');
    const err = errorOf(rows, rgb, mode.w);
    const detected = images.length ? MODES[images[0].mode].name : '-';
    table.push({ label, rate, snrDb, frontend, detected, rows: `${rows.length}/${mode.h}`, complete: done.length ? done[0].complete : '-', err: isNaN(err) ? '-' : err.toFixed(1), speed: (tx.seconds / dt).toFixed(0) + 'x' });
    if (assert) {
        check(images.length === 1 && detected === modeName, `${label}: VIS detected ${detected}`);
        check(rows.length >= Math.ceil(minRows * mode.h) && done.length === 1, `${label}: ${rows.length}/${mode.h} rows, ${done.length} done`);
        check(!isNaN(err) && err < maxErr, `${label}: mean abs error ${err.toFixed(2)} < ${maxErr}`);
        if (minRows >= 1) check(done.length === 1 && done[0].complete === true, `${label}: complete`);
    }
    return { mode, rgb, rows, images, done, err };
}

if (process.argv.includes('--sweep')) {
    // Ad hoc: node tests/sstv_codec_sim.mjs --sweep [mode]  -> SNR table only.
    const m = process.argv[3] || 'Robot 36';
    const snrs = (process.env.SSTV_SWEEP_SNR || '99,24,15,12,9,6').split(',').map(Number);
    for (const snr of snrs) for (const fe of (process.env.SSTV_SWEEP_FE || 'raw,iq').split(',')) await run(`${m} at ${snr} dB`, m, { snrDb: snr, frontend: fe, assert: false });
    console.log('\nlabel                           rate   SNR   fe   detected    rows      complete  err   speed');
    for (const r of table) console.log(`${r.label.padEnd(31)} ${String(r.rate).padStart(5)}  ${String(r.snrDb).padStart(4)}  ${r.frontend.padEnd(4)} ${r.detected.padEnd(11)} ${r.rows.padEnd(9)} ${String(r.complete).padEnd(9)} ${String(r.err).padStart(5)} ${r.speed}`);
    process.exit(0);
}

await run('Robot 36 clean', 'Robot 36');
await run('Martin 1 clean', 'Martin 1');
await run('Scottie 1 clean', 'Scottie 1');
await run('PD-120 clean', 'PD-120');
await run('Robot 36 at 8 kHz', 'Robot 36', { rate: 8000, maxErr: 15 });
await run('Martin 1, clock +1000 ppm', 'Martin 1', { clockPpm: 1000, maxErr: 8 });
await run('Scottie 1 at +12 dB', 'Scottie 1', { snrDb: 12, maxErr: 30 });
await run('Robot 36 at +15 dB', 'Robot 36', { snrDb: 15, maxErr: 30 });
await run('Robot 36 at +15 dB, raw', 'Robot 36', { snrDb: 15, frontend: 'raw', assert: false });
await run('Robot 36 at +12 dB', 'Robot 36', { snrDb: 12, assert: false });
await run('Robot 36 at +9 dB', 'Robot 36', { snrDb: 9, assert: false });
await run('Robot 36 at +6 dB', 'Robot 36', { snrDb: 6, assert: false });

// Two images back to back after a 1 s gap, one stream.
{
    const a = modeByName('Robot 36'), b = modeByName('Martin 2');
    const ra = testImage(a), rb = testImage(b);
    const ta = await encode(a, ra, 12000), tb = await encode(b, rb, 12000);
    const rate = 48000;
    const sa = withNoise(resample(ta.int16, 12000, rate), rate, 99, { lead: 0.5, tail: 0.5 });
    const sb = withNoise(resample(tb.int16, 12000, rate), rate, 99, { lead: 0.5, tail: 1.5 });
    await send({ type: 'config', rate, pinMode: -1 });
    take('row'); take('image'); take('done');
    await send({ type: 'gap', samples: rate });
    await feed(sa, rate);
    const rowsA = take('row'), imgA = take('image'), doneA = take('done');
    await feed(sb, rate);
    await until(() => replies.some(m => m.type === 'done'), 5000).catch(() => {});
    const rowsB = take('row'), imgB = take('image'), doneB = take('done');
    check(imgA.length === 1 && MODES[imgA[0].mode].name === 'Robot 36' && doneA.length === 1 && doneA[0].complete, `back to back: first image Robot 36 complete (${rowsA.length} rows)`);
    check(imgB.length === 1 && MODES[imgB[0].mode].name === 'Martin 2' && doneB.length === 1 && doneB[0].complete, `back to back: second image Martin 2 complete (${rowsB.length} rows)`);
    check(errorOf(rowsA, ra, a.w) < 6 && errorOf(rowsB, rb, b.w) < 6, 'back to back: both images clean');
}

// Reset half-way through an image: nothing of it survives, the next decodes.
{
    const m = modeByName('Robot 36');
    const rgb = testImage(m);
    const tx = await encode(m, rgb, 12000);
    const rate = 48000;
    const full = withNoise(resample(tx.int16, 12000, rate), rate, 99, { lead: 0.5, tail: 1 });
    await send({ type: 'config', rate, pinMode: -1 });
    take('row'); take('image'); take('done');
    await feed(full.slice(0, full.length >> 1), rate);
    const before = take('row').length;
    await send({ type: 'reset' });
    await tick(); await tick();
    const leaked = take('row').length, doneLeak = take('done').length;
    take('image');
    await feed(full, rate);
    await until(() => replies.some(m => m.type === 'done'), 5000).catch(() => {});
    const rows = take('row'), done = take('done'), imgs = take('image');
    check(before > 50, `reset: ${before} rows had arrived before the reset`);
    check(leaked === 0 && doneLeak === 0, `reset: no row or done leaked after the reset (${leaked}, ${doneLeak})`);
    check(imgs.length === 1 && rows.length === m.h && done.length === 1 && done[0].complete && rows.every(r => r.index < m.h), `reset: the next image decodes completely (${rows.length} rows)`);
    check(errorOf(rows, rgb, m.w) < 6, 'reset: the next image is clean');
}

// Pinned mode: a headerless Martin 1 (VIS cut off) still decodes.
{
    const m = modeByName('Martin 1');
    const rgb = testImage(m);
    const tx = await encode(m, rgb, 12000);
    const rate = 48000;
    const cut = tx.int16.slice(Math.round(12000 * 1.6));        // past the VOX tones, leader and VIS
    const audio = withNoise(resample(cut, 12000, rate), rate, 99);
    await send({ type: 'config', rate, pinMode: m.id });
    take('row'); take('image'); take('done');
    await feed(audio, rate);
    await until(() => replies.some(msg => msg.type === 'done'), 5000).catch(() => {});
    const rows = take('row'), done = take('done');
    check(rows.length >= m.h - 4 && done.length === 1, `pinned Martin 1 without a header: ${rows.length}/${m.h} rows`);
}

// A timing jump mid-picture: one 20 ms frame of silence inserted (a TX
// underrun on the bench put one in the air, 2026-10-31), or one dropped (a
// lost packet).  Upstream's cutter stayed on nominal timing for good and
// the rest of the picture came out as garbage; the vendored crate's re-lock
// patch (resources/sstv-shim/sstv-relock.patch) finds the new timing from
// three sync pulses, so only the rows around the jump are lost.
for (const [label, modeName, insert] of [['20 ms inserted', 'Robot 36', true], ['20 ms dropped', 'Robot 36', false], ['20 ms inserted', 'Martin 1', true]]) {
    const m = modeByName(modeName);
    const rgb = testImage(m);
    const tx = await encode(m, rgb, 12000);
    const rate = 48000;
    const whole = resample(tx.int16, 12000, rate);
    const at = Math.round(whole.length * 0.3), frame = rate / 50;
    const jumped = insert
        ? new Int16Array([...whole.subarray(0, at), ...new Int16Array(frame), ...whole.subarray(at)])
        : new Int16Array([...whole.subarray(0, at), ...whole.subarray(at + frame)]);
    await send({ type: 'config', rate, pinMode: -1 });
    take('row'); take('image'); take('done');
    await feed(withNoise(jumped, rate, 99), rate);
    await until(() => replies.some(msg => msg.type === 'done'), 5000).catch(() => {});
    const rows = take('row'), done = take('done');
    const bad = rows.filter(r => errorOf([r], rgb, m.w) > 40).map(r => r.index);
    const after = rows.filter(r => r.index > Math.round(m.h * 0.3) + 8);
    const e = errorOf(after, rgb, m.w);
    check(done.length === 1 && rows.length >= m.h - 2 && bad.length <= 6 && e < 10,
          `${modeName}, ${label} at 30 %: ${rows.length}/${m.h} rows, ${bad.length} rows garbled (${bad.join(' ')}), error after the jump ${e.toFixed(1)}`);
}

// START (config {force}): the decoder takes the image to begin at the next
// sample, no header search -- a picture whose VIS was missed.  The samples
// after the header (the worker reports its length with the encode) decode
// from row 0; the following picture, header and all, decodes the normal way.
{
    const m = modeByName('Scottie 1');
    const rgb = testImage(m);
    const tx = await encode(m, rgb, 12000);
    check(tx.headerSeconds > 1 && tx.headerSeconds < 3, `the encode reports the header length (${tx.headerSeconds.toFixed(2)} s)`);
    const rate = 48000;
    const lines = resample(tx.int16.slice(Math.round(12000 * tx.headerSeconds)), 12000, rate);
    await send({ type: 'config', rate, pinMode: m.id, force: true });
    take('row'); take('image'); take('done');
    await feed(withNoise(lines, rate, 99, { lead: 0, tail: 1 }), rate);
    await until(() => replies.some(msg => msg.type === 'done'), 5000).catch(() => {});
    let rows = take('row'), done = take('done'), img = take('image');
    const first = rows.length ? Math.min(...rows.map(r => r.index)) : -1;
    const e = rows.length ? errorOf(rows, rgb, m.w) : 999;
    check(img.length === 1 && img[0].mode === m.id && first === 0 && rows.length >= m.h - 2 && done.length === 1 && e < 20,
          `START decodes the lines from this moment (row ${first}, ${rows.length}/${m.h} rows, error ${e.toFixed(1)})`);
    // The next picture, with its header, through the same (still pinned) config.
    const tx2 = await encode(m, rgb, 12000);
    await feed(withNoise(resample(tx2.int16, 12000, rate), rate, 99), rate);
    await until(() => replies.some(msg => msg.type === 'done'), 5000).catch(() => {});
    rows = take('row'); done = take('done');
    check(rows.length >= m.h - 2 && done.length === 1 && take('image').length === 1, `after a START the next picture decodes by its header (${rows.length}/${m.h} rows)`);
}

// Spectrum messages carry the 1000-2500 Hz band.
{
    const spec = take('spectrum');
    const s = spec[spec.length - 1];
    check(spec.length > 10 && s && s.f0 >= 950 && s.f0 <= 1050 && s.f0 + s.df * (s.bins.length - 1) >= 2400, `spectrum: ${spec.length} frames, ${s && s.bins.length} bins from ${s && s.f0.toFixed(0)} Hz step ${s && s.df.toFixed(1)}`);
}

console.log('\nlabel                           rate   SNR   fe   detected    rows      complete  err   speed');
for (const r of table) console.log(`${r.label.padEnd(31)} ${String(r.rate).padStart(5)}  ${String(r.snrDb).padStart(4)}  ${r.frontend.padEnd(4)} ${r.detected.padEnd(11)} ${r.rows.padEnd(9)} ${String(r.complete).padEnd(9)} ${String(r.err).padStart(5)} ${r.speed}`);
console.log(fails ? `\n${fails} check(s) FAILED` : '\nall checks passed');
process.exit(fails ? 1 : 0);
