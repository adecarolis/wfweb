// SSTV codec worker (ES module worker, both builds). Owns the WebAssembly
// codec (resources/web-standalone/wasm/sstv-codec.mjs → sstv.wasm, the
// `sstv` Rust crate behind a C ABI), finds where a picture starts in the
// rig's RX audio, hands the crate that start, paints its rows, synthesizes
// pictures for transmit and posts a small spectrum of the input for the
// panel's tuning strip. Used by resources/web-shared/sstv-panel.js; driven
// under node by tests/sstv_codec_sim.mjs.
//
// Messages in:
//   {type:'config', rate, pinMode, frontend}  restart at `rate` Hz (pinMode:
//                                            mode id, -1 = any VIS; frontend
//                                            'iq' (default) or 'raw';
//                                            iqSmoothMs optional)
//   {type:'start', pinMode}                   START: the next picture begins
//                                            at its line sync pulses, no
//                                            header; pinMode -1 = tell the
//                                            mode from the pulse spacing. A
//                                            decode in progress is dropped
//                                            (no 'done')
//   {type:'pcm', int16}                       RX audio at `rate`
//   {type:'gap', samples}                     silence while we transmit
//   {type:'reset'}                            drop the image in progress and
//                                            the sync pulses seen so far
//   {type:'encode', mode, rgb, rate}          rgb = Uint8Array w*h*3
// Messages out:
//   {type:'ready', modes:[{id, key, name, w, h}]}
//   {type:'image', mode, w, h, via}           an image starts (mode = id;
//                                            via 'vis' or 'sync')
//   {type:'row', index, rgb}                  Uint8Array w*3 (transferred)
//   {type:'done', complete}                   the image ended (also when a
//                                            new header cut it short)
//   {type:'spectrum', bins, f0, df}           Float32Array, 1000-2500 Hz
//   {type:'encoded', int16, seconds, headerSeconds}
//   {type:'error', message}
//
// Image search. The crate finds a picture by splitting its frequency track
// into tones where the frequency holds within 50 Hz for 3 ms: every VIS bit
// and every sync pulse has to come through as one such tone, which white
// noise at +10 dB SNR (2.5 kHz) already defeats, and a station a few dB
// weaker than that was missed on the air (2026-10-31). So the worker looks
// for the picture itself, on the instantaneous frequency its own IQ front
// end computes anyway, averaged into 0.5 ms slots:
//  - VIS: at every slot the hypothesis "a start bit began here" is scored
//    from window means -- 150 ms of 1900 Hz leader before it (whose mean
//    also gives the station's tuning offset, applied to the bits), the
//    1200 Hz start and stop bits and the eight 30 ms data bits at 1100 /
//    1300 Hz, read over their middle 20 ms, even parity. Averaging over a
//    whole bit is what the crate's splitter lacks.
//  - sync pulses: runs of the track below 1350 Hz lasting 3 ms or more;
//    three whose ends are one line apart (a mode's sync spacing, within
//    2.5 ms) name the mode -- the smallest spacing that fits, so a Robot 36
//    pair is not read as Robot 72 -- and the sequence's other control tones
//    (porches, separators) pick the alignment, as for the Robot 36 line
//    pair whose two syncs look alike. Used for a START (any mode, or the
//    pinned one) and, as the crate did, whenever a mode is pinned.
// The samples the crate eats wait in a 4 s delay buffer, so a picture found
// 300 ms (VIS) or two lines (sync) after it began is decoded from its very
// first sample: the crate is always started headerless, pinned to the mode,
// at that sample (`at_first_sample`), and its cutter then follows the sync
// pulses it can see, or nominal timing where it cannot. A VIS heard while a
// picture is decoding ends that picture ('done' incomplete) and starts the
// new one, so a station that cuts a picture short or a START on noise do
// not leave the decoder deaf for the rest of the mode's duration.
//
// The crate's decoder pulls samples; the wasm is Asyncified so decodeNext()
// is a Promise that the `awaitSamples` callback suspends whenever the ring is
// empty. Each start / 'config' / 'reset' bumps a generation: the suspended
// decode is told to abort (awaitSamples resolves 1), unwinds, and the next
// decode starts once it has returned. Rows from an aborted generation are
// dropped.

