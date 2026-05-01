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


// --- APRS position parsers -----------------------------------------------
//
// Hand-port of src/aprsprocessor.cpp's three parsers (uncompressed,
// compressed, MIC-E). The browser-only build runs them on every UI
// frame coming off the modem and keeps an in-memory station database
// (see AprsProcessor below).

function _digOrZero(c) {
    if (c === ' ') return 0;
    var n = c.charCodeAt(0) - 48;  // '0'
    if (n >= 0 && n <= 9) return n;
    return -1;
}

function _stripTimestamp(s) {
    // "HHMMSSh" / "DDHHMMz" / "DDHHMM/" — 7-char prefix optional on `/` `@`.
    if (s.length < 7) return s;
    var suffix = s.charAt(6);
    if (suffix !== 'z' && suffix !== '/' && suffix !== 'h') return s;
    for (var i = 0; i < 6; i++) {
        var c = s.charCodeAt(i);
        if (c < 48 || c > 57) return s;
    }
    return s.substring(7);
}

function _parseUncompressed(body) {
    // "DDMM.mmN/DDDMM.mmW<sym>comment"
    if (body.length < 19) return null;
    var latStr = body.substring(0, 8);
    var latHem = latStr.charAt(7);
    var lonStr = body.substring(9, 18);
    var lonHem = lonStr.charAt(8);
    if (latHem !== 'N' && latHem !== 'S') return null;
    if (lonHem !== 'E' && lonHem !== 'W') return null;
    if (latStr.charAt(4) !== '.' || lonStr.charAt(5) !== '.') return null;

    var latDeg = _digOrZero(latStr.charAt(0)) * 10 + _digOrZero(latStr.charAt(1));
    var latMin = _digOrZero(latStr.charAt(2)) * 10 + _digOrZero(latStr.charAt(3));
    var latMinFrac = _digOrZero(latStr.charAt(5)) * 10 + _digOrZero(latStr.charAt(6));
    if (latDeg < 0 || latMin < 0 || latMinFrac < 0) return null;

    var lonDeg = _digOrZero(lonStr.charAt(0)) * 100
               + _digOrZero(lonStr.charAt(1)) * 10
               + _digOrZero(lonStr.charAt(2));
    var lonMin = _digOrZero(lonStr.charAt(3)) * 10 + _digOrZero(lonStr.charAt(4));
    var lonMinFrac = _digOrZero(lonStr.charAt(6)) * 10 + _digOrZero(lonStr.charAt(7));
    if (lonDeg < 0 || lonMin < 0 || lonMinFrac < 0) return null;

    var lat = latDeg + (latMin + latMinFrac / 100.0) / 60.0;
    if (latHem === 'S') lat = -lat;
    var lon = lonDeg + (lonMin + lonMinFrac / 100.0) / 60.0;
    if (lonHem === 'W') lon = -lon;

    return {
        lat: lat, lon: lon,
        symTable: body.charAt(8),
        symCode:  body.charAt(18),
        comment:  body.substring(19).trim(),
    };
}

function _parseCompressed(body) {
    // 13-byte fixed payload: sym + 4 lat + 4 lon + sym + 2 cs + 1 type
    if (body.length < 13) return null;
    var sym1 = body.charCodeAt(0);
    var isAlnum = (sym1 >= 48 && sym1 <= 57) || (sym1 >= 65 && sym1 <= 90) || (sym1 >= 97 && sym1 <= 122);
    if (!(sym1 === 47 || sym1 === 92 || isAlnum)) return null;

    for (var i = 1; i <= 9; i++) {
        var u = body.charCodeAt(i);
        if (u < 0x21 || u > 0x7b) return null;
    }
    var b91 = function (cc) { return body.charCodeAt(cc) - 33; };
    var y = b91(1) * 91 * 91 * 91 + b91(2) * 91 * 91 + b91(3) * 91 + b91(4);
    var x = b91(5) * 91 * 91 * 91 + b91(6) * 91 * 91 + b91(7) * 91 + b91(8);

    return {
        lat:  90.0 - y / 380926.0,
        lon: -180.0 + x / 190463.0,
        symTable: body.charAt(0),
        symCode:  body.charAt(9),
        comment:  body.substring(13).trim(),
    };
}

