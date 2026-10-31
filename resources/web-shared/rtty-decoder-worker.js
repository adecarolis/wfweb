// RTTY decoder worker — DeepRTTY (e04/DeepRTTY, MIT) on ONNX Runtime Web,
// with a classic FSK demodulator as the fallback engine.  An ES module
// worker: the page starts it with { type: 'module' }.
//
// The audio arrives as the rig's RX PCM at whatever rate the page has
// (the handleAudioData tap, Int16).  WfRttyTranslator (rtty.js) moves the
// tone pair the page points at (`centerHz`, mark low or high by `invert`)
// onto the model's 3200 Hz, 800 Hz-centred, mark-low timeline.  From there
// the DeepRTTY engine works like the DeepCW one (cw-decoder-worker.js):
// the model is a window CTC decoder with context on both sides, not a
// streaming one, so the worker keeps the last WINDOW_S seconds, decodes
// the whole window once per TICK_S of new audio and places every
// character on an absolute sample timeline.  Characters older than
// COMMIT_DELAY_S are committed once, by position, and never revisited;
// the younger ones are the pending text, redrawn every tick.  The
// committed ITA2 codes go through one stateful LTRS / FIGS decoder, the
// pending ones through a copy of it, so a figures run survives the
// boundary.
//
// Messages in:  { type: 'configure', inputRate, centerHz, invert, engine }   engine: 'deeprtty' | 'classic'
//               { type: 'loadModel' }
//               { type: 'audio', samples: Int16Array | Float32Array }         new RX samples at inputRate
//               { type: 'gap', samples: n }                                   n samples of silence (our own TX)
//               { type: 'reset' }                                             forget the text, keep the setup
// Messages out: { type: 'loadProgress', progress: 0..1 }
//               { type: 'modelLoaded', engine }
//               { type: 'spectrum', col: Uint8Array, lowHz, binHz }           a column of the tuning strip: the input audio's
//                                                                             passband, bins of binHz from lowHz up
//               { type: 'result', committed, pending, ms, engine }
//               { type: 'error', error }

// (`import * as ... from`, not a bare import: tools/fingerprint-static.py
// versions the `from` form.)
import * as rttyModule from './rtty.js';   // eslint-disable-line no-unused-vars

const WfRtty = self.WfRtty;
const MODEL_URL = new URL('./models/deeprtty.onnx', import.meta.url);
const META_URL = new URL('./models/deeprtty.onnx.json', import.meta.url);
const ORT_IMPORT_URL = new URL('./ort/ort.wasm.min.mjs', import.meta.url);
const ORT_MJS_URL = new URL('./ort/ort-wasm-simd-threaded.mjs', import.meta.url);
const ORT_WASM_URL = new URL('./ort/ort-wasm-simd-threaded.wasm', import.meta.url);
const MODEL_BYTES = 5688477, WASM_BYTES = 12361745;   // for the progress bar only

const RATE = WfRtty.MODEL_RATE;                       // 3200
const CHAR = WfRtty.CHAR_S * RATE;                    // samples per character, 528
const WINDOW_S = 12;                                  // the model's window
const TICK_S = 1;                                     // decode once this much new audio is in
const COMMIT_DELAY_S = 2;                             // a character younger than this may still change
const MIN_AUDIO_S = 2;
const MIN_SCORE = 0.2;                                // a CTC spike below this posterior is noise

// Tuning strip: a spectrum of the *input* audio over the whole passband
// (SPEC_LO_HZ..SPEC_HI_HZ), ten columns a second.  The input is box-decimated
// to about SPEC_RATE first, then a SPEC_N-point FFT gives ~8 Hz bins.  It
// shows the passband, not just the pair: in an Icom's RTTY mode the dial is
// the mark frequency and a station a few hundred Hz off the dial sits
// nowhere near the pair (IC-7300, 2026-10-31), so a strip of the pair
// alone showed only noise.  The page zooms, tunes and auto-tunes on it.

let engine = 'deeprtty';
let translator = null;
let inputRate = 48000;
let meta = null, session = null, ort = null;
let fftLength = 128, winLength = 96, hopLength = 24, outHop = 48, startBin = 0, bins = 13;
let window = null, fft = null;

