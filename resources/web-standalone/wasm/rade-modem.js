// JS wrapper around the WASM RADE V1 modem.  Mirrors the surface of
// src/radeprocessor.cpp (init/processRx/processTx + EOO callsign) so the
// SPA can run RADE end-to-end without the C++ server in the loop.
//
// Usage:
//
//   import { RadeModem } from '/wasm/rade-modem.js';
//   const rade = await RadeModem.create();
//   rade.init(48000);                            // radio (audio) sample rate
//   rade.setTxCallsign('N0CALL');
//   rade.addEventListener('statsUpdate', (ev) => { ... });
//   rade.addEventListener('rxCallsign', (ev) => { ... });
//   rade.addEventListener('rxSpeech',   (ev) => { ... });   // int16 samples
//   rade.addEventListener('txReady',    (ev) => { ... });   // int16 samples
//   rade.processRx(int16Audio);                  // RX path
//   rade.processTx(int16Mic);                    // TX path
//   const eooAudio = rade.generateEooAudio();    // call once on PTT-off
//
// Hilbert transform and EOO callsign accounting live in JS to match the
// shape of the C++ orchestration; the modem/vocoder/encoder math is in
// the WASM module.

const MODULE_URL = new URL('./rade.mjs', import.meta.url);

const RADE_FS         = 8000;     // modem rate
const RADE_FS_SPEECH  = 16000;    // speech rate
const HILBERT_NTAPS   = 127;
const HILBERT_DELAY   = 63;       // (NTAPS - 1) / 2
const RADE_TX_SCALE   = 0.33;     // matches RadeProcessor — keeps ALC ~5%
const SPEEX_QUALITY   = 5;        // matches RadeProcessor

function buildHilbertCoeffs() {
    const h = new Float32Array(HILBERT_NTAPS);
    for (let i = 0; i < HILBERT_NTAPS; i++) {
        const n = i - HILBERT_DELAY;
        if (n === 0 || (n & 1) === 0) {
            h[i] = 0;
        } else {
            const w = 0.54 - 0.46 * Math.cos((2 * Math.PI * i) / (HILBERT_NTAPS - 1));
            h[i] = (2 / (Math.PI * n)) * w;
        }
    }
    return h;
}