function _parseMicE(dst, payload) {
    // Latitude lives in the 6-char destination callsign; longitude in the
    // first 3 bytes of `info` after the DTI. APRS spec 1.0 §10.
    if (dst.length < 6 || payload.length < 8) return null;

    var latDig = new Array(6);
    var nFlag = false, wFlag = false, lonOffset100 = false;
    for (var i = 0; i < 6; i++) {
        var c = dst.charAt(i);
        var u = c.charCodeAt(0);
        if (u >= 48 && u <= 57)      latDig[i] = u - 48;       // '0'-'9'
        else if (u >= 65 && u <= 74) latDig[i] = u - 65;       // 'A'-'J'
        else if (c === 'K' || c === 'L') latDig[i] = 0;         // ambig/space
        else if (u >= 80 && u <= 89) latDig[i] = u - 80;       // 'P'-'Y'
        else if (c === 'Z')          latDig[i] = 0;
        else return null;

        if (i === 3 && u >= 80 && u <= 90) nFlag = true;
        if (i === 4 && u >= 80 && u <= 90) lonOffset100 = true;
        if (i === 5 && u >= 80 && u <= 90) wFlag = true;
    }
    var latDeg = latDig[0] * 10 + latDig[1];
    var latMin = latDig[2] * 10 + latDig[3];
    var latMinFrac = latDig[4] * 10 + latDig[5];
    var lat = latDeg + (latMin + latMinFrac / 100.0) / 60.0;
    if (!nFlag) lat = -lat;

    var lonDeg = (payload.charCodeAt(0) & 0xff) - 28;
    var lonMin = (payload.charCodeAt(1) & 0xff) - 28;
    var lonMinFrac = (payload.charCodeAt(2) & 0xff) - 28;
    if (lonOffset100) lonDeg += 100;
    if (lonDeg < 0 || lonDeg > 179) return null;
    if (lonMin < 0 || lonMin >= 60) return null;
    if (lonMinFrac < 0 || lonMinFrac > 99) return null;
    var lon = lonDeg + (lonMin + lonMinFrac / 100.0) / 60.0;
    if (wFlag) lon = -lon;

    return {
        lat: lat, lon: lon,
        symCode:  payload.charAt(6),
        symTable: payload.charAt(7),
        comment:  payload.length > 8 ? payload.substring(8).trim() : '',
    };
}

/**
 * Parse an APRS info field for position data. Returns an object with
 * { lat, lon, symTable, symCode, comment } on success, null on no match.
 *
 * @param {string} dst   AX.25 destination callsign (needed for MIC-E lat)
 * @param {string} info  AX.25 info field (the bytes after PID 0xF0)
 */
export function aprsParsePosition(dst, info) {
    if (!info) return null;
    var dti = info.charAt(0);
    if (dti === '!' || dti === '=' || dti === '/' || dti === '@') {
        var body = info.substring(1);
        if (dti === '/' || dti === '@') body = _stripTimestamp(body);
        if (!body) return null;
        var first = body.charCodeAt(0);
        if ((first >= 48 && first <= 57) || first === 32) {
            return _parseUncompressed(body);
        }
        return _parseCompressed(body);
    }
    var u = dti.charCodeAt(0);
    if (dti === "'" || dti === '`' || u === 0x1c || u === 0x1d) {
        return _parseMicE(dst, info.substring(1));
    }
    return null;
}


// --- APRS station database + beacon scheduler ---------------------------
//
// Mirrors src/aprsprocessor.cpp's stations_/beacon timer.  Browser-only
// build's serial-transport.js owns one of these and forwards events back
// to the SPA via the same JSON shape the C++ webserver emits.

