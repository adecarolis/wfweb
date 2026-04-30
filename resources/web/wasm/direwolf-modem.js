// JS wrapper around the WASM Direwolf modem subset.  Mirrors the surface
// of src/direwolfprocessor.cpp (init/processRx/transmitFrame) so the SPA's
// existing packet UI can drive it without a server in the loop.
//
// Usage:
//
//   import { DirewolfModem } from '/wasm/direwolf-modem.js';
//   const modem = await DirewolfModem.create();
//   modem.onFrame = (chan, alevel, ax25Bytes) => { ... };
//   modem.init(1200, 48000);     // baud, sample rate (Hz)
//   modem.processSamples(int16);  // mono int16 LE PCM at sample rate
//   const audio = modem.txFrame('N0CALL>APRS:hello');  // returns Int16Array

const MODULE_URL = new URL('./direwolf.mjs', import.meta.url);

export class DirewolfModem {
    constructor(Module) {
        this._mod = Module;
        this._init   = Module.cwrap('wfweb_dw_init',           'number', ['number', 'number']);
        this._proc   = Module.cwrap('wfweb_dw_process_samples','null',   ['number', 'number']);
        this._tx     = Module.cwrap('wfweb_dw_tx_frame',       'number', ['string']);
        this._txPtr  = Module.cwrap('wfweb_dw_tx_buffer_ptr',  'number', []);
        this._txLen  = Module.cwrap('wfweb_dw_tx_buffer_len',  'number', []);
        this._txRst  = Module.cwrap('wfweb_dw_tx_buffer_reset','null',   []);
        this._baud = 0;
        this._sampleRate = 0;
        this.onFrame = null;

        // Module.onRxFrame is invoked from the C dlq_rec_frame trampoline.
        // We unpack the AX.25 bytes (already a Uint8Array slice) and emit
        // them to the JS-side handler.
        Module.onRxFrame = (chan, alevel, bytes) => {
            if (typeof this.onFrame === 'function') {
                this.onFrame(chan, alevel, bytes);
            }
        };
    }

    /**
     * Build a modem.  Loads the WASM module once; cached for subsequent calls.
     */
    static async create() {
        if (!DirewolfModem._modulePromise) {
            const factory = (await import(MODULE_URL.href)).default;
            DirewolfModem._modulePromise = factory();
        }
        const Module = await DirewolfModem._modulePromise;
        return new DirewolfModem(Module);
    }

    /**
     * Configure (or reconfigure) the modem.  baud must be 300, 1200, or 9600.
     * sampleRate is the rate of samples we'll feed to processSamples().
     * 48 kHz is what the C++ wfweb uses; pass whatever your AudioContext is at.
     */
    init(baud, sampleRate) {
        if (baud !== 300 && baud !== 1200 && baud !== 9600) {
            throw new Error('baud must be 300, 1200, or 9600 — got ' + baud);
        }
        const rc = this._init(baud, sampleRate);
        if (rc !== 0) throw new Error('wfweb_dw_init failed: ' + rc);
        this._baud = baud;
        this._sampleRate = sampleRate;
    }

    get baud() { return this._baud; }
    get sampleRate() { return this._sampleRate; }

    /**
     * Push int16 LE PCM samples through the demodulator.  Decoded frames
     * arrive on this.onFrame(chan, alevel, Uint8Array).
     */
    processSamples(int16Array) {
        if (!(int16Array instanceof Int16Array)) {
            int16Array = new Int16Array(int16Array);
        }
        if (int16Array.length === 0) return;
        const ptr = this._mod._malloc(int16Array.byteLength);
        try {
            this._mod.HEAP16.set(int16Array, ptr >> 1);
            this._proc(ptr, int16Array.length);
        } finally {
            this._mod._free(ptr);
        }
    }

    /**
     * Encode a single AX.25 UI frame (TNC monitor format,
     * "SRC>DST[,VIA,...]:info") and return the resulting int16 LE PCM as
     * an Int16Array at the configured sample rate.  Returns null if
     * ax25_from_text rejected the input.
     */
    txFrame(monitor) {
        this._txRst();
        const rc = this._tx(monitor);
        if (rc !== 0) return null;
        const ptr = this._txPtr();
        const len = this._txLen();
        // Buffer holds raw little-endian int16 bytes; copy out before we
        // reset (next call will free the memory).
        const bytes = this._mod.HEAPU8.slice(ptr, ptr + len);
        this._txRst();
        // Bytes -> Int16Array.  ALLOW_MEMORY_GROWTH=1 means HEAPU8 may
        // detach across calls; slicing first keeps us decoupled from that.
        return new Int16Array(bytes.buffer, bytes.byteOffset, bytes.byteLength >> 1);
    }
}
