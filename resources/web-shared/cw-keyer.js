// CW keyer model for the CW bar, shared by both builds (#141).
//
// The radio's own memory keyer does the keying (CI-V 0x17); this module keeps
// the page's model of what it handed the keyer and when each element goes
// out.  Three things hang off that model and must agree with each other and
// with the radio:
//   - the transcript highlight (pending -> sending -> sent per character),
//   - the pacing of the text to the radio (frames of at most frameChars
//     characters, each handed over when the keyer has about leadChars left,
//     so its finite buffer never overflows and never runs dry), and
//   - the browser sidetone (WfCwSidetone), keyed from the same element times.
// Timing follows the Icom keyer: 1:3 dot/dash, one dot between elements,
// three between characters, seven between words (the three-dot character gap
// counted with the character it follows, a space adding the other four).
// PARIS at W words per minute takes exactly 60000 / W ms.
//
// The clock, the timers, the rig transport and the audio context are passed
// in, so tests/cw_keyer_sim.mjs drives the whole thing under node with a
// virtual clock and a fake AudioContext.  No DOM in here.
(function (global) {
    'use strict';

    // Everything the Icom keyer accepts (icomcommander.cpp funcSendCW, minus
    // '*' = contest serial, which has no fixed length).  A character that is
    // not in here is dropped before it reaches the model or the radio, so the
    // page, the server and the rig stay in step (a lone '!' used to be
    // modelled as nothing and keyed as '?').  Prosigns are typed <AR> and
    // count as one unit; cwToRig() rewrites them to the keyer's ^AR form.
    var CW_MORSE = {
        'A': '.-', 'B': '-...', 'C': '-.-.', 'D': '-..', 'E': '.', 'F': '..-.',
        'G': '--.', 'H': '....', 'I': '..', 'J': '.---', 'K': '-.-', 'L': '.-..',
        'M': '--', 'N': '-.', 'O': '---', 'P': '.--.', 'Q': '--.-', 'R': '.-.',
        'S': '...', 'T': '-', 'U': '..-', 'V': '...-', 'W': '.--', 'X': '-..-',
        'Y': '-.--', 'Z': '--..', '0': '-----', '1': '.----', '2': '..---',
        '3': '...--', '4': '....-', '5': '.....', '6': '-....', '7': '--...',
        '8': '---..', '9': '----.', '.': '.-.-.-', ',': '--..--', '?': '..--..',
        '/': '-..-.', '=': '-...-', '+': '.-.-.', '-': '-....-', ':': '---...',
        '\'': '.----.', '"': '.-..-.', '(': '-.--.', ')': '-.--.-', '@': '.--.-.',
        ' ': ' ',
        '<AR>': '.-.-.', '<AS>': '.-...', '<BK>': '-...-.-', '<BT>': '-...-',
        '<CT>': '-.-.-', '<HH>': '........', '<KN>': '-.--.', '<SK>': '...-.-',
        '<SN>': '...-.'
    };

    // Split text into transcript units: a known <XX> prosign is one unit,
    // every other character is its own.
    function cwTokenize(text) {
        var out = [];
        var re = /<[A-Z]{2}>/g;
        var last = 0;
        var m;
        while ((m = re.exec(text)) !== null) {
            if (!CW_MORSE[m[0]]) continue;   // unknown <XX>: falls through as plain chars
            for (var i = last; i < m.index; i++) out.push(text[i]);
            out.push(m[0]);
            last = re.lastIndex;
        }
        for (var j = last; j < text.length; j++) out.push(text[j]);
        return out;
    }

    // Rewrite <XX> prosigns into the "^XX" form the Icom keyer understands.
    function cwToRig(text) {
        return text.replace(/<([A-Z]{2})>/g, function (whole, pair) {
            return CW_MORSE[whole] ? '^' + pair : whole;
        });
    }

    // Model length of one unit in ms.  A space after a character adds only
    // the four dots the character gap does not already cover.
    function cwTokenDuration(char, prev, dot) {
        if (char === ' ') return (prev && prev !== ' ') ? 4 * dot : 7 * dot;
        var pattern = CW_MORSE[char];
        if (!pattern) return 0;
        var ms = 0;
        for (var j = 0; j < pattern.length; j++) ms += (pattern[j] === '.' ? 1 : 3) * dot + dot;
        return ms + 2 * dot;   // the last element's dot gap plus two more = character gap
    }

    // ------------------------------------------------------------------
    // Sidetone: one sine oscillator through an envelope gain with a 5 ms
    // attack and release per element, in an AudioContext of its own so it
    // never enters the RX chain (the CW decoder and the recorder tap the RX
    // PCM and must not hear it).  Elements come in wall-clock ms and are
    // moved onto the audio clock; the oscillator is parked once the furthest
    // scheduled element is out.
    // ------------------------------------------------------------------
    var CW_TONE_RAMP = 0.005;

    function WfCwSidetone(opts) {
        this.enabled = !!opts.enabled;
        this._createContext = opts.createContext;   // () -> AudioContext
        this._pitch = opts.pitch;                   // () -> Hz
        this._gain = opts.gain;                     // () -> 0..1
        this._now = opts.now || function () { return Date.now(); };
        this._setTimeout = opts.setTimeout || function (f, ms) { return setTimeout(f, ms); };
        this._clearTimeout = opts.clearTimeout || function (t) { clearTimeout(t); };
        this._onLatency = opts.onLatency || function () {};   // (ms) once a new context is playing
        this.ctx = null; this.master = null;
        this.osc = null; this.env = null;
        this._idleTimer = null;
        this._end = 0;        // audio time of the furthest element scheduled
        this._held = false;   // straight key down (keyDown / keyUp)
        this._resetPending = false;
    }

    // What the operator hears the tone behind its keying, in ms, or null.
    WfCwSidetone.prototype.latencyMs = function () {
        if (!this.ctx) return null;
        return Math.round(((this.ctx.baseLatency || 0) + (this.ctx.outputLatency || 0)) * 1000);
    };

    // The audio output changed (AirPods on or off, another default device).
    // A context keeps the latency of the device it opened on -- 168 ms after
    // AirPods, while a fresh one on the Mac speakers had 32 ms (2026-10-25)
    // -- so build a new one on the new device straight away, which also
    // measures it (onLatency) without waiting for the next element.  Never
    // mid-tone: while keying, the old context is replaced when it parks.
    WfCwSidetone.prototype.reset = function () {
        if (!this.ctx) return;
        if (this._held || (this.osc && this._end > this.ctx.currentTime)) { this._resetPending = true; return; }
        this._close();
        if (this.enabled) this._ensureContext();
    };

    WfCwSidetone.prototype._close = function () {
        if (this._idleTimer !== null) { this._clearTimeout(this._idleTimer); this._idleTimer = null; }
        if (this.osc) {
            try { this.osc.stop(); } catch (e) {}
            this.osc.disconnect();
            this.env.disconnect();
        }
        if (this.master) this.master.disconnect();
        try { if (this.ctx.close) this.ctx.close(); } catch (e) {}
        this.ctx = null; this.master = null;
        this.osc = null; this.env = null;
        this._end = 0;
        this._resetPending = false;
    };

    // Make (or wake) the audio graph.  Called from a keystroke or a tap, so
    // the context has its user activation.
    // The context and its master gain, made on first use or after a reset.
    WfCwSidetone.prototype._ensureContext = function () {
        var self = this;
        if (this.ctx) return true;
        try { this.ctx = this._createContext(); } catch (e) { this.ctx = null; }
        if (!this.ctx) return false;
        this.master = this.ctx.createGain();
        this.master.connect(this.ctx.destination);
        if (this.ctx.state === 'suspended' && this.ctx.resume) this.ctx.resume();
        // outputLatency is only known once the context plays: report it
        // then, so a slow output (Bluetooth) can be flagged.
        var ctx = this.ctx;
        this._setTimeout(function () { if (self.ctx === ctx) self._onLatency(self.latencyMs()); }, 1000);
        return true;
    };

    WfCwSidetone.prototype._ensure = function () {
        if (!this._ensureContext()) return false;
        this.master.gain.value = this._gain();
        if (this.ctx.state === 'suspended' && this.ctx.resume) this.ctx.resume();
        if (!this.osc) {
            this.env = this.ctx.createGain();
            this.env.gain.value = 0;
            this.env.connect(this.master);
            this.osc = this.ctx.createOscillator();
            this.osc.type = 'sine';
            this.osc.frequency.value = this._pitch();
            this.osc.connect(this.env);
            this.osc.start();
        }
        if (this._idleTimer !== null) { this._clearTimeout(this._idleTimer); this._idleTimer = null; }
        return true;
    };

    // Park the oscillator half a second after the furthest scheduled element
    // (not just the last call's: a typed space schedules nothing, and parking
    // then would throw away the characters still pending).
    WfCwSidetone.prototype._idle = function () {
        var self = this;
        if (this._idleTimer !== null) { this._clearTimeout(this._idleTimer); this._idleTimer = null; }
        if (this._held) return;
        var afterMs = Math.max(0, (this._end - this.ctx.currentTime) * 1000) + 500;
        this._idleTimer = this._setTimeout(function () {
            self._idleTimer = null;
            if (self.osc) {
                try { self.osc.stop(); } catch (e) {}
                self.osc.disconnect();
                self.env.disconnect();
            }
            self.osc = null;
            self.env = null;
            if (self._resetPending) { self._close(); if (self.enabled) self._ensureContext(); }
        }, afterMs);
    };

    // Key the given elements ([{on, off}] in wall-clock ms).  Elements
    // already past are skipped, so this can also catch up mid-transmission.
    WfCwSidetone.prototype.schedule = function (elements) {
        if (!this.enabled || !elements.length) return;
        if (!this._ensure()) return;
        var now = this.ctx.currentTime;
        var base = now - this._now() / 1000;   // audio time of wall-clock 0
        var g = this.env.gain;
        if (this._end < now) this._end = now;
        for (var i = 0; i < elements.length; i++) {
            var on = Math.max(base + elements[i].on / 1000, now);
            var off = base + elements[i].off / 1000;
            if (off - CW_TONE_RAMP <= on + CW_TONE_RAMP) continue;
            g.setValueAtTime(0, on);
            g.linearRampToValueAtTime(1, on + CW_TONE_RAMP);
            g.setValueAtTime(1, off - CW_TONE_RAMP);
            g.linearRampToValueAtTime(0, off);
            if (off > this._end) this._end = off;
        }
        this._idle();
    };

    // Cut the tone now: drop what was scheduled and release in one ramp.
    WfCwSidetone.prototype.stop = function () {
        if (!this.ctx || !this.env) return;
        var now = this.ctx.currentTime;
        var g = this.env.gain;
        if (g.cancelAndHoldAtTime) g.cancelAndHoldAtTime(now);
        else { g.cancelScheduledValues(now); g.setValueAtTime(g.value, now); }
        g.linearRampToValueAtTime(0, now + CW_TONE_RAMP);
        this._end = now;
        this._held = false;
        this._idle();
    };

    // Straight key (cw-paddle.js): an open-ended tone, keyed down now and
    // released by keyUp().  Same envelope; parked by _idle once released.
    WfCwSidetone.prototype.keyDown = function () {
        if (!this.enabled || !this._ensure()) return;
        var now = this.ctx.currentTime, g = this.env.gain;
        if (g.cancelAndHoldAtTime) g.cancelAndHoldAtTime(now);
        else { g.cancelScheduledValues(now); g.setValueAtTime(g.value, now); }
        g.linearRampToValueAtTime(1, now + CW_TONE_RAMP);
        if (now > this._end) this._end = now;
        this._held = true;      // never park while the key is held
    };

    WfCwSidetone.prototype.keyUp = function () {
        if (!this.ctx || !this.env) return;
        var now = this.ctx.currentTime, g = this.env.gain;
        if (g.cancelAndHoldAtTime) g.cancelAndHoldAtTime(now);
        else { g.cancelScheduledValues(now); g.setValueAtTime(g.value, now); }
        g.linearRampToValueAtTime(0, now + CW_TONE_RAMP);
        this._held = false;
        if (now + CW_TONE_RAMP > this._end) this._end = now + CW_TONE_RAMP;
        this._idle();
    };

    // One dit, so a tap on the button is heard.
    WfCwSidetone.prototype.preview = function () {
        if (!this.enabled) return;
        if (this.ctx && this._end > this.ctx.currentTime + 0.02) return;   // keying: audible already
        if (!this._ensure()) return;
        var g = this.env.gain, on = this.ctx.currentTime + 0.01, off = on + 0.08;
        g.setValueAtTime(0, on);
        g.linearRampToValueAtTime(1, on + CW_TONE_RAMP);
        g.setValueAtTime(1, off - CW_TONE_RAMP);
        g.linearRampToValueAtTime(0, off);
        if (off > this._end) this._end = off;
        this._idle();
    };

    WfCwSidetone.prototype.setEnabled = function (on, elements) {
        this.enabled = !!on;
        if (this.enabled) { if (elements) this.schedule(elements); this.preview(); }
        else { this.stop(); this.reset(); }   // TONE off / on also rebuilds the audio
    };

    // The rig's CW pitch changed: retune a running tone too.
    WfCwSidetone.prototype.pitchChanged = function () {
        if (this.osc) this.osc.frequency.setTargetAtTime(this._pitch(), this.ctx.currentTime, 0.01);
    };

    // VOL moved: follow it.
    WfCwSidetone.prototype.gainChanged = function () {
        if (this.master) this.master.gain.setTargetAtTime(this._gain(), this.ctx.currentTime, 0.01);
    };

    // ------------------------------------------------------------------
    // The length of a dot.
    // ------------------------------------------------------------------
    // The Icom keyer does not key 1200 / WPM ms: measured off the air on an
    // IC-7300 (tests/cw_rig_capture.mjs, 2026-10-17), every dot is an odd
    // number of 1/6 ms ticks, up to 1.3 % away from the textbook value and
    // not monotonic in the setting (30 WPM keys 39.5 ms, 20 WPM 60.17 ms).
    // Over a long sentence the textbook model drifted a quarter second from
    // the radio in either direction, depending on the speed (#141).  Ticks
    // per dot for 6..48 WPM, the range of the 0x14 0x0C command; the dash is
    // three dots and the gaps 1 / 3 / 7 dots, as modelled.
    var CW_ICOM_DOT_TICKS = [
        1201, 1029, 901, 801, 723, 657, 603, 549, 511, 477,
        447, 421, 399, 379, 361, 343, 327, 313, 301, 289,
        277, 265, 255, 247, 237, 231, 223, 217, 211, 205,
        199, 193, 189, 183, 181, 175, 171, 165, 163, 159,
        157, 153, 151,
    ];
    var CW_ICOM_DOT_TICKS_WPM0 = 6;

    // Dot length in ms for a keyer ('icom' or anything else) at a speed.
    function cwDotMs(keyer, wpm) {
        if (keyer === 'icom') {
            var i = Math.round(wpm) - CW_ICOM_DOT_TICKS_WPM0;
            if (i >= 0 && i < CW_ICOM_DOT_TICKS.length) return CW_ICOM_DOT_TICKS[i] / 6;
        }
        return 1200 / wpm;
    }

    // ------------------------------------------------------------------
    // The keyer model.
    // ------------------------------------------------------------------
    // A 30-byte frame is accepted on an empty keyer, so the keyer buffer holds
    // at least 30 (#141).  Frames of at most frameChars bytes handed over when
    // leadChars units are left keep the buffer under 30 with room for a few
    // prosigns (3 bytes each) in the lead, whatever its real size, and never
    // let it run dry.
    var CW_RIG_FRAME_CHARS = 20;
    var CW_RIG_LEAD_CHARS = 6;

    function WfCwKeyer(opts) {
        this._wpm = opts.wpm;                       // () -> words per minute
        this._keyer = opts.keyer || function () { return 'ideal'; };   // () -> 'icom' | 'ideal' (cwDotMs)
        this._send = opts.send;                     // (text) -> hand a frame to the rig
        this._stopRig = opts.stopRig;               // () -> cut the rig's keyer buffer
        this._onChange = opts.onChange || function () {};
        this._now = opts.now || function () { return Date.now(); };
        this._setTimeout = opts.setTimeout || function (f, ms) { return setTimeout(f, ms); };
        this._clearTimeout = opts.clearTimeout || function (t) { clearTimeout(t); };
        this.sidetone = opts.sidetone || null;      // WfCwSidetone, optional
        this.frameChars = opts.frameChars || CW_RIG_FRAME_CHARS;
        this.leadChars = opts.leadChars || CW_RIG_LEAD_CHARS;

        this.characters = [];   // [{char, status: 'pending'|'sending'|'sent', startTime, groupIndex}], mutated in place
        this.nextTime = 0;      // model ms where the next text starts
        this.base = 0;          // wall-clock ms of model 0
        this._timers = [];
        this._queue = [];       // [{text, at, first, end}] frames not yet handed to the rig
        this._queueTimer = null;
        this._rigEnd = 0;       // model index just past the last character queued for the rig
    }

    WfCwKeyer.prototype.dot = function () { return cwDotMs(this._keyer(), this._wpm()); };

    // Model time of a wall-clock instant.
    WfCwKeyer.prototype.modelNow = function () { return this._now() - this.base; };

    // Append text to the model and hand it to the rig.  Returns the number
    // of units added (0 when nothing in the text can be keyed).
    // opts.tone === false: the operator already heard it (paddle text from
    // cw-paddle.js, keyed live by its own sidetone) -- no tone for these
    // units, now or when TONE is switched on mid-way.
    WfCwKeyer.prototype.sendText = function (text, opts) {
        var noTone = !!(opts && opts.tone === false);
        if (!text) return 0;
        var tokens = cwTokenize(text.toUpperCase()).filter(function (t) { return !!CW_MORSE[t]; });
        if (!tokens.length) return 0;
        var dot = this.dot();
        var startIndex = this.characters.length;
        var modelStart = this.nextTime;
        var now = this._now();

        // Wall-clock base of the model.  First text, or the keyer went idle
        // (everything so far has been keyed): the new text starts now.
        if (startIndex === 0 || this.base + modelStart < now) this.base = now - modelStart;

        var t = modelStart;
        var prev = startIndex > 0 ? this.characters[startIndex - 1].char : null;
        for (var i = 0; i < tokens.length; i++) {
            var c = { char: tokens[i], status: 'pending', startTime: t, groupIndex: startIndex };
            if (noTone) c.noTone = true;
            this.characters.push(c);
            t += cwTokenDuration(tokens[i], prev, dot);
            prev = tokens[i];
        }
        this.nextTime = t;

        this._scheduleStatus(startIndex);
        this._queueFrames(startIndex, tokens);
        if (this.sidetone && this.sidetone.enabled && !noTone) this.sidetone.schedule(this.elements(startIndex));
        this._onChange();
        return tokens.length;
    };

    // Transcript status timers for the units from startIndex on.
    WfCwKeyer.prototype._scheduleStatus = function (startIndex) {
        var self = this;
        var dot = this.dot();
        var elapsed = this._now() - this.base;
        for (var i = startIndex; i < this.characters.length; i++) {
            (function (c, prev) {
                var start = c.startTime - elapsed;
                var dur = cwTokenDuration(c.char, prev, dot);
                self._timers.push(self._setTimeout(function () {
                    if (c.status === 'pending') { c.status = 'sending'; self._onChange(); }
                }, Math.max(0, start)));
                self._timers.push(self._setTimeout(function () {
                    if (c.status === 'sending') { c.status = 'sent'; self._onChange(); }
                }, Math.max(0, start + dur)));
            })(this.characters[i], i > 0 ? this.characters[i - 1].char : null);
        }
    };

    // Cut the new units into frames of at most frameChars (a prosign never
    // split) and queue each for the moment the keyer has leadChars left.
    WfCwKeyer.prototype._queueFrames = function (startIndex, tokens) {
        var frames = [];
        var cur = '', curFirst = startIndex;
        for (var i = 0; i < tokens.length; i++) {
            var rigTok = cwToRig(tokens[i]);
            if (cur && cur.length + rigTok.length > this.frameChars) {
                frames.push({ text: cur, first: curFirst, end: startIndex + i });
                cur = ''; curFirst = startIndex + i;
            }
            cur += rigTok;
        }
        if (cur) frames.push({ text: cur, first: curFirst, end: startIndex + tokens.length });
        var at = this._queue.length ? this._queue[this._queue.length - 1].at : 0;
        for (var f = 0; f < frames.length; f++) {
            // Due when the keyer is down to leadChars of what was queued before.
            var leadTok = Math.max(0, this._rigEnd - this.leadChars);
            at = Math.max(at, this.base + this.characters[leadTok].startTime);
            frames[f].at = at;
            this._queue.push(frames[f]);
            this._rigEnd = frames[f].end;
        }
        this._pump();
    };

    WfCwKeyer.prototype._pump = function () {
        var self = this;
        if (this._queueTimer !== null) { this._clearTimeout(this._queueTimer); this._queueTimer = null; }
        while (this._queue.length) {
            var wait = this._queue[0].at - this._now();
            if (wait > 0) { this._queueTimer = this._setTimeout(function () { self._queueTimer = null; self._pump(); }, wait); return; }
            this._send(this._queue.shift().text);
        }
    };

    // Elements ([{on, off}] in wall-clock ms) of the units from startIndex on.
    WfCwKeyer.prototype.elements = function (startIndex) {
        var dot = this.dot();
        var out = [];
        for (var i = startIndex || 0; i < this.characters.length; i++) {
            if (this.characters[i].noTone) continue;
            var pattern = CW_MORSE[this.characters[i].char];
            if (!pattern || pattern === ' ') continue;
            var t = this.base + this.characters[i].startTime;
            for (var j = 0; j < pattern.length; j++) {
                var len = (pattern[j] === '.' ? 1 : 3) * dot;
                out.push({ on: t, off: t + len });
                t += len + dot;
            }
        }
        return out;
    };

    // Drop the frames not yet handed over, cut the rig's buffer and the tone.
    WfCwKeyer.prototype._cutRig = function () {
        this._queue = [];
        if (this._queueTimer !== null) { this._clearTimeout(this._queueTimer); this._queueTimer = null; }
        this._stopRig();
        if (this.sidetone) this.sidetone.stop();
    };

    WfCwKeyer.prototype._clearTimers = function () {
        for (var i = 0; i < this._timers.length; i++) this._clearTimeout(this._timers[i]);
        this._timers = [];
    };

    // STOP / close: nothing keyed, nothing shown.
    WfCwKeyer.prototype.stop = function () {
        this._cutRig();
        this._clearTimers();
        this.characters.length = 0;
        this.nextTime = 0;
        this.base = 0;
        this._rigEnd = 0;
        this._onChange();
    };

    // Stop keying now and keep what was sent (Escape, Backspace in CHR mode):
    // cut the rig and the tone, drop everything pending, then rewind the
    // model clock to now so the next text (and its tone and highlight)
    // starts at once, like the rig does.
    WfCwKeyer.prototype.cut = function () {
        this._cutRig();
        this._clearTimers();
        var kept = this.characters.filter(function (c) { return c.status !== 'pending'; });
        this.characters.length = 0;
        for (var k = 0; k < kept.length; k++) {
            if (kept[k].status === 'sending') kept[k].status = 'sent';   // cut mid-character
            this.characters.push(kept[k]);
        }
        this.nextTime = this.characters.length ? this.modelNow() : 0;
        this._rigEnd = this.characters.length;
        this._onChange();
    };

    // Backspace: cut() when something is pending, else nothing.  The
    // characters already handed to the keyer cannot be taken back.
    WfCwKeyer.prototype.backspace = function () {
        for (var i = 0; i < this.characters.length; i++) {
            if (this.characters[i].status === 'pending') { this.cut(); return true; }
        }
        return false;
    };

    // The text still to be keyed, for the recorder and the tests.
    WfCwKeyer.prototype.pendingText = function () {
        return this.characters.filter(function (c) { return c.status !== 'sent'; }).map(function (c) { return c.char; }).join('');
    };

    global.CW_MORSE = CW_MORSE;
    global.cwTokenize = cwTokenize;
    global.cwToRig = cwToRig;
    global.cwTokenDuration = cwTokenDuration;
    global.cwDotMs = cwDotMs;
    global.CW_ICOM_DOT_TICKS = CW_ICOM_DOT_TICKS;
    global.WfCwSidetone = WfCwSidetone;
    global.WfCwKeyer = WfCwKeyer;
})(typeof window !== 'undefined' ? window : this);
