// CW Decoder Web Worker — DeepCW neural decoder (e04/deepcw-engine) on
// ONNX Runtime Web.  An ES module worker: the page starts it with
// { type: 'module' }.
//
// The model is a CRNN with a CTC head: it reads a log-magnitude spectrogram
// of a stretch of audio and returns one character class per spectrogram
// frame, using context on both sides.  It is not a streaming decoder, so
// this worker keeps a sliding window of the last WINDOW_S seconds, decodes
// the whole window every tick and places every label on an absolute sample
// timeline.  Labels still inside the last COMMIT_DELAY_S are shown as
// *pending* text — that part redraws every tick and corrects itself as more
// audio arrives (E, then I, then U) — and a word whose labels are older than
// that, and which the model has closed with a space (or followed with
// silence), is committed once, by position, and never touched again.  The
// audio is never cut on an energy guess: an earlier version did, and QSB
// fades read as key-up gaps, so windows started mid-character.
//
// Messages in:  { type: 'loadModel' }
//               { type: 'runInference', audioBuffer: Float32Array }   (new samples at 3200 Hz)
// Messages out: { type: 'loadProgress', progress: 0..1 }
//               { type: 'modelLoaded' }
//               { type: 'inferenceResult', segments: [{ text }], pending: string }
//               { type: 'error', error }

import * as ort from './ort/ort.wasm.min.mjs';

const MODEL_URL = new URL('./models/deepcw.onnx', import.meta.url);
const META_URL = new URL('./models/deepcw.onnx.json', import.meta.url);
const ORT_MJS_URL = new URL('./ort/ort-wasm-simd-threaded.mjs', import.meta.url);
const ORT_WASM_URL = new URL('./ort/ort-wasm-simd-threaded.wasm', import.meta.url);

// Streaming parameters (seconds).
const WINDOW_S = 12;          // audio kept and decoded every tick (context for the model)
const COMMIT_DELAY_S = 3;     // a label younger than this may still change: pending
const MAX_HOLD_S = 8;         // a word with no space after it is committed anyway at this age
const QUIET_TAIL_S = 1.5;     // no label for this long after a word: the over ended, commit it
const MIN_AUDIO_S = 2;        // do not bother the model with less than this

let meta = null;
let session = null;
let sampleRate = 3200;
let fftLength = 256;
let hopLength = 48;
let startBin = 0;
let bins = 65;
let window = null;
let fft = null;

// Sliding window: ring[0..ringLen) is the newest audio; ringBase is the
// absolute sample index of ring[0].  committedPos is the absolute sample
// just past the last committed label.
let ring = new Float32Array(0);
let ringLen = 0;
let ringBase = 0;
let committedPos = 0;
let committedOpen = false;     // the last commit was a partial word (no space after it yet)

function appendRing(chunk) {
    const cap = WINDOW_S * sampleRate;
    if (ring.length !== cap) {
        const grown = new Float32Array(cap);
        const keep = Math.min(ringLen, cap);
        grown.set(ring.subarray(ringLen - keep, ringLen));
        ringBase += ringLen - keep;
        ring = grown;
        ringLen = keep;
    }
    if (chunk.length >= cap) {
        ringBase += ringLen + chunk.length - cap;
        ring.set(chunk.subarray(chunk.length - cap));
        ringLen = cap;
        return;
    }
    const overflow = ringLen + chunk.length - cap;
    if (overflow > 0) {
        ring.copyWithin(0, overflow, ringLen);
        ringLen -= overflow;
        ringBase += overflow;
    }
    ring.set(chunk, ringLen);
    ringLen += chunk.length;
}

// ---------------------------------------------------------------------------
// FFT (radix-2, in place, real input)
// ---------------------------------------------------------------------------
class FFT {
    constructor(n) {
        this.n = n;
        this.rev = new Uint32Array(n);
        this.cos = new Float32Array(n / 2);
        this.sin = new Float32Array(n / 2);
        let bits = 0;
        while ((1 << bits) < n) bits++;
        for (let i = 0; i < n; i++) {
            let r = 0;
            for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
            this.rev[i] = r;
        }
        for (let i = 0; i < n / 2; i++) {
            this.cos[i] = Math.cos(-2 * Math.PI * i / n);
            this.sin[i] = Math.sin(-2 * Math.PI * i / n);
        }
        this.re = new Float32Array(n);
        this.im = new Float32Array(n);
    }