// Sliding window on the model timeline.
let ring = new Float32Array(WINDOW_S * RATE);
let ringLen = 0, ringBase = 0;
let sinceTick = 0;
let busy = false;
let committedPos = -Infinity;
let ita2 = new WfRtty.Ita2Decoder(true);

// Tuning strip state.
const SPEC_RATE = 8000, SPEC_N = 1024, SPEC_LO_HZ = 300, SPEC_HI_HZ = 3000, SPEC_COLS_PER_S = 10;
let spec = null;      // { decim, rate, hop, b0, bins, buf, fill, fft, win, acc, count, peak }

function appendRing(chunk) {
    const cap = ring.length;
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

function resetTimeline() {
    ringLen = 0; ringBase = 0; sinceTick = 0;
    committedPos = -Infinity;
    if (classic) classic.reset();
}

// ---------------------------------------------------------------------------
// FFT (radix-2, real input)
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
// Spectrogram — the reference preprocessing of examples/nodejs/rtty_stream.mjs:
// reflect pad by half a frame, a Hann window of win_length centred in the
// fft_length frame, magnitudes of the 650..950 Hz bins, then
// "noise_q25_active_log1p": the noise level is the 25 % quantile of the
// bins over the *active* frames (frames whose own 25 % quantile is at least
// 1 % of the 90 % quantile of all frames, so a muted stretch does not drag
// the level down), scaled by the Rayleigh factor, floored at a fraction of
// the peak; the output is log1p(magnitude / noise).
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
    const mags = new Float32Array(frames * bins);
    const frame = new Float32Array(fftLength);
    for (let f = 0; f < frames; f++) {
        const off = f * hopLength;
        for (let i = 0; i < fftLength; i++) frame[i] = padded[off + i] * window[i];
        fft.magnitudes(frame, startBin, startBin + bins, mags.subarray(f * bins, (f + 1) * bins));
    }

    const q = meta.noise_level_quantile;
    const frameRank = Math.floor(q * (bins - 1));
    const frameLevels = new Float32Array(frames);
    const row = new Float32Array(bins);
    for (let f = 0; f < frames; f++) {
        row.set(mags.subarray(f * bins, (f + 1) * bins));
        row.sort();
        frameLevels[f] = row[frameRank];
    }
    const levels = frameLevels.slice().sort();
    const threshold = levels[Math.floor(meta.silent_frame_reference_quantile * (frames - 1))] * meta.silent_frame_relative_level;
    let active = 0;
    for (let f = 0; f < frames; f++) if (frameLevels[f] >= threshold) active++;
    const noiseValues = new Float32Array(active * bins);
    for (let f = 0, o = 0; f < frames; f++) if (frameLevels[f] >= threshold) {
        noiseValues.set(mags.subarray(f * bins, (f + 1) * bins), o);
        o += bins;
    }
    noiseValues.sort();
    let peak = 0;
    for (let i = 0; i < mags.length; i++) if (mags[i] > peak) peak = mags[i];
    const noise = Math.max(noiseValues[Math.floor(q * (noiseValues.length - 1))] / meta.noise_level_rayleigh_factor,
                           peak * meta.noise_level_min_relative_to_peak, meta.noise_level_min);
    const data = new Float32Array(mags.length);
    for (let i = 0; i < data.length; i++) data[i] = Math.log1p(mags[i] / noise);
    return { tensor: new ort.Tensor('float32', data, [1, 1, frames, bins]), frames };
}

// Greedy CTC over [frames x classes]: one token per run of a non-blank
// class, at the run's centre frame, with the run's peak posterior.
function ctcTokens(data, frames, classes, blank) {
    const tokens = [];
    let runCode = -1, runStart = 0, runPeak = -Infinity;
    const close = (stop) => {
        if (runCode >= 0 && runCode !== blank) tokens.push({ frame: (runStart + stop - 1) / 2, code: runCode, score: Math.exp(runPeak) });
    };
    for (let f = 0; f < frames; f++) {
        let best = 0, value = -Infinity;
        const base = f * classes;
        for (let c = 0; c < classes; c++) if (data[base + c] > value) { value = data[base + c]; best = c; }
        if (best !== runCode) { close(f); runCode = best; runStart = f; runPeak = value; }
        else if (value > runPeak) runPeak = value;
    }
    close(frames);
    return tokens;
}