export class AprsProcessor extends EventTarget {
    constructor() {
        super();
        this.stations = new Map();   // src -> Station record
        this._beacon = {
            enabled: false,
            intervalSec: 600,
            src: '', lat: 0, lon: 0,
            symTable: '/', symCode: '.',
            comment: '', path: [],
            timer: null,
        };
    }

    /**
     * Build the aprsSnapshot message — same shape the C++ webserver emits
     * so packet.js's existing handler renders without changes.
     */
    snapshot() {
        var arr = [];
        this.stations.forEach((s) => arr.push(this._stationToJson(s)));
        return { type: 'aprsSnapshot', stations: arr };
    }

    clearStations() {
        this.stations.clear();
        this.dispatchEvent(new CustomEvent('cleared'));
    }

    /**
     * Feed an RX-decoded AX.25 frame (the shape parseAx25Frame() returns).
     * Non-UI / non-position frames are ignored. Position frames update the
     * station record and dispatch an 'station' event with the JSON shape.
     */
    onRxFrame(frame) {
        if (!frame || frame.ftype !== 'UI') return;
        var src = (frame.src || '').trim();
        var dst = (frame.dst || '').trim();
        if (!src || !frame.info) return;
        var pos = aprsParsePosition(dst, frame.info);
        if (!pos) return;

        var st = this.stations.get(src);
        if (!st) {
            st = { src: src, count: 0 };
            this.stations.set(src, st);
        }
        st.lat = pos.lat;
        st.lon = pos.lon;
        st.symTable = pos.symTable || '/';
        st.symCode  = pos.symCode  || '.';
        st.comment  = pos.comment  || '';
        st.path     = (frame.path || []).slice();
        st.lastHeardMs = Date.now();
        st.count++;

        var json = this._stationToJson(st);
        json.type = 'aprsStation';
        this.dispatchEvent(new CustomEvent('station', { detail: json }));
    }

    /**
     * Build the "!ddmm.mmN/dddmm.mmW>comment" info field for a position
     * report. Comment truncated to 43 chars per APRS spec.
     */
    static buildPositionInfo(lat, lon, symTable, symCode, comment) {
        var pad = function (n, w) {
            var s = String(n);
            while (s.length < w) s = '0' + s;
            return s;
        };
        var latNeg = lat < 0, lonNeg = lon < 0;
        var aLat = Math.abs(lat), aLon = Math.abs(lon);
        var latDeg = Math.floor(aLat);
        var latMinD = (aLat - latDeg) * 60;
        var latMin = Math.floor(latMinD);
        var latMinFrac = Math.round((latMinD - latMin) * 100);
        if (latMinFrac >= 100) { latMinFrac = 0; latMin++; }
        if (latMin >= 60)      { latMin = 0; latDeg++; }

        var lonDeg = Math.floor(aLon);
        var lonMinD = (aLon - lonDeg) * 60;
        var lonMin = Math.floor(lonMinD);
        var lonMinFrac = Math.round((lonMinD - lonMin) * 100);
        if (lonMinFrac >= 100) { lonMinFrac = 0; lonMin++; }
        if (lonMin >= 60)      { lonMin = 0; lonDeg++; }

        var st = symTable || '/';
        var sc = symCode  || '.';
        var c = (comment || '').slice(0, 43);
        return '!'
            + pad(latDeg, 2) + pad(latMin, 2) + '.' + pad(latMinFrac, 2) + (latNeg ? 'S' : 'N')
            + st
            + pad(lonDeg, 3) + pad(lonMin, 2) + '.' + pad(lonMinFrac, 2) + (lonNeg ? 'W' : 'E')
            + sc
            + c;
    }