    // Magnitudes of bins [b0, b1) of the windowed frame.
    magnitudes(frame, b0, b1, out) {
        const n = this.n, re = this.re, im = this.im, rev = this.rev;
        for (let i = 0; i < n; i++) { re[rev[i]] = frame[i]; im[i] = 0; }
        for (let len = 2; len <= n; len <<= 1) {
            const half = len >> 1, step = n / len;
            for (let i = 0; i < n; i += len) {
                for (let j = 0, k = 0; j < half; j++, k += step) {
                    const wr = this.cos[k], wi = this.sin[k];
                    const a = i + j, b = a + half;
                    const vr = re[b] * wr - im[b] * wi;
                    const vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr; im[b] = im[a] - vi;
                    re[a] += vr; im[a] += vi;
                }
            }
        }
        for (let k = b0; k < b1; k++) out[k - b0] = Math.hypot(re[k], im[k]);
    }
}

// ---------------------------------------------------------------------------
// Spectrogram — exactly the engine's reference preprocessing: reflect pad
// by half a frame, Hann window, magnitude, log1p.  No per-window
// normalisation (the metadata says "log1p").
// ---------------------------------------------------------------------------
function spectrogram(audio) {
    const pad = fftLength >> 1;
    const padded = new Float32Array(audio.length + 2 * pad);
    for (let i = 0; i < pad; i++) {
        padded[i] = audio[pad - i];
        padded[pad + audio.length + i] = audio[audio.length - 2 - i];
    }
    padded.set(audio, pad);

    const frames = 1 + Math.floor((padded.length - fftLength) / hopLength);
    if (frames <= 0) return null;
    const data = new Float32Array(frames * bins);
    const mags = new Float32Array(frames * bins);
    const frame = new Float32Array(fftLength);
    for (let f = 0; f < frames; f++) {
        const off = f * hopLength;
        for (let i = 0; i < fftLength; i++) frame[i] = padded[off + i] * window[i];
        const o = f * bins;
        fft.magnitudes(frame, startBin, startBin + bins, mags.subarray(o, o + bins));
        for (let b = 0; b < bins; b++) data[o + b] = Math.log1p(mags[o + b]);
    }
    return { tensor: new ort.Tensor('float32', data, [1, 1, frames, bins]), mags, frames };
}

// ---------------------------------------------------------------------------
// CTC greedy decode → per-frame class, character spans, word-space spans.
// A "character" here includes the space class; a run of equal classes is
// one character, a blank separates repeats.
// ---------------------------------------------------------------------------
function decodeFrames(logProbs) {
    const [, frames, classes] = logProbs.dims;
    const data = logProbs.data;
    const blank = meta.blank_index;
    const chars = [];          // { ch, start, end }
    const spaces = [];         // { start, end } runs of the space class
    let prev = null;
    let cur = null;
    let spaceRun = null;
    const spaceIndex = meta.chars.indexOf(' ');

    for (let f = 0; f < frames; f++) {
        let best = 0, bv = -Infinity;
        const o = f * classes;
        for (let c = 0; c < classes; c++) {
            const v = data[o + c];
            if (v > bv) { bv = v; best = c; }
        }

        if (best === spaceIndex) {
            if (!spaceRun) { spaceRun = { start: f, end: f }; spaces.push(spaceRun); }
            else spaceRun.end = f;
        } else {
            spaceRun = null;
        }

        if (best === blank) { prev = null; cur = null; continue; }
        if (best === prev) { if (cur) cur.end = f; continue; }
        prev = best;
        cur = { ch: meta.chars[best] || '', start: f, end: f };
        chars.push(cur);
    }
    return { chars, spaces };
}