// ---------------------------------------------------------------------------
// Classic engine: a non-coherent FSK demodulator on the same translated
// audio.  Mark and space energies come from one-bit-long correlators at
// 715 and 885 Hz; the sign of their difference is the line level.  Bit
// sync is a UART's: wait for the mark-to-space edge of a start bit, sample
// the start bit and the five data bits at their centres, check the stop
// bit is mark, else it was noise.  Runs on anything, needs no model.
// ---------------------------------------------------------------------------
class ClassicFsk {
    constructor() {
        this.bit = RATE / WfRtty.BAUD;                  // 70.4 samples
        this.n = Math.round(this.bit);
        this.wm = 2 * Math.PI * (WfRtty.MODEL_CENTER_HZ - WfRtty.SHIFT_HZ / 2) / RATE;
        this.ws = 2 * Math.PI * (WfRtty.MODEL_CENTER_HZ + WfRtty.SHIFT_HZ / 2) / RATE;
        this.reset();
    }
    reset() {
        this.hist = new Float32Array(this.n * 4);        // mi, mq, si, sq per sample
        this.pos = 0; this.k = 0;
        this.mi = this.mq = this.si = this.sq = 0;
        this.level = new Float32Array(Math.ceil(this.bit * 9));   // the last 9 bits of line level
        this.lpos = 0;
        this.idle = true; this.start = 0; this.codes = [];
    }
    // Returns the ITA2 codes found in this chunk.
    process(x) {
        const out = [];
        for (let i = 0; i < x.length; i++, this.k++) {
            const v = x[i], t = this.k;
            const cm = Math.cos(this.wm * t), sm = Math.sin(this.wm * t), cs = Math.cos(this.ws * t), ss = Math.sin(this.ws * t);
            const h = this.hist, p = this.pos * 4;
            this.mi += v * cm - h[p]; this.mq += v * sm - h[p + 1];
            this.si += v * cs - h[p + 2]; this.sq += v * ss - h[p + 3];
            h[p] = v * cm; h[p + 1] = v * sm; h[p + 2] = v * cs; h[p + 3] = v * ss;
            this.pos = (this.pos + 1) % this.n;
            const m = Math.hypot(this.mi, this.mq), s = Math.hypot(this.si, this.sq);
            const level = (m - s) / (m + s + 1e-9);      // +1 mark .. -1 space
            const prev = this.level[this.lpos];
            this.level[this.lpos] = level;
            this.lpos = (this.lpos + 1) % this.level.length;
            // The correlator lags the edge by half a bit: the sample that
            // just left the level buffer is 9 bits old, the newest 0.5 bits
            // behind the audio.  Sample the character once all of it is in.
            if (this.idle) {
                if (prev > 0 && level <= 0) { this.idle = false; this.start = t; }   // falling edge: start bit?
            } else if (t - this.start >= 7 * this.bit) {
                // bit centres relative to the edge; the level buffer holds
                // the last level.length samples ending at t
                const at = (b) => {
                    const age = t - (this.start + (b + 0.5) * this.bit);
                    const idx = (this.lpos - 1 - Math.round(age) + this.level.length * 2) % this.level.length;
                    return this.level[idx];
                };
                let ok = at(0) < 0 && at(6.5) > 0;
                if (ok) {
                    let code = 0;
                    for (let b = 0; b < 5; b++) if (at(1 + b) > 0) code |= 1 << b;
                    out.push(code);
                    this.idle = true;
                    // the next start edge comes after the stop bit; skip the rest of it
                    this.start = t;
                } else {
                    this.idle = true;
                }
            }
        }
        return out;
    }
}
let classic = null;

// ---------------------------------------------------------------------------
// Loading.  Model and runtime are kept in Cache Storage with their ETag and
// revalidated with If-None-Match on each load (see cw-decoder-worker.js for
// why: Chrome caches nothing for a self-signed origin).
// ---------------------------------------------------------------------------
const MODEL_CACHE = 'wfweb-rtty-model';