export class RadeModem extends EventTarget {
    constructor(Module) {
        super();
        this._mod = Module;
        const m = Module;

        // C-side surface (mirrors wfweb_rade_wasm.c).
        this._cInit          = m.cwrap('wfweb_rade_init',                'number', []);
        this._cClose         = m.cwrap('wfweb_rade_close',               'null',   []);
        this._cNFeat         = m.cwrap('wfweb_rade_n_features_in_out',   'number', []);
        this._cNTx           = m.cwrap('wfweb_rade_n_tx_out',            'number', []);
        this._cNTxEoo        = m.cwrap('wfweb_rade_n_tx_eoo_out',        'number', []);
        this._cNEoo          = m.cwrap('wfweb_rade_n_eoo_bits',          'number', []);
        this._cNinMax        = m.cwrap('wfweb_rade_nin_max',             'number', []);
        this._cNin           = m.cwrap('wfweb_rade_nin',                 'number', []);
        this._cFrameSz       = m.cwrap('wfweb_rade_lpcnet_frame_size',   'number', []);
        this._cNbTotal       = m.cwrap('wfweb_rade_nb_total_features',   'number', []);
        this._cNbFeat        = m.cwrap('wfweb_rade_nb_features',         'number', []);
        this._cExtractFeat   = m.cwrap('wfweb_rade_extract_features',    'null',   ['number', 'number']);
        this._cTx            = m.cwrap('wfweb_rade_tx',                  'number', ['number', 'number']);
        this._cTxEoo         = m.cwrap('wfweb_rade_tx_eoo',              'number', ['number']);
        this._cRx            = m.cwrap('wfweb_rade_rx',                  'number', ['number', 'number', 'number', 'number']);
        this._cSync          = m.cwrap('wfweb_rade_sync',                'number', []);
        this._cFreqOff       = m.cwrap('wfweb_rade_freq_offset',         'number', []);
        this._cSnr           = m.cwrap('wfweb_rade_snr',                 'number', []);
        this._cSetEooBits    = m.cwrap('wfweb_rade_set_eoo_bits',        'null',   ['number']);
        this._cFarganReset   = m.cwrap('wfweb_rade_fargan_reset',        'null',   []);
        this._cFarganWarmup  = m.cwrap('wfweb_rade_fargan_warmup',       'number', ['number']);
        this._cFarganSynth   = m.cwrap('wfweb_rade_fargan_synth',        'null',   ['number', 'number']);
        this._cTextEncode    = m.cwrap('wfweb_rade_text_encode',         'null',   ['string', 'number', 'number', 'number']);
        this._cResInit       = m.cwrap('wfweb_rade_resampler_init',      'number', ['number', 'number', 'number']);
        this._cResDestroy    = m.cwrap('wfweb_rade_resampler_destroy',   'null',   ['number']);
        this._cResProcess    = m.cwrap('wfweb_rade_resampler_process',   'number', ['number', 'number', 'number', 'number', 'number']);

        // C-side EM_JS trampoline calls Module.onRadeText(callsign).
        Module.onRadeText = (callsign) => {
            const cs = (callsign || '').trim();
            console.debug('[rade] onRadeText:', JSON.stringify(callsign));
            if (cs) this.dispatchEvent(new CustomEvent('rxCallsign', { detail: { callsign: cs } }));
        };

        // Cached text decode entry point (used in the RX loop).
        this._cTextDecode = m.cwrap('wfweb_rade_text_decode', 'null', ['number', 'number']);

        this._radioRate = 0;
        this._initialized = false;

        // Sizes (filled in init()).
        this._nFeatInOut = 0;
        this._nTxOut     = 0;
        this._nTxEooOut  = 0;
        this._nEooBits   = 0;
        this._ninMax     = 0;
        this._frameSz    = 0;
        this._nbTotal    = 0;
        this._nbFeat     = 0;
        this._framesPerMf = 0;

        // Hilbert (JS-side, RX only).
        this._hilbertCoeffs = buildHilbertCoeffs();
        this._hilbertHist   = new Float32Array(HILBERT_NTAPS);
        this._hilbertIdx    = 0;

        // Speex resamplers (created lazily based on radio rate).
        this._rxDown = 0;   // radio -> 8k
        this._rxUp   = 0;   // 16k -> radio
        this._txDown = 0;   // radio -> 16k
        this._txUp   = 0;   // 8k -> radio

        // RX: complex-IQ accumulator (Float32Array, interleaved real,imag).
        this._rxIq = [];
        this._rxIqLen = 0;

        // TX: int16 speech accumulator at 16 kHz.
        this._txSpeech = [];
        this._txSpeechLen = 0;

        // TX feature buffer.
        this._txFeatBuf = null;     // Float32Array, sized at init
        this._txFeatIdx = 0;
        this._txEooPrepared = false;

        this._txCallsign = '';

        // Heap scratch buffers (allocated at init, freed on close).
        this._scratch = {};
    }

    static async create() {
        if (!RadeModem._modulePromise) {
            const factory = (await import(MODULE_URL.href)).default;
            RadeModem._modulePromise = factory();
        }
        const Module = await RadeModem._modulePromise;
        return new RadeModem(Module);
    }

    /** radioRate is the AudioContext sample rate (typically 48000). */
    init(radioRate) {
        if (this._initialized) this.close();
        const rc = this._cInit();
        if (rc !== 0) throw new Error('wfweb_rade_init failed: ' + rc);

        this._radioRate    = radioRate;
        this._nFeatInOut   = this._cNFeat();
        this._nTxOut       = this._cNTx();
        this._nTxEooOut    = this._cNTxEoo();
        this._nEooBits     = this._cNEoo();
        this._ninMax       = this._cNinMax();
        this._frameSz      = this._cFrameSz();
        this._nbTotal      = this._cNbTotal();
        this._nbFeat       = this._cNbFeat();
        this._framesPerMf  = this._nFeatInOut / this._nbTotal;

        if (radioRate !== RADE_FS) {
            this._rxDown = this._cResInit(radioRate, RADE_FS, SPEEX_QUALITY);
            this._txUp   = this._cResInit(RADE_FS, radioRate, SPEEX_QUALITY);
            if (!this._rxDown || !this._txUp) throw new Error('rade resampler (modem rate) init failed');
        }
        if (radioRate !== RADE_FS_SPEECH) {
            this._rxUp   = this._cResInit(RADE_FS_SPEECH, radioRate, SPEEX_QUALITY);
            this._txDown = this._cResInit(radioRate, RADE_FS_SPEECH, SPEEX_QUALITY);
            if (!this._rxUp || !this._txDown) throw new Error('rade resampler (speech rate) init failed');
        }

        this._txFeatBuf = new Float32Array(this._nFeatInOut);
        this._txFeatIdx = 0;
        this._txEooPrepared = false;

        this._hilbertHist.fill(0);
        this._hilbertIdx = 0;
        this._rxIq = [];
        this._rxIqLen = 0;
        this._txSpeech = [];
        this._txSpeechLen = 0;

        this._allocScratch();
        this._cFarganReset();
        this._initialized = true;
    }