function textOf(chars) {
    return chars.map(c => c.ch).join('').replace(/\s+/g, ' ').trim();
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
// The model and the runtime are 27 MB, and the browser's HTTP cache cannot be
// counted on to keep them: Chrome stores nothing for an origin whose
// certificate it was told to accept, i.e. any wfweb server on its
// self-signed certificate -- every DECODE press downloaded all 27 MB again
// (2026-10-25).  So they are kept in Cache Storage with the ETag they came
// with, and each load asks the server with If-None-Match: a 304 (the server
// and GitHub Pages both answer one) means the stored copy is current, a 200
// replaces it.  A server that cannot be reached falls back to the stored copy.
const MODEL_CACHE = 'wfweb-cw-model';

async function cachedFetch(url, onBytes) {
    let cache = null, hit = null;
    try {
        if (typeof caches !== 'undefined') { cache = await caches.open(MODEL_CACHE); hit = await cache.match(url.href); }
    } catch (e) { cache = null; hit = null; }   // no Cache Storage here: download as before
    const etag = hit && hit.headers.get('ETag');
    let resp = null;
    try {
        resp = await fetch(url, etag ? { headers: { 'If-None-Match': etag } } : undefined);
    } catch (e) {
        if (!hit) throw e;
    }
    if (hit && (!resp || resp.status === 304)) {
        const b = new Uint8Array(await hit.arrayBuffer());
        onBytes(b.length);
        return b;
    }
    const bytes = await fetchWithProgress(resp, url, onBytes);
    const newTag = resp.headers.get('ETag');
    if (cache && newTag) {
        try {
            await cache.put(url.href, new Response(bytes, { headers: {
                'ETag': newTag, 'Content-Type': resp.headers.get('Content-Type') || 'application/octet-stream' } }));
        } catch (e) { console.warn('[CW Decoder Worker] could not store ' + url.pathname + ': ' + e.message); }
    }
    return bytes;
}

async function fetchWithProgress(resp, url, onBytes) {
    if (!resp.ok) throw new Error('fetch ' + url.pathname + ': ' + resp.status);
    if (!resp.body) { const b = new Uint8Array(await resp.arrayBuffer()); onBytes(b.length); return b; }
    const reader = resp.body.getReader();
    const chunks = [];
    let total = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        total += value.length;
        onBytes(value.length);
    }
    const out = new Uint8Array(total);
    let off = 0;
    for (const c of chunks) { out.set(c, off); off += c.length; }
    return out;
}

async function loadModel() {
    const expected = 15139839 + 12361745;   // model + wasm, for the progress bar only
    let received = 0;
    let lastReport = 0;
    const onBytes = (n) => {
        received += n;
        const now = performance.now();
        if (now - lastReport > 100) {
            lastReport = now;
            self.postMessage({ type: 'loadProgress', progress: Math.min(1, received / expected) });
        }
    };

    ort.env.logLevel = 'error';
    // Threads need SharedArrayBuffer, i.e. a cross-origin-isolated page; a
    // plain page runs the wasm single-threaded (still SIMD).
    ort.env.wasm.numThreads = self.crossOriginIsolated
        ? Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 2) - 1))
        : 1;
    ort.env.wasm.wasmPaths = { mjs: ORT_MJS_URL.href, wasm: ORT_WASM_URL.href };

    const [metaResp, wasmBytes, modelBytes] = await Promise.all([
        fetch(META_URL).then(r => { if (!r.ok) throw new Error('metadata ' + r.status); return r.json(); }),
        cachedFetch(ORT_WASM_URL, onBytes),
        cachedFetch(MODEL_URL, onBytes),
    ]);
    meta = metaResp;
    sampleRate = meta.sample_rate;
    fftLength = meta.fft_length;
    hopLength = meta.hop_length;
    const binHz = sampleRate / fftLength;
    startBin = Math.ceil(meta.spectrogram_min_freq_hz / binHz);
    bins = Math.floor(meta.spectrogram_max_freq_hz / binHz) + 1 - startBin;
    if (bins !== meta.spectrogram_frequency_bins) {
        throw new Error('metadata expects ' + meta.spectrogram_frequency_bins + ' bins, computed ' + bins);
    }
    window = new Float32Array(fftLength);
    for (let i = 0; i < fftLength; i++) window[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / fftLength);
    fft = new FFT(fftLength);

    ort.env.wasm.wasmBinary = wasmBytes.buffer;
    session = await ort.InferenceSession.create(modelBytes, { executionProviders: ['wasm'] });
    self.postMessage({ type: 'loadProgress', progress: 1 });
    console.log('[CW Decoder Worker] DeepCW model loaded (' + sampleRate + ' Hz, ' + bins + ' bins, '
        + ort.env.wasm.numThreads + ' thread' + (ort.env.wasm.numThreads === 1 ? '' : 's') + ')');
}