    /**
     * Configure the periodic beacon. enabled+intervalSec start the timer,
     * !enabled stops it. Other fields are stored regardless so a later
     * enable picks them up.
     *
     * Beacon TX is delegated: when the timer fires we dispatch a
     * 'txBeacon' event whose detail is { src, dst, path, info } — the
     * transport hooks that into its packet TX path.
     */
    setBeaconConfig(cfg) {
        // Only overwrite fields that the caller passed — a follow-up
        // call with just {enabled: false} (e.g. on packet-disable) needs
        // to leave src/lat/lon intact so a later re-enable still works.
        var b = this._beacon;
        b.enabled = !!cfg.enabled;
        if (cfg.intervalSec > 0) b.intervalSec = cfg.intervalSec | 0;
        if (cfg.src != null)      b.src      = String(cfg.src).trim();
        if (cfg.lat != null)      b.lat      = +cfg.lat;
        if (cfg.lon != null)      b.lon      = +cfg.lon;
        if (cfg.symTable != null) b.symTable = cfg.symTable || '/';
        if (cfg.symCode  != null) b.symCode  = cfg.symCode  || '.';
        if (cfg.comment  != null) b.comment  = cfg.comment;
        if (Array.isArray(cfg.path)) b.path  = cfg.path.slice();
        if (b.timer) { clearInterval(b.timer); b.timer = null; }
        if (b.enabled && b.src && b.intervalSec > 0) {
            var self = this;
            b.timer = setInterval(function () { self._fireBeacon(); }, b.intervalSec * 1000);
        }
    }

    /**
     * One-shot beacon TX. Same fields as setBeaconConfig but no timer.
     * Caller is the SPA's "TX now" button.
     */
    txBeaconNow(cfg) {
        if (!cfg || !cfg.src) return;
        var info = AprsProcessor.buildPositionInfo(
            +cfg.lat, +cfg.lon,
            cfg.symTable, cfg.symCode, cfg.comment);
        this.dispatchEvent(new CustomEvent('txBeacon', {
            detail: {
                src: cfg.src, dst: 'APWFWB',
                path: Array.isArray(cfg.path) ? cfg.path.slice() : [],
                info: info,
            },
        }));
    }

    _fireBeacon() {
        var b = this._beacon;
        if (!b.enabled || !b.src) return;
        this.txBeaconNow(b);
    }

    _stationToJson(s) {
        return {
            src: s.src, lat: s.lat, lon: s.lon,
            symTable: s.symTable, symCode: s.symCode,
            comment: s.comment,
            path: (s.path || []).slice(),
            lastHeard: s.lastHeardMs || 0,
            count: s.count || 0,
        };
    }
}


// --- AX.25 connected-mode link layer ------------------------------------
//
// Wraps the WASM ax25_link.c + dlq.c surface that wfweb_dw_link_* exports.
// Mirrors src/ax25linkprocessor.cpp on the C++ side: same callbacks, same
// dlq request API, same dispatcher loop. Difference is threading — WASM
// is single-threaded, so we drive the loop from a setInterval here.
//
// Usage:
//   const modem = await DirewolfModem.create();
//   modem.init(1200, 48000);
//   const link = new Ax25Link(modem);
//   link.start();
//   link.addEventListener('linkEstablished', e => …);
//   link.addEventListener('rxData', e => …);
//   link.addEventListener('transmitFrameAudio', e => /* send int16 to rig */);
//   link.registerCall(0, 0, 'N0CALL-1');
//   link.connectRequest({ chan: 0, client: 0, ownCall: 'N0CALL-1', peerCall: 'BBS-1' });
//   link.sendData(0, 0, 'N0CALL-1', 'BBS-1', [], 'hello\r');

const LINK_EV_ESTABLISHED = 1;
const LINK_EV_TERMINATED  = 2;
const LINK_EV_OUTSTANDING = 3;
const LINK_EV_ACKED       = 4;