async function cachedFetch(url, onBytes) {
    let cache = null, hit = null;
    try {
        if (typeof caches !== 'undefined') { cache = await caches.open(MODEL_CACHE); hit = await cache.match(url.href); }
    } catch (e) { cache = null; hit = null; }
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
        } catch (e) { console.warn('[RTTY Decoder Worker] could not store ' + url.pathname + ': ' + e.message); }
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
    if (engine === 'classic') {
        classic = classic || new ClassicFsk();
        self.postMessage({ type: 'loadProgress', progress: 1 });
        return;
    }
    const expected = MODEL_BYTES + WASM_BYTES;
    let received = 0, lastReport = 0;
    const onBytes = (n) => {
        received += n;
        const now = performance.now();
        if (now - lastReport > 100) {
            lastReport = now;
            self.postMessage({ type: 'loadProgress', progress: Math.min(1, received / expected) });
        }
    };
    ort = await import(ORT_IMPORT_URL.href);
    ort.env.logLevel = 'error';
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
    if (meta.sample_rate !== RATE) throw new Error('model expects ' + meta.sample_rate + ' Hz');
    fftLength = meta.fft_length;
    winLength = meta.win_length;
    hopLength = meta.hop_length;
    outHop = meta.output_hop_length;
    const binHz = RATE / fftLength;
    startBin = Math.ceil(meta.spectrogram_min_freq_hz / binHz);
    bins = Math.floor(meta.spectrogram_max_freq_hz / binHz) + 1 - startBin;
    if (bins !== meta.spectrogram_frequency_bins) {
        throw new Error('metadata expects ' + meta.spectrogram_frequency_bins + ' bins, computed ' + bins);
    }
    window = new Float32Array(fftLength);
    const off = Math.floor((fftLength - winLength) / 2);
    for (let i = 0; i < winLength; i++) window[off + i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / winLength);
    fft = new FFT(fftLength);

    ort.env.wasm.wasmBinary = wasmBytes.buffer;
    session = await ort.InferenceSession.create(modelBytes, { executionProviders: ['wasm'] });
    self.postMessage({ type: 'loadProgress', progress: 1 });
    console.log('[RTTY Decoder Worker] DeepRTTY model loaded (' + ort.env.wasm.numThreads + ' thread'
        + (ort.env.wasm.numThreads === 1 ? '' : 's') + ')');
}

// ---------------------------------------------------------------------------
// Audio in
// ---------------------------------------------------------------------------
function specSetup() {
    const decim = Math.max(1, Math.round(inputRate / SPEC_RATE));
    const rate = inputRate / decim, binHz = rate / SPEC_N;
    const b0 = Math.round(SPEC_LO_HZ / binHz);
    const b1 = Math.min(SPEC_N / 2 - 1, Math.round(SPEC_HI_HZ / binHz));
    const win = new Float32Array(SPEC_N);
    for (let i = 0; i < SPEC_N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / SPEC_N);
    spec = { decim, rate, binHz, hop: Math.round(rate / SPEC_COLS_PER_S), b0, bins: b1 - b0 + 1,
             buf: new Float32Array(SPEC_N), fill: 0, fft: new FFT(SPEC_N), win, acc: 0, count: 0, peak: 1e-3 };
}

// Raw input samples (Int16 or Float32 at inputRate) -> strip columns.
function feedSpectrum(input) {
    if (!spec || spec.decim !== Math.max(1, Math.round(inputRate / SPEC_RATE))) specSetup();
    const sp = spec, scale = input instanceof Int16Array ? 1 / 32768 : 1;
    const frame = new Float32Array(SPEC_N), mags = new Float32Array(sp.bins);
    for (let i = 0; i < input.length; i++) {
        sp.acc += input[i] * scale;
        if (++sp.count < sp.decim) continue;
        const v = sp.acc / sp.decim;
        sp.acc = 0; sp.count = 0;
        if (sp.fill < SPEC_N) { sp.buf[sp.fill++] = v; continue; }
        // one column
        for (let k = 0; k < SPEC_N; k++) frame[k] = sp.buf[k] * sp.win[k];
        sp.fft.magnitudes(frame, sp.b0, sp.b0 + sp.bins, mags);
        let peak = 0;
        for (let k = 0; k < sp.bins; k++) if (mags[k] > peak) peak = mags[k];
        sp.peak = Math.max(peak, sp.peak * 0.98, 1e-4);          // slow AGC
        const col = new Uint8Array(sp.bins);
        for (let k = 0; k < sp.bins; k++) {
            const db = 20 * Math.log10(mags[k] / sp.peak + 1e-6);   // 0 dB = recent peak
            col[k] = Math.max(0, Math.min(255, Math.round((db + 40) / 40 * 255)));
        }
        self.postMessage({ type: 'spectrum', col, lowHz: sp.b0 * sp.binHz, binHz: sp.binHz }, [col.buffer]);
        sp.buf.copyWithin(0, sp.hop);
        sp.fill = SPEC_N - sp.hop;
        sp.buf[sp.fill++] = v;
    }
}