// ---------------------------------------------------------------------------
// One streaming tick
// ---------------------------------------------------------------------------
// Words of the decode, on the absolute timeline: a chunk is a run of
// non-space labels; `closed` says the model put a space after it.
function chunksOf(chars) {
    const out = [];
    let cur = null;
    for (const c of chars) {
        if (c.ch === ' ') { if (cur) cur.closed = true; cur = null; continue; }
        if (!cur) { cur = { chars: [], closed: false }; out.push(cur); }
        cur.chars.push(c);
    }
    return out;
}

async function runInference(chunk, msg_debug) {
    const t0 = performance.now();
    if (chunk && chunk.length) appendRing(chunk);

    const reply = (segments, pending, dbg) => self.postMessage({
        type: 'inferenceResult', segments, pending,
        ms: performance.now() - t0, audioS: ringLen / sampleRate, dbg: dbg || null,
    });

    if (ringLen < MIN_AUDIO_S * sampleRate) { reply([], ''); return; }

    const audio = ring.subarray(0, ringLen);
    const spec = spectrogram(audio);
    if (!spec) { reply([], ''); return; }
    const out = await session.run({ [meta.onnx_input_name]: spec.tensor });
    const { chars } = decodeFrames(out[meta.onnx_output_name]);

    // Frame f is centred on sample f * hop (reflect padding), so labels go
    // onto the absolute timeline as ringBase + f * hop.
    for (const c of chars) {
        c.s = ringBase + c.start * hopLength;
        c.e = ringBase + c.end * hopLength;
    }
    const windowEnd = ringBase + ringLen;
    const commitBefore = windowEnd - COMMIT_DELAY_S * sampleRate;
    const forceBefore = windowEnd - MAX_HOLD_S * sampleRate;
    let lastLabelEnd = 0;
    for (const c of chars) if (c.e > lastLabelEnd) lastLabelEnd = c.e;

    let dbg = null;
    if (msg_debug) {
        const lab = new Array(spec.frames).fill('-');
        for (const c of chars) lab[c.start] = c.ch === ' ' ? '_' : c.ch;
        dbg = { labels: lab.join(''), committedFrame: Math.round((committedPos - ringBase) / hopLength) };
    }

    const segments = [];
    const pendingWords = [];
    // Label positions jitter a frame or two between decodes.  committedPos
    // sits one hop past the end of the last committed label, so a label is
    // new when its *end* clears that by a couple of frames (the next label's
    // end is at least a character gap — six frames or more — further on).
    const slack = 2 * hopLength;

    for (const ck of chunksOf(chars)) {
        const fresh = ck.chars.filter(c => c.e >= committedPos + slack);
        if (!fresh.length) continue;                       // all of it committed earlier
        const newWord = fresh[0] === ck.chars[0];          // not a continuation of a partly committed word
        const last = ck.chars[ck.chars.length - 1];
        const quietAfter = last.e === lastLabelEnd && windowEnd - last.e >= QUIET_TAIL_S * sampleRate;
        const complete = ck.closed || quietAfter;

        let commit = [];
        if (last.e < commitBefore && complete) {
            commit = fresh;
        } else {
            // A word still open: only what is about to leave the window.
            commit = fresh.filter(c => c.e < forceBefore);
        }
        if (commit.length) {
            const text = commit.map(c => c.ch).join('');
            const wholeWord = commit.length === fresh.length && complete;
            // Every complete word ends with its space; a word after a forced
            // partial commit of another word needs one in front.
            segments.push({ text: (newWord && committedOpen ? ' ' : '') + text + (wholeWord ? ' ' : '') });
            committedPos = commit[commit.length - 1].e + hopLength;
            committedOpen = !wholeWord;
        }
        const rest = fresh.slice(commit.length);
        if (rest.length) pendingWords.push(rest.map(c => c.ch).join(''));
    }

    const merged = segments.map(s => s.text).join('');
    reply(merged ? [{ text: merged }] : [], pendingWords.join(' '), dbg);
}

self.onmessage = async (e) => {
    const msg = e.data;
    try {
        if (msg.type === 'loadModel') {
            await loadModel();
            self.postMessage({ type: 'modelLoaded' });
        } else if (msg.type === 'runInference') {
            if (!session) { self.postMessage({ type: 'error', error: 'Model not loaded' }); return; }
            await runInference(msg.audioBuffer, msg.debug === true);
        } else {
            self.postMessage({ type: 'error', error: 'Unknown message type' });
        }
    } catch (err) {
        self.postMessage({ type: 'error', error: err && err.message ? err.message : String(err) });
    }
};
