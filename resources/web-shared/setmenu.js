// Set menu window: the radio's SET mode items (Icom CI-V 1A 05 xxxx), read
// and written through raw CI-V, laid out from a per-model JSON table.
//
//   setmenu/<model>.json   the table (format below), one file per model
//   WfSetMenuCodec         value <-> CI-V data bytes for each item type
//   WfSetMenu              reads / writes items through a civ() function
//   WfSetMenuPanel         the window body (theme.css .wf-* kit, .setmenu-*)
//
// The page supplies civ(hex) -> Promise<{reply, data, error}>: the server
// build sends the WebSocket civRaw command (REST_API.md, POST
// /api/v1/radio/civ); reply is 'ok' (FB), 'ng' (FA), 'data' (the value
// after the echoed command, hex), 'timeout' or 'error'.
//
// A write is never blind: the item is read first and must read back what
// the window shows (else the radio changed it, and the window refreshes and
// writes nothing), then set, then read again and compared.
//
// Table format:
// {
//   "model": "IC-7300",
//   "source": "where the item numbers come from",
//   "groups": [
//     { "path": "SET > Connectors",
//       "items": [
//         { "item": "0061", "name": "ACC/USB AF SQL", "type": "enum",
//           "options": [["00", "OFF (OPEN)"], ["01", "ON"]],
//           "firmware": "1.40+",           optional, informational
//           "source": "...",               optional, per item
//           "note": "..." } ] } ] }
// Types, data as the radio sends it after "1A 05 xxxx":
//   bool    one byte 00 / 01 (labels optional: "options" as for enum)
//   enum    one byte, the codes listed in "options" [[code, label], ...]
//   level   "digits" BCD digits (default 4: 0000-0255), "min" / "max",
//           shown as a number, or as % of max with "percent": true
//   number  "digits" BCD digits, "min" / "max", optional "scale" (shown =
//           raw * scale), "unit", "decimals"
//   raw     anything else (date, time, HPF/LPF pairs): shown as hex, read-only
(function (global) {
    'use strict';

    // ------------------------------------------------------------------
    // Codec
    // ------------------------------------------------------------------
    function compact(hex) { return String(hex || '').replace(/[\s:,-]/g, '').toUpperCase(); }
    function spaced(hex) { return compact(hex).replace(/(..)(?!$)/g, '$1 '); }
    function isBcd(hex) { return /^[0-9]*$/.test(hex); }

    var WfSetMenuCodec = {
        // CI-V data (hex) -> {ok, value, text} where value is what the
        // control holds: a code string for bool / enum, a number for level /
        // number, the hex for raw.
        decode: function (item, dataHex) {
            var d = compact(dataHex);
            switch (item.type) {
            case 'bool':
            case 'enum': {
                if (d.length !== 2) return { ok: false, text: 'unexpected data ' + spaced(d) };
                var opt = WfSetMenuCodec.option(item, d);
                return { ok: true, value: d, text: opt ? opt[1] : ('code ' + d) };
            }
            case 'level':
            case 'number': {
                var digits = item.digits || 4;
                if (d.length !== digits || !isBcd(d)) return { ok: false, text: 'unexpected data ' + spaced(d) };
                var n = parseInt(d, 10);
                return { ok: true, value: n, text: WfSetMenuCodec.format(item, n) };
            }
            default:
                return { ok: true, value: d, text: spaced(d) || '(empty)' };
            }
        },
        // Control value -> CI-V data (hex, no spaces); throws on a value the
        // item does not take.
        encode: function (item, value) {
            switch (item.type) {
            case 'bool':
            case 'enum': {
                var c = compact(value);
                if (!WfSetMenuCodec.option(item, c)) throw new Error('not one of the item\'s values: ' + value);
                return c;
            }
            case 'level':
            case 'number': {
                var n = Number(value);
                var digits = item.digits || 4;
                var min = item.min !== undefined ? item.min : 0;
                var max = item.max !== undefined ? item.max : Math.pow(10, digits) - 1;
                if (!isFinite(n) || Math.floor(n) !== n || n < min || n > max)
                    throw new Error('out of range ' + min + '-' + max + ': ' + value);
                var s = String(n);
                while (s.length < digits) s = '0' + s;
                return s;
            }
            default:
                throw new Error('this item is read-only here');
            }
        },
        option: function (item, code) {
            var opts = item.options || (item.type === 'bool' ? [['00', 'OFF'], ['01', 'ON']] : []);
            for (var i = 0; i < opts.length; i++) if (compact(opts[i][0]) === code) return opts[i];
            return null;
        },
        options: function (item) {
            return item.options || (item.type === 'bool' ? [['00', 'OFF'], ['01', 'ON']] : []);
        },
        format: function (item, n) {
            if (item.percent && item.max) return Math.round(n * 100 / item.max) + '% (' + n + ')';
            if (item.scale) {
                var v = n * item.scale;
                return v.toFixed(item.decimals !== undefined ? item.decimals : 1) + (item.unit ? ' ' + item.unit : '');
            }
            return n + (item.unit ? ' ' + item.unit : '');
        },
        writable: function (item) {
            return item.type === 'bool' || item.type === 'enum' || item.type === 'level' || item.type === 'number';
        },
        readHex: function (item) { return '1A 05 ' + spaced(item.item); },
        setHex: function (item, dataHex) { return '1A 05 ' + spaced(item.item) + ' ' + spaced(dataHex); }
    };

    // ------------------------------------------------------------------
    // Reads and checked writes
    // ------------------------------------------------------------------
    // state per item: {status: 'idle'|'reading'|'ok'|'missing'|'error'|'writing',
    //                  value, text, error}
    function WfSetMenu(opts) {
        this.civ = opts.civ;
        this.table = null;
        this.state = {};
        this._listeners = [];
        this._busy = Promise.resolve();
    }
    WfSetMenu.prototype.onChange = function (fn) { this._listeners.push(fn); };
    WfSetMenu.prototype._emit = function (itemNo) {
        for (var i = 0; i < this._listeners.length; i++) this._listeners[i](itemNo);
    };
    WfSetMenu.prototype.setTable = function (table) {
        this.table = table;
        this.state = {};
        var self = this;
        this.items().forEach(function (it) { self.state[it.item] = { status: 'idle' }; });
        this._emit(null);
    };
    WfSetMenu.prototype.items = function () {
        var out = [];
        if (!this.table) return out;
        (this.table.groups || []).forEach(function (g) {
            (g.items || []).forEach(function (it) { out.push(it); });
        });
        return out;
    };
    WfSetMenu.prototype.find = function (itemNo) {
        var all = this.items();
        for (var i = 0; i < all.length; i++) if (all[i].item === itemNo) return all[i];
        return null;
    };
    // One operation at a time, in order: the window never interleaves the
    // read-set-read of one item with another's.
    WfSetMenu.prototype._serial = function (fn) {
        var p = this._busy.then(fn, fn);
        this._busy = p.then(function () {}, function () {});
        return p;
    };
    WfSetMenu.prototype._set = function (itemNo, st) {
        this.state[itemNo] = st;
        this._emit(itemNo);
    };
    // Raw read, no state change: {ok, value, text} or {ok: false, missing, error}
    WfSetMenu.prototype._readRaw = function (item) {
        return Promise.resolve(this.civ(WfSetMenuCodec.readHex(item))).then(function (r) {
            if (!r) return { ok: false, error: 'no answer' };
            if (r.reply === 'data') {
                var d = WfSetMenuCodec.decode(item, r.data);
                return d.ok ? d : { ok: false, error: d.text };
            }
            if (r.reply === 'ng') return { ok: false, missing: true, error: 'the radio refused the read (NG): not on this model or firmware' };
            if (r.reply === 'ok') return { ok: false, error: 'the radio answered OK to a read' };
            return { ok: false, error: r.error || r.reply || 'no answer' };
        }, function (e) {
            return { ok: false, error: (e && e.message) || String(e) };
        });
    };
    WfSetMenu.prototype.read = function (itemNo) {
        var self = this, item = this.find(itemNo);
        if (!item) return Promise.resolve({ ok: false, error: 'no such item' });
        return this._serial(function () {
            self._set(itemNo, { status: 'reading', value: (self.state[itemNo] || {}).value, text: (self.state[itemNo] || {}).text });
            return self._readRaw(item).then(function (r) {
                if (r.ok) self._set(itemNo, { status: 'ok', value: r.value, text: r.text });
                else self._set(itemNo, { status: r.missing ? 'missing' : 'error', error: r.error });
                return r;
            });
        });
    };
    WfSetMenu.prototype.readAll = function (onProgress) {
        var self = this, all = this.items(), done = 0;
        var chain = Promise.resolve();
        all.forEach(function (it) {
            chain = chain.then(function () {
                return self.read(it.item).then(function () {
                    done++;
                    if (onProgress) onProgress(done, all.length);
                });
            });
        });
        return chain;
    };
    // Checked write: read (must match what the window holds), set, read back.
    // Resolves {ok, error?}.  Nothing is written unless the first read
    // succeeded and matched.
    WfSetMenu.prototype.write = function (itemNo, newValue) {
        var self = this, item = this.find(itemNo);
        if (!item) return Promise.resolve({ ok: false, error: 'no such item' });
        return this._serial(function () {
            var shown = self.state[itemNo] || {};
            if (shown.status !== 'ok') {
                return { ok: false, error: 'read the item first' };
            }
            var data;
            try { data = WfSetMenuCodec.encode(item, newValue); }
            catch (e) { return { ok: false, error: e.message }; }
            var fail = function (msg, keep) {
                self._set(itemNo, keep || { status: 'error', error: msg });
                return { ok: false, error: msg };
            };
            self._set(itemNo, { status: 'writing', value: shown.value, text: shown.text });
            return self._readRaw(item).then(function (before) {
                if (!before.ok) return fail('read before write failed: ' + before.error + '; nothing written');
                if (before.value !== shown.value) {
                    return fail('the radio now reads ' + before.text + ', not ' + shown.text + '; nothing written',
                                { status: 'ok', value: before.value, text: before.text,
                                  error: 'changed on the radio; nothing written' });
                }
                if (WfSetMenuCodec.encode(item, before.value) === data) {
                    self._set(itemNo, { status: 'ok', value: before.value, text: before.text });
                    return { ok: true, unchanged: true };
                }
                return Promise.resolve(self.civ(WfSetMenuCodec.setHex(item, data))).then(function (w) {
                    if (!w || w.reply !== 'ok') {
                        var why = !w ? 'no answer' : w.reply === 'ng' ? 'the radio refused the value (NG)'
                                : (w.error || w.reply);
                        // Whatever happened, show what the radio holds now.
                        return self._readRaw(item).then(function (now) {
                            return fail('write failed: ' + why,
                                        now.ok ? { status: 'ok', value: now.value, text: now.text, error: 'write failed: ' + why }
                                               : null);
                        });
                    }
                    return self._readRaw(item).then(function (after) {
                        if (!after.ok) return fail('written, but the read back failed: ' + after.error);
                        var expect = WfSetMenuCodec.decode(item, data);
                        if (after.value !== expect.value) {
                            return fail('read back ' + after.text + ', expected ' + expect.text,
                                        { status: 'ok', value: after.value, text: after.text,
                                          error: 'read back ' + after.text + ', expected ' + expect.text });
                        }
                        self._set(itemNo, { status: 'ok', value: after.value, text: after.text, written: true });
                        return { ok: true };
                    });
                });
            }, function (e) { return fail((e && e.message) || String(e)); });
        });
    };

    // ------------------------------------------------------------------
    // Window body
    // ------------------------------------------------------------------
    function WfSetMenuPanel(el, menu) {
        this.el = el;
        this.menu = menu;
        this.rows = {};
        this.status = null;
        this.message = '';
        var self = this;
        menu.onChange(function (itemNo) {
            if (itemNo === null) self.render();
            else self._updateRow(itemNo);
        });
    }
    WfSetMenuPanel.prototype.setMessage = function (text, cls) {
        this.message = text || '';
        if (this.status) {
            this.status.textContent = this.message;
            this.status.className = 'midi-status' + (cls ? ' ' + cls : '');
        }
    };
    WfSetMenuPanel.prototype.render = function () {
        var self = this, menu = this.menu, doc = this.el.ownerDocument;
        function h(tag, cls, text) {
            var e = doc.createElement(tag);
            if (cls) e.className = cls;
            if (text !== undefined) e.textContent = text;
            return e;
        }
        this.el.innerHTML = '';
        this.el.classList.add('midi-panel', 'setmenu-panel', 'mode-midi');
        this.rows = {};

        var bar = h('div', 'midi-row');
        var readAll = h('button', 'wf-btn', 'READ ALL');
        readAll.type = 'button';
        readAll.onclick = function () {
            readAll.disabled = true;
            self.setMessage('Reading...');
            menu.readAll(function (done, total) { self.setMessage('Read ' + done + ' / ' + total); })
                .then(function () {
                    var miss = 0, err = 0;
                    menu.items().forEach(function (it) {
                        var st = menu.state[it.item] || {};
                        if (st.status === 'missing') miss++;
                        else if (st.status === 'error') err++;
                    });
                    self.setMessage('Read ' + menu.items().length + ' items' +
                                    (miss ? ', ' + miss + ' not on this radio' : '') +
                                    (err ? ', ' + err + ' failed' : ''), err ? '' : 'ok');
                    readAll.disabled = false;
                });
        };
        bar.appendChild(readAll);
        this.status = h('span', 'midi-status', this.message);
        bar.appendChild(this.status);
        this.el.appendChild(bar);

        if (!menu.table) {
            this.el.appendChild(h('div', 'midi-unsupported', this.message || 'No Set menu table for this radio.'));
            return;
        }
        var head = h('div', 'setmenu-source', (menu.table.model || '') +
                     (menu.table.source ? ' · ' + menu.table.source : ''));
        this.el.appendChild(head);

        var list = h('div', 'setmenu-list');
        (menu.table.groups || []).forEach(function (g) {
            list.appendChild(h('div', 'setmenu-group', g.path || ''));
            (g.items || []).forEach(function (it) {
                var row = h('div', 'setmenu-row');
                var name = h('div', 'setmenu-name');
                name.appendChild(h('span', '', it.name));
                name.appendChild(h('span', 'setmenu-item', it.item));
                if (it.firmware) name.appendChild(h('span', 'setmenu-item', 'fw ' + it.firmware));
                if (it.note) name.title = it.note;
                row.appendChild(name);
                var ctl = h('div', 'setmenu-ctl');
                row.appendChild(ctl);
                var st = h('div', 'setmenu-state');
                row.appendChild(st);
                list.appendChild(row);
                self.rows[it.item] = { item: it, row: row, ctl: ctl, st: st };
                self._updateRow(it.item);
            });
        });
        this.el.appendChild(list);
    };
    WfSetMenuPanel.prototype._updateRow = function (itemNo) {
        var r = this.rows[itemNo];
        if (!r) return;
        var self = this, menu = this.menu, it = r.item, st = menu.state[itemNo] || { status: 'idle' };
        var doc = this.el.ownerDocument;
        var busy = st.status === 'reading' || st.status === 'writing';
        r.ctl.innerHTML = '';

        if (st.status === 'ok' || busy) {
            if (WfSetMenuCodec.writable(it) && st.value !== undefined) {
                var field;
                if (it.type === 'bool' || it.type === 'enum') {
                    field = doc.createElement('select');
                    field.className = 'wf-field';
                    WfSetMenuCodec.options(it).forEach(function (o) {
                        var op = doc.createElement('option');
                        op.value = o[0];
                        op.textContent = o[1];
                        field.appendChild(op);
                    });
                    if (!WfSetMenuCodec.option(it, st.value)) {
                        var unk = doc.createElement('option');
                        unk.value = st.value;
                        unk.textContent = 'code ' + st.value;
                        field.appendChild(unk);
                    }
                    field.value = st.value;
                    field.onchange = function () { self._write(itemNo, field.value); };
                } else {
                    field = doc.createElement('input');
                    field.className = 'wf-field';
                    field.type = 'number';
                    field.min = it.min !== undefined ? it.min : 0;
                    if (it.max !== undefined) field.max = it.max;
                    field.step = 1;
                    field.value = st.value;
                    field.onchange = function () { self._write(itemNo, field.value); };
                }
                field.disabled = busy;
                r.ctl.appendChild(field);
                if (it.type === 'level' || it.type === 'number') {
                    var shownText = doc.createElement('span');
                    shownText.className = 'setmenu-item';
                    shownText.textContent = st.text || '';
                    r.ctl.appendChild(shownText);
                }
            } else {
                r.ctl.appendChild(doc.createTextNode(st.text || ''));
            }
        }
        var b = doc.createElement('button');
        b.type = 'button';
        b.className = 'wf-btn';
        b.textContent = 'READ';
        b.disabled = busy;
        b.onclick = function () { menu.read(itemNo); };
        r.ctl.appendChild(b);

        var text = { idle: '', reading: 'reading...', writing: 'writing...', ok: st.written ? 'written ✓' : '',
                     missing: 'not on this radio', error: '' }[st.status] || '';
        if (st.error) text = st.error;
        r.st.textContent = text;
        r.st.className = 'setmenu-state' + (st.error || st.status === 'error' ? ' err'
                                            : st.status === 'missing' ? ' dim' : st.written ? ' ok' : '');
        r.row.classList.toggle('missing', st.status === 'missing');
    };
    WfSetMenuPanel.prototype._write = function (itemNo, value) {
        var self = this;
        this.menu.write(itemNo, value).then(function (res) {
            if (!res.ok) self._updateRow(itemNo);
        });
    };

    var api = { WfSetMenuCodec: WfSetMenuCodec, WfSetMenu: WfSetMenu, WfSetMenuPanel: WfSetMenuPanel };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    for (var k in api) global[k] = api[k];
})(typeof window !== 'undefined' ? window : globalThis);
