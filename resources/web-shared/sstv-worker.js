// SSTV codec worker (ES module worker, both builds). Owns the WebAssembly
// codec (resources/web-standalone/wasm/sstv-codec.mjs → sstv.wasm, the
// `sstv` Rust crate behind a C ABI), decodes the rig's RX audio into image
// rows, synthesizes images for transmit and posts a small spectrum of the
// input for the panel's tuning strip. Used by resources/web-shared/
// sstv-panel.js; driven under node by tests/sstv_codec_sim.mjs.
//
// Messages in:
//   {type:'config', rate, pinMode, frontend, force}
//                                            restart decoding at `rate` Hz
//                                            (pinMode: mode id, -1 = VIS;
//                                            force: the first image starts
//                                            at the next sample, no header
//                                            search -- the START button;
//                                            frontend: 'iq' (default) or
//                                            'raw'; iqSmoothMs optional)
//   {type:'pcm', int16}                       RX audio at `rate`
//   {type:'gap', samples}                     silence while we transmit
//   {type:'reset'}                            drop the image in progress
//   {type:'encode', mode, rgb, rate}          rgb = Uint8Array w*h*3
// Messages out:
//   {type:'ready', modes:[{id, key, name, w, h}]}
//   {type:'image', mode, w, h}                an image starts (mode = id)
//   {type:'row', index, rgb}                  Uint8Array w*3 (transferred)
//   {type:'done', complete}                   the image ended
//   {type:'spectrum', bins, f0, df}           Float32Array, 1000-2500 Hz
//   {type:'encoded', int16, seconds}          Int16Array at the asked rate
//   {type:'error', message}
//
// The crate's decoder pulls samples; the wasm is Asyncified so decodeNext()
// is a Promise that the `awaitSamples` callback suspends whenever the ring is
// empty. Each 'config' / 'reset' bumps a generation: the suspended decode is
// told to abort (awaitSamples resolves 1), unwinds, and a new loop starts
// once it has returned. Rows from an aborted generation are dropped.

const CODEC_URL = (typeof self !== 'undefined' && self.SSTV_CODEC_URL)
    ? self.SSTV_CODEC_URL
    : new URL('./wasm/sstv-codec.mjs', import.meta.url).href;
const WASM_URL_OVERRIDE = (typeof self !== 'undefined' && self.SSTV_WASM_BYTES) || null;

const SPECTRUM_LO = 1000, SPECTRUM_HI = 2500;
const SPECTRUM_INTERVAL_MS = 100;

let codec = null;
let rate = 48000;
let pin = -1;
let forceOnce = false;    // the next decode takes the image to start at once (config {force})
let frontend = null;      // {kind, state} or null for raw audio
let iqSmoothMs = 0;       // 0 = IQ_SMOOTH_MS; a config may override it (the sim's sweep)
let gen = 0;
let loopDone = Promise.resolve();
let waiter = null;        // {resolve, gen}

const post = (msg, transfer) => self.postMessage(msg, transfer || []);

// ---------------------------------------------------------------- codec ---

async function loadCodec() {
    const mod = await import(CODEC_URL);
    codec = await mod.loadSstvCodec(WASM_URL_OVERRIDE ? { wasmBytes: WASM_URL_OVERRIDE } : {});
    post({ type: 'ready', modes: codec.modes.map(m => ({ id: m.id, key: m.key, name: m.name, w: m.w, h: m.h })) });
}

// ------------------------------------------------------------- decoding ---

function wakeWaiter(value) {
    const w = waiter;
    waiter = null;
    if (w) w.resolve(value);
}

async function runLoop(myGen) {
    while (myGen === gen && codec) {
        let result;
        const headerless = forceOnce;
        forceOnce = false;
        try {
            result = await codec.decodeNext(rate, pin, {
                awaitSamples: () => new Promise(resolve => {
                    if (myGen !== gen) { resolve(1); return; }
                    waiter = { resolve, gen: myGen };
                }),
                onMode: (id, w, h) => { if (myGen === gen) post({ type: 'image', mode: id, w, h }); },
                onRow: (index, rgb) => { if (myGen === gen) post({ type: 'row', index, rgb }, [rgb.buffer]); },
            }, headerless);
        } catch (e) {
            // A wasm trap leaves the instance in an unknown state: reload it.
            post({ type: 'error', message: 'decoder: ' + (e && e.message || e) });
            codec = null;
            try { await loadCodec(); } catch (e2) { post({ type: 'error', message: 'codec reload failed: ' + (e2 && e2.message || e2) }); return; }
            if (myGen !== gen) return;
            continue;
        }
        if (myGen !== gen) return;        // aborted by config / reset
        if (result >= 0) post({ type: 'done', complete: result === 1 });
        // result === -1 only when the samples ended, i.e. an abort; the gen
        // check above has already returned in that case.
    }
}

async function restart() {
    gen++;
    wakeWaiter(1);
    await loopDone;
    if (!codec) return;
    codec.resetRx();
    const myGen = gen;
    loopDone = runLoop(myGen);
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
// 2.5 kHz (2026-10-31):
//     smoothing   clean err   15 dB   12 dB   9 dB
//     raw            4.2        -       -      -     (works from ~27 dB)
//     0.3 ms         3.3       18.7     -      -
//     0.6 ms         3.4       15.5   20.8     -     <- default
//     1.0 ms         4.0       11.5   15.3     -
// Smoothing blurs horizontally (0.6 ms = 2.2 px on Robot 36, 1.3 on
// Martin 1); the cliff below 12 dB is the crate's VIS / sync tone detection.

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
        f.outPh += 2 * Math.PI * sm / f.rate; if (f.outPh > 2 * Math.PI) f.outPh -= 2 * Math.PI;
        out[n] = Math.round(16000 * Math.sin(f.outPh));
    }
    return out;
}

function makeFrontend(kind, rate) {
    if (kind === 'raw') return null;
    return { kind: 'iq', apply: iqApply, state: makeIq(rate) };
}

// ------------------------------------------------------------- messages ---

self.onmessage = async (ev) => {
    const msg = ev.data || {};
    try {
        switch (msg.type) {
        case 'config':
            rate = (msg.rate | 0) || 48000;
            pin = typeof msg.pinMode === 'number' ? msg.pinMode : -1;
            forceOnce = !!msg.force;
            iqSmoothMs = msg.iqSmoothMs > 0 ? msg.iqSmoothMs : 0;
            frontend = makeFrontend(msg.frontend || 'iq', rate);
            spectrumReset();
            if (!codec) await loadCodec();
            await restart();
            break;
        case 'pcm': {
            if (!codec) return;
            let s = msg.int16;
            if (!(s instanceof Int16Array)) s = new Int16Array(s.buffer || s);
            spectrumFeed(s);
            if (frontend) s = frontend.apply(frontend.state, s);
            codec.pushRx(s);
            wakeWaiter(0);
            break;
        }
        case 'gap':
            if (!codec) return;
            codec.pushGap(msg.samples | 0);
            wakeWaiter(0);
            break;
        case 'reset':
            if (codec) await restart();
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