function onAudio(x) {
    if (!x.length) return;
    if (engine === 'classic') {
        if (!classic) classic = new ClassicFsk();
        const codes = classic.process(x);
        if (codes.length) {
            const text = ita2.decode(codes);
            if (text) self.postMessage({ type: 'result', committed: text, pending: '', ms: 0, engine });
        }
        return;
    }
    appendRing(x);
    sinceTick += x.length;
    if (sinceTick >= TICK_S * RATE && !busy && session) {
        sinceTick = 0;
        tick();
    }
}

async function tick() {
    busy = true;
    const t0 = performance.now();
    try {
        if (ringLen < MIN_AUDIO_S * RATE) return;
        const spec = spectrogram(ring.subarray(0, ringLen));
        if (!spec) return;
        const out = await session.run({ [meta.onnx_input_name]: spec.tensor });
        const lp = out[meta.onnx_output_name];
        const [, frames, classes] = lp.dims;
        const tokens = ctcTokens(lp.data, frames, classes, meta.blank_index)
            .filter(t => t.score >= MIN_SCORE)
            .map(t => ({ sample: ringBase + Math.round(t.frame * outHop), code: t.code }));
        const windowEnd = ringBase + ringLen;
        const commitBefore = windowEnd - COMMIT_DELAY_S * RATE;
        // Positions jitter a frame or two between decodes; a character is
        // new once it clears the last committed one by half a character.
        const fresh = tokens.filter(t => t.sample > committedPos + CHAR / 2);
        const commit = fresh.filter(t => t.sample < commitBefore);
        const pend = fresh.filter(t => t.sample >= commitBefore);
        let committed = '';
        if (commit.length) {
            committed = ita2.decode(commit.map(t => t.code));
            committedPos = commit[commit.length - 1].sample;
        }
        const pending = ita2.clone().decode(pend.map(t => t.code));
        self.postMessage({ type: 'result', committed, pending, ms: performance.now() - t0, engine });
    } finally {
        busy = false;
    }
}

self.onmessage = async (e) => {
    const msg = e.data;
    try {
        if (msg.type === 'configure') {
            const changed = !translator || msg.inputRate !== inputRate || msg.centerHz !== translator.centerHz
                || !!msg.invert !== translator.invert || (msg.engine && msg.engine !== engine);
            inputRate = msg.inputRate || inputRate;
            if (msg.engine) engine = msg.engine;
            if (!translator || msg.inputRate !== translator.inputRate) translator = new WfRtty.WfRttyTranslator(inputRate, msg.centerHz, msg.invert);
            else { translator.setCenter(msg.centerHz); translator.invert = !!msg.invert; }
            if (changed) { translator.reset(); resetTimeline(); }
            self.postMessage({ type: 'configured' });
        } else if (msg.type === 'loadModel') {
            await loadModel();
            self.postMessage({ type: 'modelLoaded', engine });
        } else if (msg.type === 'audio') {
            if (!translator) return;
            feedSpectrum(msg.samples);
            onAudio(translator.process(msg.samples));
        } else if (msg.type === 'gap') {
            if (!translator) return;
            const zeros = new Float32Array(msg.samples);
            feedSpectrum(zeros);
            onAudio(translator.process(zeros));
        } else if (msg.type === 'reset') {
            resetTimeline();
            ita2 = new WfRtty.Ita2Decoder(true);
        } else {
            self.postMessage({ type: 'error', error: 'Unknown message type' });
        }
    } catch (err) {
        self.postMessage({ type: 'error', error: err && err.message ? err.message : String(err) });
    }
};