    close() {
        if (!this._initialized) return;
        if (this._rxDown) { this._cResDestroy(this._rxDown); this._rxDown = 0; }
        if (this._rxUp)   { this._cResDestroy(this._rxUp);   this._rxUp   = 0; }
        if (this._txDown) { this._cResDestroy(this._txDown); this._txDown = 0; }
        if (this._txUp)   { this._cResDestroy(this._txUp);   this._txUp   = 0; }
        this._freeScratch();
        this._cClose();
        this._initialized = false;
    }

    setTxCallsign(callsign) {
        this._txCallsign = String(callsign || '').toUpperCase().trim();
        this._txEooPrepared = false;   // re-encode on next TX
    }

    /* --------------------------------------------------------- RX path */

    /** Push int16 PCM samples (mono, at radioRate) through the RADE RX. */
    processRx(int16In) {
        if (!this._initialized) return;
        if (!(int16In instanceof Int16Array)) int16In = new Int16Array(int16In);
        if (int16In.length === 0) return;

        // Step 1: int16 -> float, resample to 8 kHz modem rate
        const modemFloat = this._resampleInt16ToFloat(int16In, this._rxDown, RADE_FS);

        // Step 2: Hilbert transform (real -> IQ)
        const N = modemFloat.length;
        const iqInterleaved = new Float32Array(N * 2);
        const coeffs = this._hilbertCoeffs;
        const hist = this._hilbertHist;
        let histIdx = this._hilbertIdx;
        for (let i = 0; i < N; i++) {
            const sample = modemFloat[i];
            hist[histIdx] = sample;
            let imag = 0;
            let idx = histIdx;
            for (let k = 0; k < HILBERT_NTAPS; k++) {
                imag += coeffs[k] * hist[idx];
                idx = idx === 0 ? HILBERT_NTAPS - 1 : idx - 1;
            }
            let delayIdx = histIdx - HILBERT_DELAY;
            if (delayIdx < 0) delayIdx += HILBERT_NTAPS;
            iqInterleaved[i * 2]     = hist[delayIdx];
            iqInterleaved[i * 2 + 1] = imag;
            histIdx = (histIdx + 1) % HILBERT_NTAPS;
        }
        this._hilbertIdx = histIdx;

        // Step 3: accumulate IQ
        this._rxIq.push(iqInterleaved);
        this._rxIqLen += N;

        // Step 4: process complete frames
        let nin = this._cNin();
        while (this._rxIqLen >= nin) {
            const iqFrame = this._takeIq(nin);

            // Push iqFrame to scratch heap, call rade_rx
            this._mod.HEAPF32.set(iqFrame, this._scratch.iq >> 2);
            const nFeat = this._cRx(this._scratch.iq, this._scratch.feat,
                                    this._scratch.hasEoo, this._scratch.eoo);

            const hasEoo = this._mod.HEAP32[this._scratch.hasEoo >> 2];
            if (hasEoo) {
                console.debug('[rade] EOO detected, decoding', this._nEooBits, 'bits');
                this._cTextDecode(this._scratch.eoo, this._nEooBits);
            }

            // Speech synthesis frame-by-frame
            const sync = this._cSync() !== 0;
            if (nFeat > 0) {
                const nFrames = Math.floor(nFeat / this._nbTotal);
                for (let f = 0; f < nFrames; f++) {
                    const featOffset = this._scratch.feat + f * this._nbTotal * 4;
                    if (this._cFarganWarmup(featOffset) !== 1) continue;
                    this._cFarganSynth(featOffset, this._scratch.pcm160);
                    const speechI16 = new Int16Array(this._frameSz);
                    speechI16.set(
                        this._mod.HEAP16.subarray(this._scratch.pcm160 >> 1,
                                                  (this._scratch.pcm160 >> 1) + this._frameSz));
                    const out = this._resampleInt16ToInt16(speechI16, this._rxUp, this._radioRate);
                    if (out.length > 0) {
                        this.dispatchEvent(new CustomEvent('rxSpeech', { detail: { samples: out } }));
                    }
                }
            }

            const snr = sync ? this._cSnr() : -5;
            const foff = this._cFreqOff();
            this.dispatchEvent(new CustomEvent('statsUpdate', {
                detail: { snr, sync, freqOffset: foff },
            }));
            if (!sync) {
                // FARGAN history goes stale on sync loss — wipe it so the
                // next over starts from warmup again, like radeprocessor.cpp.
                this._cFarganReset();
            }

            nin = this._cNin();
        }
    }

