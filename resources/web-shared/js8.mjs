// JS8 codec bridge — loads the wasm encoder/decoder and exposes a thin
// JS-friendly API. Loaded as a module by both the server SPA
// (resources/web/index.html) and the standalone SPA
// (resources/web-standalone/index.html); both resolve "./wasm/js8.mjs"
// the same way thanks to the qrc alias in web.qrc and tools/build-static.sh's
// copy step.
//
// API (all methods are imported individually):
//   await js8Init()             → JS8Module (wasm-backed)
//   js8Module.encode(...)       → Int32Array of 79 tones
//   js8Module.newDecoder(...)   → JS8Decoder (push samples, drain decodes)
//   synthesize(tones, opts)     → Float32Array @ 12 kHz, 8-FSK CPFSK audio
//   getSubmode(id)              → submode timing constants (slotSeconds, etc)

// ─── Submode parameter table — mirrors JS8_Include/commons.h ──────────

// Names track JS8Call 3.0.0's user-visible labels: "JS8 Normal/Fast/Slow"
// for the canonical three, and the bare-number "JS8 40" / "JS8 60" for the
// two former internal names (Turbo / Ultra) that the 3.0.0 client now
// surfaces.
const SUBMODE = {
    // id : { name,        symbolSamples, slotSeconds, prerollMs }
    0:    { name: "JS8 Normal", symbolSamples: 1920, slotSeconds: 15, prerollMs: 500 },
    1:    { name: "JS8 Fast",   symbolSamples: 1200, slotSeconds: 10, prerollMs: 200 },
    2:    { name: "JS8 40",     symbolSamples: 600,  slotSeconds: 6,  prerollMs: 100 },
    4:    { name: "JS8 Slow",   symbolSamples: 3840, slotSeconds: 30, prerollMs: 500 },
    8:    { name: "JS8 60",     symbolSamples: 384,  slotSeconds: 4,  prerollMs: 100 },
};

export function getSubmode(id) {
    return SUBMODE[id] ?? SUBMODE[0];
}

// ─── 8-FSK synthesis at 12 kHz ────────────────────────────────────────
//
// JS8 is continuous-phase 8-FSK. Tone n is at baseHz + n*baud, where
// baud = 12000/symbolSamples. Continuous-phase = phase accumulator
// stays continuous across symbol boundaries; the alternative (reset to
// 0 each symbol) splatters spectral energy and causes sync to fail.
//
// Returns a Float32Array sized to fit one full slot (slotSeconds *
// 12000 samples), with 'prerollMs' of leading silence + the 79-symbol
// payload + trailing silence to fill the slot. Amplitude is 0.5 (-6 dB
// FS) — enough headroom for noise to be added on top.

const SAMPLE_RATE = 12000;
const NUM_SYMBOLS = 79;

export function synthesize(tones, opts = {}) {
    const submode    = opts.submode ?? 0;
    const baseHz     = opts.baseHz  ?? 1500;
    const amplitude  = opts.amp     ?? 0.5;
    const params     = getSubmode(submode);
    const symSamples = params.symbolSamples;
    const slotSamples = params.slotSeconds * SAMPLE_RATE;
    const preroll    = Math.round(params.prerollMs / 1000 * SAMPLE_RATE);
    const baud       = SAMPLE_RATE / symSamples;

    if (!tones || tones.length !== NUM_SYMBOLS) {
        throw new Error(`tones must be length ${NUM_SYMBOLS}, got ${tones?.length}`);
    }

    const out = new Float32Array(slotSamples);
    let phase = 0;
    for (let s = 0; s < NUM_SYMBOLS; ++s) {
        const freq = baseHz + tones[s] * baud;
        const dphi = 2 * Math.PI * freq / SAMPLE_RATE;
        const off = preroll + s * symSamples;
        for (let i = 0; i < symSamples; ++i) {
            out[off + i] = amplitude * Math.sin(phase);
            phase += dphi;
            if (phase > 2 * Math.PI) phase -= 2 * Math.PI;
        }
    }
    return out;
}

// ─── 12 kHz → 48 kHz upsampler — the wfweb SPA's TX path is 48 kHz ────
//
// Linear interpolation between 12 kHz samples (matches the FT8 TX path
// in index.html, which also uses linear). Cheap; the spectral image
// near 6 kHz is well outside JS8's 50 Hz tone band so it doesn't matter.

export function upsampleTo48(samples12k) {
    const out = new Float32Array(samples12k.length * 4);
    for (let i = 0; i < samples12k.length - 1; ++i) {
        const a = samples12k[i], b = samples12k[i + 1];
        const o = i * 4;
        out[o]     = a;
        out[o + 1] = a + (b - a) * 0.25;
        out[o + 2] = a + (b - a) * 0.5;
        out[o + 3] = a + (b - a) * 0.75;
    }
    // Last sample: just hold (no next sample to interpolate toward)
    const last = samples12k[samples12k.length - 1];
    out[out.length - 4] = last;
    out[out.length - 3] = last;
    out[out.length - 2] = last;
    out[out.length - 1] = last;
    return out;
}

let modulePromise = null;

// Public init: resolves to a thin handle exposing encode/decoder.
// Idempotent — repeated calls return the same module.
export async function js8Init() {
    if (modulePromise) return modulePromise;
    modulePromise = (async () => {
        const createJS8 = (await import("./wasm/js8.mjs")).default;
        const Module = await createJS8();
        return new JS8Module(Module);
    })();
    return modulePromise;
}