export class Ax25Link extends EventTarget {
    constructor(modem) {
        super();
        const mod = modem._mod;
        this._modem = modem;
        this._mod   = mod;
        // cwrap each export. Strings cross the JS/WASM boundary as char*
        // so we let cwrap copy on the way in; on the way out we read from
        // HEAPU8 manually (UTF8ToString in EM_JS handles that).
        this._cInit       = mod.cwrap('wfweb_dw_link_init',                'number', []);
        this._cStep       = mod.cwrap('wfweb_dw_link_step',                'number', []);
        this._cSetBaud    = mod.cwrap('wfweb_dw_link_set_baud',            'null',   ['number']);
        this._cPaclen     = mod.cwrap('wfweb_dw_link_paclen',              'number', []);
        this._cReg        = mod.cwrap('wfweb_dw_link_register_call',       'null',   ['number', 'number', 'string']);
        this._cUnreg      = mod.cwrap('wfweb_dw_link_unregister_call',     'null',   ['number', 'number', 'string']);
        this._cConnect    = mod.cwrap('wfweb_dw_link_connect',             'null',   ['number', 'number', 'string', 'string', 'string']);
        this._cDisconnect = mod.cwrap('wfweb_dw_link_disconnect',          'null',   ['number', 'number', 'string', 'string', 'string']);
        this._cSendData   = mod.cwrap('wfweb_dw_link_send_data',           'null',   ['number', 'number', 'string', 'string', 'string', 'number', 'number', 'number']);
        this._cOutstand   = mod.cwrap('wfweb_dw_link_outstanding_request', 'null',   ['number', 'number', 'string', 'string']);
        this._cClientCleanup = mod.cwrap('wfweb_dw_link_client_cleanup',   'null',   ['number']);
        this._cTxPtr      = mod.cwrap('wfweb_dw_tx_buffer_ptr',            'number', []);
        this._cTxLen      = mod.cwrap('wfweb_dw_tx_buffer_len',            'number', []);
        this._cTxReset    = mod.cwrap('wfweb_dw_tx_buffer_reset',          'null',   []);

        // Callsigns registered with the link layer (server_callsign_lookup
        // hits us for every inbound SABM). Map upper-case call -> client id.
        this._registered = new Map();

        // The C-side EM_JS trampolines invoke these:
        mod.onLinkEvent = (kind, chan, client, remote, own, param) => {
            this._dispatchLinkEvent(kind, chan, client, remote, own, param);
        };
        mod.onLinkData = (chan, client, remote, own, pid, bytes) => {
            this.dispatchEvent(new CustomEvent('rxData', { detail: {
                chan, client, remote, own, pid,
                data: bytes,   // Uint8Array, already sliced out of the heap
            } }));
        };
        mod.onCallsignLookup = (callsign) => {
            const c = (callsign || '').toUpperCase();
            return this._registered.has(c) ? this._registered.get(c) : -1;
        };

        this._tickInterval = null;
        this._tickPeriod = 50;   // ms — tight enough for v2.0 timer accuracy
        this._started = false;
    }

    /** Initialise the WASM link layer + start the dispatcher tick. */
    start() {
        if (this._started) return;
        const rc = this._cInit();
        if (rc !== 0) throw new Error('wfweb_dw_link_init failed: ' + rc);
        // Match the link timing to the modem's current baud (frack/paclen).
        if (this._modem.baud) this._cSetBaud(this._modem.baud);
        this._tickInterval = setInterval(() => this._tick(), this._tickPeriod);
        this._started = true;
    }

    stop() {
        if (!this._started) return;
        clearInterval(this._tickInterval);
        this._tickInterval = null;
        this._started = false;
    }

    /** Update link-layer parameters when the modem baud changes. */
    setBaud(baud) {
        if (this._started) this._cSetBaud(baud | 0);
    }

    /** Current AX.25 paclen — the SPA's YAPP/file-xfer code uses this to
     *  size info-field chunks so they fit one I-frame. */
    get paclen() { return this._started ? this._cPaclen() : 128; }