const CODEC_URL = (typeof self !== 'undefined' && self.SSTV_CODEC_URL)
    ? self.SSTV_CODEC_URL
    : new URL('./wasm/sstv-codec.mjs', import.meta.url).href;
const WASM_URL_OVERRIDE = (typeof self !== 'undefined' && self.SSTV_WASM_BYTES) || null;

const SPECTRUM_LO = 1000, SPECTRUM_HI = 2500;
const SPECTRUM_INTERVAL_MS = 100;

let codec = null;
let layouts = null;       // per mode id: codec.modeLayout(id)
let visByCode = null;     // Map: VIS code -> mode id
let rate = 48000;
let pin = -1;             // the only mode accepted, or -1
let armed = false;        // START: the next sync train starts a picture
let frontend = null;      // {kind, state}
let iqSmoothMs = 0;       // 0 = IQ_SMOOTH_MS; a config may override it (the sim's sweep)
let gen = 0;
let loopDone = Promise.resolve();
let waiter = null;        // {resolve, gen}
let decoding = null;      // {mode, via, from} while the crate decodes a picture

const post = (msg, transfer) => self.postMessage(msg, transfer || []);

// ---------------------------------------------------------------- codec ---

async function loadCodec() {
    const mod = await import(CODEC_URL);
    codec = await mod.loadSstvCodec(WASM_URL_OVERRIDE ? { wasmBytes: WASM_URL_OVERRIDE } : {});
    layouts = codec.modes.map(m => codec.modeLayout(m.id));
    visByCode = new Map();
    for (const m of codec.modes) { const v = codec.modeVis(m.id); if (v >= 0) visByCode.set(v, m.id); }
    post({ type: 'ready', modes: codec.modes.map(m => ({ id: m.id, key: m.key, name: m.name, w: m.w, h: m.h })) });
}

// ------------------------------------------------------------- decoding ---

function wakeWaiter(value) {
    const w = waiter;
    waiter = null;
    if (w) w.resolve(value);
}

// One picture: the crate, pinned to `mode`, takes the ring to begin at the
// picture's first sample.
async function runLoop(myGen, mode, via) {
    let result;
    try {
        result = await codec.decodeNext(rate, mode, {
            awaitSamples: () => new Promise(resolve => {
                if (myGen !== gen) { resolve(1); return; }
                waiter = { resolve, gen: myGen };
            }),
            onMode: (id, w, h) => { if (myGen === gen) post({ type: 'image', mode: id, w, h, via }); },
            onRow: (index, rgb) => { if (myGen === gen) post({ type: 'row', index, rgb }, [rgb.buffer]); },
        }, true);
    } catch (e) {
        // A wasm trap leaves the instance in an unknown state: reload it.
        post({ type: 'error', message: 'decoder: ' + (e && e.message || e) });
        codec = null;
        if (myGen === gen) decoding = null;
        try { await loadCodec(); } catch (e2) { post({ type: 'error', message: 'codec reload failed: ' + (e2 && e2.message || e2) }); }
        return;
    }
    if (myGen !== gen) return;        // aborted by a start / config / reset
    decoding = null;
    // result === -1 only when the samples ended, i.e. an abort, handled above.
    if (result >= 0) post({ type: 'done', complete: result === 1 });
}

// End the decode in progress, if any; `cutShort` reports it as an
// incomplete picture (a new header took over), else it just goes away.
async function abortDecode(cutShort) {
    const was = decoding;
    decoding = null;
    gen++;
    wakeWaiter(1);
    await loopDone;
    if (was && cutShort) post({ type: 'done', complete: false });
}

