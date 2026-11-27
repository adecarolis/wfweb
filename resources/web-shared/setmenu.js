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
//           shown as a number, as % of max with "percent": true, or on
//           the radio's own scale with "range": [lo, hi]
//   number  "digits" BCD digits, "min" / "max", optional "scale" (shown =
//           raw * scale), "unit", "decimals"
//   raw     anything else: read-only; "format" renders it (rgb, date, time,
//           utcOffset, ascii, edges, offset, hpflpf), else it is shown as hex
// Any item with "readonly": true is shown but never written from here
// (settings that can cut wfweb's own link to the radio, or key it).
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
                return { ok: true, value: d, text: WfSetMenuCodec.rawText(item, d) };
            }
        },
        // A readable rendering of the multi-byte formats the Icom tables
        // describe ("format" on a raw item); the hex when it does not fit.
        rawText: function (item, d) {
            var hex = spaced(d) || '(empty)';
            var b = [];
            for (var i = 0; i + 1 < d.length; i += 2) b.push(d.substr(i, 2));
            var bcd = function (s) { return isBcd(s) ? parseInt(s, 10) : NaN; };
            var khz = function (s) {   // 5 bytes, 1 Hz digits last-in: "XX XX XX XX 00" little-endian BCD
                if (s.length !== 10 || !isBcd(s)) return null;
                var digits = '';
                for (var j = 8; j >= 0; j -= 2) digits += s.substr(j, 2);
                return (parseInt(digits, 10) / 1e6).toFixed(3);
            };
            switch (item.format) {
            case 'rgb':
                if (b.length === 6 && isBcd(d)) return 'R ' + bcd(b[0] + b[1]) + ' G ' + bcd(b[2] + b[3]) + ' B ' + bcd(b[4] + b[5]);
                break;
            case 'date':
                if (d.length === 8 && isBcd(d)) return d.substr(0, 4) + '-' + d.substr(4, 2) + '-' + d.substr(6, 2);
                break;
            case 'time':
                if (d.length === 4 && isBcd(d)) return d.substr(0, 2) + ':' + d.substr(2, 2);
                break;
            case 'utcOffset':
                if (d.length === 6 && isBcd(d)) return (b[2] === '01' ? '-' : '+') + d.substr(0, 2) + ':' + d.substr(2, 2);
                break;
            case 'ascii': {
                var s = '';
                for (var k = 0; k < b.length; k++) s += String.fromCharCode(parseInt(b[k], 16));
                return '"' + s + '"';
            }
            case 'edges':
                // Two frequencies, each 3 BCD bytes least significant first
                // (100 Hz digit fixed 0): 100 Hz/1 kHz, 10 kHz/100 kHz, 1 MHz/10 MHz.
                if (d.length === 12 && isBcd(d)) {
                    var mhz = function (o) { return (parseInt(b[o + 2] + b[o + 1] + b[o], 10) / 10000).toFixed(3); };
                    return mhz(0) + ' - ' + mhz(3) + ' MHz';
                }
                if (d.length === 20) {
                    var lo = khz(d.substr(0, 10)), hi = khz(d.substr(10, 10));
                    if (lo && hi) return lo + ' - ' + hi + ' MHz';
                }
                break;
            case 'offset':
                // 3 BCD bytes least significant first (100 Hz digit fixed 0), then 00 = +, 01 = -
                if (b.length === 4 && isBcd(d)) {
                    return (b[3] === '01' ? '-' : '+') + (parseInt(b[2] + b[1] + b[0], 10) / 10000).toFixed(3) + ' MHz';
                }
                break;
            case 'hpflpf':
                if (b.length === 2 && isBcd(d)) {
                    var h = bcd(b[0]), l = bcd(b[1]);
                    return 'HPF ' + (h === 0 ? 'through' : h * 100 + ' Hz') + ', LPF ' + (l >= 25 ? 'through' : l * 100 + ' Hz');
                }
                break;
            }
            return hex;
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
            if (item.range && item.max) {   // [lo, hi]: what the radio's screen shows for 0 .. max
                var shown = item.range[0] + n * (item.range[1] - item.range[0]) / item.max;
                return Math.round(shown) + (item.unit ? ' ' + item.unit : '') + ' (' + n + ')';
            }
            if (item.scale) {
                var v = n * item.scale;
                return v.toFixed(item.decimals !== undefined ? item.decimals : 1) + (item.unit ? ' ' + item.unit : '');
            }
            return n + (item.unit ? ' ' + item.unit : '');
        },
        // Level / number fields are edited in the units the radio's own
        // screen uses (%, its 1-100 scale, seconds), not in the raw value:
        // typing 50 into a 0-255 level meant 20%.  shown <-> raw.
        unitStep: function (item) {
            if (item.type === 'number' && item.scale) return item.scale;
            return 1;
        },
        toShown: function (item, raw) {
            if (item.type === 'level' && item.percent && item.max) return Math.round(raw * 100 / item.max);
            if (item.type === 'level' && item.range && item.max)
                return Math.round(item.range[0] + raw * (item.range[1] - item.range[0]) / item.max);
            if (item.type === 'number' && item.scale)
                return Number((raw * item.scale).toFixed(item.decimals !== undefined ? item.decimals : 1));
            return raw;
        },
        fromShown: function (item, shown) {
            var v = Number(shown);
            if (!isFinite(v)) return NaN;
            if (item.type === 'level' && item.percent && item.max) return Math.round(v * item.max / 100);
            if (item.type === 'level' && item.range && item.max)
                return Math.round((v - item.range[0]) * item.max / (item.range[1] - item.range[0]));
            if (item.type === 'number' && item.scale) return Math.round(v / item.scale);
            return v;
        },
        shownRange: function (item) {
            var lo = item.min !== undefined ? item.min : 0;
            var hi = item.max !== undefined ? item.max : Math.pow(10, item.digits || 4) - 1;
            return [WfSetMenuCodec.toShown(item, lo), WfSetMenuCodec.toShown(item, hi)];
        },
        writable: function (item) {
            if (item.readonly) return false;
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
        this._q = [];              // jobs waiting: {fn, kind, item, resolve, promise}
        this._running = false;
        this._pendingRead = {};    // item -> its waiting read job
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
    // One operation at a time: the window never interleaves the read-set-read
    // of one item with another's.  Unlike a promise chain, the queue can be
    // reordered (the section opened last goes first) and its waiting reads
    // dropped (a section closed before it was read sends nothing more).
    WfSetMenu.prototype._job = function (kind, item, fn) {
        var job = { kind: kind, item: item, fn: fn };
        job.promise = new Promise(function (resolve) { job.resolve = resolve; });
        return job;
    };
    WfSetMenu.prototype._serial = function (fn, front) {
        var job = this._job('op', null, fn);
        if (front) this._q.unshift(job); else this._q.push(job);
        this._pump();
        return job.promise;
    };
    WfSetMenu.prototype._pump = function () {
        var self = this;
        if (this._running || !this._q.length) return;
        var job = this._q.shift();
        if (job.kind === 'read') delete this._pendingRead[job.item];
        this._running = true;
        var done = function (v) {
            self._running = false;
            job.resolve(v);
            self._pump();
        };
        var r;
        try { r = job.fn(); } catch (e) { done({ ok: false, error: e.message }); return; }
        Promise.resolve(r).then(done, function (e) { done({ ok: false, error: (e && e.message) || String(e) }); });
    };
    // Is a read of this item waiting (not yet on the wire)?
    WfSetMenu.prototype.readPending = function (itemNo) { return !!this._pendingRead[itemNo]; };
    // Queue reads of these items, in this order; with front, ahead of
    // everything waiting (a read already waiting moves up with them).
    // Returns one promise per item.
    WfSetMenu.prototype.readMany = function (itemNos, opts) {
        var self = this, front = opts && opts.front, batch = [], promises = [];
        itemNos.forEach(function (no) {
            var job = self._pendingRead[no];
            if (job) {
                if (front) self._q.splice(self._q.indexOf(job), 1);
                else { promises.push(job.promise); return; }
            } else {
                var item = self.find(no);
                if (!item) { promises.push(Promise.resolve({ ok: false, error: 'no such item' })); return; }
                job = self._job('read', no, function () { return self._doRead(no, item); });
                self._pendingRead[no] = job;
            }
            batch.push(job);
            promises.push(job.promise);
        });
        if (front) Array.prototype.unshift.apply(this._q, batch);
        else Array.prototype.push.apply(this._q, batch);
        this._pump();
        return promises;
    };
    // Drop the waiting reads of these items (one on the wire finishes).
    // Their promises resolve {ok: false, cancelled: true}; the items stay idle.
    WfSetMenu.prototype.cancelReads = function (itemNos) {
        var self = this, n = 0;
        itemNos.forEach(function (no) {
            var job = self._pendingRead[no];
            if (!job) return;
            self._q.splice(self._q.indexOf(job), 1);
            delete self._pendingRead[no];
            job.resolve({ ok: false, cancelled: true });
            n++;
        });
        return n;
    };
    WfSetMenu.prototype._set = function (itemNo, st) {
        this.state[itemNo] = st;
        this._emit(itemNo);
    };
    // Raw read, no state change: {ok, value, text} or {ok: false, missing, error}.
    // A read that gets no answer is asked again, twice at most: reading
    // twice changes nothing, and over USB the odd reply is lost below us
    // (about one read in twenty on an IC-7300 with the scope streaming).
    WfSetMenu.prototype._readRaw = function (item, tries) {
        var self = this;
        tries = tries === undefined ? 2 : tries;
        return Promise.resolve(this.civ(WfSetMenuCodec.readHex(item))).then(function (r) {
            if (r && r.reply === 'timeout' && tries > 0) return self._readRaw(item, tries - 1);
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
    WfSetMenu.prototype.read = function (itemNo, opts) {
        return this.readMany([itemNo], opts)[0];
    };
    WfSetMenu.prototype._doRead = function (itemNo, item) {
        var self = this;
        self._set(itemNo, { status: 'reading', value: (self.state[itemNo] || {}).value, text: (self.state[itemNo] || {}).text });
        return self._readRaw(item).then(function (r) {
            if (r.ok) self._set(itemNo, { status: 'ok', value: r.value, text: r.text });
            else self._set(itemNo, { status: r.missing ? 'missing' : 'error', error: r.error });
            return r;
        });
    };
    WfSetMenu.prototype.readAll = function (onProgress) {
        var all = this.items().map(function (it) { return it.item; }), done = 0;
        return Promise.all(this.readMany(all).map(function (p) {
            return p.then(function (r) { done++; if (onProgress) onProgress(done, all.length); return r; });
        }));
    };
    // Checked write: read (must match what the window holds), set, read back.
    // Resolves {ok, error?}.  Nothing is written unless the first read
    // succeeded and matched.
    WfSetMenu.prototype.write = function (itemNo, newValue) {
        var self = this, item = this.find(itemNo);
        if (!item) return Promise.resolve({ ok: false, error: 'no such item' });
        // The operator's change goes ahead of reads still waiting.
        return this._serial(function () {
            var shown = self.state[itemNo] || {};
            if (!WfSetMenuCodec.writable(item)) {
                return { ok: false, error: 'read-only here' + (item.note ? ': ' + item.note : '') };
            }
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
                    if (w && w.reply === 'timeout') {
                        // The set is not sent again; what the radio holds now says
                        // whether it was taken and only the OK got lost.
                        return self._readRaw(item).then(function (now) {
                            var expect = WfSetMenuCodec.decode(item, data);
                            if (now.ok && now.value === expect.value) {
                                self._set(itemNo, { status: 'ok', value: now.value, text: now.text, written: true });
                                return { ok: true, lostAck: true };
                            }
                            return fail('write not confirmed: no answer to the set',
                                        now.ok ? { status: 'ok', value: now.value, text: now.text,
                                                   error: 'write not confirmed: no answer to the set' } : null);
                        });
                    }
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
        }, true);
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
        this.status = h('span', 'midi-status', this.message);
        bar.appendChild(this.status);
        this.el.appendChild(bar);

        if (!menu.table) {
            this.el.appendChild(h('div', 'midi-unsupported', this.message || 'No Set menu table for this radio.'));
            return;
        }
        // One line: the model and how many items; the table's source on hover.
        var head = h('div', 'setmenu-source', (menu.table.model || '') + ' · ' + menu.items().length +
                     ' items' + (menu.table.source ? ' · source ⓘ' : ''));
        if (menu.table.source) head.title = menu.table.source;
        this.el.appendChild(head);

        // The tree: each group's path ("SET > Function > SPLIT") is a branch;
        // groups that share a path (the table repeats "SET > Function"
        // around its sub-menus) land in the same node, in table order.
        var root = { name: '', key: '', children: [], byName: {}, items: [] };
        (menu.table.groups || []).forEach(function (g) {
            var node = root;
            String(g.path || 'Other').split('>').map(function (s) { return s.trim(); }).forEach(function (seg) {
                if (!node.byName[seg]) {
                    var child = { name: seg, key: (node.key ? node.key + ' > ' : '') + seg, children: [], byName: {}, items: [] };
                    node.byName[seg] = child;
                    node.children.push(child);
                }
                node = node.byName[seg];
            });
            (g.items || []).forEach(function (it) { node.items.push(it); });
        });
        function count(n) {
            return n.items.length + n.children.reduce(function (s, c) { return s + count(c); }, 0);
        }
        this.nodes = [];
        var openKeys = this._openKeys();

        var search = h('input', 'wf-field setmenu-search');
        search.type = 'search';
        search.placeholder = 'Find a setting or item number';
        search.value = this.query || '';
        search.oninput = function () { self.query = search.value; self._applyFilter(); };
        bar.insertBefore(search, this.status);

        var tree = h('div', 'setmenu-tree');
        function build(node, parentEl, depth) {
            var det = h('details', 'setmenu-node depth-' + Math.min(depth, 2));
            var sum = h('summary');
            sum.appendChild(h('span', 'setmenu-node-name', node.name));
            var prog = h('span', 'setmenu-progress');
            sum.appendChild(prog);
            sum.appendChild(h('span', 'setmenu-count', String(count(node))));
            det.appendChild(sum);
            var body = h('div', 'setmenu-node-body');
            det.appendChild(body);
            if (openKeys.indexOf(node.key) >= 0) det.open = true;
            var rec = { node: node, el: det, rows: [], prog: prog, batch: null };
            self.nodes.push(rec);
            node.items.forEach(function (it) {
                var row = h('div', 'setmenu-row');
                var name = h('div', 'setmenu-name');
                name.appendChild(h('span', 'setmenu-label', it.name));
                name.appendChild(h('span', 'setmenu-item', it.item));
                if (it.firmware) name.appendChild(h('span', 'setmenu-item', 'fw ' + it.firmware));
                name.title = [it.note, it.source].filter(Boolean).join('\n');
                row.appendChild(name);
                var ctl = h('div', 'setmenu-ctl');
                row.appendChild(ctl);
                var st = h('div', 'setmenu-state');
                row.appendChild(st);
                body.appendChild(row);
                self.rows[it.item] = { item: it, row: row, ctl: ctl, st: st, node: rec };
                rec.rows.push(it.item);
                self._updateRow(it.item);
            });
            node.children.forEach(function (c) { build(c, body, depth + 1); });
            det.addEventListener('toggle', function () {
                if (det.open) self._readSection(rec);
                else self._cancelSection(rec);
                // What the search opens is not what the operator left open.
                if (!self._filtering && !String(self.query || '').trim()) self._saveOpen();
            });
            parentEl.appendChild(det);
        }
        root.children.forEach(function (c) { build(c, tree, 0); });
        this.tree = tree;
        this.el.appendChild(tree);
        this._applyFilter();
        // Sections left open last time: read what they show, top to bottom.
        this.nodes.filter(function (rec) { return rec.el.open; }).reverse()
            .forEach(function (rec) { self._readSection(rec); });
    };
    // Opening a section reads its own items (not its sub-sections'), one at
    // a time, ahead of anything still waiting: the section opened last is
    // the one being looked at.  Items read before are not asked again; the
    // row's READ is there for that.  The header counts the reads down.
    WfSetMenuPanel.prototype._readSection = function (rec) {
        var self = this, menu = this.menu;
        var todo = rec.rows.filter(function (no) {
            var st = menu.state[no];
            return !st || st.status === 'idle' || menu.readPending(no);
        });
        if (!todo.length) return;
        var batch = { total: todo.length, done: 0 };
        rec.batch = batch;
        this._showProgress(rec);
        menu.readMany(todo, { front: true }).forEach(function (p) {
            p.then(function (r) {
                if (rec.batch !== batch) return;   // closed, or opened again since
                if (r && r.cancelled) return;
                batch.done++;
                if (batch.done >= batch.total) rec.batch = null;
                self._showProgress(rec);
            });
        });
    };
    // Closing a section drops its reads still waiting (one on the wire ends).
    WfSetMenuPanel.prototype._cancelSection = function (rec) {
        this.menu.cancelReads(rec.rows);
        rec.batch = null;
        this._showProgress(rec);
    };
    WfSetMenuPanel.prototype._showProgress = function (rec) {
        var b = rec.batch;
        rec.prog.textContent = b ? 'reading ' + b.done + '/' + b.total : '';
    };
    WfSetMenuPanel.prototype._openKeys = function () {
        try { return JSON.parse(global.localStorage.getItem('wfSetMenuOpen') || '[]'); } catch (e) { return []; }
    };
    WfSetMenuPanel.prototype._saveOpen = function () {
        var keys = this.nodes.filter(function (r) { return r.el.open; }).map(function (r) { return r.node.key; });
        try { global.localStorage.setItem('wfSetMenuOpen', JSON.stringify(keys)); } catch (e) {}
    };
    // Search: rows whose name, item number or section match; the sections
    // that hold a match open (without being remembered as open), the rest hide.
    WfSetMenuPanel.prototype._applyFilter = function () {
        var self = this, q = String(this.query || '').trim().toLowerCase();
        this._filtering = true;
        var openKeys = this._openKeys();
        // Children are pushed after their parent: walk backwards so a parent
        // sees whether any child matched.
        var visible = {};
        for (var i = this.nodes.length - 1; i >= 0; i--) {
            var rec = this.nodes[i], any = false;
            rec.rows.forEach(function (no) {
                var r = self.rows[no], it = r.item;
                var hit = !q || (it.name + ' ' + it.item + ' ' + rec.node.key).toLowerCase().indexOf(q) >= 0;
                r.row.style.display = hit ? '' : 'none';
                if (hit) any = true;
            });
            rec.node.children.forEach(function (c) { if (visible[c.key]) any = true; });
            visible[rec.node.key] = any;
            rec.el.style.display = any ? '' : 'none';
            rec.el.open = q ? any : openKeys.indexOf(rec.node.key) >= 0;
        }
        // The toggle events of this pass fire after it (and read what they open).
        setTimeout(function () { self._filtering = false; }, 0);
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
                    var span = WfSetMenuCodec.shownRange(it);
                    field = doc.createElement('input');
                    field.className = 'wf-field';
                    field.type = 'number';
                    field.min = span[0];
                    field.max = span[1];
                    field.step = WfSetMenuCodec.unitStep(it);
                    field.value = WfSetMenuCodec.toShown(it, st.value);
                    field.title = span[0] + ' to ' + span[1] + (it.percent ? ' %' : it.unit ? ' ' + it.unit : '');
                    field.onchange = function () {
                        var raw = WfSetMenuCodec.fromShown(it, field.value);
                        if (isNaN(raw)) { self._updateRow(itemNo); return; }
                        self._write(itemNo, raw);
                    };
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
        b.className = 'wf-btn setmenu-rd';
        b.textContent = 'READ';
        b.title = 'Read ' + it.item + ' from the radio again';
        b.disabled = busy;
        b.onclick = function () { menu.read(itemNo); };
        r.ctl.appendChild(b);

        var text = { idle: '', reading: 'reading...', writing: 'writing...', ok: st.written ? 'written ✓' : '',
                     missing: 'not on this radio', error: '' }[st.status] || '';
        if (st.error && st.status !== 'missing') text = st.error;
        r.st.textContent = text;
        r.st.title = st.error || '';
        r.st.className = 'setmenu-state' + (st.status === 'missing' ? ' dim'
                                            : st.error || st.status === 'error' ? ' err' : st.written ? ' ok' : '');
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
