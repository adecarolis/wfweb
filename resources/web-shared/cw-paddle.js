// Paddle / straight key as CW text entry, shared by both builds.
//
// Icom CI-V has no element-level key-down / key-up, so a paddle cannot key
// the radio directly.  Instead the paddle is a keyboard: a browser iambic
// keyer (or the operator's straight key) is heard at once through the CW
// bar's sidetone (WfCwSidetone, cw-keyer.js), its elements are decoded into
// characters, and each finished character goes to WfCwKeyer.sendText() --
// the same path as typing in CHR mode -- so the radio's own keyer sends it
// with clean timing, one character behind the fist by design.
// The radio's own sidetone, coming back in the RX audio, is that late echo,
// so the RX audio is cut while the paddle sends (WfCwRxMute).
//
// Gaps follow the keyer model: one dot between elements, three between
// characters, seven between words.  A character is closed after a gap of
// charGap dots (2, half-way between 1 and 3) and a word after wordGap (5,
// half-way between 3 and 7).
//
// Loaded after cw-keyer.js (needs CW_MORSE).  The clock and the timers are
// passed in, so tests/cw_paddle_sim.mjs drives it under node.  No DOM.
(function (global) {
    'use strict';

    // Pattern -> character.  Plain characters come first in CW_MORSE, so a
    // pattern shared with a prosign ('+' / <AR>, '=' / <BT>, '(' / <KN>)
    // decodes to the plain character; the radio keys the same elements.
    var cwPatternMap = null;
    function cwFromPattern(pattern) {
        if (!cwPatternMap) {
            cwPatternMap = {};
            var M = global.CW_MORSE;
            for (var ch in M) {
                if (ch === ' ') continue;
                if (!(M[ch] in cwPatternMap)) cwPatternMap[M[ch]] = ch;
            }
        }
        return cwPatternMap[pattern] || null;
    }

    // A keyed pattern that is no character still goes out: a wrong character
    // keeps the rhythm of the transmission, sending nothing left a hole in
    // it.  Its longest known head is sent; what is left after it is
    //   - one element: a slip, dropped (...... -> 5);
    //   - more: two characters run together, decoded the same way
    //     (..-.-- -> F M, so -.- .---- ..-.-- still sends K1FM).
    // Every pattern ends at E or T at worst, so something is always sent.
    function cwSplitPattern(pattern) {
        var out = [];
        while (pattern) {
            var ch = cwFromPattern(pattern);
            if (ch) { out.push(ch); break; }
            var head = pattern.slice(0, -1);
            while (!cwFromPattern(head)) head = head.slice(0, -1);
            out.push(cwFromPattern(head));
            pattern = pattern.slice(head.length);
            if (pattern.length < 2) break;   // a single extra element: dropped
        }
        return out;
    }

    function timers(opts, self) {
        self._now = opts.now || function () { return Date.now(); };
        self._setTimeout = opts.setTimeout || function (f, ms) { return setTimeout(f, ms); };
        self._clearTimeout = opts.clearTimeout || function (t) { clearTimeout(t); };
    }

    // ------------------------------------------------------------------
    // Elements -> characters.  element() is called with each element's
    // wall-clock on / off times (off may be in the future for the iambic
    // keyer, which knows it ahead); keyDown() says a new element has begun
    // before its length is known (straight key).
    // ------------------------------------------------------------------
    function WfCwElementDecoder(opts) {
        timers(opts, this);
        this._dot = opts.dot;                         // () -> ms
        this._onChar = opts.onChar || function () {};
        this._onUnknown = opts.onUnknown || function () {};   // (pattern, sentInstead)
        this.charGap = opts.charGap || 2;
        this.wordGap = opts.wordGap || 5;
        this.pattern = '';
        this._lastOff = 0;
        this._timer = null;
        this._spaceDue = false;   // a character since the last word space
    }

    WfCwElementDecoder.prototype._cancel = function () {
        if (this._timer !== null) { this._clearTimeout(this._timer); this._timer = null; }
    };

    WfCwElementDecoder.prototype.keyDown = function () { this._cancel(); };

    WfCwElementDecoder.prototype.element = function (kind, on, off) {
        var self = this;
        this._cancel();
        this.pattern += kind;
        this._lastOff = off;
        this._timer = this._setTimeout(function () { self._timer = null; self._endChar(); },
            Math.max(0, off + this.charGap * this._dot() - this._now()));
    };

    WfCwElementDecoder.prototype._endChar = function () {
        var self = this;
        var pattern = this.pattern;
        this.pattern = '';
        var ch = cwFromPattern(pattern);
        var chars = ch ? [ch] : cwSplitPattern(pattern);
        if (!ch) this._onUnknown(pattern, chars.join(''));
        for (var i = 0; i < chars.length; i++) { this._spaceDue = true; this._onChar(chars[i]); }
        if (!this._spaceDue) return;
        this._timer = this._setTimeout(function () {
            self._timer = null;
            self._spaceDue = false;
            self._onChar(' ');
        }, Math.max(0, this._lastOff + this.wordGap * this._dot() - this._now()));
    };

    // Drop a half-keyed character and the pending word space.
    WfCwElementDecoder.prototype.reset = function () {
        this._cancel();
        this.pattern = '';
        this._spaceDue = false;
    };

    // ------------------------------------------------------------------
    // Iambic keyer.  Elements are one or three dots long, each followed by a
    // one-dot gap; at the end of the gap the next element is chosen:
    //   - both paddles held (squeeze): alternate;
    //   - one held: repeat it;
    //   - mode B also sends the opposite element when that paddle was closed
    //     at any time during the element just sent, so releasing a squeeze
    //     gives one more alternate element (Curtis B).  Mode A only looks at
    //     the paddles as they are at the end of the gap.
    // paddle('left' | 'right', closed): left is the dit paddle unless swap.
    // ------------------------------------------------------------------
    function WfPaddleKeyer(opts) {
        timers(opts, this);
        this._dot = opts.dot;                       // () -> ms
        this._onElement = opts.onElement || function () {};   // (kind, on, off)
        this.sidetone = opts.sidetone || null;      // WfCwSidetone, optional
        this.mode = opts.mode === 'A' ? 'A' : 'B';
        this.swap = !!opts.swap;
        this.closed = { '.': false, '-': false };
        this._latch = { '.': false, '-': false };
        this._last = null;      // kind of the element being sent, null when idle
        this._timer = null;
    }

    WfPaddleKeyer.prototype.paddle = function (side, closed) {
        var kind = ((side === 'left') !== this.swap) ? '.' : '-';
        this.closed[kind] = !!closed;
        if (!closed) return;
        if (this._last === null) this._start(kind);
        else if (kind !== this._last) this._latch[kind] = true;
    };

    WfPaddleKeyer.prototype._start = function (kind) {
        var self = this;
        var dot = this._dot();
        var on = this._now();
        var off = on + (kind === '.' ? 1 : 3) * dot;
        var opp = kind === '.' ? '-' : '.';
        this._last = kind;
        this._latch['.'] = this._latch['-'] = false;
        if (this.closed[opp]) this._latch[opp] = true;   // squeezed as the element starts
        if (this.sidetone) this.sidetone.schedule([{ on: on, off: off }]);
        this._onElement(kind, on, off);
        this._timer = this._setTimeout(function () { self._timer = null; self._next(); }, off + dot - on);
    };

    WfPaddleKeyer.prototype._next = function () {
        var last = this._last;
        var opp = last === '.' ? '-' : '.';
        var next = null;
        if (this.closed[opp] || (this.mode === 'B' && this._latch[opp])) next = opp;
        else if (this.closed[last]) next = last;
        this._last = null;
        if (next) this._start(next);
    };

    WfPaddleKeyer.prototype.stop = function () {
        if (this._timer !== null) { this._clearTimeout(this._timer); this._timer = null; }
        this._last = null;
        this.closed['.'] = this.closed['-'] = false;
        this._latch['.'] = this._latch['-'] = false;
        if (this.sidetone) this.sidetone.stop();
    };

    // ------------------------------------------------------------------
    // Straight key.  The tone follows the key; each element is classed by
    // its length against an adaptive dot (dit < 2 dots), which follows the
    // operator's speed (a dit counts as one dot, a dah as three).
    // ------------------------------------------------------------------
    function WfStraightKey(opts) {
        timers(opts, this);
        this._initialDot = opts.dot;                // () -> ms, the CW speed's dot
        this._onElement = opts.onElement || function () {};   // (kind, on, off)
        this._onKeyDown = opts.onKeyDown || function () {};
        this._onKeyUp = opts.onKeyUp || function () {};
        this.sidetone = opts.sidetone || null;
        this.dotMs = 0;
        this._downAt = null;
    }

    WfStraightKey.prototype.dot = function () { return this.dotMs || this._initialDot(); };

    WfStraightKey.prototype.key = function (closed) {
        var now = this._now();
        if (closed) {
            if (this._downAt !== null) return;
            this._downAt = now;
            if (this.sidetone) this.sidetone.keyDown();
            this._onKeyDown();
            return;
        }
        if (this._downAt === null) return;
        var on = this._downAt;
        this._downAt = null;
        if (this.sidetone) this.sidetone.keyUp();
        this._onKeyUp(now);
        var dur = now - on;
        var dot = this.dot();
        if (dur < 0.3 * dot) return;                // contact bounce
        var kind = dur < 2 * dot ? '.' : '-';
        var est = kind === '.' ? dur : dur / 3;
        var base = this._initialDot();
        est = Math.min(Math.max(est, base / 3), base * 3);   // stay within a factor 3 of the set speed
        this.dotMs = 0.7 * dot + 0.3 * est;
        this._onElement(kind, on, now);
    };

    WfStraightKey.prototype.stop = function () {
        if (this._downAt !== null) this._onKeyUp(this._now());
        this._downAt = null;
        if (this.sidetone) this.sidetone.keyUp();
    };

    // ------------------------------------------------------------------
    // RX mute while the paddle sends.  The radio keys a paddle character only
    // once it is decoded, so its own sidetone (back in the RX audio over USB)
    // trails the fist by a character -- an echo of the sending.  The RX audio
    // is cut on the first element and stays cut until the radio has keyed
    // everything decoded so far and that audio has come back through the RX
    // path (rxDelay: transport + jitter buffer).  The check is re-run until
    // then, since a character decoded later moves the radio's end out.
    // ------------------------------------------------------------------
    function WfCwRxMute(opts) {
        timers(opts, this);
        this._dot = opts.dot;                       // () -> ms
        this._rigEnd = opts.rigEnd || function () { return 0; };     // () -> wall ms the radio's keying ends
        this._rxDelay = opts.rxDelay || function () { return 400; }; // () -> ms from the radio's keying to the speaker
        this._onChange = opts.onChange || function () {};            // (muted)
        this.wordGap = opts.wordGap || 5;           // the decoder's last word: a space is decided by then
        this.muted = false;
        this._held = false;
        this._lastOff = 0;
        this._timer = null;
    }

    // An element was keyed, ending at off (wall ms).
    WfCwRxMute.prototype.activity = function (off) {
        if (off > this._lastOff) this._lastOff = off;
        if (!this.muted) { this.muted = true; this._onChange(true); }
        this._check();
    };

    // Straight key down / up: muted for as long as it is held.
    WfCwRxMute.prototype.hold = function (down) {
        this._held = !!down;
        this.activity(this._now());
    };

    WfCwRxMute.prototype.due = function () {
        return Math.max(this._lastOff + this.wordGap * this._dot(), this._rigEnd()) + this._rxDelay();
    };

    WfCwRxMute.prototype._check = function () {
        var self = this;
        if (this._timer !== null) { this._clearTimeout(this._timer); this._timer = null; }
        if (!this.muted || this._held) return;
        var wait = this.due() - this._now();
        if (wait <= 0) { this.muted = false; this._onChange(false); return; }
        this._timer = this._setTimeout(function () { self._timer = null; self._check(); }, wait);
    };

    // ------------------------------------------------------------------
    // What a page builds: paddle + straight key, each with its decoder,
    // feeding one onChar(ch).
    // ------------------------------------------------------------------
    // opts.onRxMute(muted), with opts.rigEnd and opts.rxDelay, cuts the RX
    // audio while the paddle sends (WfCwRxMute).
    function WfCwPaddle(opts) {
        var self = this;
        var t = { now: opts.now, setTimeout: opts.setTimeout, clearTimeout: opts.clearTimeout };
        function mk(o) { for (var k in t) if (t[k]) o[k] = t[k]; return o; }
        this.rxMute = opts.onRxMute ? new WfCwRxMute(mk({
            dot: function () { return Math.max(opts.dot(), self.straight.dot()); },
            rigEnd: opts.rigEnd, rxDelay: opts.rxDelay, onChange: opts.onRxMute
        })) : null;
        this.keyer = new WfPaddleKeyer(mk({
            dot: opts.dot, sidetone: opts.sidetone, mode: opts.mode, swap: opts.swap,
            onElement: function (kind, on, off) {
                if (self.rxMute) self.rxMute.activity(off);
                self.keyerDecoder.element(kind, on, off);
            }
        }));
        this.keyerDecoder = new WfCwElementDecoder(mk({
            dot: opts.dot, onChar: opts.onChar, onUnknown: opts.onUnknown
        }));
        this.straight = new WfStraightKey(mk({
            dot: opts.dot, sidetone: opts.sidetone,
            onKeyDown: function () { if (self.rxMute) self.rxMute.hold(true); self.straightDecoder.keyDown(); },
            onKeyUp: function () { if (self.rxMute) self.rxMute.hold(false); },
            onElement: function (kind, on, off) { self.straightDecoder.element(kind, on, off); }
        }));
        this.straightDecoder = new WfCwElementDecoder(mk({
            dot: function () { return self.straight.dot(); }, onChar: opts.onChar, onUnknown: opts.onUnknown
        }));
    }

    WfCwPaddle.prototype.paddle = function (side, closed) { this.keyer.paddle(side, closed); };
    WfCwPaddle.prototype.straightKey = function (closed) { this.straight.key(closed); };
    WfCwPaddle.prototype.setMode = function (mode) { this.keyer.mode = mode === 'A' ? 'A' : 'B'; };
    WfCwPaddle.prototype.setSwap = function (on) { this.keyer.swap = !!on; };

    // Escape / STOP: drop the half-keyed character and silence the tone.
    WfCwPaddle.prototype.stop = function () {
        this.keyer.stop();
        this.straight.stop();
        this.keyerDecoder.reset();
        this.straightDecoder.reset();
    };

    global.cwFromPattern = cwFromPattern;
    global.cwSplitPattern = cwSplitPattern;
    global.WfCwElementDecoder = WfCwElementDecoder;
    global.WfPaddleKeyer = WfPaddleKeyer;
    global.WfStraightKey = WfStraightKey;
    global.WfCwRxMute = WfCwRxMute;
    global.WfCwPaddle = WfCwPaddle;
})(typeof window !== 'undefined' ? window : this);