// Start decoding `mode` from absolute sample `from` (in the delay buffer).
// Called from inside the search, mid-chunk: it ends the decode in progress
// (microtasks, so the rest of the chunk is in the delay buffer by the time
// the new decode is fed) and refuses a second start meanwhile.
let starting = false;
async function beginDecode(mode, from, via) {
    if (starting) return;
    starting = true;
    armed = false;
    try {
        await abortDecode(true);
        if (!codec) return;
        codec.resetRx();
        from = Math.round(from);
        // The start may lie a few ms past the audio received so far (the
        // edge scan, Scottie's extra sync pulse): skip that much of what
        // comes next.
        decoding = { mode, via, from, skip: Math.max(0, from - delay.pos) };
        if (from < delay.pos) codec.pushRx(delaySlice(from));
        const myGen = gen;
        loopDone = runLoop(myGen, mode, via);
    } finally {
        starting = false;
    }
}

// --------------------------------------------------------- delay buffer ---

const DELAY_S = 4;
let delay = null;         // {buf, pos}: the last DELAY_S s of what the crate eats; pos = samples since config

function delayReset() { delay = { buf: new Int16Array(rate * DELAY_S), pos: 0 }; }

function delayPush(int16) {
    const b = delay.buf, n = b.length;
    let p = delay.pos % n;
    for (let i = 0; i < int16.length; i++) { b[p] = int16[i]; if (++p === n) p = 0; }
    delay.pos += int16.length;
}

// Feed the decode in progress what the crate should see of `out`.
function feedDecode(out) {
    const d = decoding;
    if (d.skip >= out.length) { d.skip -= out.length; return; }
    if (d.skip > 0) { out = out.subarray(d.skip); d.skip = 0; }
    codec.pushRx(out);
    wakeWaiter(0);
}

// Samples [from, now).
function delaySlice(from) {
    const b = delay.buf, n = b.length;
    from = Math.max(from, delay.pos - n, 0);
    const out = new Int16Array(delay.pos - from);
    let p = from % n;
    for (let i = 0; i < out.length; i++) { out[i] = b[p]; if (++p === n) p = 0; }
    return out;
}

// ------------------------------------------------------------- spectrum ---

let specN = 2048, specBuf = null, specPos = 0, specLast = 0, specWin = null;

function spectrumReset() {
    specN = rate >= 24000 ? 2048 : 1024;
    specBuf = new Float32Array(specN);
    specPos = 0;
    specWin = new Float32Array(specN);
    for (let i = 0; i < specN; i++) specWin[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (specN - 1));
}

