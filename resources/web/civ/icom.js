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
        // response parsers
        parseFrequencyReply: parseFrequencyReply,
        parseModeReply: parseModeReply,
        parsePTTReply: parsePTTReply,
        parseSMeterReply: parseSMeterReply,
        parseTransceiverIDReply: parseTransceiverIDReply,
        parseAck: parseAck,
    };
})(window);
