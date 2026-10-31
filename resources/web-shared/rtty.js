// RTTY (Baudot / ITA2, 45.45 Bd, 170 Hz shift) — the parts both the page and
// the decoder worker need, one copy for both builds.  A plain script on
// purpose: the page loads it with <script>, a classic worker with
// importScripts(), a module worker with import './rtty.js' and the node
// tests through the same import with a `self` shim.  It defines
// self.WfRtty = { ... } and nothing else.
//
//   ITA2 tables + encodeIta2() / Ita2Decoder   the 5-bit code, LTRS/FIGS shift, USOS
//   WfRttySynth                                 AFSK TX audio: start bit, 5 data bits
//                                               LSB first, 1.5 stop bits, continuous phase
//   WfRttyTranslator                            RX audio at any rate -> the 3200 Hz,
//                                               800 Hz-centred, mark-low audio the
//                                               DeepRTTY model was trained on
//
// Conventions.  On the air the MARK tone is the higher RF frequency (the
// 1.5-stop-bit idle state).  Received on LSB (or in an Icom's RTTY / RTTY-R
// mode, which demodulates as LSB and keeps the mark at the MARK FREQUENCY
// setting) mark is the LOWER audio tone: 2125 / 2295 Hz with the usual
// settings.  Received on USB it is the HIGHER one.  The model wants mark at
// 715 Hz and space at 885 Hz, so the translator shifts the pair down to a
// centre of 800 Hz and, with `invert`, flips the spectrum around it.
(function (root) {
    'use strict';

    const BAUD = 45.45;
    const SHIFT_HZ = 170;
    const MODEL_RATE = 3200;
    const MODEL_CENTER_HZ = 800;
    const BITS_PER_CHAR = 1 + 5 + 1.5;          // start, data, stop
    const CHAR_S = BITS_PER_CHAR / BAUD;         // 165 ms

    const LTRS = 31, FIGS = 27;
    const LETTERS = { 1: 'E', 3: 'A', 5: 'S', 6: 'I', 7: 'U', 9: 'D', 10: 'R', 11: 'J', 12: 'N', 13: 'F', 14: 'C', 15: 'K', 16: 'T', 17: 'Z', 18: 'L', 19: 'W', 20: 'H', 21: 'Y', 22: 'P', 23: 'Q', 24: 'O', 25: 'B', 26: 'G', 28: 'M', 29: 'X', 30: 'V' };
    // US TTY figures case (the one fldigi / MMTTY send): 5 = BELL, 9 = $, 13 = !, 20 = #, 26 = &
    const FIGURES = { 1: '3', 3: '-', 5: '\x07', 6: '8', 7: '7', 9: '$', 10: '4', 11: '\'', 12: ',', 13: '!', 14: ':', 15: '(', 16: '5', 17: '"', 18: ')', 19: '2', 20: '#', 21: '6', 22: '0', 23: '1', 24: '9', 25: '?', 26: '&', 28: '.', 29: '/', 30: ';' };
    const BOTH = { 2: '\n', 4: ' ', 8: '\r' };

    function invert(table) {
        const out = {};
        for (const k of Object.keys(table)) out[table[k]] = Number(k);
        return out;
    }
    const LETTER_CODES = invert(LETTERS), FIGURE_CODES = invert(FIGURES), BOTH_CODES = invert(BOTH);

    // Characters the code can carry (what a macro may contain); the rest is dropped.
    function canSend(ch) {
        return LETTER_CODES[ch] !== undefined || FIGURE_CODES[ch] !== undefined || BOTH_CODES[ch] !== undefined;
    }

    // Text -> ITA2 codes with the shift characters in place.  USOS: a space
    // resets the receiver to letters, so figures after a space are re-shifted.
    function encodeIta2(text, usos) {
        if (usos === undefined) usos = true;
        const codes = [];
        let shift = null;                            // the shift the receiver is in: null = unknown
        for (const ch of String(text).toUpperCase()) {
            if (BOTH_CODES[ch] !== undefined) {
                codes.push(BOTH_CODES[ch]);
                if (ch === ' ' && usos) shift = 'letters';
            } else if (LETTER_CODES[ch] !== undefined) {
                if (shift !== 'letters') { codes.push(LTRS); shift = 'letters'; }
                codes.push(LETTER_CODES[ch]);
            } else if (FIGURE_CODES[ch] !== undefined) {
                if (shift !== 'figures') { codes.push(FIGS); shift = 'figures'; }
                codes.push(FIGURE_CODES[ch]);
            }
            // anything else: not in the code, dropped
        }
        return codes;
    }

    // Stateful ITA2 decoder: the letters / figures shift persists across calls.
    class Ita2Decoder {
        constructor(usos, shift) {
            this.usos = usos === undefined ? true : usos;
            this.shift = shift || 'letters';
        }
        clone() { return new Ita2Decoder(this.usos, this.shift); }
        decode(codes) {
            let figures = this.shift === 'figures', text = '';
            for (const code of codes) {
                if (code === LTRS) figures = false;
                else if (code === FIGS) figures = true;
                else if (code === 0) continue;                      // NUL: idle
                else if (BOTH[code] !== undefined) { text += BOTH[code]; if (code === 4 && this.usos) figures = false; }
                else text += (figures ? FIGURES : LETTERS)[code] || '';
            }
            this.shift = figures ? 'figures' : 'letters';
            return text;
        }
    }

    // ------------------------------------------------------------------
    // TX: AFSK audio for the rig's data input.
    // ------------------------------------------------------------------
    // One over: `leadS` of steady mark (the receiver's bit sync and the rig's
    // TX settling), two LTRS, the text, LTRS, `tailS` of mark.  `markLow`
    // says the mark tone is the lower one (LSB), otherwise the higher (USB).
    // Phase-continuous; bit edges land on fractional samples through an
    // accumulator so 45.45 Bd stays exact at any sample rate.
    class WfRttySynth {
        constructor(opts) {
            opts = opts || {};
            this.sampleRate = opts.sampleRate || 12000;
            this.centerHz = opts.centerHz || 2210;
            this.shiftHz = opts.shiftHz || SHIFT_HZ;
            this.baud = opts.baud || BAUD;
            this.markLow = opts.markLow !== false;
            this.usos = opts.usos !== false;
            this.amplitude = opts.amplitude || 0.8;
        }
        get markHz() { return this.centerHz + (this.markLow ? -1 : 1) * this.shiftHz / 2; }
        get spaceHz() { return this.centerHz + (this.markLow ? 1 : -1) * this.shiftHz / 2; }

        // Bits of one character: start (space), 5 data bits LSB first, 1.5 stop (mark).
        static charBits(code) {
            const bits = [[0, 1]];
            for (let i = 0; i < 5; i++) bits.push([(code >> i) & 1, 1]);
            bits.push([1, 1.5]);
            return bits;
        }

        // The bit plan of an over: [[level, bitLengths], ...]; 1 = mark.
        plan(text, leadS, tailS) {
            const bits = [];
            const lead = (leadS === undefined ? 0.4 : leadS) * this.baud, tail = (tailS === undefined ? 0.2 : tailS) * this.baud;
            if (lead > 0) bits.push([1, lead]);
            const codes = [LTRS, LTRS].concat(encodeIta2(text, this.usos), [LTRS]);
            for (const code of codes) for (const b of WfRttySynth.charBits(code)) bits.push(b);
            if (tail > 0) bits.push([1, tail]);
            return bits;
        }

        // Duration of an over in seconds.
        duration(text, leadS, tailS) {
            let bits = 0;
            for (const b of this.plan(text, leadS, tailS)) bits += b[1];
            return bits / this.baud;
        }

        // Float32 samples of one over.
        render(text, leadS, tailS) {
            const bits = this.plan(text, leadS, tailS);
            const sr = this.sampleRate, bitSamples = sr / this.baud;
            let total = 0;
            for (const b of bits) total += b[1] * bitSamples;
            const out = new Float32Array(Math.ceil(total));
            const wMark = 2 * Math.PI * this.markHz / sr, wSpace = 2 * Math.PI * this.spaceHz / sr;
            let phase = 0, edge = 0, n = 0;
            for (const [level, len] of bits) {
                edge += len * bitSamples;
                const w = level ? wMark : wSpace;
                for (; n < edge && n < out.length; n++) {
                    out[n] = this.amplitude * Math.sin(phase);
                    phase += w;
                    if (phase > 2 * Math.PI) phase -= 2 * Math.PI;
                }
            }
            // 5 ms ramps keep the key clicks off the air.
            const ramp = Math.min(Math.round(0.005 * sr), out.length >> 1);
            for (let i = 0; i < ramp; i++) { const g = i / ramp; out[i] *= g; out[out.length - 1 - i] *= g; }
            return out;
        }
    }

    // ------------------------------------------------------------------
    // RX: any-rate audio -> 3200 Hz, mark 715 / space 885.
    // ------------------------------------------------------------------
    // Mix the tone pair at `centerHz` down to DC (complex), decimate with a
    // triangular window (a box average applied twice: its nulls sit on the
    // alias bands and the noise next to them folds in 50 dB down, where a
    // single box leaves it at -27 dB and a ramp at -11 dB, which cost the
    // model a dB at -10 dB SNR) to a rate near 3200 Hz, low-pass the
    // baseband to +/- LPF_HZ with a short FIR, interpolate linearly onto
    // the 3200 Hz grid (the baseband is 10x oversampled, the error is far
    // below the noise) and move it back up to 800 Hz, which is exactly a
    // quarter of the output rate: the carrier is the sequence 1, j, -1, -j.
    // `invert` conjugates the baseband, i.e. mirrors the spectrum around
    // the centre (USB: mark is the upper tone).
    const LPF_HZ = 450;                              // flat over the model's 650..950 band (a shaped noise floor costs it dB), out by 600
    const LPF_TAPS = 63;

    class WfRttyTranslator {
        constructor(inputRate, centerHz, invert) {
            this.inputRate = inputRate;
            this.outputRate = MODEL_RATE;
            this.invert = !!invert;
            // Decimation: the biggest integer that keeps the box rate >= ~2.5x the LPF corner
            this.decim = Math.max(1, Math.floor(inputRate / 3200));
            this.midRate = inputRate / this.decim;
            this.setCenter(centerHz);
            this.lpf = WfRttyTranslator.lowpass(LPF_TAPS, LPF_HZ / this.midRate);
            this.histI = new Float32Array(LPF_TAPS); this.histQ = new Float32Array(LPF_TAPS); this.histPos = 0;
            // the mixed input, for the triangular decimator: 2*decim-1 taps
            const tri = 2 * this.decim - 1;
            this.tri = new Float32Array(tri);
            for (let k = 0; k < tri; k++) this.tri[k] = (this.decim - Math.abs(k - (this.decim - 1))) / (this.decim * this.decim);
            this.mixI = new Float32Array(tri); this.mixQ = new Float32Array(tri); this.mixPos = 0; this.count = 0;
            this.lastI = 0; this.lastQ = 0; this.havePrev = false;   // last filtered baseband sample, for the interpolator
            this.frac = 0;                                           // position of the next output sample between the last two mid-rate samples
            this.outPhase = 0;                                       // 0..3, the fs/4 carrier
            this.out = new Float32Array(0);
        }

        static lowpass(taps, fc) {                   // windowed-sinc, fc as a fraction of the sample rate
            const h = new Float32Array(taps), m = (taps - 1) / 2;
            let sum = 0;
            for (let i = 0; i < taps; i++) {
                const x = i - m;
                const sinc = x === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * x) / (Math.PI * x);
                const w = 0.54 - 0.46 * Math.cos(2 * Math.PI * i / (taps - 1));
                h[i] = sinc * w; sum += h[i];
            }
            for (let i = 0; i < taps; i++) h[i] /= sum;
            return h;
        }

        setCenter(centerHz) {
            this.centerHz = centerHz;
            this.mixStep = 2 * Math.PI * centerHz / this.inputRate;
            this.mixPhase = this.mixPhase || 0;
        }

        // Float32 or Int16 input samples -> Float32 at 3200 Hz (as many as fall due).
        process(input) {
            const scale = input instanceof Int16Array ? 1 / 32768 : 1;
            const n = input.length, decim = this.decim, step = this.midRate / this.outputRate;   // mid-rate samples per output sample
            let out = this.out;
            const need = Math.ceil(n / decim / step) + 2;
            if (out.length < need) out = this.out = new Float32Array(need);
            let produced = 0;
            const h = this.lpf, taps = LPF_TAPS, histI = this.histI, histQ = this.histQ;
            for (let i = 0; i < n; i++) {
                const x = input[i] * scale;
                const c = Math.cos(this.mixPhase), s = -Math.sin(this.mixPhase);
                this.mixPhase += this.mixStep;
                if (this.mixPhase > 2 * Math.PI) this.mixPhase -= 2 * Math.PI;
                const mixI = this.mixI, mixQ = this.mixQ, tri = this.tri, triLen = tri.length;
                mixI[this.mixPos] = x * c; mixQ[this.mixPos] = x * s;
                this.mixPos = (this.mixPos + 1) % triLen;
                if (++this.count < decim) continue;
                this.count = 0;
                // one mid-rate complex sample: the triangular window over the newest 2*decim-1 inputs
                let bi = 0, bq = 0;
                for (let k = 0, q = this.mixPos === 0 ? triLen - 1 : this.mixPos - 1; k < triLen; k++) {
                    bi += tri[k] * mixI[q]; bq += tri[k] * mixQ[q];
                    q = q === 0 ? triLen - 1 : q - 1;
                }
                // FIR
                histI[this.histPos] = bi; histQ[this.histPos] = bq;
                let fi = 0, fq = 0, p = this.histPos;
                for (let k = 0; k < taps; k++) { fi += h[k] * histI[p]; fq += h[k] * histQ[p]; p = p === 0 ? taps - 1 : p - 1; }
                this.histPos = (this.histPos + 1) % taps;
                if (!this.havePrev) { this.lastI = fi; this.lastQ = fq; this.havePrev = true; continue; }
                // outputs due between the previous mid sample (frac = 0) and this one (frac = 1)
                while (this.frac < 1) {
                    const ii = this.lastI + (fi - this.lastI) * this.frac;
                    const qq = (this.lastQ + (fq - this.lastQ) * this.frac) * (this.invert ? -1 : 1);
                    // Re((I + jQ) * j^phase)
                    let y;
                    switch (this.outPhase) { case 0: y = ii; break; case 1: y = -qq; break; case 2: y = -ii; break; default: y = qq; }
                    this.outPhase = (this.outPhase + 1) & 3;
                    out[produced++] = y;
                    this.frac += step;
                }
                this.frac -= 1;
                this.lastI = fi; this.lastQ = fq;
            }
            return out.subarray(0, produced);
        }

        // Forget the past (after a retune): the next output starts clean.
        reset() {
            this.histI.fill(0); this.histQ.fill(0);
            this.mixI.fill(0); this.mixQ.fill(0); this.count = 0;
            this.havePrev = false; this.frac = 0;
        }
    }

    root.WfRtty = {
        BAUD, SHIFT_HZ, MODEL_RATE, MODEL_CENTER_HZ, CHAR_S, BITS_PER_CHAR,
        LTRS, FIGS, LETTERS, FIGURES, BOTH,
        canSend, encodeIta2, Ita2Decoder, WfRttySynth, WfRttyTranslator,
    };
})(typeof self !== 'undefined' ? self : globalThis);