class JS8Module {
    constructor(M) {
        this.M = M;
        this._encode = M._js8_encode;
        this._pack   = M._js8_pack;
        this._decoder_new        = M._js8_decoder_new;
        this._decoder_free       = M._js8_decoder_free;
        this._decoder_push       = M._js8_decoder_push;
        this._decoder_run        = M._js8_decoder_run;
        this._decoder_run_modes  = M._js8_decoder_run_modes;
        this._decoder_pop        = M._js8_decoder_pop;
        this._free_string  = M._js8_free_string;
        this._malloc = M._malloc;
        this._free   = M._free;
    }

    // Pack a natural-language line into one or more JS8 frames via the
    // upstream Varicode::buildMessageFrames pipeline. Each returned entry
    // is { frame: "<12-char raw>", type: <FrameType> } ready to feed
    // straight into encode().
    pack(mycall, mygrid, selectedCall, text, submode = 0) {
        const enc = (s) => {
            const bytes = new TextEncoder().encode((s || "") + "\0");
            const ptr = this._malloc(bytes.length);
            this.M.HEAPU8.set(bytes, ptr);
            return ptr;
        };
        const pMycall = enc(mycall);
        const pMygrid = enc(mygrid);
        const pSel    = enc(selectedCall);
        const pText   = enc(text);
        const outPtr = this._pack(pMycall, pMygrid, pSel, pText, submode);
        this._free(pMycall); this._free(pMygrid); this._free(pSel); this._free(pText);
        if (!outPtr) return null;
        let end = outPtr;
        while (this.M.HEAPU8[end] !== 0) ++end;
        const json = new TextDecoder().decode(this.M.HEAPU8.subarray(outPtr, end));
        this._free_string(outPtr);
        try { return JSON.parse(json); } catch { return null; }
    }

    // Encode a 12-character JS8 message of the given frame type and
    // submode. Returns Int32Array of 79 tones (each 0..7), or null on
    // bad input. submode is a Varicode::SubmodeType ID (Normal=0,
    // Fast=1, JS8 40=2, Slow=4, JS8 60=8) — selects the Costas sync
    // pattern (ORIGINAL for Normal, MODIFIED for the others).
    encode(frameType, msg, submode = 0) {
        if (typeof msg !== "string" || msg.length !== 12) return null;
        const msgPtr = this._malloc(13);
        for (let i = 0; i < 12; i++) this.M.HEAPU8[msgPtr+i] = msg.charCodeAt(i);
        this.M.HEAPU8[msgPtr+12] = 0;
        const tonesPtr = this._malloc(79 * 4);
        const rc = this._encode(submode, frameType, msgPtr, tonesPtr);
        let out = null;
        if (rc === 0) {
            out = new Int32Array(79);
            for (let i = 0; i < 79; i++) out[i] = this.M.HEAP32[tonesPtr/4 + i];
        }
        this._free(msgPtr);
        this._free(tonesPtr);
        return out;
    }

    // Create a Decoder object for the given submode (matching the
    // bits of Varicode::SubmodeType: Normal=0, Fast=1, JS8 40=2,
    // Slow=4, JS8 60=8).
    newDecoder(submode = 0) {
        const ptr = this._decoder_new(submode);
        if (!ptr) return null;
        return new JS8Decoder(this, ptr);
    }
}

class JS8Decoder {
    constructor(js8, ptr) {
        this.js8 = js8;
        this.ptr = ptr;
    }

    // Push 12 kHz mono Float32Array. Returns samples consumed.
    push(samples) {
        if (this.ptr === 0) return 0;
        const n = samples.length;
        const sPtr = this.js8._malloc(n * 4);
        new Float32Array(this.js8.M.HEAPF32.buffer, sPtr, n).set(samples);
        const consumed = this.js8._decoder_push(this.ptr, sPtr, n);
        this.js8._free(sPtr);
        return consumed;
    }

    // Run a decode pass. Returns the number of decoded messages
    // queued (drain via pop()).
    run() {
        if (this.ptr === 0) return 0;
        return this.js8._decoder_run(this.ptr);
    }

    // Run a decode pass over caller-controlled mode windows. Use this
    // when you know which modes' slots just ended and where in the
    // staged audio their slot lives. nsubmodes is a bitmask
    // (Normal=1, Fast=2, Turbo[JS8 40]=4, Slow=8, Ultra[JS8 60]=16).
    // Each pair kpos*/ksz* gives the offset and length of that mode's
    // slot inside the staged audio. For modes whose bit isn't set,
    // pass 0/0.
    runModes(nsubmodes, posA, szA, posB, szB, posC, szC, posE, szE, posI, szI) {
        if (this.ptr === 0) return 0;
        return this.js8._decoder_run_modes(
            this.ptr, nsubmodes,
            posA, szA, posB, szB, posC, szC, posE, szE, posI, szI);
    }

    // Pop one queued decoded message (object with snr/dt/freq/text/...
    // fields per the JSON format in api/js8_wasm_api.cpp), or null.
    pop() {
        if (this.ptr === 0) return null;
        const sPtr = this.js8._decoder_pop(this.ptr);
        if (!sPtr) return null;
        // UTF-8 read until NUL
        const u8 = this.js8.M.HEAPU8;
        let end = sPtr;
        while (u8[end] !== 0) ++end;
        const json = new TextDecoder().decode(u8.subarray(sPtr, end));
        this.js8._free_string(sPtr);
        try { return JSON.parse(json); } catch { return null; }
    }

    free() {
        if (this.ptr) {
            this.js8._decoder_free(this.ptr);
            this.ptr = 0;
        }
    }
}