    /* --------------------------------------------------------- TX path */

    /** Push int16 mic PCM (mono, at radioRate) through the RADE TX. */
    processTx(int16In) {
        if (!this._initialized) return;
        if (!(int16In instanceof Int16Array)) int16In = new Int16Array(int16In);
        if (int16In.length === 0) return;

        // Step 1: resample mic -> 16 kHz speech
        const speech16 = this._resampleInt16ToInt16(int16In, this._txDown, RADE_FS_SPEECH);
        this._txSpeech.push(speech16);
        this._txSpeechLen += speech16.length;

        // Step 2: pull 160-sample (10ms @ 16k) frames; one feature per frame
        while (this._txSpeechLen >= this._frameSz) {
            const pcm = this._takeSpeech(this._frameSz);

            this._mod.HEAP16.set(pcm, this._scratch.pcm160 >> 1);
            this._cExtractFeat(this._scratch.pcm160,
                this._scratch.feat + this._txFeatIdx * this._nbTotal * 4);
            this._txFeatIdx++;

            if (this._txFeatIdx >= this._framesPerMf) {
                // Encode a modem frame.
                const nIQ = this._cTx(this._scratch.feat, this._scratch.iq);
                this._txFeatIdx = 0;
                if (nIQ > 0) {
                    const modemI16 = this._iqRealToInt16(this._scratch.iq, nIQ);
                    const out = this._resampleInt16ToInt16(modemI16, this._txUp, this._radioRate);
                    if (out.length > 0) {
                        this.dispatchEvent(new CustomEvent('txReady', { detail: { samples: out } }));
                    }
                }
                if (!this._txEooPrepared) {
                    this._encodeEooBits();
                    this._txEooPrepared = true;
                }
            }
        }
    }

    /** Generate the EOO frame audio.  Call once on PTT-off, append to TX buffer. */
    generateEooAudio() {
        if (!this._initialized) return new Int16Array(0);
        if (!this._txEooPrepared) this._encodeEooBits();
        const nIQ = this._cTxEoo(this._scratch.iqEoo);
        if (nIQ <= 0) return new Int16Array(0);
        this._txEooPrepared = false;   // next over re-encodes
        const modemI16 = this._iqRealToInt16(this._scratch.iqEoo, nIQ);
        return this._resampleInt16ToInt16(modemI16, this._txUp, this._radioRate);
    }

    /* --------------------------------------------------------- internals */

    _encodeEooBits() {
        if (!this._txCallsign) return;
        // Encode callsign into EOO bits (writes nEooBits floats into scratch.eoo)
        const cs = this._txCallsign;
        this._cTextEncode(cs, cs.length, this._scratch.eoo, this._nEooBits);
        this._cSetEooBits(this._scratch.eoo);
    }

    _allocScratch() {
        const m = this._mod;
        const f32 = (n) => m._malloc(n * 4);
        const i16 = (n) => m._malloc(n * 2);
        this._scratch.iq        = f32(this._ninMax * 2);                 // RX IQ pairs (also TX IQ output)
        this._scratch.iqEoo     = f32(this._nTxEooOut * 2);
        this._scratch.feat      = f32(Math.max(this._nFeatInOut, this._nbTotal * this._framesPerMf));
        this._scratch.eoo       = f32(this._nEooBits);
        this._scratch.hasEoo    = m._malloc(4);
        this._scratch.pcm160    = i16(this._frameSz);
        // Resampler I/O scratch is allocated on demand inside the helpers.
    }

    _freeScratch() {
        const m = this._mod;
        for (const k of Object.keys(this._scratch)) {
            if (this._scratch[k]) m._free(this._scratch[k]);
        }
        this._scratch = {};
    }

    _takeIq(nPairs) {
        // Concat the head of this._rxIq queue until we have nPairs interleaved.
        const out = new Float32Array(nPairs * 2);
        let written = 0;
        while (written < out.length && this._rxIq.length > 0) {
            const head = this._rxIq[0];
            const room = out.length - written;
            if (head.length <= room) {
                out.set(head, written);
                written += head.length;
                this._rxIq.shift();
            } else {
                out.set(head.subarray(0, room), written);
                this._rxIq[0] = head.subarray(room);
                written += room;
            }
        }
        this._rxIqLen -= nPairs;
        return out;
    }

