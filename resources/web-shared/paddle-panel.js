// Paddle input and the PADDLE window of the CW bar, shared by both builds.
//
// A paddle reaches the keyer (cw-paddle.js) through one of two adapters:
//   - a MIDI controller's paddle jack (CTR2-MIDI): notes, mapped in the MIDI
//     window (midi.js, FUNC > MIDI) like its knob and buttons;
//   - a USB keyboard adapter (VBand, Vail adapter in keyboard mode): the
//     contacts are keys, left and right Ctrl by default.  Keyboard events
//     reach every browser, phones included (USB-C / OTG), where Web MIDI
//     does not -- no Safari, no iOS.
//
//   WfPaddleSettings  keyer options + keyboard binding (localStorage
//                     wfPaddleOpts), shared by both adapters.
//   WfKeyboardPaddle  key events -> contacts (left / right / straight).
//   WfPaddlePanel     the PADDLE window body (theme.css .wf-* kit).
//
// The keyboard adapter is opt-in: its keys are ordinary Ctrl keys, so until
// it is switched on they stay shortcuts.  The page decides whether a contact
// keys (CW / CW-R only); the window's lamps show every contact, so the wiring
// can be checked in any mode.
(function (global) {
    'use strict';

    function load(storage, key) {
        try { return storage ? storage.getItem(key) : null; } catch (e) { return null; }
    }
    function store(storage, key, value) {
        try { if (storage) storage.setItem(key, value); } catch (e) {}
    }

    // ------------------------------------------------------------------
    // Settings
    // ------------------------------------------------------------------
    var PADDLE_DEFAULTS = {
        keyerMode: 'B',          // iambic A / B
        swap: false,             // left contact is the dah
        keyboard: false,         // USB keyboard adapter on
        keyMode: 'paddle',       // 'paddle' (two contacts) | 'straight' (one, left key)
        keyLeft: 'ControlLeft',  // KeyboardEvent.code of each contact (VBand: left / right Ctrl)
        keyRight: 'ControlRight'
    };

    function WfPaddleSettings(opts) {
        opts = opts || {};
        this._storage = 'storage' in opts ? opts.storage
            : (function () { try { return global.localStorage; } catch (e) { return null; } })();
        this._listeners = [];
        this.v = {};
        for (var k in PADDLE_DEFAULTS) this.v[k] = PADDLE_DEFAULTS[k];
        var saved = null;
        try { saved = JSON.parse(load(this._storage, 'wfPaddleOpts') || 'null'); } catch (e) {}
        if (!saved) {
            // The keyer options lived in the MIDI window until the PADDLE
            // window took them over: carry them across once.
            try {
                var m = JSON.parse(load(this._storage, 'wfMidiOpts') || 'null');
                if (m) saved = { keyerMode: m.keyerMode, swap: m.swap };
            } catch (e) {}
        }
        if (saved) for (var s in saved) if (s in PADDLE_DEFAULTS && saved[s] !== undefined) this.v[s] = saved[s];
    }

    WfPaddleSettings.prototype.get = function (k) { return this.v[k]; };
    WfPaddleSettings.prototype.set = function (k, value) {
        this.v[k] = value;
        store(this._storage, 'wfPaddleOpts', JSON.stringify(this.v));
        for (var i = 0; i < this._listeners.length; i++) {
            try { this._listeners[i](this); } catch (e) { if (global.console) console.error(e); }
        }
    };
    WfPaddleSettings.prototype.subscribe = function (fn) { this._listeners.push(fn); };

    // ------------------------------------------------------------------
    // Keyboard adapter.  onContact(role, closed) for every contact while the
    // adapter is on, role 'left' | 'right' | 'straight'.  While a contact is
    // held, and isActive() says it keys, every other key is held back: a
    // letter typed with Ctrl down would otherwise be a shortcut (the browser's
    // own Ctrl+W / Ctrl+T on Windows and Linux cannot be held back by a page).
    // A lost focus releases what is held -- its keyup would never arrive.
    // ------------------------------------------------------------------
    function WfKeyboardPaddle(opts) {
        var self = this;
        this.settings = opts.settings;
        this._target = opts.target || global;
        this._onContact = opts.onContact || function () {};
        this._isActive = opts.isActive || function () { return true; };
        this._onLearn = opts.onLearn || function () {};
        this._onTrace = opts.onTrace || function () {};
        this.held = {};
        this.trace = [];         // the last key events of the paddle keys, for the window
        this.learning = null;    // 'keyLeft' | 'keyRight' while a key is being learnt
        this._down = function (e) { self._keydown(e); };
        this._up = function (e) { self._keyup(e); };
        this._blur = function () { self.releaseAll(); };
        this._target.addEventListener('keydown', this._down, true);
        this._target.addEventListener('keyup', this._up, true);
        this._target.addEventListener('blur', this._blur);
    }

    WfKeyboardPaddle.prototype._role = function (code) {
        var s = this.settings;
        if (s.get('keyMode') === 'straight') return code === s.get('keyLeft') ? 'straight' : null;
        if (code === s.get('keyLeft')) return 'left';
        if (code === s.get('keyRight')) return 'right';
        return null;
    };

    // Safari (WebKit) loses a Ctrl key's release while the other Ctrl key is
    // held -- both are one "Control" flag on a Mac -- and a squeezed dah
    // stayed keyed until it was pressed again (VBand, 2026-10-26; Chromium
    // reports each side).  Every key event also carries the modifier state:
    // a contact on a modifier key that the event says is no longer down was
    // released, whatever events went missing.
    var MODIFIER_OF = { Control: 'Control', Shift: 'Shift', Alt: 'Alt', Meta: 'Meta' };
    function modifierOf(code) {
        var m = /^(Control|Shift|Alt|Meta)(Left|Right)$/.exec(code || '');
        return m ? MODIFIER_OF[m[1]] : null;
    }

    WfKeyboardPaddle.prototype._codeOf = function (role) {
        var s = this.settings;
        return role === 'right' ? s.get('keyRight') : s.get('keyLeft');
    };

    WfKeyboardPaddle.prototype._resync = function (e) {
        if (!e.getModifierState) return;
        for (var r in this.held) {
            if (!this.held[r]) continue;
            var mod = modifierOf(this._codeOf(r));
            if (mod && !e.getModifierState(mod)) { this.held[r] = false; this._onContact(r, false); }
        }
    };

    WfKeyboardPaddle.prototype._log = function (e) {
        var code = e.code || '';
        if (!modifierOf(code) && !this._role(code)) return;   // never the text being typed
        var mod = modifierOf(code);
        this.trace.push((e.type === 'keydown' ? '\u2193' : '\u2191') + keyName(code)
            + (mod && e.getModifierState && e.getModifierState(mod) && e.type === 'keyup' ? ' (' + mod + ' still down)' : '')
            + (e.repeat ? ' rep' : ''));
        if (this.trace.length > 8) this.trace.shift();
        this._onTrace(this.trace);
    };

    WfKeyboardPaddle.prototype._anyHeld = function () {
        for (var r in this.held) if (this.held[r]) return true;
        return false;
    };

    WfKeyboardPaddle.prototype._keydown = function (e) {
        if (this.learning) {
            if (e.repeat || !e.code) return;
            var which = this.learning;
            this.learning = null;
            e.preventDefault();
            e.stopPropagation();
            this.settings.set(which, e.code);
            // Learning a key means using it: with USB KEYS left off, the
            // learnt keys did nothing (first bench try, 2026-10-25).
            if (!this.settings.get('keyboard')) this.settings.set('keyboard', true);
            this._onLearn(which, e.code);
            return;
        }
        if (!this.settings.get('keyboard')) return;
        this._log(e);
        this._resync(e);
        var role = this._role(e.code);
        if (role) {
            if (!e.repeat && !this.held[role]) {
                this.held[role] = true;
                this._onContact(role, true);
            }
            if (this._isActive()) { e.preventDefault(); e.stopPropagation(); }
            return;
        }
        if (this._anyHeld() && this._isActive()) { e.preventDefault(); e.stopPropagation(); }
    };

    WfKeyboardPaddle.prototype._keyup = function (e) {
        if (!this.settings.get('keyboard')) return;
        this._log(e);
        var role = this._role(e.code);
        var had = role && this.held[role];
        if (had) { this.held[role] = false; this._onContact(role, false); }
        this._resync(e);
        if (had && this._isActive()) { e.preventDefault(); e.stopPropagation(); }
    };

    WfKeyboardPaddle.prototype.releaseAll = function () {
        for (var r in this.held) {
            if (this.held[r]) { this.held[r] = false; this._onContact(r, false); }
        }
    };

    // Bind the next key pressed to a contact ('keyLeft' | 'keyRight'), or
    // stop learning (null).
    WfKeyboardPaddle.prototype.learn = function (which) {
        this.releaseAll();
        this.learning = which || null;
    };

    // "ControlLeft" -> "Left Ctrl", for the window.
    function keyName(code) {
        if (!code) return '\u2014';
        var m = /^(Control|Shift|Alt|Meta)(Left|Right)$/.exec(code);
        if (m) return m[2] + ' ' + ({ Control: 'Ctrl', Shift: 'Shift', Alt: 'Alt', Meta: 'Cmd' })[m[1]];
        m = /^(Key|Digit)(.)$/.exec(code);
        if (m) return m[2];
        return code;
    }

    // ------------------------------------------------------------------
    // The PADDLE window.  hooks: midiStatus() -> text, openMidi().
    // ------------------------------------------------------------------
    function WfPaddlePanel(container, settings, keyboard, hooks) {
        var self = this;
        this.el = container;
        this.settings = settings;
        this.kb = keyboard;
        this.hooks = hooks || {};
        this.lamps = {};
        settings.subscribe(function () { if (self.visible()) self.render(); });
    }

    WfPaddlePanel.prototype.visible = function () {
        var shell = this.el.closest ? this.el.closest('.mem-modal') : null;
        return !shell || shell.classList.contains('visible');
    };

    WfPaddlePanel.prototype.render = function () {
        var self = this, s = this.settings, kb = this.kb;
        var doc = this.el.ownerDocument;
        function h(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        function btn(label, active, onclick, extra) {
            var b = h('button', 'wf-btn' + (extra ? ' ' + extra : '') + (active ? ' active' : ''), label);
            b.type = 'button';
            b.onclick = onclick;
            return b;
        }
        function row(label) {
            var r = h('div', 'midi-row');
            if (label) r.appendChild(h('span', 'midi-label paddle-label', label));
            self.el.appendChild(r);
            return r;
        }
        this.el.innerHTML = '';
        this.el.classList.add('midi-panel', 'paddle-panel', 'mode-cw');

        var r = row('KEYER');
        r.appendChild(btn('IAMBIC A', s.get('keyerMode') === 'A', function () { s.set('keyerMode', 'A'); }, 'toggle'));
        r.appendChild(btn('IAMBIC B', s.get('keyerMode') !== 'A', function () { s.set('keyerMode', 'B'); }, 'toggle'));
        r.appendChild(btn('SWAP', !!s.get('swap'), function () { s.set('swap', !s.get('swap')); }, 'toggle'));

        r = row('USB KEYS');
        r.appendChild(btn(s.get('keyboard') ? 'ON' : 'OFF', !!s.get('keyboard'), function () {
            kb.releaseAll();
            s.set('keyboard', !s.get('keyboard'));
        }, 'toggle'));
        r.appendChild(btn('PADDLE', s.get('keyMode') !== 'straight', function () { kb.releaseAll(); s.set('keyMode', 'paddle'); }, 'toggle'));
        r.appendChild(btn('STRAIGHT', s.get('keyMode') === 'straight', function () { kb.releaseAll(); s.set('keyMode', 'straight'); }, 'toggle'));
        r.appendChild(h('span', 'midi-status', 'VBand-style adapter'));

        var keys = s.get('keyMode') === 'straight' ? [['keyLeft', 'KEY']] : [['keyLeft', 'LEFT'], ['keyRight', 'RIGHT']];
        keys.forEach(function (k) {
            var kr = row(k[1]);
            kr.appendChild(h('span', 'paddle-key', kb.learning === k[0] ? 'press a key\u2026' : keyName(s.get(k[0]))));
            kr.appendChild(btn('LEARN', kb.learning === k[0], function () {
                kb.learn(kb.learning === k[0] ? null : k[0]);
                self.render();
            }));
        });

        r = row('MIDI');
        r.appendChild(h('span', 'midi-status', this.hooks.midiStatus ? this.hooks.midiStatus() : ''));
        if (this.hooks.openMidi) r.appendChild(btn('MIDI\u2026', false, this.hooks.openMidi));

        // Check: one lamp per contact, lit while it is closed, from either
        // adapter.  With SWAP the left contact is the dah.
        r = row('CHECK');
        var dit = s.get('swap') ? 'right' : 'left';
        this.lamps = {};
        (s.get('keyMode') === 'straight' && s.get('keyboard') ? ['straight', 'left', 'right'] : ['left', 'right', 'straight']).forEach(function (role) {
            var name = role === 'straight' ? 'KEY' : (role === 'left' ? 'LEFT' : 'RIGHT') + (role === dit ? ' \u00b7' : ' \u2212');
            var l = h('span', 'paddle-lamp', name);
            self.lamps[role] = l;
            r.appendChild(l);
        });

        this.traceEl = h('div', 'midi-monitor paddle-trace');
        this.el.appendChild(this.traceEl);
        this.showTrace(kb.trace);
        this.el.appendChild(h('div', 'midi-monitor',
            'Keys only in CW / CW-R. While a paddle key is down, other keys are held back (no Ctrl shortcuts mid-character).'));
    };

    // The last key events of the paddle keys, as the browser reported them.
    WfPaddlePanel.prototype.showTrace = function (trace) {
        if (this.traceEl) this.traceEl.textContent = 'Keys: ' + (trace && trace.length ? trace.join('  ') : '\u2014');
    };

    // A contact opened or closed: light its lamp.
    WfPaddlePanel.prototype.lamp = function (role, on) {
        var l = this.lamps[role];
        if (l) l.classList.toggle('on', !!on);
    };

    global.PADDLE_DEFAULTS = PADDLE_DEFAULTS;
    global.WfPaddleSettings = WfPaddleSettings;
    global.WfKeyboardPaddle = WfKeyboardPaddle;
    global.WfPaddlePanel = WfPaddlePanel;
    global.paddleKeyName = keyName;
})(typeof window !== 'undefined' ? window : this);
