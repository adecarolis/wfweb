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

// --- AX.25 frame parser --------------------------------------------------
//
// Produces the same shape src/direwolfprocessor.cpp::buildFrameJson emits
// over the WebSocket so packet.js renders identically. We don't bother
// with the more obscure frame types (XID/TEST/SABME) since the Direwolf
// SANITY_APRS gate filters most of them out before they reach us anyway.

/**
 * Parse a raw AX.25 frame (as bytes off the wire) into the JSON shape
 * the SPA's packet UI consumes. Returns null on malformed input.
 *
 * @param {Uint8Array} bytes  AX.25 frame: addresses, control, [PID, info], FCS
 * @param {number} chan       channel number (we always pass 0)
 * @param {number} alevel     audio level reported by demod (0..100)
 */
export function parseAx25Frame(bytes, chan, alevel) {
    if (!bytes || bytes.length < 14) return null;

    const addrs = [];
    let off = 0;
    // Each address is 7 bytes: 6 ASCII shifted left 1 + 1 SSID byte. Bit 0
    // of the SSID byte set marks the last address in the chain.
    while (off + 7 <= bytes.length) {
        let cs = '';
        for (let i = 0; i < 6; i++) {
            const c = bytes[off + i] >> 1;
            if (c >= 0x20 && c < 0x7f) cs += String.fromCharCode(c);
        }
        cs = cs.replace(/\s+$/, '');
        const ssid = (bytes[off + 6] >> 1) & 0x0f;
        addrs.push(ssid ? `${cs}-${ssid}` : cs);
        const last = bytes[off + 6] & 0x01;
        off += 7;
        if (last) break;
    }
    if (addrs.length < 2 || off >= bytes.length) return null;

    // Address ordering: AX.25 puts destination first, source second, then
    // any digipeater path. (Yes, it's the opposite of "callee.callsign".)
    const dst = addrs[0];
    const src = addrs[1];
    const path = addrs.slice(2);

    const ctrl = bytes[off++];
    let info = '';
    let ftype = '?';

    // Frame-type discrimination per AX.25 v2.2. Modulo-128 frames have a
    // 2-byte control field; we don't see them in normal APRS traffic so
    // detect by the U/S/I pattern of the low bits.
    if ((ctrl & 0x01) === 0) {
        // I-frame: NS in bits 1..3, NR in 5..7, P/F in bit 4.
        const ns = (ctrl >> 1) & 0x07;
        const nr = (ctrl >> 5) & 0x07;
        ftype = `I N(S)=${ns} N(R)=${nr}`;
        if (off < bytes.length) off++;  // skip PID byte
    } else if ((ctrl & 0x03) === 0x01) {
        // S-frame: type in bits 2..3, NR in 5..7.
        const nr = (ctrl >> 5) & 0x07;
        const sub = (ctrl >> 2) & 0x03;
        ftype = ['RR', 'RNR', 'REJ', 'SREJ'][sub] + ` N(R)=${nr}`;
    } else {
        // U-frame: type in bits 2..7 (5 of 6 modifier bits + P/F).
        const utype = ctrl & 0xef;
        if (utype === 0x03) {
            ftype = 'UI';
            if (off < bytes.length) off++;  // skip PID
        } else if (utype === 0x2f) ftype = 'SABM';
        else if (utype === 0x6f) ftype = 'SABME';
        else if (utype === 0x43) ftype = 'DISC';
        else if (utype === 0x0f) ftype = 'DM';
        else if (utype === 0x63) ftype = 'UA';
        else if (utype === 0x87) ftype = 'FRMR';
        else                     ftype = 'U?';
    }

    // Strip trailing 2-byte FCS (Direwolf hands us frames with FCS still
    // attached; the demod path that calls dlq_rec_frame checks it before
    // emitting, so we can drop it without re-validating).
    let infoEnd = bytes.length;
    if (infoEnd >= 2) infoEnd -= 2;
    if (off < infoEnd) {
        let s = '';
        for (let i = off; i < infoEnd; i++) s += String.fromCharCode(bytes[i]);
        info = s;
    }

    let rawHex = '';
    for (let i = 0; i < bytes.length; i++) {
        rawHex += bytes[i].toString(16).padStart(2, '0');
    }

    return {
        chan: chan | 0,
        level: alevel | 0,
        ts: Date.now(),
        src, dst, path, info, ftype, rawHex,
    };
}

// --- APRS uncompressed-position formatter --------------------------------
//
// Mirrors aprsProcessor::buildPosition() — emits the standard uncompressed
// APRS data extension: "!ddmm.mmN/dddmm.mmW>comment". Lat/lon come from
// the SPA as decimal degrees; APRS expects degrees-minutes with hundredths.

/**
 * Build a "!lat/lonsymcomment" uncompressed APRS info string.
 * @param {number} lat   decimal degrees, +north
 * @param {number} lon   decimal degrees, +east
 * @param {string} symTable  '/' (primary) or '\\' (alternate)
 * @param {string} symCode    one ASCII char
 * @param {string} comment    free text
 */
export function aprsBuildUncompressedPosition(lat, lon, symTable, symCode, comment) {
    const fmt = (deg, len, posCh, negCh) => {
        const ns = deg < 0 ? negCh : posCh;
        const a = Math.abs(deg);
        const d = Math.floor(a);
        const m = (a - d) * 60.0;
        return String(d).padStart(len, '0')
             + m.toFixed(2).padStart(5, '0')
             + ns;
    };
    const latStr = fmt(lat, 2, 'N', 'S');
    const lonStr = fmt(lon, 3, 'E', 'W');
    const t = symTable || '/';
    const c = symCode || '>';
    const cmt = comment ? String(comment) : '';
    return `!${latStr}${t}${lonStr}${c}${cmt}`;
}

/**
 * Build a TNC monitor string ("SRC>DST[,VIA,...]:info") suitable for
 * passing to DirewolfModem.txFrame(). dst defaults to "APRS" — the
 * generic destination call APRS UI frames use.
 */
export function aprsBuildMonitorLine(src, info, path, dst) {
    const d = dst || 'APRS';
    const pathStr = (Array.isArray(path) && path.length)
        ? ',' + path.join(',') : '';
    return `${src}>${d}${pathStr}:${info}`;
}
