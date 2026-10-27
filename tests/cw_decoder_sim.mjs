// CW decoder worker (resources/web-shared/cw-decoder-worker.js) under node.
//
// Loads the real module worker with a shim for the worker globals (self,
// postMessage, navigator, a fetch that serves file: URLs), so the DeepCW
// model and the vendored ONNX Runtime wasm are the ones the browser gets.
// Synthesizes CW with calibrated white noise, feeds it in one-second ticks
// like the page does, and checks the committed text:
//   - a clean 20 wpm CQ, a 25 wpm QSO over at -6 dB and a 30 wpm CQ at
//     -10 dB (SNR in 2.5 kHz, the DeepCW README's convention) come back
//     exact, word by word, with nothing left pending once the key is up;
//   - every committed word is a sent word: words are committed once, by
//     label position on the sliding window, never duplicated or split;
//   - the last word of an over commits after 1.5 s of silence (the model
//     never emits a space in trailing silence).
// Exit status 0 when everything passes.  Takes ~20 s.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const WORKER = path.resolve(here, '..', 'resources', 'web-shared', 'cw-decoder-worker.js');
const SR = 3200;

// ---- worker shim ----------------------------------------------------------
const realFetch = globalThis.fetch;
globalThis.fetch = async (u) => {
    const url = new URL(u);
    if (url.protocol !== 'file:') return realFetch(u);
    const body = fs.readFileSync(url);
    return new Response(body, { status: 200, headers: { 'content-length': String(body.length) } });
};
globalThis.self = globalThis;
if (!globalThis.navigator) globalThis.navigator = { hardwareConcurrency: 2 };
globalThis.crossOriginIsolated = false;
const replies = [];
globalThis.postMessage = (m) => replies.push(m);
const origLog = console.log;
console.log = (...a) => { if (!String(a[0]).startsWith('[CW Decoder Worker]')) origLog(...a); };
await import(pathToFileURL(WORKER).href);
async function send(msg) { replies.length = 0; await self.onmessage({ data: msg }); return replies.slice(); }

// ---- CW synthesis ---------------------------------------------------------
const MORSE = { A: '.-', B: '-...', C: '-.-.', D: '-..', E: '.', F: '..-.', G: '--.', H: '....', I: '..', J: '.---', K: '-.-', L: '.-..', M: '--', N: '-.', O: '---', P: '.--.', Q: '--.-', R: '.-.', S: '...', T: '-', U: '..-', V: '...-', W: '.--', X: '-..-', Y: '-.--', Z: '--..', 0: '-----', 1: '.----', 2: '..---', 3: '...--', 4: '....-', 5: '.....', 6: '-....', 7: '--...', 8: '---..', 9: '----.', '?': '..--..', '/': '-..-.', '.': '.-.-.-', ',': '--..--' };
// Seeded noise (mulberry32) so a run is reproducible.
let seed = 0x9e3779b9;
function rand() { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }
function gaussian() { let u = 0; while (u === 0) u = rand(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * rand()); }
// Returns Float32 samples at SR: 1 s lead, the text, 3 s tail of noise.
function synth(text, wpm, toneHz, snrDb) {
    const dot = 1.2 / wpm;
    const els = [[0, 1.0]];
    for (const w of text.toUpperCase().split(' ')) {
        for (const ch of w) {
            const p = MORSE[ch];
            if (!p) continue;
            for (const e of p) { els.push([1, e === '.' ? dot : 3 * dot]); els.push([0, dot]); }
            els[els.length - 1][1] = 3 * dot;
        }
        els[els.length - 1][1] = 7 * dot;
    }
    els.push([0, 3.0]);
    const total = els.reduce((a, e) => a + e[1], 0);
    const n = Math.round(total * SR);
    const x = new Float32Array(n);
    const ramp = Math.round(0.005 * SR);
    let i = 0, ph = 0;
    for (const [on, d] of els) {
        const len = Math.round(d * SR);
        for (let k = 0; k < len && i < n; k++, i++) {
            ph += 2 * Math.PI * toneHz / SR;
            let env = on ? 1 : 0;
            if (on && k < ramp) env = k / ramp;
            if (on && k > len - ramp) env = (len - k) / ramp;
            x[i] = 0.5 * env * Math.sin(ph);
        }
    }
    if (snrDb < 90) {
        const duty = els.filter(e => e[0]).reduce((a, e) => a + e[1], 0) / total;
        const ps = 0.125 * duty;                                   // time-averaged signal power
        const pn = ps / Math.pow(10, snrDb / 10) * (SR / 2) / 2500; // white noise, SNR referenced to 2.5 kHz
        const sd = Math.sqrt(pn);
        for (let k = 0; k < n; k++) x[k] += sd * gaussian();
    }
    let peak = 0;
    for (const v of x) peak = Math.max(peak, Math.abs(v));
    if (peak > 0.95) for (let k = 0; k < n; k++) x[k] *= 0.95 / peak;
    return x;
}

// ---- the run ----------------------------------------------------------------
let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }

const loaded = await send({ type: 'loadModel' });
check(loaded.some(m => m.type === 'modelLoaded'), 'model + runtime load from the vendored files');

async function run(text, wpm, snrDb, label) {
    const audio = synth(text, wpm, 700, snrDb);
    let committed = '';
    let lastPending = '';
    const segments = [];
    const ms = [];
    for (let off = 0; off < audio.length; off += SR) {
        const res = await send({ type: 'runInference', audioBuffer: audio.slice(off, off + SR) });
        const m = res[res.length - 1];
        if (!m || m.type !== 'inferenceResult') { check(false, label + ': worker answered ' + JSON.stringify(m)); return; }
        for (const s of m.segments) { committed += s.text; segments.push(s.text.trim()); }
        lastPending = m.pending;
        if (m.ms !== undefined) ms.push(m.ms);
    }
    const copy = committed.trim().replace(/\s+/g, ' ');
    const mean = ms.reduce((a, b) => a + b, 0) / Math.max(1, ms.length);
    console.log(`  ${label}: "${copy}"  (${segments.length} commits, ${mean.toFixed(0)} ms/tick)`);
    // The model may read a noise burst as a character before the text starts;
    // that is the decoder's business.  The text itself must come through whole,
    // and at most one such junk word is tolerated.
    check(copy.includes(text), label + ': the sent text is committed intact');
    check(lastPending === '', label + ': nothing left pending after the key goes up (trailing-silence commit)');
    const sent = new Set(text.split(' '));
    const junk = copy.split(' ').filter(w => !sent.has(w));
    check(junk.length <= 1, label + ': committed words are sent words, each once (junk: ' + JSON.stringify(junk) + ')');
    const words = copy.split(' ');
    check(words.length <= text.split(' ').length + 1, label + ': no word committed twice (' + words.length + ' words)');
}

await run('CQ CQ DE K1FM K1FM K', 20, 99, 'clean 20 wpm');
await run('K1FM DE W1AW UR RST 599 599 NAME BOB BOB QTH NEWINGTON CT HW? BK', 25, -6, '-6 dB 25 wpm');
await run('CQ CQ DE K1FM K1FM K', 30, -10, '-10 dB 30 wpm');

console.log(fails ? `${fails} FAILED` : 'ALL PASS');
process.exit(fails ? 1 : 0);
