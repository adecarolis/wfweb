// CTCSS tone detector — the engine behind the TONE panel's Scan button.
//
// CI-V has no tone-scan command on any Icom, but the received FM audio still
// carries the repeater's sub-tone, so a Goertzel bank tuned to the standard
// tones simply reads it off. Nothing is asked of the radio: no tone squelch is
// engaged, no register is written, and the receiver never goes quiet.
//
// Each ~1 s Hann-windowed block (50% overlap) is measured at every tone. A
// block nominates a candidate only when its strongest bin clears the median
// bin by a wide dominance margin (rejects broadband noise) and the runner-up
// by a smaller one (rejects speech: voiced fundamentals sit in the same
// 67-254 Hz band but wobble, smearing energy across neighbouring bins, while a
// CTCSS tone is rock-stable). The same tone must win several consecutive
// blocks before it is declared.
//
// Harmonics need care. Off-air CTCSS is never the clean sine the encoder made:
// the rig's audio high-pass tilts against the fundamental while the second
// harmonic sails through — measured on an IC-7300, 67-110 Hz sits ~50 dB below
// the 500-2500 Hz reference while 220-280 Hz is only ~5 dB down. The tone
// table is close enough to geometric that 2x (and 3x) of a low tone lands
// within a bin of a real higher entry (114.8 x 2 = 229.6 ~ 229.1). So a
// winning bin sitting on a harmonic of a table tone that shows credible energy
// of its own is reported as that fundamental, and bins harmonically tied to
// the winner are excluded from the runner-up when measuring stability —
// otherwise a tone's own harmonic vetoes its detection.
//
// Ported from the same detector in FM-Remote (K1FM), which is where the
// harmonic-folding behaviour was worked out against live repeaters.
(function (global) {
    'use strict';

    // The standard tones in tenths of a Hz, used when the rig's own table
    // isn't available. Rigs supply theirs through the caps message.
    var STANDARD_TONES = [
        670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035,
        1072, 1109, 1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567,
        1598, 1622, 1655, 1679, 1738, 1773, 1799, 1835, 1862, 1928, 1966, 1995,
        2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
    ];

    // A table entry counts as `fund`'s harmonic when it sits within a bin's
    // reach of 2x or 3x (tenths of Hz; the ~1 s Hann mainlobe spans about
    // +/-2 Hz).
    function harmonicallyTied(fund, harm) {
        return Math.abs(2 * fund - harm) <= 25 || Math.abs(3 * fund - harm) <= 38;
    }

    // A fundamental must clear the median bin by 6 dB before a harmonic winner
    // is folded down onto it.
    var MIN_FUNDAMENTAL_RATIO = Math.pow(10, 0.6);

    function CtcssDetector(opts) {
        opts = opts || {};
        this.tones = (opts.tones && opts.tones.length) ? opts.tones.slice() : STANDARD_TONES.slice();
        this.minDominanceDb = opts.minDominanceDb !== undefined ? opts.minDominanceDb : 14;
        this.minMarginDb = opts.minMarginDb !== undefined ? opts.minMarginDb : 6;
        this.confirmations = opts.confirmations !== undefined ? opts.confirmations : 3;

        var rate = opts.sampleRate || 48000;
        // Everything of interest is under 260 Hz, so the bank runs on audio
        // decimated to ~4 kHz: a 48 kHz block would be 49k samples across 48
        // bins, and that much work on the audio callback glitches playback.
        // Two cascaded box averages (a 2nd-order CIC) put nulls on the fold
        // centres; the droop at 254 Hz is a fraction of a dB.
        this.decim = Math.max(1, Math.round(rate / 4000));
        this.rate = rate / this.decim;
        this.blockSize = Math.round(this.rate * 1.024);
        this.hopSize = this.blockSize >> 1;

        this.hann = new Float64Array(this.blockSize);
        for (var i = 0; i < this.blockSize; i++) {
            this.hann[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (this.blockSize - 1));
        }
        this.coeffs = new Float64Array(this.tones.length);
        for (var t = 0; t < this.tones.length; t++) {
            this.coeffs[t] = 2 * Math.cos(2 * Math.PI * (this.tones[t] / 10) / this.rate);
        }
        this.buf = new Float64Array(this.blockSize * 2);
        this.reset();
    }

    // Forget the confirmation streak and any pending audio — a fresh scan.
    CtcssDetector.prototype.reset = function () {
        this.have = 0;
        this.streakTone = 0;
        this.streakLen = 0;
        this.detected = null;
        this._acc1 = 0; this._acc2 = 0; this._n = 0; this._prev = 0;
    };

    // Feed Int16 RX PCM. Returns the events the newly completed blocks
    // concluded: {type:'window', ...} for every analysed block so the panel can
    // show a live best guess, and {type:'detected', tone} once confirmed.
    CtcssDetector.prototype.feed = function (pcm) {
        if (this.detected !== null) return [];
        var events = [];
        for (var i = 0; i < pcm.length; i++) {
            // 2nd-order CIC: two cascaded running means over `decim` samples.
            this._acc1 += pcm[i];
            if (++this._n >= this.decim) {
                var s = this._acc1 / this.decim;
                this._acc1 = 0; this._n = 0;
                var y = (s + this._prev) * 0.5;   // second stage
                this._prev = s;
                if (this.have < this.buf.length) this.buf[this.have++] = y;
                if (this.have >= this.blockSize) {
                    var ev = this._analyze();
                    if (ev) events.push(ev);
                    this.buf.copyWithin(0, this.hopSize, this.have);
                    this.have -= this.hopSize;
                    if (this.detected !== null) return events;
                }
            }
        }
        return events;
    };

    CtcssDetector.prototype._analyze = function () {
        var n = this.blockSize, i, t;
        // Silence (closed squelch, stream gap) carries no tone — skip the
        // block and restart confirmation, so chopped audio can't stitch a
        // detection together out of unrelated transmissions.
        var sumSq = 0;
        for (i = 0; i < n; i++) sumSq += this.buf[i] * this.buf[i];
        if (sumSq / n <= 4) { this.streakLen = 0; return null; }

        var powers = new Float64Array(this.tones.length);
        for (t = 0; t < this.coeffs.length; t++) {
            var c = this.coeffs[t], s1 = 0, s2 = 0;
            for (i = 0; i < n; i++) {
                var s0 = this.buf[i] * this.hann[i] + c * s1 - s2;
                s2 = s1; s1 = s0;
            }
            powers[t] = s1 * s1 + s2 * s2 - c * s1 * s2;
        }

        var sorted = Array.prototype.slice.call(powers).sort(function (a, b) { return a - b; });
        var median = Math.max(sorted[sorted.length >> 1], Number.MIN_VALUE);
        var best = 0;
        for (t = 1; t < powers.length; t++) if (powers[t] > powers[best]) best = t;

        // A winner sitting on the 2nd/3rd harmonic of a table tone that shows
        // credible energy of its own is that tone's harmonic riding the rig's
        // high-pass tilt — report the fundamental instead. A genuine high tone
        // never folds: its subharmonic bin holds only noise, below the bar.
        var reported = best, fundPower = 0;
        for (t = 0; t < this.tones.length; t++) {
            if (t === best) continue;
            if (!harmonicallyTied(this.tones[t], this.tones[best])) continue;
            if (powers[t] <= fundPower) continue;
            if (powers[t] < median * MIN_FUNDAMENTAL_RATIO) continue;
            fundPower = powers[t];
            reported = t;
        }

        // Stability: strongest bin over the runner-up — but a tone's own
        // harmonics don't count as competition, or 114.8's energy at 229.1
        // vetoes 114.8 itself.
        var runnerUp = Number.MIN_VALUE;
        for (t = 0; t < powers.length; t++) {
            if (t === best || t === reported) continue;
            if (harmonicallyTied(this.tones[reported], this.tones[t])) continue;
            if (powers[t] > runnerUp) runnerUp = powers[t];
        }

        var dominanceDb = 10 * Math.log10(powers[best] / median);
        var marginDb = 10 * Math.log10(powers[best] / runnerUp);
        var tone = this.tones[reported];
        var qualifies = dominanceDb >= this.minDominanceDb && marginDb >= this.minMarginDb;

        if (qualifies && tone === this.streakTone) {
            this.streakLen++;
        } else {
            this.streakTone = tone;
            this.streakLen = qualifies ? 1 : 0;
        }
        if (this.streakLen >= this.confirmations) {
            this.detected = tone;
            return { type: 'detected', tone: tone };
        }
        return {
            type: 'window', tone: tone, qualifies: qualifies,
            dominanceDb: dominanceDb, marginDb: marginDb,
        };
    };

    global.CtcssDetector = CtcssDetector;
    global.CTCSS_STANDARD_TONES = STANDARD_TONES;
})(typeof window !== 'undefined' ? window : globalThis);
