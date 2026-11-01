// RTTY decoder worker (resources/web-shared/rtty-decoder-worker.js) under node.
//
// Loads the real module worker with a shim for the worker globals (self,
// postMessage, navigator, a fetch that serves file: URLs), so the DeepRTTY
// model and the vendored ONNX Runtime wasm are the ones the browser gets.
// The RX audio is made by the page's own AFSK synthesizer (WfRttySynth in
// rtty.js) at the rig's rate with the tones where a rig puts them, 2125 /
// 2295 Hz, plus calibrated white noise, and fed in 100 ms Int16 chunks the
// way handleAudioData does.  Checks:
//   - the translator: mark low (LSB, the rig's RTTY mode) at 12 kHz and at
//     48 kHz, and mark high (USB, `invert`) come back as the sent text;
//   - a clean CQ and a QSO over at -6 dB (SNR in 2.5 kHz, the DeepRTTY
//     README's convention) come back exact, with nothing left pending once
//     the signal stops; at -10 dB the character error rate stays under 15 %;
//   - characters are committed once, by position on the sliding window:
//     the committed text contains the sent text exactly once;
//   - a FIGS run across a tick boundary keeps its shift (the stateful
//     ITA2 decoder);
//   - the classic FSK engine copies a clean over;
//   - the TX synthesizer's encoding round-trips through the decoder;
//   - the streaming synthesizer (the open channel: lead, characters one at
//     a time, LTRS diddles while idle, LTRS + tail) renders sample for
//     sample what the one-shot render does, and its idle diddles decode
//     to nothing: a typed over with pauses comes back as the typed text.
// Exit status 0 when everything passes.  Takes ~30 s.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const WORKER = path.resolve(here, '..', 'resources', 'web-shared', 'rtty-decoder-worker.js');

// ---- worker shim ----------------------------------------------------------
const realFetch = globalThis.fetch;
globalThis.fetch = async (u) => {
    const url = new URL(u);
    if (url.protocol !== 'file:') return realFetch(u);
    const body = fs.readFileSync(url);
    return new Response(body, { status: 200, headers: { 'content-length': String(body.length) } });
};
globalThis.self = globalThis;
globalThis.crossOriginIsolated = false;
const replies = [];
globalThis.postMessage = (m) => replies.push(m);
const origLog = console.log;
console.log = (...a) => { if (!String(a[0]).startsWith('[RTTY Decoder Worker]')) origLog(...a); };
await import(pathToFileURL(WORKER).href);
const WfRtty = globalThis.WfRtty;
// A decode runs after onmessage returns (the ONNX session is async), so
// give the worker a turn of the event loop before collecting its replies.
async function send(msg) { replies.length = 0; await self.onmessage({ data: msg }); await new Promise(r => setTimeout(r, 0)); return replies.slice(); }

// ---- signal ---------------------------------------------------------------
let seed = 0x9e3779b9;
function rand() { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }
function gaussian() { let u = 0; while (u === 0) u = rand(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * rand()); }

// 1 s of noise, the over, 3 s of noise; Int16 at `rate`.  `markLow` false
// puts the mark on the upper tone (USB).
// `typed` (an array of strings) renders the over the way the panel's open
// channel does: a character at a time through a stateful encoder, with
// `idleChars` LTRS between the pieces.
function synth(text, rate, snrDb, markLow, typed, idleChars = 0) {
    const synth = new WfRtty.WfRttySynth({ sampleRate: rate, centerHz: 2210, markLow, amplitude: 0.5 });
    let over;
    if (typed) {
        synth.reset();
        const enc = new WfRtty.Ita2Encoder();
        const parts = [synth.renderMark(0.5), synth.renderCodes([WfRtty.LTRS, WfRtty.LTRS])];
        for (const piece of typed) {
            for (const ch of piece) parts.push(synth.renderCodes(enc.encode(ch)));
            for (let i = 0; i < idleChars; i++) parts.push(synth.renderCodes(enc.idle()));
        }
        parts.push(synth.renderCodes([WfRtty.LTRS]), synth.renderMark(0.2));
        over = new Float32Array(parts.reduce((n, p) => n + p.length, 0));
        let off = 0;
        for (const p of parts) { over.set(p, off); off += p.length; }
    } else {
        over = synth.render(text, 0.5, 0.2);
    }
    const lead = rate, tail = 3 * rate;
    const n = lead + over.length + tail;
    const x = new Float32Array(n);
    x.set(over, lead);
    if (snrDb < 90) {
        const ps = 0.125;                                            // 0.5 amplitude sine
        const pn = ps / Math.pow(10, snrDb / 10) * (rate / 2) / 2500; // white noise, SNR referenced to 2.5 kHz
        const sd = Math.sqrt(pn);
        for (let k = 0; k < n; k++) x[k] += sd * gaussian();
    }
    let peak = 0;
    for (const v of x) peak = Math.max(peak, Math.abs(v));
    const g = peak > 0.95 ? 0.95 / peak : 1;
    const i16 = new Int16Array(n);
    for (let k = 0; k < n; k++) i16[k] = Math.round(x[k] * g * 32767);
    return i16;
}

// ---- the run ----------------------------------------------------------------
let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }
const norm = (s) => s.replace(/[\r\x07]/g, '').replace(/\s+/g, ' ').trim();

