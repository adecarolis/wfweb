// Web MIDI controller support, shared by both builds.
//
// A MIDI controller (built around the CTR2-MIDI: one knob with four modes of
// turn / push-and-turn, six dual-function buttons, a paddle / key / PTT jack)
// drives actions the page registers -- the same functions its own buttons
// call, so nothing new reaches the server.  The map from MIDI messages to
// actions is a table: presets for the CTR2, MIDI learn for anything else,
// kept in localStorage.
//
//   WfMidi       Web MIDI access, hot-plug, message parsing, the map, learn.
//                No DOM; the access request, storage and clock are passed in
//                so tests/midi_sim.mjs drives it under node.
//   WfMidiTuner  Coalesces knob ticks into one setFrequency every 40 ms.
//   WfMidiPanel  The settings window body (theme.css .wf-* kit).
//
// Web MIDI needs a secure context and Chromium (Firefox behind a site
// permission add-on); without navigator.requestMIDIAccess everything here
// stays idle.  The page shows its MIDI tile only while a controller is
// plugged in: watch() follows hot-plug without a permission prompt once the
// site holds the MIDI permission.
(function (global) {
    'use strict';

    // ------------------------------------------------------------------
    // Presets.  The CTR2-MIDI sends everything on channel 1 (manual v2.01):
    //   knob CC 100 / 101 (Home: turn / push-and-turn), 102-107 (modes 1-3);
    //   buttons NoteOn on release, 1-6 short and 11-16 long press;
    //   paddle jack: normal paddle mode 20 / 21 (left / right), extended
    //   paddle mode 30 / 31 (straight key / PTT).
    // Extended BTN mode gives every knob mode its own twelve buttons (notes
    // 1-48: Home 1-6 short, 25-30 long), so the paddle jack moves to 96-99.
    // The two button layouts collide (20 is a paddle in one, a button in the
    // other), hence two presets.
    // ------------------------------------------------------------------
    var CTR2_KNOB = [
        { type: 'cc', num: 100, enc: 'wheelA', action: 'tuneFine' },
        { type: 'cc', num: 101, enc: 'wheelA', action: 'tuneCoarse' },
        { type: 'cc', num: 102, enc: 'slider', action: 'afGain' },
        { type: 'cc', num: 103, enc: 'slider', action: 'squelch' },
        { type: 'cc', num: 104, enc: 'slider', action: 'rfGain' },
        { type: 'cc', num: 106, enc: 'slider', action: 'cwWpm' }
    ];
    var CTR2_BUTTONS = ['bandDown', 'bandUp', 'modeDown', 'modeUp', 'cwBar', 'spanUp'];
    var CTR2_LONG = ['preampAtt', 'nr', 'notch', 'tuner', 'cwCut', 'pttToggle'];
    var CTR2_JACK = ['paddleLeft', 'paddleRight', 'straightKey', 'ptt'];

    function ctr2Preset(shortBase, longBase, jack) {
        var b = CTR2_KNOB.slice();
        for (var i = 0; i < 6; i++) {
            b.push({ type: 'note', num: shortBase + i, action: CTR2_BUTTONS[i] });
            b.push({ type: 'note', num: longBase + i, action: CTR2_LONG[i] });
        }
        for (var j = 0; j < 4; j++) b.push({ type: 'note', num: jack[j], action: CTR2_JACK[j] });
        return b;
    }

    var MIDI_PRESETS = {
        ctr2:    { label: 'CTR2-MIDI', bindings: ctr2Preset(1, 11, [20, 21, 30, 31]) },
        ctr2ext: { label: 'CTR2-MIDI \u00b7 Extended BTN', bindings: ctr2Preset(1, 25, [96, 97, 98, 99]) },
        none:    { label: 'Empty', bindings: [] }
    };

    var MIDI_ENCODINGS = {
        wheelA: 'WheelA',   // 64 = centre, 64 + n = n ticks CW (speed-sensitive)
        wheelB: 'WheelB',   // direction only: 1..63 CW, 64..127 CCW
        slider: 'Slider'    // absolute 0..127
    };

    function store(storage, key, value) {
        try { if (storage) storage.setItem(key, value); } catch (e) {}
    }
    function load(storage, key) {
        try { return storage ? storage.getItem(key) : null; } catch (e) { return null; }
    }

    // ------------------------------------------------------------------
    // WfMidi
    // ------------------------------------------------------------------
    function WfMidi(opts) {
        opts = opts || {};
        var nav = typeof navigator !== 'undefined' ? navigator : null;
        this._requestAccess = 'requestAccess' in opts ? opts.requestAccess
            : (nav && nav.requestMIDIAccess ? function () { return nav.requestMIDIAccess({ sysex: false }); } : null);
        // -> Promise of 'granted' | 'prompt' | 'denied'; never prompts.
        this._queryPermission = 'queryPermission' in opts ? opts.queryPermission
            : (nav && nav.permissions && nav.permissions.query ? function () {
                return nav.permissions.query({ name: 'midi' }).then(function (s) { return s.state; });
            } : null);
        this._storage = 'storage' in opts ? opts.storage
            : (function () { try { return global.localStorage; } catch (e) { return null; } })();
        this.actions = {};
        this.order = [];            // registration order, for the panel
        this._listeners = [];
        this._access = null;
        this._last = {};            // 'cc:N' -> last value (slider -> ticks, CC buttons)
        this._closedGates = {};     // action -> true while a gate is held
        this.status = this._requestAccess ? 'off' : 'unsupported';
        this.devices = [];
        this.learning = null;
        this.lastMessage = null;

        var saved = null;
        try { saved = JSON.parse(load(this._storage, 'wfMidiMap') || 'null'); } catch (e) {}
        this.bindings = saved && saved.bindings ? saved.bindings : MIDI_PRESETS.ctr2.bindings.map(function (b) { return Object.assign({}, b); });
        this.opts = { fineStep: 10 };   // the paddle keyer's options live in paddle-panel.js
        try { Object.assign(this.opts, JSON.parse(load(this._storage, 'wfMidiOpts') || '{}')); } catch (e) {}
        this.enabled = load(this._storage, 'wfMidiEnabled') === '1';
    }

    WfMidi.supported = function () {
        return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess;
    };

    // kind: 'rel' fn(ticks) | 'abs' fn(0..1), get() -> 0..1 | 'btn' fn() | 'gate' fn(closed)
    WfMidi.prototype.register = function (name, def) {
        if (!this.actions[name]) this.order.push(name);
        this.actions[name] = def;
    };

    WfMidi.prototype.subscribe = function (fn) { this._listeners.push(fn); };
    // what: 'message' for an incoming message (only the monitor changes),
    // undefined for anything else.
    WfMidi.prototype._changed = function (what) {
        for (var i = 0; i < this._listeners.length; i++) {
            try { this._listeners[i](this, what); } catch (e) { if (global.console) console.error(e); }
        }
    };

    WfMidi.prototype._saveMap = function () { store(this._storage, 'wfMidiMap', JSON.stringify({ v: 1, bindings: this.bindings })); };

    WfMidi.prototype.setOpt = function (key, value) {
        this.opts[key] = value;
        store(this._storage, 'wfMidiOpts', JSON.stringify(this.opts));
        this._changed();
    };

    // ---- access / hot-plug ----
    WfMidi.prototype.enable = function () {
        var self = this;
        this.enabled = true;
        store(this._storage, 'wfMidiEnabled', '1');
        if (!this._requestAccess) { this.status = 'unsupported'; this._changed(); return Promise.resolve(false); }
        if (this._access) { this._attach(); this.status = 'ready'; this._changed(); return Promise.resolve(true); }
        this.status = 'pending';
        this._changed();
        var p;
        try { p = Promise.resolve(this._requestAccess()); } catch (e) { p = Promise.reject(e); }
        return p.then(function (access) {
            self._access = access;
            self._attach();
            if (!self.enabled) { self._changed(); return false; }   // switched off while the prompt was up
            self.status = 'ready';
            self._changed();
            return true;
        }, function () {
            self.status = 'denied';
            self._changed();
            return false;
        });
    };

    WfMidi.prototype.disable = function () {
        this.enabled = false;
        store(this._storage, 'wfMidiEnabled', '0');
        if (this._access) this._attach();   // still follows hot-plug, deaf to messages
        this._releaseGates();
        this.learning = null;
        if (this.status !== 'unsupported') this.status = 'off';
        this._changed();
    };

    // Watch for a controller without asking.  Switched on: open MIDI as
    // enable() does.  Switched off: only where the site already holds the
    // permission (a prompt at every page load would be worse than no tile),
    // then `devices` follows hot-plug while no message is acted on.
    WfMidi.prototype.watch = function () {
        var self = this;
        if (this.enabled) return this.enable();
        if (!this._requestAccess || !this._queryPermission) return Promise.resolve(false);
        var p;
        try { p = Promise.resolve(this._queryPermission()); } catch (e) { p = Promise.reject(e); }
        return p.then(function (state) {
            if (state !== 'granted' || self._access) return !!self._access;
            return Promise.resolve(self._requestAccess()).then(function (access) {
                if (!self._access) { self._access = access; self._attach(); self._changed(); }
                return true;
            });
        }).catch(function () { return false; });
    };

    // Lists the inputs and follows hot-plug; messages reach handle() only
    // while switched on.
    WfMidi.prototype._attach = function () {
        var self = this;
        var access = this._access;
        var handler = this.enabled ? function (ev) { self.handle(ev.data); } : null;
        var names = [];
        access.inputs.forEach(function (input) {
            input.onmidimessage = handler;
            if (input.state !== 'disconnected') names.push(input.name || input.manufacturer || 'MIDI input');
        });
        this.devices = names;
        access.onstatechange = function (ev) {
            // A controller unplugged with a paddle or PTT held must not leave
            // the rig keyed.
            if (ev && ev.port && ev.port.type === 'input' && ev.port.state === 'disconnected') self._releaseGates();
            self._attach();
            self._changed();
        };
    };

    WfMidi.prototype._releaseGates = function () {
        for (var name in this._closedGates) {
            var a = this.actions[name];
            if (a && a.kind === 'gate') a.fn(false);
        }
        this._closedGates = {};
    };

    // ---- messages ----
    // Returns the decoded {type, num, value, ch, on} or null.
    function parseMidi(data) {
        if (!data || data.length < 2) return null;
        var st = data[0] & 0xF0, ch = (data[0] & 0x0F) + 1;
        var num = data[1] & 0x7F, value = data.length > 2 ? data[2] & 0x7F : 0;
        if (st === 0x90) return { type: 'note', num: num, value: value, ch: ch, on: value > 0 };
        if (st === 0x80) return { type: 'note', num: num, value: value, ch: ch, on: false };
        if (st === 0xB0) return { type: 'cc', num: num, value: value, ch: ch };
        return null;
    }

    // Signed ticks of a relative CC value.
    function ccTicks(enc, value) {
        if (enc === 'wheelB') return value === 0 ? 0 : (value < 64 ? 1 : -1);
        return value - 64;   // wheelA
    }

    WfMidi.prototype.bindingFor = function (msg) {
        for (var i = 0; i < this.bindings.length; i++) {
            var b = this.bindings[i];
            if (b.type === msg.type && b.num === msg.num) return b;
        }
        return null;
    };

    WfMidi.prototype.handle = function (data) {
        var msg = parseMidi(data);
        if (!msg) return;
        var key = msg.type + ':' + msg.num;
        var prev = this._last[key];
        this._last[key] = msg.value;
        this.lastMessage = msg;
        if (this.learning) { if (this._learn(msg)) return; }
        var b = this.bindingFor(msg);
        var a = b && this.actions[b.action];
        if (a) this._dispatch(a, b, msg, prev);
        this._changed('message');
    };

    WfMidi.prototype._dispatch = function (a, b, msg, prev) {
        if (msg.type === 'note') {
            if (a.kind === 'btn') { if (msg.on) a.fn(); }
            else if (a.kind === 'gate') this._gate(b.action, a, msg.on);
            return;
        }
        var v = msg.value, enc = b.enc || 'slider';
        switch (a.kind) {
        case 'rel': {
            var ticks = enc === 'slider' ? (prev === undefined ? 0 : v - prev) : ccTicks(enc, v);
            if (ticks) a.fn(ticks);
            break;
        }
        case 'abs':
            if (enc === 'slider') a.fn(v / 127);
            else if (a.get) a.fn(Math.min(1, Math.max(0, a.get() + ccTicks(enc, v) / 127)));
            break;
        case 'btn':
            if (v >= 64 && !(prev >= 64)) a.fn();
            break;
        case 'gate':
            this._gate(b.action, a, v >= 64);
            break;
        }
    };

    WfMidi.prototype._gate = function (name, a, closed) {
        if (closed) this._closedGates[name] = true; else delete this._closedGates[name];
        a.fn(closed);
    };

    // ---- learn ----
    // Bind the next message to the action (replacing its bindings and taking
    // the message off any other action).  Knob actions only learn a CC, and
    // a note is only learnt from its NoteOn.
    WfMidi.prototype.learn = function (name) {
        this.learning = name || null;
        this._changed();
    };

    WfMidi.prototype._learn = function (msg) {
        var name = this.learning;
        var a = this.actions[name];
        if (!a) { this.learning = null; return false; }
        if (msg.type === 'note' && (!msg.on || a.kind === 'rel' || a.kind === 'abs')) return true;
        var b = { type: msg.type, num: msg.num, action: name };
        if (msg.type === 'cc') {
            if (a.kind === 'rel') b.enc = (msg.value >= 1 && msg.value <= 15) || msg.value >= 113 ? 'wheelB' : 'wheelA';
            else if (a.kind === 'abs') b.enc = 'slider';
        }
        this.bindings = this.bindings.filter(function (x) {
            return x.action !== name && !(x.type === b.type && x.num === b.num);
        });
        this.bindings.push(b);
        this.learning = null;
        this._saveMap();
        this._changed();
        return true;
    };

    WfMidi.prototype.bindingsOf = function (name) {
        return this.bindings.filter(function (b) { return b.action === name; });
    };

    WfMidi.prototype.clear = function (name) {
        this.bindings = this.bindings.filter(function (b) { return b.action !== name; });
        this._saveMap();
        this._changed();
    };

    WfMidi.prototype.setEncoding = function (name, enc) {
        if (!MIDI_ENCODINGS[enc]) return;
        var last = this._last;
        this.bindings.forEach(function (b) {
            if (b.action !== name || b.type !== 'cc') return;
            b.enc = enc;
            delete last['cc:' + b.num];   // a value read under another encoding is no slider origin
        });
        this._saveMap();
        this._changed();
    };

    WfMidi.prototype.applyPreset = function (id) {
        var p = MIDI_PRESETS[id];
        if (!p) return;
        this._releaseGates();
        this.bindings = p.bindings.map(function (b) { return Object.assign({}, b); });
        this._saveMap();
        this._changed();
    };

    // ------------------------------------------------------------------
    // WfMidiTuner: knob ticks -> setFrequency.  Ticks are summed and sent at
    // most every `interval` ms; while the radio's readback trails the knob,
    // each step starts from the last frequency sent (within `hold` ms), not
    // the page's stale frequency, so a fast spin loses nothing.
    // ------------------------------------------------------------------
    function WfMidiTuner(opts) {
        this._getFreq = opts.getFreq;     // () -> Hz, the rig's frequency as the page knows it
        this._send = opts.send;           // (Hz) -> setFrequency
        this._now = opts.now || function () { return Date.now(); };
        this._setTimeout = opts.setTimeout || function (f, ms) { return setTimeout(f, ms); };
        this.interval = opts.interval || 40;
        this.hold = opts.hold || 500;
        this._pending = 0;
        this._snap = 0;
        this._timer = null;
        this._lastSent = -Infinity;
        this._target = 0;
    }

    // Move by hz; snap > 0 rounds the result to a multiple of snap Hz (the
    // coarse step lands on whole kHz like the page's tuneUp / tuneDown).
    WfMidiTuner.prototype.add = function (hz, snap) {
        var self = this;
        this._pending += hz;
        this._snap = snap || 0;
        if (this._timer !== null) return;
        var wait = this._lastSent + this.interval - this._now();
        if (wait <= 0) this._flush();
        else this._timer = this._setTimeout(function () { self._timer = null; self._flush(); }, wait);
    };

    WfMidiTuner.prototype._flush = function () {
        if (!this._pending) return;
        var now = this._now();
        var base = (now - this._lastSent < this.hold && this._target > 0) ? this._target : this._getFreq();
        if (!(base > 0)) { this._pending = 0; return; }
        var target = base + this._pending;
        if (this._snap > 0) target = Math.round(target / this._snap) * this._snap;
        this._pending = 0;
        if (target <= 0) return;
        this._target = target;
        this._lastSent = now;
        this._send(target);
    };

    // ------------------------------------------------------------------
    // WfMidiPanel: the MIDI window body.  Built into `container` (the page's
    // .mem-modal shell) and re-rendered on every change.
    // ------------------------------------------------------------------
    var MIDI_STATUS_TEXT = {
        off: 'Off', unsupported: '',
        pending: 'Waiting for permission\u2026', denied: 'MIDI permission denied', ready: ''
    };

    function bindingText(b) {
        if (b.type === 'note') return 'Note ' + b.num;
        return 'CC ' + b.num;
    }

    function WfMidiPanel(container, midi) {
        var self = this;
        this.el = container;
        this.midi = midi;
        midi.subscribe(function (m, what) {
            if (!self.visible()) return;
            if (what === 'message') self._monitor(); else self.render();
        });
    }

    WfMidiPanel.prototype.visible = function () {
        var shell = this.el.closest ? this.el.closest('.mem-modal') : null;
        return !shell || shell.classList.contains('visible');
    };

    WfMidiPanel.prototype.render = function () {
        var midi = this.midi;
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
        // Keep the map list's scroll position across re-renders.
        var oldMap = this.el.querySelector('.midi-map');
        var scroll = oldMap ? oldMap.scrollTop : 0;
        this.el.innerHTML = '';
        this.el.classList.add('midi-panel', 'mode-midi');

        if (midi.status === 'unsupported') {
            // Web MIDI is only exposed to secure pages, and Safari has none.
            var secure = global.isSecureContext !== false;
            this.el.appendChild(h('div', 'midi-status midi-unsupported', secure
                ? 'This browser has no Web MIDI. Open this page in Chrome or Edge (Safari and iOS cannot use a MIDI controller).'
                : 'Web MIDI needs a secure page. Open this page with https:// in Chrome or Edge.'));
            return;
        }
        var top = h('div', 'midi-row');
        top.appendChild(btn(midi.enabled ? 'ON' : 'OFF', midi.enabled, function () {
            if (midi.enabled) midi.disable(); else midi.enable();
        }));
        var st = midi.status === 'ready'
            ? (midi.devices.length ? midi.devices.join(', ') : 'No MIDI device connected')
            : MIDI_STATUS_TEXT[midi.status] || midi.status;
        top.appendChild(h('span', 'midi-status' + (midi.status === 'ready' && midi.devices.length ? ' ok' : ''), st));
        this.el.appendChild(top);

        var pre = h('div', 'midi-row');
        pre.appendChild(h('span', 'midi-label', 'MAP'));
        var sel = h('select', 'wf-field midi-preset');
        for (var id in MIDI_PRESETS) {
            var o = h('option', null, MIDI_PRESETS[id].label);
            o.value = id;
            sel.appendChild(o);
        }
        pre.appendChild(sel);
        pre.appendChild(btn('LOAD', false, function () { midi.applyPreset(sel.value); }));
        this.el.appendChild(pre);

        var key = h('div', 'midi-row');
        key.appendChild(h('span', 'midi-label', 'KNOB'));
        var step = h('select', 'wf-field');
        [10, 50, 100].forEach(function (hz) {
            var o = h('option', null, hz + ' Hz');
            o.value = hz;
            if (+midi.opts.fineStep === hz) o.selected = true;
            step.appendChild(o);
        });
        step.onchange = function () { midi.setOpt('fineStep', +step.value); };
        key.appendChild(step);
        key.appendChild(h('span', 'midi-hint', 'Paddle keyer: CW bar \u203a PADDLE'));
        this.el.appendChild(key);

        var map = h('div', 'midi-map');
        midi.order.forEach(function (name) {
            var a = midi.actions[name];
            var row = h('div', 'midi-map-row');
            row.appendChild(h('span', 'midi-action', a.label || name));
            var bs = midi.bindingsOf(name);
            var cell = h('span', 'midi-binding');
            if (midi.learning === name) cell.textContent = a.kind === 'rel' || a.kind === 'abs' ? 'turn a knob\u2026' : 'press a button\u2026';
            else cell.textContent = bs.length ? bs.map(bindingText).join(', ') : '\u2014';
            row.appendChild(cell);
            var cc = bs.filter(function (b) { return b.type === 'cc'; })[0];
            if (cc && (a.kind === 'rel' || a.kind === 'abs')) {
                var es = h('select', 'wf-field midi-enc');
                for (var e in MIDI_ENCODINGS) {
                    var o = h('option', null, MIDI_ENCODINGS[e]);
                    o.value = e;
                    if ((cc.enc || 'slider') === e) o.selected = true;
                    es.appendChild(o);
                }
                es.onchange = function () { midi.setEncoding(name, es.value); };
                row.appendChild(es);
            } else row.appendChild(h('span', 'midi-enc'));
            row.appendChild(btn('LEARN', midi.learning === name, function () {
                midi.learn(midi.learning === name ? null : name);
            }));
            row.appendChild(btn('\u2715', false, function () { midi.clear(name); }, 'sq'));
            map.appendChild(row);
        });
        this.el.appendChild(map);
        map.scrollTop = scroll;

        this.el.appendChild(h('div', 'midi-monitor'));
        this._monitor();
    };

    // The last message received, so a controller's numbers can be read off.
    WfMidiPanel.prototype._monitor = function () {
        var el = this.el.querySelector('.midi-monitor');
        if (!el) return;
        var midi = this.midi, m = midi.lastMessage;
        if (!m) { el.textContent = 'Last: \u2014'; return; }
        // Name the action it drives, so a stray binding (a LEARN that caught
        // the wrong control) shows here instead of as a dead control.
        var bnd = midi.bindingFor(m), a = bnd && midi.actions[bnd.action];
        el.textContent = 'Last: ' + (m.type === 'cc' ? 'CC ' + m.num + ' = ' + m.value : 'Note ' + m.num + (m.on ? ' on' : ' off')) +
            ' (ch ' + m.ch + ') \u2192 ' + (a ? a.label || bnd.action : 'unmapped');
    };

    global.MIDI_PRESETS = MIDI_PRESETS;
    global.MIDI_ENCODINGS = MIDI_ENCODINGS;
    global.parseMidi = parseMidi;
    global.WfMidi = WfMidi;
    global.WfMidiTuner = WfMidiTuner;
    global.WfMidiPanel = WfMidiPanel;
})(typeof window !== 'undefined' ? window : this);
