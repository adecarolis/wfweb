// Icom CI-V codec — minimal subset for Phase 1 (Direct mode MVP).
//
// This is a hand-port of the relevant parts of src/radio/icomcommander.cpp.
// Scope (intentionally small — grow as features come online):
//   build/parse the FE FE … FD frame envelope
//   set/get frequency  (cmd 0x05 / 0x03)
//   set/get mode+filter (cmd 0x06 / 0x04)
//   set/get PTT        (cmd 0x1C 0x00)
//   get S-meter        (cmd 0x15 0x02)
//   read transceiver ID (cmd 0x19 0x00)
//
// Frame format (little-endian BCD throughout):
//   FE FE <to> <from> <cmd> [<sub>] [<data...>] FD
// Echo: every byte sent is echoed back by the rig. Parser must skip echoes.
//
// Frequencies are 5 BCD bytes for HF/VHF/UHF: byte 0 = 1Hz/10Hz, byte 1 =
// 100Hz/1kHz, byte 2 = 10kHz/100kHz, byte 3 = 1MHz/10MHz, byte 4 = 100MHz/1GHz.
// SHF rigs (IC-905) use 6 bytes.

(function (global) {
    'use strict';

    var FE = 0xFE;
    var FD = 0xFD;
    var CONTROLLER_ADDR = 0xE0;  // wfweb (default; can be overridden)

    // ---------- Frame framing ---------------------------------------------

    function buildFrame(toAddr, fromAddr, payload) {
        var len = 4 + payload.length + 1;  // FE FE to from <payload> FD
        var out = new Uint8Array(len);
        out[0] = FE;
        out[1] = FE;
        out[2] = toAddr & 0xFF;
        out[3] = fromAddr & 0xFF;
        out.set(payload, 4);
        out[len - 1] = FD;
        return out;
    }

    /**
     * Streaming CI-V parser. Feed bytes; receive complete frames via onFrame.
     * Filters out our own echo (frames whose 'from' equals controllerAddr).
     */
    class CivParser {
        constructor(opts) {
            opts = opts || {};
            this.controllerAddr = opts.controllerAddr || CONTROLLER_ADDR;
            this.buf = [];
            this.inFrame = false;
            this.preambleSeen = 0;  // count of consecutive FE
            this.onFrame = opts.onFrame || function () {};
        }

        feed(bytes) {
            for (var i = 0; i < bytes.length; i++) {
                var b = bytes[i] & 0xFF;
                if (!this.inFrame) {
                    if (b === FE) {
                        this.preambleSeen++;
                        if (this.preambleSeen >= 2) {
                            this.inFrame = true;
                            this.buf = [];
                            // collapse any further leading FEs
                        }
                    } else {
                        this.preambleSeen = 0;
                    }
                    continue;
                }
                // in-frame
                if (b === FE) {
                    // extra FE inside body — Icom rigs send these as filler;
                    // restart frame buffer.
                    this.buf = [];
                    continue;
                }
                if (b === FD) {
                    this.inFrame = false;
                    this.preambleSeen = 0;
                    if (this.buf.length >= 2) {
                        var to = this.buf[0];
                        var from = this.buf[1];
                        var payload = this.buf.slice(2);
                        // Drop our own echo.
                        if (from !== this.controllerAddr) {
                            this.onFrame({ to: to, from: from, payload: payload });
                        }
                    }
                    this.buf = [];
                    continue;
                }
                this.buf.push(b);
            }
        }
    }

    // ---------- BCD helpers -----------------------------------------------

    // Encode a non-negative integer as little-endian BCD pairs into N bytes.
    // Each byte holds two BCD digits: low nibble = lower digit, high nibble = upper.
    function encodeBcdLE(value, numBytes) {
        var out = new Uint8Array(numBytes);
        var v = Math.floor(Math.max(0, value));
        for (var i = 0; i < numBytes; i++) {
            var lo = v % 10; v = Math.floor(v / 10);
            var hi = v % 10; v = Math.floor(v / 10);
            out[i] = (hi << 4) | lo;
        }
        return out;
    }

    function decodeBcdLE(bytes) {
        var v = 0, mul = 1;
        for (var i = 0; i < bytes.length; i++) {
            var lo = bytes[i] & 0x0F;
            var hi = (bytes[i] >> 4) & 0x0F;
            v += lo * mul; mul *= 10;
            v += hi * mul; mul *= 10;
        }
        return v;
    }

    // Encode a 2-digit BCD byte (e.g. for PBT centre, levels expressed as 0-99).
    function encodeBcd2(num) {
        var n = Math.max(0, Math.min(99, Math.floor(num)));
        return ((Math.floor(n / 10) << 4) | (n % 10)) & 0xFF;
    }

    // Encode a 0..255 value as 4-digit BCD across 2 bytes (Icom level format,
    // where 0 -> 0x0000 and 255 -> 0x0255).
    function encodeBcdLevel(value) {
        var v = Math.max(0, Math.min(255, Math.floor(value)));
        var thousands = Math.floor(v / 1000); v -= thousands * 1000;
        var hundreds  = Math.floor(v / 100);  v -= hundreds  * 100;
        var tens      = Math.floor(v / 10);   v -= tens      * 10;
        var units     = v;
        return new Uint8Array([
            (thousands << 4) | hundreds,
            (tens << 4) | units,
        ]);
    }

    function decodeBcdLevel(b0, b1) {
        var thousands = (b0 >> 4) & 0x0F;
        var hundreds  =  b0       & 0x0F;
        var tens      = (b1 >> 4) & 0x0F;
        var units     =  b1       & 0x0F;
        return thousands * 1000 + hundreds * 100 + tens * 10 + units;
    }

    // ---------- Mode tables ------------------------------------------------
    //
    // Icom mode codes (cmd 0x06 byte 0). Names match wfweb's existing JSON
    // 'mode' string surface; the SPA already speaks these.
    var MODE_TO_CODE = {
        'LSB':    0x00,
        'USB':    0x01,
        'AM':     0x02,
        'CW':     0x03,
        'RTTY':   0x04,
        'FM':     0x05,
        'CW-R':   0x07,
        'RTTY-R': 0x08,
        'DV':     0x17,
        'DD':     0x22,
    };
    var CODE_TO_MODE = {};
    for (var k in MODE_TO_CODE) CODE_TO_MODE[MODE_TO_CODE[k]] = k;

    // ---------- Command builders ------------------------------------------
    //
    // Each builder returns a Uint8Array of just the FRAME PAYLOAD (cmd + data),
    // ready to be wrapped with buildFrame(toAddr, fromAddr, payload).

    function cmdReadFrequency() {
        return new Uint8Array([0x03]);
    }

    function cmdSetFrequency(hz, numBytes) {
        numBytes = numBytes || (hz >= 1e10 ? 6 : 5);
        var bcd = encodeBcdLE(hz, numBytes);
        var out = new Uint8Array(1 + numBytes);
        out[0] = 0x05;
        out.set(bcd, 1);
        return out;
    }

    function cmdReadMode() {
        return new Uint8Array([0x04]);
    }

    function cmdSetMode(modeName, filterIndex) {
        var code = MODE_TO_CODE[modeName];
        if (code === undefined) throw new Error('unknown mode: ' + modeName);
        if (filterIndex === undefined || filterIndex === null) {
            return new Uint8Array([0x06, code]);
        }
        return new Uint8Array([0x06, code, filterIndex & 0xFF]);
    }

    function cmdReadPTT() {
        return new Uint8Array([0x1C, 0x00]);
    }

    function cmdSetPTT(on) {
        return new Uint8Array([0x1C, 0x00, on ? 0x01 : 0x00]);
    }

    function cmdReadSMeter() {
        return new Uint8Array([0x15, 0x02]);
    }

    function cmdReadTransceiverID() {
        return new Uint8Array([0x19, 0x00]);
    }

    // ---------- Analog levels (cmd 0x14) ---------------------------------
    // Sub-commands map to functions like AF gain, RF gain, etc. Values are
    // 0..255 encoded as 4-digit BCD in 2 bytes (encodeBcdLevel format).
    function cmdReadAnalogLevel(sub) {
        return new Uint8Array([0x14, sub]);
    }
    function cmdSetAnalogLevel(sub, level) {
        var bcd = encodeBcdLevel(level);
        var out = new Uint8Array(2 + bcd.length);
        out[0] = 0x14; out[1] = sub;
        out.set(bcd, 2);
        return out;
    }
    function parseAnalogLevelReply(payload) {
        // [0x14, sub, b0, b1]  -> { sub, value }
        if (payload.length < 4 || payload[0] !== 0x14) return null;
        return { sub: payload[1], value: decodeBcdLevel(payload[2], payload[3]) };
    }

    // ---------- Bool toggles (cmd 0x16) ----------------------------------
    function cmdReadBoolFunc(sub) {
        return new Uint8Array([0x16, sub]);
    }
    function cmdSetBoolFunc(sub, on) {
        return new Uint8Array([0x16, sub, on ? 0x01 : 0x00]);
    }
    // Preamp is multi-state (0=off, 1=Pre1, 2=Pre2). Treat separately.
    function cmdSetPreamp(level) {
        return new Uint8Array([0x16, 0x02, level & 0xFF]);
    }
    function parseBoolFuncReply(payload) {
        // [0x16, sub, value]
        if (payload.length < 3 || payload[0] !== 0x16) return null;
        return { sub: payload[1], value: payload[2] };
    }

    // ---------- Attenuator (cmd 0x11) ------------------------------------
    function cmdReadAttenuator() { return new Uint8Array([0x11]); }
    function cmdSetAttenuator(dB) {
        // 0 = off, otherwise BCD-encoded dB value (0x14 = 20dB on IC-7300)
        return new Uint8Array([0x11, encodeBcd2(dB)]);
    }

    // ---------- Filter width (cmd 0x1A 0x03) -----------------------------
    // Encoded as a single BCD byte holding an "index" into the rig's
    // mode-dependent passband table:
    //   AM:        index = (pass / 200) - 1, range 0..49 (200..10000 Hz)
    //   SSB/CW/PSK pass < 600: index = (pass / 50) - 1, range 0..9 (50..500 Hz)
    //   SSB/CW/PSK pass >= 600: index = (pass / 100) + 4, range 10..40 (600..3600 Hz)
    // Mirrors src/radio/icomcommander.cpp::makeFilterWidth().
    function _hzToFilterWidthIdx(hz, modeName) {
        var calc;
        var isAM = (modeName === 'AM');
        if (isAM) {
            calc = Math.floor(hz / 200) - 1;
            if (calc > 49) calc = 49;
        } else if (hz >= 600) {
            calc = Math.floor(hz / 100) + 4;
            if (calc > 40) calc = 40;
        } else {
            calc = Math.floor(hz / 50) - 1;
        }
        if (calc < 0) calc = 0;
        var tens = Math.floor(calc / 10);
        var units = calc - 10 * tens;
        return ((tens & 0x0F) << 4) | (units & 0x0F);
    }
    function cmdReadFilterWidth() { return new Uint8Array([0x1A, 0x03]); }
    function cmdSetFilterWidth(hz, modeName) {
        return new Uint8Array([0x1A, 0x03, _hzToFilterWidthIdx(hz, modeName)]);
    }
    function _filterWidthIdxToHz(idx, modeName) {
        if (modeName === 'AM') return (idx + 1) * 200;
        if (idx <= 9) return (idx + 1) * 50;
        return (idx - 4) * 100;
    }
    function parseFilterWidthReply(payload, modeName) {
        // [0x1A, 0x03, BCD-byte]
        if (payload.length < 3 || payload[0] !== 0x1A || payload[1] !== 0x03) return null;
        var b = payload[2];
        var idx = ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
        return _filterWidthIdxToHz(idx, modeName || 'USB');
    }
    function parseAttenuatorReply(payload) {
        // [0x11, level]
        if (payload.length < 2 || payload[0] !== 0x11) return null;
        // BCD-decode the level byte to dB
        var hi = (payload[1] >> 4) & 0x0F;
        var lo = payload[1] & 0x0F;
        return hi * 10 + lo;
    }

    // ---------- Split (cmd 0x0F) -----------------------------------------
    // 0x0F (no value) = read current split state
    // 0x0F 0x00 = set split off, 0x0F 0x01 = set split on
    function cmdReadSplit() { return new Uint8Array([0x0F]); }
    function cmdSetSplit(on) {
        return new Uint8Array([0x0F, on ? 0x01 : 0x00]);
    }
    function parseSplitReply(payload) {
        if (payload.length < 2 || payload[0] !== 0x0F) return null;
        return payload[1] !== 0;
    }

    // ---------- VFO ops (cmd 0x07) ---------------------------------------
    function cmdSelectVFO(letter) {
        // 'A' -> 0x07 0x00, 'B' -> 0x07 0x01
        return new Uint8Array([0x07, letter === 'B' ? 0x01 : 0x00]);
    }
    function cmdSwapVFO()      { return new Uint8Array([0x07, 0xB0]); }
    function cmdEqualizeVFO()  { return new Uint8Array([0x07, 0xA0]); }

    // ---------- Tuner (cmd 0x1C 0x01) ------------------------------------
    // value: 0 = off, 1 = on, 2 = start tune
    function cmdSetTuner(value) {
        return new Uint8Array([0x1C, 0x01, value & 0xFF]);
    }
    function cmdReadTuner() { return new Uint8Array([0x1C, 0x01]); }
    function parseTunerReply(payload) {
        if (payload.length < 3 || payload[0] !== 0x1C || payload[1] !== 0x01) return null;
        return payload[2];
    }

    // ---------- Power on/off (cmd 0x18) ----------------------------------
    function cmdSetPower(on) {
        // Power-on from a powered-off rig requires a long FE preamble. The
        // simple form here works while the rig is already on (turn it off)
        // or for the rare case where transceive auto-on is configured.
        return new Uint8Array([0x18, on ? 0x01 : 0x00]);
    }

    // ---------- TX meters (cmd 0x15 0x11/0x12/0x13) ----------------------
    function cmdReadPowerMeter() { return new Uint8Array([0x15, 0x11]); }
    function cmdReadSwrMeter()   { return new Uint8Array([0x15, 0x12]); }
    function cmdReadAlcMeter()   { return new Uint8Array([0x15, 0x13]); }
    function parseTxMeterReply(payload, sub) {
        if (payload.length < 4 || payload[0] !== 0x15 || payload[1] !== sub) return null;
        return decodeBcdLevel(payload[2], payload[3]);
    }

    // Default meter calibration tables. Used when the detected rig either
    // has no entry in window.IcomRigCaps or has no entry for that meter
    // kind. The defaults are the IC-7300 tables — a reasonable fit for any
    // modern Icom HF rig and graceful for older / receiver-only rigs.
    // Each entry is [rigVal (0..255), actualVal]. Caller's lookup table
    // takes precedence; we just interpolate over it.
    var DEFAULT_METER_CAL = {
        sMeter: [[0,-54],[10,-48],[30,-36],[60,-24],[90,-12],[120,0],[241,64]],
        swr:    [[0,1.0],[48,1.5],[80,2.0],[120,3.0],[255,6.0]],
        power:  [[0,0],[21,5],[43,10],[65,15],[83,20],[95,25],[105,30],
                 [114,35],[124,40],[143,50],[183,75],[213,100],[255,120]],
        alc:    [[0,0],[120,1],[255,2]],
        // Less common meters — no useful default, return raw if absent.
        center: null, comp: null, voltage: null, current: null,
    };
    // Piecewise-linear interpolate `raw` through the [rigVal, actualVal]
    // table, clamping to the endpoints (mirrors rigCommander::getMeterCal).
    function interpolate(tbl, raw) {
        if (!tbl || !tbl.length || raw == null) return raw;
        if (raw <= tbl[0][0]) return tbl[0][1];
        if (raw >= tbl[tbl.length - 1][0]) return tbl[tbl.length - 1][1];
        for (var i = 1; i < tbl.length; i++) {
            if (raw <= tbl[i][0]) {
                var k0 = tbl[i - 1][0], v0 = tbl[i - 1][1];
                var k1 = tbl[i][0],     v1 = tbl[i][1];
                return v0 + (v1 - v0) * (raw - k0) / (k1 - k0);
            }
        }
        return raw;
    }
    // Public entry point — caller passes the rig-specific cal table (from
    // window.IcomRigCaps[civAddr].meters[kind]). Falls back to the IC-7300
    // default for that kind when the rig has no entry.
    function calMeter(kind, raw, rigTable) {
        var tbl = rigTable || DEFAULT_METER_CAL[kind];
        if (!tbl) return raw;
        return interpolate(tbl, raw);
    }
    // Best-effort lookup helper. Reads from the global IcomRigCaps that
    // rig-caps.js publishes. Returns null when the rig isn't registered or
    // doesn't have that meter — caller should fall back to the default.
    function getRigMeterTable(civAddr, kind) {
        var caps = (typeof globalThis !== 'undefined' ? globalThis : window).IcomRigCaps;
        if (!caps) return null;
        var entry = caps[civAddr];
        if (!entry || !entry.meters) return null;
        return entry.meters[kind] || null;
    }

    // ---------- CW keyer (cmd 0x17) --------------------------------------
    function cmdSendCW(text) {
        // ASCII bytes after 0x17. Max 30 chars per frame on IC-7300.
        var s = String(text).slice(0, 30);
        var out = new Uint8Array(1 + s.length);
        out[0] = 0x17;
        for (var i = 0; i < s.length; i++) out[i + 1] = s.charCodeAt(i) & 0xFF;
        return out;
    }
    function cmdStopCW() {
        // Sending 0xFF as the only byte cancels in-progress CW transmission.
        return new Uint8Array([0x17, 0xFF, 0xFF]);
    }

    // ---------- Scope center-span (cmd 0x27 0x15) -----------------------
    // 3 BCD-LE bytes give the ± half-span in Hz (e.g. ±50kHz = 50000).
    // Frame layout matches the C++ wfweb's funcScopeSpan: cmd sub recv data.
    function cmdSetScopeSpan(hz) {
        var bcd = encodeBcdLE(hz, 3);
        var out = new Uint8Array(3 + 3);
        out[0] = 0x27; out[1] = 0x15; out[2] = 0x00;  // recv = 0 (single-receiver)
        out.set(bcd, 3);
        return out;
    }
    function cmdReadScopeSpan() {
        return new Uint8Array([0x27, 0x15, 0x00]);
    }
    function parseScopeSpanReply(payload) {
        // [0x27, 0x15, recv, b0, b1, b2]
        if (payload.length < 6 || payload[0] !== 0x27 || payload[1] !== 0x15) return null;
        return decodeBcdLE(payload.slice(3, 6));
    }

    // ---------- MOD INPUT — modulation source selector -----------------
    // Two registers: "Data OFF Mod Input" (used in voice modes) and
    // "DATA1 Mod Input" (used in DATA-on modes — USB-D, LSB-D, …). The
    // CI-V byte sequence varies per rig (0x1A 0x05 NN MM); we get the
    // exact bytes from the rig-caps table generated from rigs/*.rig.
    //
    // The "input value" written into the register is also rig-specific:
    // each .rig file's Inputs section lists which Reg byte means USB,
    // MIC, etc. Defaults below match the IC-7300 (the most common rig).
    var DEFAULT_MOD_INPUTS = {
        off:    [0x1A, 0x05, 0x00, 0x66],  // IC-7300 Data OFF
        data1:  [0x1A, 0x05, 0x00, 0x67],  // IC-7300 DATA1
        usbReg: 0x03,                       // IC-7300 input value for USB
        micReg: 0x00,                       // IC-7300 input value for MIC
    };
    function getRigModInputs(civAddr) {
        var caps = (typeof globalThis !== 'undefined' ? globalThis : window).IcomRigCaps;
        var entry = caps && caps[civAddr];
        return {
            off:    (entry && entry.cmds && entry.cmds.modOff)   || DEFAULT_MOD_INPUTS.off,
            data1:  (entry && entry.cmds && entry.cmds.modData1) || DEFAULT_MOD_INPUTS.data1,
            usbReg: (entry && entry.inputs && entry.inputs.USB != null)
                        ? entry.inputs.USB : DEFAULT_MOD_INPUTS.usbReg,
            micReg: (entry && entry.inputs && entry.inputs.MIC != null)
                        ? entry.inputs.MIC : DEFAULT_MOD_INPUTS.micReg,
        };
    }
    // Append a single value byte to a CI-V prefix, returning a fresh Uint8Array.
    function _appendByte(prefix, value) {
        var out = new Uint8Array(prefix.length + 1);
        out.set(prefix);
        out[prefix.length] = value & 0xFF;
        return out;
    }
    function cmdSetModInput(prefix, regByte) { return _appendByte(prefix, regByte); }
    function cmdReadModInput(prefix) { return new Uint8Array(prefix); }
    function parseModInputReply(payload, prefix) {
        if (payload.length < prefix.length + 1) return null;
        for (var i = 0; i < prefix.length; i++) {
            if (payload[i] !== prefix[i]) return null;
        }
        return payload[prefix.length];
    }
    // Back-compat shims — same surface, IC-7300 defaults.
    function cmdSetDataOffMod(input) { return cmdSetModInput(DEFAULT_MOD_INPUTS.off, input); }
    function cmdReadDataOffMod()     { return cmdReadModInput(DEFAULT_MOD_INPUTS.off); }
    function cmdSetDataMod(input)    { return cmdSetModInput(DEFAULT_MOD_INPUTS.data1, input); }
    function parseDataOffModReply(payload) { return parseModInputReply(payload, DEFAULT_MOD_INPUTS.off); }
    var MOD_INPUT_USB = DEFAULT_MOD_INPUTS.usbReg;
    var MOD_INPUT_MIC = DEFAULT_MOD_INPUTS.micReg;

    // Probe frame for auto-detecting the rig: broadcast read-transceiver-ID.
    // Any rig listening on the bus replies with its own CI-V address in the
    // frame's "from" field (regardless of how the rig is currently addressed).
    var PROBE_FRAME = new Uint8Array([0xFE, 0xFE, 0x00, 0xE0, 0x19, 0x00, 0xFD]);

    // ---------- Response parsers ------------------------------------------
    //
    // Parsers consume the frame payload (i.e. starting at the cmd byte).
    // Return null if the payload doesn't match.

    function parseFrequencyReply(payload) {
        // Solicited: [0x03, b0..b4 or b5]
        // Unsolicited (the rig pushes 0x00 frames whenever the dial moves):
        //                  [0x00, b0..b4 or b5]
        if (payload.length < 6) return null;
        if (payload[0] !== 0x03 && payload[0] !== 0x00) return null;
        return decodeBcdLE(payload.slice(1));
    }

    function parseModeReply(payload) {
        // Solicited: [0x04, mode, filter]
        // Unsolicited: [0x01, mode, filter]
        if (payload.length < 2) return null;
        if (payload[0] !== 0x04 && payload[0] !== 0x01) return null;
        var modeName = CODE_TO_MODE[payload[1]] || ('mode_0x' + payload[1].toString(16));
        var filter = payload.length >= 3 ? payload[2] : null;
        return { mode: modeName, filter: filter };
    }

    function parsePTTReply(payload) {
        // [0x1C, 0x00, state]
        if (payload.length < 3 || payload[0] !== 0x1C || payload[1] !== 0x00) return null;
        return payload[2] !== 0x00;
    }

    function parseSMeterReply(payload) {
        // [0x15, 0x02, b0, b1] -> 0..255
        if (payload.length < 4 || payload[0] !== 0x15 || payload[1] !== 0x02) return null;
        return decodeBcdLevel(payload[2], payload[3]);
    }

    function parseTransceiverIDReply(payload) {
        // [0x19, 0x00, civAddr]
        if (payload.length < 3 || payload[0] !== 0x19 || payload[1] !== 0x00) return null;
        return payload[2];
    }

    // OK / NG response (when issuing a set command without expecting data back).
    // OK = single 0xFB payload; NG = single 0xFA payload.
    function parseAck(payload) {
        if (payload.length === 1 && payload[0] === 0xFB) return true;
        if (payload.length === 1 && payload[0] === 0xFA) return false;
        return null;
    }

    global.IcomCiv = {
        FE: FE, FD: FD, CONTROLLER_ADDR: CONTROLLER_ADDR,
        PROBE_FRAME: PROBE_FRAME,
        buildFrame: buildFrame,
        CivParser: CivParser,
        encodeBcdLE: encodeBcdLE,
        decodeBcdLE: decodeBcdLE,
        encodeBcd2: encodeBcd2,
        encodeBcdLevel: encodeBcdLevel,
        decodeBcdLevel: decodeBcdLevel,
        modeToCode: MODE_TO_CODE,
        codeToMode: CODE_TO_MODE,
        // command builders
        cmdReadFrequency: cmdReadFrequency,
        cmdSetFrequency: cmdSetFrequency,
        cmdReadMode: cmdReadMode,
        cmdSetMode: cmdSetMode,
        cmdReadPTT: cmdReadPTT,
        cmdSetPTT: cmdSetPTT,
        cmdReadSMeter: cmdReadSMeter,
        cmdReadTransceiverID: cmdReadTransceiverID,
        cmdReadAnalogLevel: cmdReadAnalogLevel,
        cmdSetAnalogLevel: cmdSetAnalogLevel,
        cmdReadBoolFunc: cmdReadBoolFunc,
        cmdSetBoolFunc: cmdSetBoolFunc,
        cmdSetPreamp: cmdSetPreamp,
        cmdReadAttenuator: cmdReadAttenuator,
        cmdSetAttenuator: cmdSetAttenuator,
        cmdReadFilterWidth: cmdReadFilterWidth,
        cmdSetFilterWidth: cmdSetFilterWidth,
        parseFilterWidthReply: parseFilterWidthReply,
        cmdReadSplit: cmdReadSplit,
        cmdSetSplit: cmdSetSplit,
        cmdSelectVFO: cmdSelectVFO,
        cmdSwapVFO: cmdSwapVFO,
        cmdEqualizeVFO: cmdEqualizeVFO,
        cmdReadTuner: cmdReadTuner,
        cmdSetTuner: cmdSetTuner,
        cmdSetPower: cmdSetPower,
        cmdReadPowerMeter: cmdReadPowerMeter,
        cmdReadSwrMeter: cmdReadSwrMeter,
        cmdReadAlcMeter: cmdReadAlcMeter,
        cmdSendCW: cmdSendCW,
        cmdStopCW: cmdStopCW,
        cmdSetScopeSpan: cmdSetScopeSpan,
        cmdReadScopeSpan: cmdReadScopeSpan,
        parseScopeSpanReply: parseScopeSpanReply,
        cmdSetDataOffMod: cmdSetDataOffMod,
        cmdReadDataOffMod: cmdReadDataOffMod,
        cmdSetDataMod: cmdSetDataMod,
        parseDataOffModReply: parseDataOffModReply,
        MOD_INPUT_USB: MOD_INPUT_USB,
        MOD_INPUT_MIC: MOD_INPUT_MIC,
        // Per-rig MOD INPUT (preferred over the back-compat helpers above)
        getRigModInputs: getRigModInputs,
        cmdSetModInput: cmdSetModInput,
        cmdReadModInput: cmdReadModInput,
        parseModInputReply: parseModInputReply,
        // response parsers
        parseFrequencyReply: parseFrequencyReply,
        parseModeReply: parseModeReply,
        parsePTTReply: parsePTTReply,
        parseSMeterReply: parseSMeterReply,
        parseTransceiverIDReply: parseTransceiverIDReply,
        parseAnalogLevelReply: parseAnalogLevelReply,
        parseBoolFuncReply: parseBoolFuncReply,
        parseAttenuatorReply: parseAttenuatorReply,
        parseSplitReply: parseSplitReply,
        parseTunerReply: parseTunerReply,
        parseTxMeterReply: parseTxMeterReply,
        calMeter: calMeter,
        getRigMeterTable: getRigMeterTable,
        parseAck: parseAck,
    };
})(window);