    _takeSpeech(nSamples) {
        const out = new Int16Array(nSamples);
        let written = 0;
        while (written < out.length && this._txSpeech.length > 0) {
            const head = this._txSpeech[0];
            const room = out.length - written;
            if (head.length <= room) {
                out.set(head, written);
                written += head.length;
                this._txSpeech.shift();
            } else {
                out.set(head.subarray(0, room), written);
                this._txSpeech[0] = head.subarray(room);
                written += room;
            }
        }
        this._txSpeechLen -= nSamples;
        return out;
    }

    /* IQ real-extract + scale + clip -> int16. Mirrors RadeProcessor. */
    _iqRealToInt16(heapPtr, nPairs) {
        const f32 = this._mod.HEAPF32;
        const base = heapPtr >> 2;
        const out = new Int16Array(nPairs);
        for (let i = 0; i < nPairs; i++) {
            let v = f32[base + i * 2] * 32768 * RADE_TX_SCALE;
            if (v > 32767) v = 32767;
            else if (v < -32767) v = -32767;
            out[i] = v;
        }
        return out;
    }

    /** Resample int16 -> float at outRate.  When resampler==0 (matching rates),
     *  just int16 -> float in place. */
    _resampleInt16ToFloat(int16, resampler, outRate) {
        if (!resampler) {
            const out = new Float32Array(int16.length);
            for (let i = 0; i < int16.length; i++) out[i] = int16[i] / 32768;
            return out;
        }
        const inFloat = new Float32Array(int16.length);
        for (let i = 0; i < int16.length; i++) inFloat[i] = int16[i] / 32768;
        const ratio = outRate / this._radioRate;
        const outLen = Math.ceil(int16.length * ratio) + 64;
        const m = this._mod;
        const inPtr  = m._malloc(inFloat.length * 4);
        const outPtr = m._malloc(outLen * 4);
        const inLenPtr  = m._malloc(4);
        const outLenPtr = m._malloc(4);
        try {
            m.HEAPF32.set(inFloat, inPtr >> 2);
            m.HEAP32[inLenPtr  >> 2] = inFloat.length;
            m.HEAP32[outLenPtr >> 2] = outLen;
            this._cResProcess(resampler, inPtr, inLenPtr, outPtr, outLenPtr);
            const outProduced = m.HEAP32[outLenPtr >> 2];
            return new Float32Array(m.HEAPF32.buffer,
                m.HEAPF32.byteOffset + outPtr, outProduced).slice();
        } finally {
            m._free(inPtr); m._free(outPtr); m._free(inLenPtr); m._free(outLenPtr);
        }
    }

    /** Resample int16 -> int16 (clamped) at outRate.  When resampler==0 (matching
     *  rates), pass-through. */
    _resampleInt16ToInt16(int16, resampler, outRate) {
        if (!resampler) return new Int16Array(int16);   // copy so caller can hold it
        // route int16 -> float -> resample -> int16
        const inFloat = new Float32Array(int16.length);
        for (let i = 0; i < int16.length; i++) inFloat[i] = int16[i] / 32768;
        const inRate = (outRate === this._radioRate) ?
            (resampler === this._rxUp ? RADE_FS_SPEECH : RADE_FS) :
            this._radioRate;
        const ratio = outRate / inRate;
        const outLen = Math.ceil(int16.length * ratio) + 64;
        const m = this._mod;
        const inPtr  = m._malloc(inFloat.length * 4);
        const outPtr = m._malloc(outLen * 4);
        const inLenPtr  = m._malloc(4);
        const outLenPtr = m._malloc(4);
        try {
            m.HEAPF32.set(inFloat, inPtr >> 2);
            m.HEAP32[inLenPtr  >> 2] = inFloat.length;
            m.HEAP32[outLenPtr >> 2] = outLen;
            this._cResProcess(resampler, inPtr, inLenPtr, outPtr, outLenPtr);
            const outProduced = m.HEAP32[outLenPtr >> 2];
            const out = new Int16Array(outProduced);
            const f32base = outPtr >> 2;
            for (let i = 0; i < outProduced; i++) {
                let v = m.HEAPF32[f32base + i] * 32768;
                if (v > 32767) v = 32767;
                else if (v < -32768) v = -32768;
                out[i] = v;
            }
            return out;
        } finally {
            m._free(inPtr); m._free(outPtr); m._free(inLenPtr); m._free(outLenPtr);
        }
    }
}