function editDistance(a, b) {
    let prev = [...Array(b.length + 1).keys()];
    for (let i = 1; i <= a.length; i++) {
        const cur = [i];
        for (let j = 1; j <= b.length; j++) cur.push(Math.min(cur[j - 1] + 1, prev[j] + 1, prev[j - 1] + (a[i - 1] !== b[j - 1] ? 1 : 0)));
        prev = cur;
    }
    return prev[b.length];
}

async function run(label, text, { rate = 12000, snrDb = 99, markLow = true, invert = false, engine = 'deeprtty', maxCer = 0, typed = null, idleChars = 0 } = {}) {
    await send({ type: 'configure', inputRate: rate, centerHz: 2210, invert, engine });
    await send({ type: 'reset' });
    const audio = synth(text, rate, snrDb, markLow, typed, idleChars);
    let committed = '', pending = '';
    const ms = [];
    const chunk = Math.round(rate / 10);
    for (let off = 0; off < audio.length; off += chunk) {
        const res = await send({ type: 'audio', samples: audio.slice(off, off + chunk) });
        for (const m of res) {
            if (m.type === 'error') { check(false, label + ': worker error ' + m.error); return ''; }
            if (m.type !== 'result') continue;
            committed += m.committed;
            pending = m.pending;
            if (m.ms) ms.push(m.ms);
        }
    }
    const copy = norm(committed);
    const mean = ms.reduce((a, b) => a + b, 0) / Math.max(1, ms.length);
    console.log(`  ${label}: "${copy}"  (${mean.toFixed(0)} ms/tick)`);
    const want = norm(text);
    if (maxCer === 0) {
        check(copy.includes(want), label + ': the sent text is committed intact');
        check(copy.indexOf(want) === copy.lastIndexOf(want), label + ': committed once');
    } else {
        const cer = editDistance(want, copy) / want.length;
        check(cer <= maxCer, label + ': character error rate ' + (100 * cer).toFixed(1) + '% (limit ' + (100 * maxCer) + '%)');
    }
    if (engine === 'deeprtty') check(pending === '', label + ': nothing left pending after the signal stops');
    return copy;
}

const loaded = await send({ type: 'loadModel' });
check(loaded.some(m => m.type === 'modelLoaded'), 'model + runtime load from the vendored files');

await run('clean LSB 12 kHz', 'CQ CQ CQ DE K1FM K1FM K1FM PSE K');
await run('clean LSB 48 kHz', 'CQ CQ DE K1FM K1FM K', { rate: 48000 });
await run('clean USB (mark high)', 'CQ CQ DE K1FM K1FM K', { markLow: false, invert: true });
await run('-6 dB QSO over', 'K1FM DE W1AW W1AW UR RST 599 599 NAME IS BOB BOB QTH NEWINGTON CT HW? K1FM DE W1AW K', { snrDb: -6 });
// The README puts 10 % CER at -11 dB; at -10 dB a few characters may go.
await run('-10 dB CQ', 'CQ TEST DE JA1ABC JA1ABC K W1AW DE JA1ABC 599 001 001 K', { snrDb: -10, maxCer: 0.15 });
// A figures run long enough to straddle several ticks and the commit boundary.
await run('figures across ticks', 'W1AW DE JA1ABC 599 001 001 ' + '1234567890/'.repeat(8) + ' K', { snrDb: 0 });
await run('classic engine, clean', 'CQ CQ DE K1FM K1FM K', { engine: 'classic' });

// The streaming synthesizer: piecewise == one-shot, sample for sample
// (the ramps of the one-shot render aside), with no idle inserted.
{
    const one = new WfRtty.WfRttySynth({ sampleRate: 12000, centerHz: 2210 });
    const whole = one.render('CQ DE K1FM 599', 0.4, 0.2);
    const two = new WfRtty.WfRttySynth({ sampleRate: 12000, centerHz: 2210 });
    two.reset();
    const enc = new WfRtty.Ita2Encoder();
    const parts = [two.renderMark(0.4), two.renderCodes([WfRtty.LTRS, WfRtty.LTRS])];
    for (const ch of 'CQ DE K1FM 599') parts.push(two.renderCodes(enc.encode(ch)));
    parts.push(two.renderCodes([WfRtty.LTRS]), two.renderMark(0.2));
    const len = parts.reduce((n, p) => n + p.length, 0);
    let diffs = 0, i = 0;
    for (const p of parts) for (const v of p) { if (i > 60 && i < len - 61 && Math.abs(v - whole[i]) > 1e-5) diffs++; i++; }
    check(len === whole.length && diffs === 0, `streaming synth renders the one-shot over sample for sample (${len} vs ${whole.length}, ${diffs} differ)`);
}
// A typed over: pieces with LTRS diddles between them, figures kept across a pause.
await run('typed over with idle diddles', 'HELLO W1AW UR 599 599 QRZ? DE K1FM K', { typed: ['HELLO ', 'W1AW ', 'UR 599', ' 599 ', 'QRZ? DE ', 'K1FM K'], idleChars: 4 });

console.log(fails ? `${fails} FAILED` : 'ALL PASS');
process.exit(fails ? 1 : 0);