    registerCall(chan, client, callsign) {
        const c = (callsign || '').toUpperCase();
        if (!c) return;
        this._registered.set(c, client | 0);
        this._cReg(chan | 0, client | 0, c);
    }
    unregisterCall(chan, client, callsign) {
        const c = (callsign || '').toUpperCase();
        if (!c) return;
        this._registered.delete(c);
        this._cUnreg(chan | 0, client | 0, c);
    }

    /** Outgoing connect request. Returns immediately; the
     *  'linkEstablished' event fires when the SABM/UA exchange completes
     *  (or 'linkTerminated' if it fails / times out). */
    connectRequest({ chan, client, ownCall, peerCall, digis }) {
        const csv = (digis && digis.length) ? digis.join(',') : '';
        this._cConnect(chan | 0, client | 0,
            (ownCall || '').toUpperCase(),
            (peerCall || '').toUpperCase(), csv);
    }

    disconnectRequest({ chan, client, ownCall, peerCall, digis }) {
        const csv = (digis && digis.length) ? digis.join(',') : '';
        this._cDisconnect(chan | 0, client | 0,
            (ownCall || '').toUpperCase(),
            (peerCall || '').toUpperCase(), csv);
    }

    /** Queue data for transmission on a connected link. The payload is
     *  shipped as one or more I-frames sized at paclen. ax25_link copies
     *  the buffer internally (cdata_t), so the JS heap memory frees on
     *  return. */
    sendData({ chan, client, ownCall, peerCall, digis, pid, data }) {
        const bytes = (data instanceof Uint8Array)
            ? data
            : new TextEncoder().encode(String(data));
        const ptr = this._mod._malloc(bytes.length);
        try {
            this._mod.HEAPU8.set(bytes, ptr);
            const csv = (digis && digis.length) ? digis.join(',') : '';
            this._cSendData(chan | 0, client | 0,
                (ownCall || '').toUpperCase(),
                (peerCall || '').toUpperCase(),
                csv, pid | 0xF0, ptr, bytes.length);
        } finally {
            this._mod._free(ptr);
        }
    }

    outstandingRequest({ chan, client, ownCall, peerCall }) {
        this._cOutstand(chan | 0, client | 0,
            (ownCall || '').toUpperCase(),
            (peerCall || '').toUpperCase());
    }

    clientCleanup(client) { this._cClientCleanup(client | 0); }

    // --- internals -------------------------------------------------------

    _dispatchLinkEvent(kind, chan, client, remote, own, param) {
        switch (kind) {
        case LINK_EV_ESTABLISHED:
            this.dispatchEvent(new CustomEvent('linkEstablished', { detail: {
                chan, client, remote, own, incoming: param !== 0,
            } })); break;
        case LINK_EV_TERMINATED:
            this.dispatchEvent(new CustomEvent('linkTerminated', { detail: {
                chan, client, remote, own, timeout: param,
            } })); break;
        case LINK_EV_OUTSTANDING:
            this.dispatchEvent(new CustomEvent('outstandingFrames', { detail: {
                chan, client, remote, own, count: param,
            } })); break;
        case LINK_EV_ACKED:
            this.dispatchEvent(new CustomEvent('dataAcked', { detail: {
                chan, client, remote, own, count: param,
            } })); break;
        }
    }

    /** Drain DLQ + service link timers + flush any TX audio that the
     *  link's lm_data_request emitted. tx_buf is reset every tick so it
     *  doesn't accumulate (and so the one-shot APRS txFrame path can
     *  safely reuse the same buffer between ticks). */
    _tick() {
        this._cTxReset();
        this._cStep();
        const len = this._cTxLen();
        if (len > 0) {
            const ptr = this._cTxPtr();
            const bytes = this._mod.HEAPU8.slice(ptr, ptr + len);
            const audio = new Int16Array(bytes.buffer, bytes.byteOffset, bytes.byteLength >> 1);
            this.dispatchEvent(new CustomEvent('transmitFrameAudio', { detail: {
                chan: 0, audio,
            } }));
        }
    }
}