// In-place radix-2 FFT on (re, im).
function fft(re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) {
        let bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (let len = 2; len <= n; len <<= 1) {
        const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
        for (let i = 0; i < n; i += len) {
            let cr = 1, ci = 0;
            for (let k = 0; k < len / 2; k++) {
                const ur = re[i + k], ui = im[i + k];
                const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

function spectrumFeed(int16) {
    if (!specBuf) spectrumReset();
    for (let i = 0; i < int16.length; i++) {
        specBuf[specPos++] = int16[i] / 32768;
        if (specPos === specN) {
            specPos = 0;
            const now = Date.now();
            if (now - specLast >= SPECTRUM_INTERVAL_MS) { specLast = now; spectrumPost(); }
        }
    }
}

function spectrumPost() {
    const re = new Float32Array(specN), im = new Float32Array(specN);
    for (let i = 0; i < specN; i++) re[i] = specBuf[i] * specWin[i];
    fft(re, im);
    const df = rate / specN;
    const b0 = Math.max(1, Math.round(SPECTRUM_LO / df));
    const b1 = Math.min(specN / 2 - 1, Math.round(SPECTRUM_HI / df));
    const bins = new Float32Array(b1 - b0 + 1);
    for (let b = b0; b <= b1; b++) {
        const p = re[b] * re[b] + im[b] * im[b];
        bins[b - b0] = 10 * Math.log10(p / (specN * specN) + 1e-12);   // dBFS-ish
    }
    post({ type: 'spectrum', bins, f0: b0 * df, df }, [bins.buffer]);
}

// ------------------------------------------------------------- front end ---
// The crate measures frequency by midline crossings and splits the track into
// tones where it stays within ±50 Hz for 3 ms. Raw receiver audio does not
// (tests/sstv_codec_sim.mjs --sweep): with white noise at +24 dB SNR in
// 2.5 kHz not even the VIS header is found, and a 1000-2600 Hz band-pass
// alone changes nothing — the in-band crossing jitter is the problem. So the
// worker demodulates first: mix to 1900 Hz, 4th-order Butterworth low-pass on
// I/Q at 900 Hz (the VIS bits sit 800 Hz below the carrier), phase difference
// → instantaneous frequency, moving average over IQ_SMOOTH_MS, re-synthesized
// as a clean constant-amplitude tone. The crate then reads tones whose
// estimates sit inside its splitter window. Robot 36, white noise, SNR in
// 2.5 kHz (2026-10-31), the crate finding the picture by itself:
//     smoothing   clean err   15 dB   12 dB   9 dB
//     raw            4.2        -       -      -     (works from ~27 dB)
//     0.3 ms         3.3       18.7     -      -
//     0.6 ms         3.4       15.5   20.8     -     <- default
//     1.0 ms         4.0       11.5   15.3     -
// Smoothing blurs horizontally (0.6 ms = 2.2 px on Robot 36, 1.3 on
// Martin 1); the cliff below 12 dB was the crate's VIS / sync tone detection,
// which the worker's own image search (above) replaces. The same smoothed
// frequency feeds that search's 0.5 ms track.

const IQ_FC = 1900, IQ_LPF_HZ = 900, IQ_SMOOTH_MS = 0.6;

function biquadLp(rate, fc, q) {
    const w0 = 2 * Math.PI * fc / rate, alpha = Math.sin(w0) / (2 * q), c = Math.cos(w0);
    const a0 = 1 + alpha;
    return { b0: (1 - c) / 2 / a0, b1: (1 - c) / a0, b2: (1 - c) / 2 / a0, a1: -2 * c / a0, a2: (1 - alpha) / a0, x1: 0, x2: 0, y1: 0, y2: 0 };
}
function biquadRun(f, x) {
    const y = f.b0 * x + f.b1 * f.x1 + f.b2 * f.x2 - f.a1 * f.y1 - f.a2 * f.y2;
    f.x2 = f.x1; f.x1 = x; f.y2 = f.y1; f.y1 = y;
    return y;
}

function makeIq(rate) {
    const m = Math.max(1, Math.round((iqSmoothMs || IQ_SMOOTH_MS) / 1000 * rate));
    return {
        rate, w0: 2 * Math.PI * IQ_FC / rate, ph: 0,
        // Butterworth 4th order = two biquads with Q 0.5412 and 1.3066.
        li: [biquadLp(rate, IQ_LPF_HZ, 0.5412), biquadLp(rate, IQ_LPF_HZ, 1.3066)],
        lq: [biquadLp(rate, IQ_LPF_HZ, 0.5412), biquadLp(rate, IQ_LPF_HZ, 1.3066)],
        pi: 1, pq: 0,
        ring: new Float32Array(m), rpos: 0, rsum: 0, m,
        outPh: 0,
    };
}

// Demodulate `int16`: the smoothed instantaneous frequency of every sample
// goes to the image search's track, and the re-synthesized tone is returned.
function iqApply(f, int16) {
    const out = new Int16Array(int16.length);
    const k = f.rate / (2 * Math.PI);
    for (let n = 0; n < int16.length; n++) {
        const x = int16[n] / 32768;
        let i = x * Math.cos(f.ph), q = -x * Math.sin(f.ph);
        f.ph += f.w0; if (f.ph > 2 * Math.PI) f.ph -= 2 * Math.PI;
        i = biquadRun(f.li[1], biquadRun(f.li[0], i));
        q = biquadRun(f.lq[1], biquadRun(f.lq[0], q));
        // z[n] * conj(z[n-1]) → phase advance per sample.
        const dphi = Math.atan2(q * f.pi - i * f.pq, i * f.pi + q * f.pq);
        f.pi = i; f.pq = q;
        let hz = IQ_FC + dphi * k;
        if (hz < 500) hz = 500; else if (hz > 3300) hz = 3300;
        f.rsum += hz - f.ring[f.rpos]; f.ring[f.rpos] = hz; if (++f.rpos === f.m) f.rpos = 0;
        const sm = f.rsum / f.m;
        trackSample(sm, x);
        f.outPh += 2 * Math.PI * sm / f.rate; if (f.outPh > 2 * Math.PI) f.outPh -= 2 * Math.PI;
        out[n] = Math.round(16000 * Math.sin(f.outPh));
    }
    return out;
}

function makeFrontend(kind, rate) {
    // The IQ demodulator always runs for the image search; 'raw' only
    // changes what the crate is fed.
    return { kind: kind === 'raw' ? 'raw' : 'iq', state: makeIq(rate) };
}

// RX audio in: spectrum, demodulation, the search, the delay buffer; returns
// what the crate eats.
function ingest(int16) {
    spectrumFeed(int16);
    const demod = iqApply(frontend.state, int16);
    const out = frontend.kind === 'raw' ? int16 : demod;
    delayPush(out);
    return out;
}

// --------------------------------------------------------- image search ---

const TRACK_HZ = 2000;                 // slots per second (0.5 ms)
const TRACK_S = DELAY_S;
const SLOT = 1000 / TRACK_HZ;          // ms per slot
const ms = (x) => x * TRACK_HZ / 1000; // ms -> slots
let track = null;
let pulses = [];                       // {start, end} in slots, oldest first
let low = { in: false, start: 0, glitch: 0 };
let visHold = 0;                       // no VIS accepted before this slot (debounce)

// The VIS is read from matched tone energies, not from the frequency
// track: a bit's frequency estimate averaged over its 20 ms has ~70 Hz of
// noise at +6 dB SNR against the 100 Hz between 1200 and a data bit, so the
// header came and went with the noise draw. A coherent DFT of the raw audio
// over the same 20 ms sees the tone in a 50 Hz bandwidth instead of the
// demodulator's 1800 Hz. One complex accumulator per bin and sample: the
// three bit tones and the leader, each at five tuning offsets (a 20 ms
// window's main lobe is +/-50 Hz, so 30 Hz steps lose at most 1.4 dB) --
// per slot the sums go to complex prefix sums, so any window's energy is
// one subtraction.
const VIS_OFFS = [-60, -30, 0, 30, 60], VIS_TONES = [1100, 1200, 1300, 1900];
const NBINS = VIS_OFFS.length * VIS_TONES.length;
let dft = null;

function searchReset() {
    const N = TRACK_HZ * TRACK_S;
    track = { N, per: rate / TRACK_HZ, acc: 0, cnt: 0, fill: 0, slot: 0, hz: new Float32Array(N), P: new Float64Array(N + 1), sum: 0 };
    dft = { wr: new Float64Array(NBINS), wi: new Float64Array(NBINS), cr: new Float64Array(NBINS), ci: new Float64Array(NBINS),
            ar: new Float64Array(NBINS), ai: new Float64Array(NBINS), sr: new Float64Array(NBINS), si: new Float64Array(NBINS),
            Pr: new Float64Array(NBINS * (N + 1)), Pi: new Float64Array(NBINS * (N + 1)) };
    for (let o = 0; o < VIS_OFFS.length; o++) for (let t = 0; t < VIS_TONES.length; t++) {
        const k = o * VIS_TONES.length + t, w = -2 * Math.PI * (VIS_TONES[t] + VIS_OFFS[o]) / rate;
        dft.wr[k] = Math.cos(w); dft.wi[k] = Math.sin(w); dft.cr[k] = 1; dft.ci[k] = 0;
    }
    pulses = [];
    low = { in: false, start: 0, glitch: 0 };
    visHold = 0;
}

// One sample: the demodulated frequency for the track, the raw sample for
// the DFT bank; completes a slot every `per` samples.
function trackSample(hz, x) {
    const t = track, d = dft;
    t.acc += hz; t.cnt++; t.fill++;
    for (let k = 0; k < NBINS; k++) {
        const cr = d.cr[k], ci = d.ci[k];
        d.ar[k] += x * cr; d.ai[k] += x * ci;
        d.cr[k] = cr * d.wr[k] - ci * d.wi[k]; d.ci[k] = cr * d.wi[k] + ci * d.wr[k];
    }
    if (t.fill < t.per) return;
    t.fill -= t.per;
    const v = t.acc / t.cnt;
    t.acc = 0; t.cnt = 0;
    t.hz[t.slot % t.N] = v;
    t.sum += v;
    t.slot++;
    t.P[t.slot % (t.N + 1)] = t.sum;
    const row = (t.slot % (t.N + 1)) * NBINS, prev = ((t.slot - 1) % (t.N + 1)) * NBINS;
    for (let k = 0; k < NBINS; k++) {
        d.sr[k] += d.ar[k]; d.si[k] += d.ai[k]; d.ar[k] = 0; d.ai[k] = 0;
        d.Pr[row + k] = d.sr[k]; d.Pi[row + k] = d.si[k];
        const m = Math.hypot(d.cr[k], d.ci[k]);      // keep the phasor on the unit circle
        d.cr[k] /= m; d.ci[k] /= m;
    }
    onSlot(t.slot - 1);
}

// Energy of bin `k` over slots [a, b).
function energy(k, a, b) {
    const t = track, d = dft;
    const ia = (Math.round(a) % (t.N + 1)) * NBINS + k, ib = (Math.round(b) % (t.N + 1)) * NBINS + k;
    const re = d.Pr[ib] - d.Pr[ia], im = d.Pi[ib] - d.Pi[ia];
    return re * re + im * im;
}

// Mean of slots [a, b), integers within the last TRACK_S s.
function mean(a, b) {
    const t = track;
    a = Math.round(a); b = Math.round(b);
    if (b <= a) return NaN;
    return (t.P[b % (t.N + 1)] - t.P[a % (t.N + 1)]) / (b - a);
}
const slotHz = (s) => track.hz[((s % track.N) + track.N) % track.N];
const oldestSlot = () => Math.max(0, track.slot - track.N + 2);

// Slot `k` just completed.
function onSlot(k) {
    pulseStep(k - 1);
    visStep(k - VIS_SPAN);
}

// --- VIS ---
const BIT = ms(30), VIS_SPAN = 10 * ms(30), VIS_GUARD = ms(5), LEADER_WIN = ms(150);

// Does a start bit begin at slot `e`? Scored at every tuning offset of the
// bank; the strongest wins, then the edge is placed where the header's tones
// are strongest over the slots around `e` (the hypothesis holds a few slots
// either side) and refined on the frequency track. The picture begins at
// the stop bit's end, 300 ms on.
function visStep(e) {
    if (starting || e < LEADER_WIN + 4 || e < visHold || e - LEADER_WIN < oldestSlot()) return;
    let best = null;
    for (let o = 0; o < VIS_OFFS.length; o++) {
        const r = visScore(e, o);
        if (r && (!best || r.energy > best.energy)) best = { energy: r.energy, mode: r.mode, o };
    }
    if (!best || (pin >= 0 && best.mode !== pin)) return;
    let edge = e, top = best.energy;
    for (let d = -8; d <= 8; d++) {
        if (!d) continue;
        const r = visScore(e + d, best.o);
        if (r && r.mode === best.mode && r.energy > top) { top = r.energy; edge = e + d; }
    }
    // The edge from the leader into the start bit, to a fraction of a slot.
    const mid = 1550 + VIS_OFFS[best.o];
    for (let s = edge - 3; s <= edge + 3; s++) {
        const a = slotHz(s - 1), b = slotHz(s);
        if (a >= mid && b < mid) { edge = s + Math.max(0, Math.min(1, (a - mid) / (a - b))); break; }
    }
    visHold = e + ms(1500);
    const L = layouts[best.mode];
    // The stop bit ends 300 ms after the edge; Scottie sends one more sync
    // pulse before its first line (the crate's `after_header`).
    const start = edge + VIS_SPAN + (L.startingSync ? L.syncDurS * TRACK_HZ : 0);
    beginDecode(best.mode, start * track.per, 'vis');
}

// The header read at tuning offset `o` with its start bit at slot `e`:
// {energy, mode}, or null. The leader (150 ms before the bit, in 20 ms
// pieces) must carry four times the energy at 1900 Hz than at the two data
// tones; in each of the ten bits the expected class (1200 for start and
// stop, 1100 or 1300 for data) must beat the others by 3 dB; even parity;
// a VIS code of a known mode.
function visScore(e, o) {
    const k0 = o * VIS_TONES.length;
    let lead = 0, other = 0;
    for (let w = 0; w < 7; w++) {
        const a = e - LEADER_WIN + w * 40 + 2, b = a + 40;
        lead += energy(k0 + 3, a, b);
        other += energy(k0, a, b) + energy(k0 + 2, a, b);
    }
    if (lead < 4 * other) return null;
    let energySum = lead, code = 0, ones = 0;
    for (let i = 0; i < 10; i++) {
        const a = e + BIT * i + VIS_GUARD, b = e + BIT * (i + 1) - VIS_GUARD;
        const e1 = energy(k0, a, b), e2 = energy(k0 + 1, a, b), e3 = energy(k0 + 2, a, b);
        let win, second;
        if (i === 0 || i === 9) { win = e2; second = Math.max(e1, e3); }
        else {
            if (e2 >= Math.max(e1, e3)) return null;
            win = Math.max(e1, e3); second = Math.max(Math.min(e1, e3), e2);
            if (e1 > e3) { if (i <= 7) code |= 1 << (i - 1); ones++; }
        }
        if (win < 2 * second) return null;
        energySum += win;
    }
    if (ones & 1) return null;
    const mode = visByCode.get(code);
    return mode === undefined ? null : { energy: energySum, mode };
}

// --- sync pulses ---
const LOW_HZ = 1350, MIN_PULSE = ms(3);

// Slot `k`, with k+1 complete: a 1.5 ms mean below LOW_HZ is sync (or a VIS
// bit); one slot above does not end it.
function pulseStep(k) {
    if (k < 1) return;
    const v = mean(k - 1, k + 2);
    if (v < LOW_HZ) {
        if (!low.in) { low.in = true; low.start = k; }
        low.glitch = 0;
        return;
    }
    if (!low.in) return;
    if (++low.glitch <= 1) return;
    const end = k - low.glitch + 1;
    low.in = false;
    if (end - low.start < MIN_PULSE) return;
    pulses.push({ start: low.start, end });
    if (pulses.length > 64) pulses.shift();
    maybeSyncStart();
}

// The newest pulse completes a train of three one line apart?
function maybeSyncStart() {
    if (decoding || starting || !(armed || pin >= 0) || pulses.length < 3 || !layouts) return;
    const p3 = pulses[pulses.length - 1];
    const cands = pin >= 0 ? [pin] : layouts.map((_, id) => id);
    let best = null;
    for (const id of cands) {
        const L = layouts[id];
        const sp = L.spacingS * TRACK_HZ, tol = Math.max(ms(2.5), sp * 0.004), dur = L.syncDurS * TRACK_HZ;
        const okLen = (p) => (p.end - p.start) >= dur * 0.5 - 1 && (p.end - p.start) <= dur * 2 + ms(4);
        if (!okLen(p3)) continue;
        const p2 = pulseEndingNear(p3.end - sp, tol, okLen, pulses.length - 2);
        if (!p2) continue;
        const p1 = pulseEndingNear(p2.p.end - sp, tol, okLen, p2.i - 1);
        if (!p1) continue;
        const a = alignSequence(L, p1.p);
        if (!a) continue;
        if (!best || a.score > best.score || (a.score === best.score && sp < best.sp)) best = { id, start: a.start, score: a.score, sp };
    }
    if (best) beginDecode(best.id, best.start * track.per, 'sync');
}

function pulseEndingNear(end, tol, okLen, from) {
    for (let i = from; i >= 0; i--) {
        const p = pulses[i];
        if (p.end < end - tol) break;
        if (Math.abs(p.end - end) <= tol && okLen(p)) return { p, i };
    }
    return null;
}

// Where the sequence holding sync pulse `p1` begins: one candidate per sync
// pulse of the sequence (two for Robot 36), scored by the other control
// tones (1.5 ms or longer, middle half) the track shows at their place; a
// tone past the newest slot is checked one sequence earlier.
function alignSequence(L, p1) {
    const seq = L.sequenceS * TRACK_HZ, dur = L.syncDurS * TRACK_HZ;
    const s1 = p1.end - dur, now = track.slot, oldest = oldestSlot();
    let best = null;
    for (const c of L.controls) {
        if (c.hz !== 1200 || Math.abs(c.durS - L.syncDurS) > 1e-9) continue;
        let start = s1 - c.offsetS * TRACK_HZ;
        if (start < oldest) start += seq;
        let score = 0;
        for (const t of L.controls) {
            if (t.hz === 1200 || t.durS < 0.0015) continue;
            const len = t.durS * TRACK_HZ;
            let a = start + t.offsetS * TRACK_HZ + len / 4, b = a + len / 2;
            while (b > now) { a -= seq; b -= seq; }
            if (a < oldest) continue;
            if (Math.abs(mean(a, b) - t.hz) <= 80) score += t.durS;
        }
        if (!best || score > best.score) best = { start, score };
    }
    return best;
}

// ------------------------------------------------------------- messages ---

async function restart() {
    await abortDecode(false);
    if (!codec) return;
    codec.resetRx();
    frontend = makeFrontend(frontend ? frontend.kind : 'iq', rate);
    delayReset();
    searchReset();
    spectrumReset();
}

self.onmessage = async (ev) => {
    const msg = ev.data || {};
    try {
        switch (msg.type) {
        case 'config':
            rate = (msg.rate | 0) || 48000;
            pin = typeof msg.pinMode === 'number' ? msg.pinMode : -1;
            armed = false;
            iqSmoothMs = msg.iqSmoothMs > 0 ? msg.iqSmoothMs : 0;
            frontend = makeFrontend(msg.frontend || 'iq', rate);
            if (!codec) await loadCodec();
            await restart();
            if (msg.force) armed = true;           // old name of 'start'
            break;
        case 'start':
            if (!codec || !track) return;
            await abortDecode(false);
            pin = typeof msg.pinMode === 'number' ? msg.pinMode : -1;
            armed = true;
            maybeSyncStart();                       // a train may be in the buffer already
            break;
        case 'pcm': {
            if (!codec || !track) return;
            let s = msg.int16;
            if (!(s instanceof Int16Array)) s = new Int16Array(s.buffer || s);
            const out = ingest(s);
            if (decoding) feedDecode(out);
            break;
        }
        case 'gap': {
            if (!codec || !track) return;
            let n = msg.samples | 0;
            while (n > 0) {
                const piece = Math.min(n, rate);
                const out = ingest(new Int16Array(piece));
                if (decoding) feedDecode(out);
                n -= piece;
            }
            break;
        }
        case 'reset':
            if (!codec) return;
            await abortDecode(false);
            pulses = [];
            low = { in: false, start: 0, glitch: 0 };
            break;
        case 'encode': {
            if (!codec) await loadCodec();
            const txRate = (msg.rate | 0) || 12000;
            const r = codec.encode(msg.mode | 0, msg.rgb, txRate);
            // headerSeconds: the VOX tones, leader and VIS before the first line
            post({ type: 'encoded', int16: r.samples, seconds: r.seconds,
                   headerSeconds: codec.modeHeaderSamples(msg.mode | 0, txRate) / txRate }, [r.samples.buffer]);
            break;
        }
        default:
            break;
        }
    } catch (e) {
        post({ type: 'error', message: (e && e.message) || String(e) });
    }
};

loadCodec().catch(e => post({ type: 'error', message: 'codec load failed: ' + (e && e.message || e) }));
