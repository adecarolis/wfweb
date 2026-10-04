// wfweb recorder — records what the operator hears and, for video, sees.
//
// Shared by both builds and entirely browser-side: the server is not involved.
// The host page feeds it PCM from the three places audio already flows through
//   Recorder.feedRx()     every received chunk          (handleAudioData)
//   Recorder.feedTx()     every 0x03 frame actually sent (sendTxAudio)
//   Recorder.feedTxPcm()  the 0x04 packet-TX tee         (handleBinaryMessage)
// and a mixer worklet turns those into one real-time mono track.
//
//   Audio recording → MP3, encoded by the vendored lamejs in a worker.
//   Video recording → a composited 1280x720 frame (status header + the live
//                     spectrum and waterfall; a CW or FT8 view when those are
//                     in use) captured with MediaRecorder, as MP4 where the
//                     browser can write it, else WebM.
//
// The finished file is handed to the browser as a download.
(function (global) {
'use strict';

var DEFAULT_RATE = 48000;
var MIC_RATE = 48000;            // micAudioCtx is fixed at 48 kHz in both builds
var MP3_KBPS = 64;
var VIDEO = { w: 1280, h: 720, fps: 15, bps: 2000000 };
var HEADER_H = 132;              // status header height inside the video frame
var METER_MAX_W = 600;           // widest the copied meter may be drawn
var CREDIT = { name: 'wfweb', url: 'wfweb.k1fm.us' };   // shown in every video frame
// digits-sprite.png, the page's frequency font: ten 25x25 cells (their left
// edges are not evenly spaced) followed by a 6 px wide decimal point.
var DIGITS = { h: 25, w: 25, x: [0, 25, 51, 75, 100, 125, 150, 175, 200, 225], dotX: 250, dotW: 6 };
// Recordings are held in memory until saved, so cap them: stop and save here.
var MAX_MS = { audio: 6 * 3600 * 1000, video: 30 * 60 * 1000 };
// First supported wins. MP4 plays everywhere; Firefox can only write WebM.
var VIDEO_TYPES = [
    'video/mp4;codecs=avc1.42E01F,mp4a.40.2',
    'video/mp4;codecs=avc1.42E01F,opus',
    'video/mp4',
    'video/webm;codecs=vp9,opus',
    'video/webm;codecs=vp8,opus',
    'video/webm'
];

var host = null;        // init() options
var btn = null;
var timeEl = null;      // elapsed-time label inside the button
var menu = null;
var rec = null;         // the recording in progress
var busy = false;       // starting or finalising — the button ignores taps
var uiTimer = null;

// ---------------------------------------------------------------------------
// Mixer worklet. Stringified into a Blob URL (same trick as the host's
// playback processor), so it must not reference anything outside itself.
//
// Both queues are delayed by the same 200 ms, which keeps the timeline
// consistent: while TX audio plays, RX is consumed in step and discarded, so
// RX resumes exactly where the transmission ended. TX replaces RX rather than
// adding to it — a rig with MONITOR on loops the transmit audio back into the
// RX stream and summing would record it twice.
// ---------------------------------------------------------------------------
function mixerWorklet() {
    var PCM_BLOCK = 4800;

    function Fifo() { this.q = []; this.off = 0; this.len = 0; }
    Fifo.prototype.push = function (d) { this.q.push(d); this.len += d.length; };
    // Move up to n samples into out, or just drop them when out is null.
    Fifo.prototype.take = function (out, n) {
        var done = 0;
        while (done < n && this.q.length) {
            var head = this.q[0], k = Math.min(n - done, head.length - this.off);
            if (out) out.set(head.subarray(this.off, this.off + k), done);
            done += k; this.off += k;
            if (this.off >= head.length) { this.q.shift(); this.off = 0; }
        }
        this.len -= done;
        return done;
    };

    class WfwebRecMixer extends AudioWorkletProcessor {
        constructor() {
            super();
            var self = this;
            this.rx = new Fifo(); this.tx = new Fifo();
            this.rxArmed = false; this.txArmed = false; this.txWait = 0;
            this.pcm = null; this.pcmPos = 0; this.done = false;
            this.port.onmessage = function (e) {
                var m = e.data;
                if (m.k === 'rx') self.rx.push(m.d);
                else if (m.k === 'tx') self.tx.push(m.d);
                else if (m.k === 'capture') self.pcm = new Int16Array(PCM_BLOCK);
                else if (m.k === 'stop') {
                    if (self.pcm && self.pcmPos) {
                        self.port.postMessage({ k: 'pcm', d: self.pcm.slice(0, self.pcmPos) });
                    }
                    self.pcm = null; self.done = true;
                    self.port.postMessage({ k: 'end' });
                }
            };
        }
        process(inputs, outputs) {
            var out = outputs[0][0], n = out.length;    // zero-filled on entry
            var rx = this.rx, tx = this.tx, delay = sampleRate * 0.2;

            if (!this.txArmed && tx.len) {
                this.txWait += n;
                if (this.txWait >= delay) this.txArmed = true;
            }
            if (this.txArmed) {
                tx.take(out, n);
                if (!tx.len) { this.txArmed = false; this.txWait = 0; }
                if (this.rxArmed && rx.take(null, n) < n) this.rxArmed = false;
            } else if (this.rxArmed) {
                if (rx.take(out, n) < n) this.rxArmed = false;   // underrun: prebuffer again
            } else if (rx.len >= delay) {
                this.rxArmed = true;
            }
            // A burst after a stall must not push the recording behind real time.
            if (rx.len > sampleRate) rx.take(null, rx.len - delay);

            if (this.pcm) {
                for (var i = 0; i < n; i++) {
                    var s = out[i];
                    this.pcm[this.pcmPos++] = (s < -1 ? -1 : s > 1 ? 1 : s) * 32767;
                    if (this.pcmPos === PCM_BLOCK) {
                        this.port.postMessage({ k: 'pcm', d: this.pcm }, [this.pcm.buffer]);
                        this.pcm = new Int16Array(PCM_BLOCK); this.pcmPos = 0;
                    }
                }
            }
            return !this.done;
        }
    }
    registerProcessor('wfweb-rec-mixer', WfwebRecMixer);
}

// MP3 encoder worker (also stringified). lamejs is loaded by absolute URL
// because a Blob worker has no base URL of its own.
function mp3Worker() {
    var enc = null;
    function emit(b) { if (b.length) self.postMessage({ k: 'mp3', d: b.buffer }, [b.buffer]); }
    self.onmessage = function (e) {
        var m = e.data;
        try {
            if (m.k === 'init') {
                try { importScripts(m.url); }
                catch (e) { throw new Error('the MP3 encoder could not be loaded'); }
                enc = new lamejs.Mp3Encoder(1, m.rate, m.kbps);
                self.postMessage({ k: 'ready' });
            } else if (m.k === 'pcm') {
                emit(enc.encodeBuffer(m.d));
            } else if (m.k === 'end') {
                emit(enc.flush());
                self.postMessage({ k: 'done' });
            }
        } catch (err) {
            self.postMessage({ k: 'error', msg: String((err && err.message) || err) });
        }
    };
}

function blobUrl(fn) {
    return URL.createObjectURL(new Blob(['(' + fn.toString() + ')();'], { type: 'application/javascript' }));
}

function assetUrl(path) {
    // fingerprint-static.py does not rewrite runtime-built URLs, so the
    // cache-buster is appended by hand (as cw-decoder.js does for its worker).
    var v = global.__WFWEB_V__ ? '?v=' + global.__WFWEB_V__ : '';
    return new URL(path, document.baseURI).href + v;
}

// ---------------------------------------------------------------------------
// PCM feeds
// ---------------------------------------------------------------------------

// Int16 at inRate → Float32 at outRate. Linear interpolation, with the last
// sample and the fractional phase carried in st so chunk boundaries are seamless.
function resample(st, pcm, inRate, outRate) {
    var n = pcm.length, out, i;
    if (inRate === outRate) {
        out = new Float32Array(n);
        for (i = 0; i < n; i++) out[i] = pcm[i] / 32768;
        return out;
    }
    var step = inRate / outRate, pos = st.pos, k = 0;
    out = new Float32Array(Math.max(0, Math.ceil((n - 1 - pos) / step)) + 1);
    while (pos < n - 1) {
        i = Math.floor(pos);
        var a = i < 0 ? st.prev : pcm[i], b = pcm[i + 1];
        out[k++] = (a + (b - a) * (pos - i)) / 32768;
        pos += step;
    }
    st.pos = pos - n;           // index -1 of the next chunk is this chunk's last sample
    st.prev = n ? pcm[n - 1] : st.prev;
    return out.subarray(0, k);
}

function post(r, kind, f32) {
    if (!f32.length) return;
    // subarray() results share a buffer that is longer than the view; copy so
    // the transfer hands over exactly these samples.
    if (f32.byteLength !== f32.buffer.byteLength) f32 = new Float32Array(f32);
    r.node.port.postMessage({ k: kind, d: f32 }, [f32.buffer]);
}

function feedRx(pcm, rate) {
    if (!rec) return;
    post(rec, 'rx', resample(rec.rxRs, pcm, rate || DEFAULT_RATE, rec.ctx.sampleRate));
}

// frame: a [0x03][tag][seq u16][0 u16][Int16 PCM] ArrayBuffer. modemRate is the
// rate of modem-tagged audio (FT8/FT4/JS8/tune); mic audio is always 48 kHz.
function feedTx(frame, modemRate) {
    if (!rec || !frame || frame.byteLength < 8) return;
    var head = new Uint8Array(frame, 0, 2);
    if (head[0] !== 0x03) return;
    var modem = (head[1] & 0x01) !== 0;
    // The mic streams for as long as it is enabled, keyed or not — only the
    // part that actually goes on the air belongs in the recording.
    if (!modem && !host.isTx()) return;
    var pcm = new Int16Array(frame, 6, (frame.byteLength - 6) >> 1);
    post(rec, 'tx', resample(rec.txRs, pcm, modem ? (modemRate || DEFAULT_RATE) : MIC_RATE, rec.ctx.sampleRate));
}

function feedTxPcm(pcm, rate) {
    if (!rec) return;
    post(rec, 'tx', resample(rec.txRs, pcm, rate || DEFAULT_RATE, rec.ctx.sampleRate));
}

// ---------------------------------------------------------------------------
// Audio (MP3)
// ---------------------------------------------------------------------------
function startMp3(r) {
    return new Promise(function (resolve, reject) {
        var url = blobUrl(mp3Worker);
        var w = r.worker = new Worker(url);
        var onDone = null, endTimer = null;
        r.ext = 'mp3';

        function endEncoder() { clearTimeout(endTimer); endTimer = null; w.postMessage({ k: 'end' }); }

        w.onmessage = function (e) {
            var m = e.data;
            if (m.k === 'mp3') r.parts.push(m.d);
            else if (m.k === 'ready') {
                URL.revokeObjectURL(url);
                r.node.port.postMessage({ k: 'capture' });
                resolve();
            } else if (m.k === 'done') { if (onDone) onDone(); }
            else if (m.k === 'error') {
                reject(new Error(m.msg));
                if (rec === r) abort(new Error(m.msg));
            }
        };
        w.onerror = function (e) {
            var err = new Error(e.message || 'MP3 encoder failed to load');
            reject(err);
            if (rec === r) abort(err);
        };

        r.node.port.onmessage = function (e) {
            var m = e.data;
            if (m.k === 'pcm') w.postMessage({ k: 'pcm', d: m.d }, [m.d.buffer]);
            else if (m.k === 'end' && endTimer) endEncoder();
        };

        r.finish = function () {
            return new Promise(function (res) {
                var giveUp = setTimeout(done, 5000);
                function done() {
                    clearTimeout(giveUp); onDone = null;
                    res(new Blob(r.parts, { type: 'audio/mpeg' }));
                }
                onDone = done;
                // The worklet flushes its last block and answers 'end'; a
                // suspended context never would, so close the encoder anyway.
                endTimer = setTimeout(endEncoder, 1000);
                r.node.port.postMessage({ k: 'stop' });
            });
        };

        w.postMessage({ k: 'init', url: assetUrl('lamejs/lame.min.js'), rate: r.ctx.sampleRate, kbps: MP3_KBPS });
    });
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
function videoType() {
    if (!global.MediaRecorder || !global.HTMLCanvasElement ||
        !HTMLCanvasElement.prototype.captureStream) return '';
    for (var i = 0; i < VIDEO_TYPES.length; i++) {
        if (MediaRecorder.isTypeSupported(VIDEO_TYPES[i])) return VIDEO_TYPES[i];
    }
    return '';
}

function videoExt(type) { return type.indexOf('video/mp4') === 0 ? 'mp4' : 'webm'; }

function cssVar(name, fallback) {
    var v = getComputedStyle(document.documentElement).getPropertyValue(name);
    return (v && v.trim()) || fallback;
}

function formatFreq(hz) {
    if (!(hz > 0)) return '---.---.---';
    hz = Math.round(hz);
    function p3(v) { return ('00' + v).slice(-3); }
    return Math.floor(hz / 1e6) + '.' + p3(Math.floor(hz / 1000) % 1000) + '.' + p3(hz % 1000);
}

// The page's digit font. Loaded before the first frame of the first video is
// drawn, so a recording never starts in one font and continues in another.
var digitSprite = null;
function loadDigits() {
    if (digitSprite) return Promise.resolve();
    return new Promise(function (resolve) {
        var img = new Image();
        var giveUp = setTimeout(done, 2000);
        function done() { clearTimeout(giveUp); digitSprite = img; resolve(); }
        img.onload = img.onerror = done;
        img.src = assetUrl('digits-sprite.png');
    });
}

// The frequency in the page's own digit font, h pixels tall, left edge at x;
// plain text for the whole recording if the sprite could not be loaded.
function drawFreq(r, text, x, y, h) {
    var g = r.g, img = digitSprite;
    if (!r.useSprite) {
        g.textAlign = 'left';
        g.font = 'bold ' + h + 'px ' + r.theme.mono;
        g.fillStyle = r.theme.text;
        g.fillText(text, x, y + h / 2);
        return;
    }
    var k = h / DIGITS.h;
    g.imageSmoothingQuality = 'high';
    g.globalCompositeOperation = 'lighten';     // drop the sprite's black cell background
    for (var i = 0; i < text.length; i++) {
        var ch = text.charAt(i);
        if (ch === '.') {
            g.drawImage(img, DIGITS.dotX, 0, DIGITS.dotW, DIGITS.h, x, y, DIGITS.dotW * k, h);
            x += DIGITS.dotW * k;
        } else {
            if (ch === '-') {                    // no frequency to show
                g.fillStyle = r.theme.dim;
                g.fillRect(x + DIGITS.w * k * 0.25, y + h / 2 - 2, DIGITS.w * k * 0.5, 4);
            } else {
                g.drawImage(img, DIGITS.x[+ch], 0, DIGITS.w, DIGITS.h, x, y, DIGITS.w * k, h);
            }
            x += DIGITS.w * k;
        }
    }
    g.globalCompositeOperation = 'source-over';
}

function drawSource(g, cv, x, y, w, h) {
    if (cv && cv.width && cv.height) g.drawImage(cv, x, y, w, h);
}

function byId(id) { return document.getElementById(id); }

// '2.4k' → '2.4 kHz', '500' → '500 Hz'; anything else ('FIL2') is shown as is.
function bwLabel(s) {
    s = String(s || '');
    if (/^[\d.]+k$/.test(s)) return s.slice(0, -1) + ' kHz';
    if (/^\d+$/.test(s)) return s + ' Hz';
    return s;
}

// A filled tag on even pixel edges. Filled rather than outlined: a 1 px
// coloured outline does not survive the video encoder's chroma subsampling.
function tile(g, x, y, w, h, fill, color, label) {
    g.fillStyle = fill;
    g.beginPath();
    if (g.roundRect) g.roundRect(x, y, w, h, 4); else g.rect(x, y, w, h);
    g.fill();
    g.fillStyle = color;
    g.textAlign = 'center';
    g.fillText(label, x + w / 2, y + h / 2 + 1);
    g.textAlign = 'left';
}

// Word-wrap text to at most n characters per line (the first line to `first`,
// when given); returns [start, end) offsets.
function wrap(text, n, first) {
    var lines = [], start = 0, w = first || n;
    while (text.length - start > w) {
        var cut = text.lastIndexOf(' ', start + w);
        if (cut <= start) cut = start + w;              // one very long word
        lines.push([start, cut]);
        start = text.charAt(cut) === ' ' ? cut + 1 : cut;
        w = n;
    }
    lines.push([start, text.length]);
    return lines;
}

// ---------------------------------------------------------------------------
// Mode views. The page's mode code is not modular, so — as js8-panel.mjs does
// with its host helpers — these readers take what they need straight from
// page globals and from what the page has already rendered. All page access
// for the mode views is in cwView(), ft8View(), js8View(), packetView() and
// freedvView(); each returns null when its mode is not in use.
// ---------------------------------------------------------------------------
var CW_PANEL_H = 250;
var CW_SEED_CHARS = 100;         // decoded text carried in when recording starts mid-QSO
var FT8_ROWS = 16;               // list rows that fit under the FT8 waterfall
var FDV_PANEL_H = 160;
var FDV_SPAN_MS = 60000;         // FreeDV timeline length
var FDV_SNR = [-5, 25];          // dB range of the FreeDV SNR trace

function cwView() {
    var mode = global.currentMode;
    if (mode !== 'CW' && mode !== 'CW-R') return null;
    var dec = global.CWDecoder && global.CWDecoder.state;
    var sent = global.cwCharacters || [];
    var decoding = !!(dec && dec.enabled);
    if (!decoding && !sent.length) return null;
    var partner = byId('cwQsoInput'), band = byId('cwFilterBand');
    return {
        decoding: decoding,
        rx: decoding ? dec.textBuffer : '',
        sent: sent,                                      // [{char, status: pending|sending|sent}]
        scope: decoding ? byId('cwScopeCanvas') : null,
        // The decoder's passband marker is a DOM overlay, positioned in percent.
        band: band && band.classList.contains('active')
            ? { top: parseFloat(band.style.top) / 100, height: parseFloat(band.style.height) / 100 } : null,
        wpm: global.cwSpeed,
        partner: partner ? partner.value.trim().toUpperCase() : ''
    };
}

function ft8View() {
    if (!global.digiBarVisible || typeof global.getDigiSlotInfo !== 'function') return null;
    var status = byId('digiStatus'), dx = byId('digiDxCall');
    var band = typeof global.getCurrentDigiBand === 'function' ? global.getCurrentDigiBand() : null;
    return {
        mode: global.digiMode || 'FT8',
        band: band ? band.label : '',
        slot: global.getDigiSlotInfo(),                  // {slotPhase, remaining, period}
        tx: !!global.digiTxActive,
        status: status ? status.textContent : '',
        dx: dx ? dx.value.trim().toUpperCase() : '',
        txFreq: global.digiTxFreq, rxFreq: global.digiRxFreq,
        lo: global.WF_FREQ_LOW || 300, hi: global.WF_FREQ_HIGH || 2800,
        waterfall: byId('digiWfCanvas'), labels: byId('digiWfLabelCanvas'),
        txMarker: markerOf('digiWfTxMarker'), rxMarker: markerOf('digiWfRxMarker'),
        activity: ft8Rows(byId('digiRxPanel')),
        mine: ft8Rows(byId('digiDirectedPanel'))
    };
}

// The newest rows of one of the page's decode lists, with the page's own
// classification (CQ, addressed to me, my TX, worked before, …) read off the
// row classes instead of being worked out a second time here.
function ft8Rows(panel) {
    var out = [];
    if (!panel) return out;
    function txt(el, sel) { var n = el.querySelector(sel); return n ? n.textContent : ''; }
    var kids = panel.children;
    for (var i = Math.max(0, kids.length - FT8_ROWS); i < kids.length; i++) {
        var el = kids[i], c = ' ' + el.className + ' ';
        if (c.indexOf(' digi-slot-divider ') >= 0) { out.push({ kind: 'divider', msg: el.textContent }); continue; }
        if (c.indexOf(' digi-logged-row ') >= 0) { out.push({ kind: 'logged', msg: el.textContent }); continue; }
        var star = el.querySelector('.digi-decode-star');
        out.push({
            kind: c.indexOf(' digi-tx-pending ') >= 0 ? 'txnow' : c.indexOf(' tx-row ') >= 0 ? 'tx'
                : c.indexOf(' directed ') >= 0 ? 'directed' : c.indexOf(' highlight ') >= 0 ? 'highlight'
                : c.indexOf(' cq-row ') >= 0 ? 'cq' : 'plain',
            worked: c.indexOf(' worked ') >= 0,
            snr: txt(el, '.digi-decode-snr'), dt: txt(el, '.digi-decode-dt'),
            freq: txt(el, '.digi-decode-freq'), msg: txt(el, '.digi-decode-msg'),
            ent: txt(el, '.digi-decode-ent'),
            star: star ? (star.className.indexOf('new-one') >= 0 ? 'one' : 'band') : ''
        });
    }
    return out;
}

// JS8 keeps its state private to its module, but everything a frame needs is
// on the page: the status strip, the waterfall, the heard list and the feed.
// #js8Feed is always the full monitor feed, even while a QSO tab is selected.
function js8View() {
    if (!document.body.classList.contains('js8-open')) return null;
    function txt(el, sel) { var n = el.querySelector(sel); return n ? n.textContent.trim() : ''; }
    function picked(id) { var el = byId(id); return el && el.selectedOptions[0] ? el.selectedOptions[0].textContent.trim() : ''; }
    function label(id) { var el = byId(id); return el ? el.textContent.trim() : ''; }
    var bar = byId('js8ProgressBar'), freq = byId('js8TxFreq'), i, el;
    var heard = [], rows = document.querySelectorAll('#js8Stations .js8-station');
    for (i = 0; i < rows.length && i < 20; i++) {
        heard.push({ call: txt(rows[i], '.js8-station-call'), age: txt(rows[i], '.js8-station-age'),
                     snr: txt(rows[i], '.js8-station-dbm') });
    }
    var feed = [];
    rows = document.querySelectorAll('#js8Feed .js8-feed-row');
    for (i = 0; i < rows.length && i < 24; i++) {            // newest first, as rendered
        el = rows[i];
        var badge = el.querySelector('.js8-tx-badge');
        var to = txt(el, '.to'), msg = clean(txt(el, '.msg'));
        // The wire text of a directed message starts with the callsign it is
        // addressed to; the "from → to" column already says that.
        if (to && msg.indexOf(to) === 0) msg = msg.slice(to.length).replace(/^[\s:]+/, '');
        feed.push({
            mine: el.classList.contains('from-me'), toMe: el.classList.contains('to-me'),
            ts: txt(el, '.ts'), from: txt(el, '.from'), to: to,
            msg: msg, snr: txt(el, '.snr'),
            badge: badge ? badge.textContent.trim() : '',
            badgeKind: badge ? badge.className.replace('js8-tx-badge', '').trim() : ''
        });
    }
    return {
        submode: picked('js8SubmodeSel'), band: picked('js8BandSel'),
        frac: bar ? (parseFloat(bar.style.width) || 0) / 100 : 0,
        tx: !!(bar && bar.classList.contains('tx')),
        clock: label('js8Clock'), status: label('js8Status'),
        txFreq: freq ? freq.value : '',
        lo: global.WF_FREQ_LOW || 300, hi: global.WF_FREQ_HIGH || 2800,
        waterfall: byId('js8WfCanvas'), txMarker: markerOf('js8WfTxMarker'),
        heard: heard, feed: feed
    };
}

// Packet exposes its whole state (Packet.state); the page's own renderers add
// nothing to it, so the state is read directly.
function packetView() {
    var s = global.Packet && global.Packet.state;
    if (!s || !s.visible) return null;
    var term = s.terminal || {}, aprs = s.aprs || {}, stations = [], k;
    for (k in (aprs.stations || {})) stations.push(aprs.stations[k]);
    stations.sort(function (a, b) { return (b.lastHeard || 0) - (a.lastHeard || 0); });
    return {
        baud: s.mode,
        own: term.compose ? term.compose.ownCall || '' : '',
        tx: !!s.txBusy || Date.now() < (s.txActiveUntilMs || 0),
        scope: byId('packetScopeCanvas'),
        frames: s.frames || [],                          // {ts, src, dst, path[], ftype, info, tx}
        tab: s.activeTab,                                // 'aprs' | 'term'
        session: term.sessions && term.activeSid ? term.sessions[term.activeSid] || null : null,
        stations: stations
    };
}

// FreeDV / RADE has no panel on the page, only an indicator chip over the scope.
function freedvView() {
    if (!global.freedvEnabled) return null;
    return {
        mode: global.freedvModeName || '',
        sync: !!global.freedvSync,
        snr: +global.freedvSNR || 0,
        fo: +global.freedvFreqOffset || 0,               // RADE only
        call: global.radeRxCallsign || ''                // latest received callsign, or ''
    };
}

// --- CW transcript ----------------------------------------------------------

// What was added to a rolling buffer that only keeps its last N characters:
// cur is prev plus new text, possibly with the front trimmed off.
function appended(prev, cur) {
    if (cur === prev) return '';
    if (cur.indexOf(prev) === 0) return cur.slice(prev.length);
    for (var n = Math.min(prev.length, cur.length); n > 0; n--) {
        if (prev.slice(prev.length - n) === cur.slice(0, n)) return cur.slice(n);
    }
    return cur;                                          // nothing in common: decoder restarted
}

function cwSay(log, dir, text) {
    var last = log.turns[log.turns.length - 1];
    if (!last || last.dir !== dir) {
        text = text.replace(/^\s+/, '');
        if (!text) return;                               // a lone space does not open a turn
        last = { dir: dir, text: '' };
        log.turns.push(last);
        if (log.turns.length > 40) log.turns.shift();
    }
    last.text = (last.text + text).replace(/\s+/g, ' ').slice(-1500);
}

// Fold what happened since the last frame into the conversation: decoded text
// extends an RX turn, keyed characters extend a TX turn.
function cwUpdate(r, v, tx) {
    var log = r.cw, i;
    if (!log) {
        // First CW frame: carry in the tail of what is already decoded, and
        // skip what was keyed before the recording began.
        log = r.cw = { turns: [], rxSeen: v.rx, txIdx: 0 };
        var tail = v.rx.slice(-CW_SEED_CHARS);
        if (tail.length < v.rx.length) tail = tail.slice(tail.indexOf(' ') + 1);
        cwSay(log, 'rx', tail);
        while (log.txIdx < v.sent.length && v.sent[log.txIdx].status !== 'pending') log.txIdx++;
    }
    var added = appended(log.rxSeen, v.rx);
    log.rxSeen = v.rx;
    // While keyed, anything decoded is the rig's own sidetone.
    if (added && !tx) cwSay(log, 'rx', added);

    if (v.sent.length < log.txIdx) log.txIdx = 0;        // keyer transcript was cleared
    for (i = log.txIdx; i < v.sent.length && v.sent[i].status !== 'pending'; i++) {
        cwSay(log, 'tx', v.sent[i].char);
    }
    log.txIdx = i;
    log.pending = '';
    for (; i < v.sent.length; i++) log.pending += v.sent[i].char;
}

function isCallsign(w) {                                 // same test as cw-decoder.js
    if (w.length === 3) return /^[KNW][0-9][A-Z]$/.test(w);
    return w.length >= 4 && w.length <= 8 && /^[A-Z0-9]{1,3}[0-9][A-Z]{1,4}$/.test(w);
}

var CW_COLORS = { rx: '#c8f5c8', tx: '#ffdf9e', pending: '#7a7360', call: '#ffee55' };

function drawCwPanel(r, v, top, st) {
    var g = r.g, W = VIDEO.w, H = VIDEO.h, T = r.theme, y = top;
    cwUpdate(r, v, st.tx);

    g.fillStyle = T.bar; g.fillRect(0, y, W, 30);
    g.font = 'bold 15px ' + T.mono;
    tile(g, 12, y + 4, 40, 22, '#0a0', '#000', 'CW');
    g.fillStyle = T.dim;
    g.fillText([v.wpm ? v.wpm + ' WPM' : '', v.partner ? 'QSO ' + v.partner : '']
        .filter(Boolean).join('   '), 64, y + 16);
    y += 34;

    if (v.scope && v.scope.width && v.scope.height) {
        g.drawImage(v.scope, 12, y, W - 24, 64);
        if (v.band) {
            g.fillStyle = 'rgba(255,255,255,0.55)';
            g.fillRect(12, y + Math.round(64 * v.band.top), W - 24, 2);
            g.fillRect(12, y + Math.round(64 * (v.band.top + v.band.height)), W - 24, 2);
        }
        y += 72;
    }

    // The conversation: one tagged block per turn, newest at the bottom.
    g.font = 'bold 20px ' + T.mono;
    var charW = g.measureText('M').width, textX = 76, lineH = 26;
    var perLine = Math.floor((W - textX - 12) / charW);
    var maxLines = Math.floor((H - 6 - y) / lineH);
    var turns = r.cw.turns, lines = [], t, i;
    var showPending = r.cw.pending && (!turns.length || turns[turns.length - 1].dir === 'tx');
    for (t = 0; t < turns.length; t++) {
        var last = t === turns.length - 1;
        var text = turns[t].text + (last && showPending ? r.cw.pending : '');
        var spans = wrap(text, perLine);
        for (i = 0; i < spans.length; i++) {
            lines.push({ dir: turns[t].dir, first: i === 0, text: text.slice(spans[i][0], spans[i][1]),
                         // offset within this line where not-yet-keyed text begins
                         dimFrom: last && showPending ? turns[t].text.length - spans[i][0] : Infinity });
        }
    }
    if (!turns.length && r.cw.pending) lines.push({ dir: 'tx', first: true, text: r.cw.pending, dimFrom: 0 });
    lines = lines.slice(-maxLines);

    var myCall = (st.call || '').toUpperCase();
    for (i = 0; i < lines.length; i++) {
        var ln = lines[i], ly = y + i * lineH + lineH / 2;
        if (ln.first || i === 0) {
            g.font = 'bold 15px ' + T.mono;
            if (ln.dir === 'tx') tile(g, 12, ly - 11, 48, 22, '#a00', '#fff', 'TX');
            else tile(g, 12, ly - 11, 48, 22, '#075', '#fff', 'RX');
            g.font = 'bold 20px ' + T.mono;
        }
        if (ln.dir === 'tx') {
            var cut = Math.max(0, Math.min(ln.text.length, ln.dimFrom));
            g.fillStyle = CW_COLORS.tx; g.fillText(ln.text.slice(0, cut), textX, ly);
            g.fillStyle = CW_COLORS.pending; g.fillText(ln.text.slice(cut), textX + cut * charW, ly);
        } else {
            var words = ln.text.split(/(\s+)/), col = 0;
            for (var k = 0; k < words.length; k++) {
                g.fillStyle = isCallsign(words[k]) && words[k] !== myCall ? CW_COLORS.call : CW_COLORS.rx;
                g.fillText(words[k], textX + col * charW, ly);
                col += words[k].length;
            }
        }
    }
}

// --- Shared pieces of the mode views -----------------------------------------

function clean(s) {                                      // one printable line
    return String(s == null ? '' : s).replace(/[\x00-\x1f\x7f]/g, '·');
}

function hhmmss(ms) { return new Date(ms).toISOString().substr(11, 8); }

// Slot / progress bar of a status strip.
function drawProgress(g, x, y, w, frac, tx) {
    g.fillStyle = '#1c2530'; g.fillRect(x, y, w, 10);
    g.fillStyle = tx ? '#d22' : '#28f';
    g.fillRect(x, y, Math.round(w * Math.max(0, Math.min(1, frac || 0))), 10);
}

// Title of a list column, with its rule; returns the y of the first row.
function listHeader(r, title, x, y, w, rule) {
    var g = r.g;
    g.font = 'bold 14px ' + r.theme.mono;
    g.fillStyle = r.theme.dim; g.fillText(title, x + 8, y + 12);
    g.fillStyle = rule; g.fillRect(x, y + 24, w, 2);
    return y + 28;
}

// The FT8 / JS8 audio waterfall: the page's canvas (plus its label canvas),
// then what the page shows as DOM overlays and so is not in the canvas — the
// translucent RX/TX marker bands and the frequency scale. Returns the y below.
function drawAudioWaterfall(r, y, h, canvas, labels, markers, lo, hi) {
    var g = r.g, W = VIDEO.w, i;
    drawSource(g, canvas, 0, y, W, h);
    if (labels && canvas && labels.width === canvas.width) drawSource(g, labels, 0, y, W, h);
    for (i = 0; i < markers.length; i++) {
        if (!markers[i].at) continue;
        g.fillStyle = markers[i].fill;
        g.fillRect(markers[i].at.left * W, y, Math.max(2, markers[i].at.width * W), h);
    }
    y += h;
    g.font = '13px ' + r.theme.mono;
    g.fillStyle = r.theme.dim;
    for (var f = lo; f <= hi; f += 500) {
        g.textAlign = f === lo ? 'left' : f + 500 > hi ? 'right' : 'center';
        g.fillText(f >= 1000 ? (f / 1000) + 'k' : String(f),
                   Math.max(4, Math.min(W - 4, (f - lo) / (hi - lo) * W)), y + 10);
    }
    g.textAlign = 'left';
    return y + 24;
}

// A DOM marker band positioned in percent (FT8 RX/TX, JS8 TX).
function markerOf(id) {
    var el = byId(id);
    if (!el || !el.style.left) return null;
    return { left: parseFloat(el.style.left) / 100, width: parseFloat(el.style.width) / 100,
             armed: el.classList.contains('armed') };
}

// --- FT8 / FT4 --------------------------------------------------------------

var FT8_ROW = {      // [row background, message colour] per kind of row
    plain:     [null,      '#cfe3f5'],
    cq:        ['#001c38', '#9cd0ff'],
    highlight: ['#002c58', '#ffffff'],
    directed:  ['#3a2800', '#ffd28a'],
    tx:        ['#002a00', '#a6f5a6'],
    txnow:     ['#5a1010', '#ffffff'],
    logged:    ['#002000', '#8fe88f']
};

function drawFt8List(r, title, rows, x, y, w, wide) {
    var g = r.g, T = r.theme, rowH = 22, i;
    y = listHeader(r, title, x, y, w, '#048');
    g.save();
    g.beginPath(); g.rect(x, y, w, FT8_ROWS * rowH); g.clip();
    g.font = 'bold 16px ' + T.mono;
    var msgX = x + (wide ? 172 : 116);
    for (i = 0; i < rows.length; i++) {
        var row = rows[i], cy = y + i * rowH + rowH / 2;
        if (row.kind === 'divider') {                    // start of a new slot
            g.fillStyle = '#048'; g.fillRect(x, cy - 1, w, 2);
            g.fillStyle = T.bg; g.fillRect(x, cy - 8, 88, 16);
            g.font = 'bold 13px ' + T.mono;
            g.fillStyle = '#7ab8ff'; g.fillText(row.msg, x + 8, cy);
            g.font = 'bold 16px ' + T.mono;
            continue;
        }
        var look = FT8_ROW[row.kind] || FT8_ROW.plain;
        if (look[0]) { g.fillStyle = look[0]; g.fillRect(x, y + i * rowH, w, rowH - 2); }
        if (row.kind === 'logged') { g.fillStyle = look[1]; g.fillText(row.msg, x + 8, cy); continue; }
        g.textAlign = 'right';
        g.fillStyle = row.kind === 'txnow' ? '#fff' : '#9cd0ff';
        g.fillText(row.kind === 'txnow' ? 'TX' : row.snr, x + 44, cy);
        if (wide) { g.fillStyle = '#7f8ca0'; g.fillText(row.dt, x + 100, cy); }
        g.fillStyle = '#5aa8ff'; g.fillText(row.freq, x + (wide ? 156 : 100), cy);
        g.textAlign = 'left';
        g.fillStyle = row.worked && row.kind !== 'directed' ? '#7d93a8' : look[1];
        g.fillText(row.msg, msgX, cy);
        var after = msgX + g.measureText(row.msg).width + 10;
        if (row.star) { g.fillStyle = row.star === 'one' ? '#ffc83d' : '#b8c2cf'; g.fillText('★', after, cy); }
        if (row.ent) {
            g.textAlign = 'right';
            g.fillStyle = '#8da4bd'; g.fillText(row.ent, x + w - 8, cy);
            g.textAlign = 'left';
        }
    }
    g.restore();
}

function drawFt8Body(r, v) {
    var g = r.g, W = VIDEO.w, T = r.theme, y = HEADER_H, x;

    // Status strip: mode, band, slot progress, status, DX call, audio frequencies.
    g.fillStyle = T.bar; g.fillRect(0, y, W, 32);
    g.font = 'bold 15px ' + T.mono;
    tile(g, 12, y + 5, 48, 22, '#06c', '#fff', v.mode);
    g.fillStyle = T.text; g.fillText(v.band, 72, y + 17);
    drawProgress(g, 130, y + 11, 240, v.slot.period ? v.slot.slotPhase / v.slot.period : 0, v.tx);
    g.fillStyle = T.dim; g.fillText(Math.ceil(v.slot.remaining) + ' s', 380, y + 17);
    g.fillStyle = v.tx ? '#ff8a8a' : T.text; g.fillText(v.status, 440, y + 17);
    g.textAlign = 'right';
    x = W - 12;
    g.fillStyle = '#7fe0a8'; g.fillText('RX ' + Math.round(v.rxFreq) + ' Hz', x, y + 17);
    x -= 150;
    g.fillStyle = '#ffb060'; g.fillText('TX ' + Math.round(v.txFreq) + ' Hz', x, y + 17);
    x -= 150;
    if (v.dx) { g.fillStyle = T.text; g.fillText('DX ' + v.dx, x, y + 17); }
    g.textAlign = 'left';

    y = drawAudioWaterfall(r, y + 36, 132, v.waterfall, v.labels, [
        { at: v.rxMarker, fill: 'rgba(0,200,100,0.28)' },
        { at: v.txMarker, fill: v.txMarker && v.txMarker.armed ? 'rgba(255,60,0,0.34)' : 'rgba(255,136,0,0.3)' }
    ], v.lo, v.hi);

    drawFt8List(r, 'Band activity', v.activity, 0, y, 736, true);
    drawFt8List(r, 'My QSO', v.mine, 752, y, W - 752, false);
}

// --- JS8 --------------------------------------------------------------------

function drawJs8Body(r, v) {
    var g = r.g, W = VIDEO.w, H = VIDEO.h, T = r.theme, y = HEADER_H, i;

    // Status strip: submode, slot progress, status, band — TX audio frequency.
    g.fillStyle = T.bar; g.fillRect(0, y, W, 32);
    g.font = 'bold 15px ' + T.mono;
    tile(g, 12, y + 5, 48, 22, '#c80', '#000', 'JS8');
    g.fillStyle = T.text; g.fillText(v.submode, 72, y + 17);
    var px = Math.max(250, 72 + g.measureText(v.submode).width + 20);
    drawProgress(g, px, y + 11, 200, v.frac, v.tx);
    g.fillStyle = T.dim; g.fillText(v.clock + ' s', px + 210, y + 17);
    g.fillStyle = v.tx ? '#ff8a8a' : T.text; g.fillText(v.status, px + 270, y + 17);
    g.fillStyle = T.dim; g.fillText(v.band, px + 340, y + 17);
    g.textAlign = 'right';
    g.fillStyle = '#ffb060'; g.fillText('TX ' + v.txFreq + ' Hz', W - 12, y + 17);
    g.textAlign = 'left';

    y = drawAudioWaterfall(r, y + 36, 110, v.waterfall, null, [
        { at: v.txMarker, fill: v.txMarker && v.txMarker.armed ? 'rgba(255,60,0,0.34)' : 'rgba(255,170,0,0.3)' }
    ], v.lo, v.hi);

    // Heard stations, most recent first.
    var rowH = 22, heardW = 250, hy = listHeader(r, 'Heard', 0, y, heardW, '#850');
    g.font = 'bold 16px ' + T.mono;
    for (i = 0; i < v.heard.length && hy + (i + 1) * rowH <= H; i++) {
        var s = v.heard[i], cy = hy + i * rowH + rowH / 2;
        g.fillStyle = '#ffe9c4'; g.fillText(s.call, 8, cy);
        g.textAlign = 'right';
        g.fillStyle = '#b89a66'; g.fillText(s.age, heardW - 84, cy);
        g.fillStyle = '#ffcf80'; g.fillText(s.snr, heardW - 8, cy);
        g.textAlign = 'left';
    }

    // Messages, newest at the top as on the page: time, from → to, then the
    // text, which wraps under itself for up to three lines.
    var mx = heardW + 16, mw = W - mx, my = listHeader(r, 'Messages', mx, y, mw, '#850');
    g.font = 'bold 16px ' + T.mono;
    var charW = g.measureText('M').width, perLine = Math.floor((mw - 16) / charW), line = 0;
    var maxLines = Math.floor((H - my) / rowH);
    for (i = 0; i < v.feed.length && line < maxLines; i++) {
        var m = v.feed[i];
        var addr = m.from + (m.to ? ' → ' + m.to : '');
        var head = 10 + (addr ? addr.length + 2 : 0);                // "HH:MM:SS  " + address
        var tail = m.badge ? m.badge.length + 2 : m.snr ? m.snr.length + 2 : 0;
        var spans = wrap(m.msg, perLine - 10 - tail, Math.max(8, perLine - head - tail)).slice(0, 3);
        var n = Math.min(spans.length, maxLines - line), top = my + line * rowH;
        if (m.mine || m.toMe) {
            g.fillStyle = m.mine ? '#2a0808' : '#062a10';
            g.fillRect(mx, top, mw, n * rowH - 2);
        }
        var ty = top + rowH / 2;
        g.fillStyle = '#b89a66'; g.fillText(m.ts, mx + 8, ty);
        var ax = mx + 8 + 10 * charW;
        g.fillStyle = '#ffcf80'; g.fillText(m.from, ax, ty);
        if (m.to) {
            g.fillStyle = '#b89a66'; g.fillText(' → ', ax + m.from.length * charW, ty);
            g.fillStyle = '#ffe14d'; g.fillText(m.to, ax + (m.from.length + 3) * charW, ty);
        }
        g.fillStyle = m.mine ? '#ffeaea' : m.toMe ? '#e6ffe6' : '#f2f2f2';
        for (var k = 0; k < n; k++) {
            g.fillText(m.msg.slice(spans[k][0], spans[k][1]),
                       mx + 8 + (k === 0 ? head : 10) * charW, ty + k * rowH);
        }
        g.textAlign = 'right';
        if (m.badge) {
            g.fillStyle = m.badgeKind === 'aborted' ? '#ff9a9a' : m.badgeKind === 'tx' ? '#ffb040' : '#c9a86a';
            g.fillText(m.badge, W - 8, ty);
        } else if (m.snr) {
            g.fillStyle = '#b89a66'; g.fillText(m.snr, W - 8, ty);
        }
        g.textAlign = 'left';
        line += n;
    }
}

// --- Packet -----------------------------------------------------------------

var PKT_COLORS = { rx: '#c8f5c8', tx: '#ffdf9e', pending: '#8a8168', info: '#8f9aa6' };

// A terminal scrollback as display lines. Entries are chunks of a byte stream:
// text carries its own line endings, so a chunk may continue the line the
// previous chunk of the same direction left open.
function termLines(entries) {
    var lines = [], open = null, i, j;
    for (i = 0; i < entries.length; i++) {
        var e = entries[i], dir = e.dir === 'tx' || e.dir === 'info' ? e.dir : 'rx';
        if (dir === 'info') { lines.push({ dir: dir, text: clean(e.data) }); open = null; continue; }
        var parts = String(e.data == null ? '' : e.data).split(/\r\n|\r|\n/);
        for (j = 0; j < parts.length; j++) {
            var lastPart = j === parts.length - 1;
            if (lastPart && parts[j] === '') { if (parts.length > 1) open = null; continue; }
            if (j === 0 && open && open.dir === dir) open.text += clean(parts[j]);
            else { open = { dir: dir, text: clean(parts[j]), pending: !!e.pending }; lines.push(open); }
            if (!lastPart) open = null;
        }
    }
    return lines;
}

function aprsAge(ms) {
    var s = Math.max(0, Math.round(ms / 1000));
    return s < 60 ? s + 's' : s < 3600 ? Math.floor(s / 60) + 'm' : s < 86400 ? Math.floor(s / 3600) + 'h' : Math.floor(s / 86400) + 'd';
}

function drawPacketBody(r, v) {
    var g = r.g, W = VIDEO.w, H = VIDEO.h, T = r.theme, y = HEADER_H, i, x;

    // Status strip: baud, own call, the active session — TX mark on the right.
    g.fillStyle = T.bar; g.fillRect(0, y, W, 32);
    g.font = 'bold 15px ' + T.mono;
    tile(g, 12, y + 5, 76, 22, '#0a0', '#000', 'PACKET');
    g.fillStyle = T.text; g.fillText(v.baud + ' bd', 100, y + 17);
    g.fillStyle = '#ffe98a'; g.fillText(v.own, 190, y + 17);
    if (v.session) {
        var st = v.session.state, up = st === 'connected';
        x = 190 + g.measureText(v.own).width + 24;
        var label = st.toUpperCase(), tw = Math.ceil(g.measureText(label).width) + 20;
        tile(g, x, y + 5, tw, 22, up ? '#075' : st === 'disconnected' ? '#333' : '#850', '#fff', label);
        g.fillStyle = T.text; g.fillText(v.session.peerCall || '', x + tw + 10, y + 17);
    }
    if (v.tx) tile(g, W - 60, y + 5, 48, 22, '#c00', '#fff', 'TX');
    y += 36;

    drawSource(g, v.scope, 0, y, W, 96);                 // the panel's modem spectrogram
    y += 104;

    // Monitor: every frame heard or sent, newest at the bottom.
    var rowH = 20, colW = 632, my = listHeader(r, 'Monitor', 0, y, colW, '#0a0');
    var rows = Math.floor((H - my) / rowH), frames = v.frames.slice(-rows);
    g.save();
    g.beginPath(); g.rect(0, my, colW, H - my); g.clip();
    g.font = 'bold 14px ' + T.mono;
    for (i = 0; i < frames.length; i++) {
        var f = frames[i], cy = my + i * rowH + rowH / 2;
        if (f.tx) { g.fillStyle = '#241200'; g.fillRect(0, my + i * rowH, colW, rowH - 2); }
        x = 8;
        var seg = function (text, color) { g.fillStyle = color; g.fillText(text, x, cy); x += g.measureText(text).width; };
        seg(hhmmss(f.ts) + ' ', '#8a94a0');
        seg(f.tx ? 'TX ' : '   ', '#ffa040');
        seg(clean(f.src), '#ffe98a'); seg('>', '#8a94a0'); seg(clean(f.dst), '#9fe8ff');
        if (f.path && f.path.length) seg(' via ' + clean(f.path.join(',')), '#8a94a0');
        seg(' [' + clean(f.ftype) + '] ', f.tx ? '#ffcf90' : '#9cd0ff');
        if (f.info) seg(clean(String(f.info).replace(/[\r\n]+$/, '')), PKT_COLORS.rx);
    }
    g.restore();

    // Right pane follows the page's tab: the terminal conversation, or APRS.
    var px = colW + 16, pw = W - px, py;
    g.save();
    g.beginPath(); g.rect(px, y, pw, H - y); g.clip();
    if (v.tab === 'term') {
        py = listHeader(r, 'Terminal' + (v.session ? ' · ' + (v.session.peerCall || '') : ''), px, y, pw, '#0a0');
        g.font = 'bold 15px ' + T.mono;
        var charW = g.measureText('M').width, perLine = Math.floor((pw - 16) / charW), out = [];
        var src = v.session ? termLines(v.session.scrollback.slice(-80)) : [];
        for (i = 0; i < src.length; i++) {
            var text = (src[i].dir === 'tx' ? '> ' : '') + src[i].text, spans = wrap(text, perLine);
            for (var k = 0; k < spans.length; k++) out.push({ line: src[i], text: text.slice(spans[k][0], spans[k][1]) });
        }
        out = out.slice(-Math.floor((H - py) / rowH));
        for (i = 0; i < out.length; i++) {
            var ln = out[i].line;
            g.fillStyle = ln.dir === 'info' ? PKT_COLORS.info
                : ln.dir === 'tx' ? (ln.pending ? PKT_COLORS.pending : PKT_COLORS.tx) : PKT_COLORS.rx;
            g.fillText(out[i].text, px + 8, py + i * rowH + rowH / 2);
        }
    } else {
        py = listHeader(r, 'APRS · ' + v.stations.length + ' heard', px, y, pw, '#0a0');
        g.font = 'bold 14px ' + T.mono;
        var now = Date.now(), fit = Math.floor((H - py) / rowH);
        for (i = 0; i < v.stations.length && i < fit; i++) {
            var s = v.stations[i], sy = py + i * rowH + rowH / 2;
            g.fillStyle = '#ffe98a'; g.fillText(clean(s.src), px + 8, sy);
            if (typeof s.lat === 'number' && typeof s.lon === 'number') {
                g.fillStyle = PKT_COLORS.rx;
                g.fillText(Math.abs(s.lat).toFixed(3) + (s.lat < 0 ? 'S' : 'N') + ' ' +
                           Math.abs(s.lon).toFixed(3) + (s.lon < 0 ? 'W' : 'E'), px + 104, sy);
            }
            g.fillStyle = '#b9c7d6'; g.fillText(clean(s.comment).slice(0, 34), px + 268, sy);
            g.textAlign = 'right';
            g.fillStyle = '#8fd48f'; g.fillText(aprsAge(now - s.lastHeard), px + pw - 8, sy);
            g.textAlign = 'left';
        }
    }
    g.restore();
}

// --- FreeDV / RADE ----------------------------------------------------------

// The page keeps only the latest received callsign (and the server clears it
// after a few seconds), so the recording keeps its own list: a callsign is
// logged when it appears or changes. Never gated on sync — the callsign
// arrives with the end-of-over frame, after sync is lost.
function freedvHeard(log, call, nowMs) {
    if (call && call !== log.lastCall) {
        log.heard.unshift({ t: hhmmss(nowMs), call: call });
        if (log.heard.length > 6) log.heard.pop();
    }
    log.lastCall = call;
}

function drawFreedvPanel(r, v, top, st) {
    var g = r.g, W = VIDEO.w, T = r.theme, y = top, now = Date.now(), i, x;
    var log = r.fdv || (r.fdv = { samples: [], heard: [], lastCall: '' });
    freedvHeard(log, v.call, now);
    var last = log.samples[log.samples.length - 1];
    if (!last || now - last.t >= 200) log.samples.push({ t: now, sync: v.sync, snr: v.snr, tx: st.tx });
    while (log.samples.length && now - log.samples[0].t > FDV_SPAN_MS) log.samples.shift();

    // Title strip: mode, sync, SNR, frequency offset (RADE only).
    g.fillStyle = T.bar; g.fillRect(0, y, W, 30);
    g.font = 'bold 15px ' + T.mono;
    tile(g, 12, y + 4, 76, 22, '#08a', '#fff', 'FreeDV');
    g.fillStyle = T.text; g.fillText(v.mode, 100, y + 16);
    x = 100 + g.measureText(v.mode).width + 20;
    tile(g, x, y + 4, v.sync ? 56 : 84, 22, v.sync ? '#075' : '#611', '#fff', v.sync ? 'SYNC' : 'NO SYNC');
    x += (v.sync ? 56 : 84) + 20;
    // Out of sync the modem reports a placeholder SNR, not a measurement.
    g.fillStyle = T.text; g.fillText('SNR ' + (v.sync ? v.snr.toFixed(0) + ' dB' : '\u2014'), x, y + 16);
    if (v.mode === 'RADE' && v.sync) { g.fillStyle = T.dim; g.fillText('FO ' + v.fo.toFixed(0) + ' Hz', x + 130, y + 16); }
    y += 34;

    // Last minute: SNR while in sync (green), my transmissions (red band).
    var gx = 12, gw = W - 24, gh = 84, perMs = gw / FDV_SPAN_MS;
    g.fillStyle = '#0a1016'; g.fillRect(gx, y, gw, gh);
    for (i = 0; i < log.samples.length; i++) {
        var s = log.samples[i], next = log.samples[i + 1];
        var x0 = gx + gw - (now - s.t) * perMs, w = Math.max(2, ((next ? next.t : now) - s.t) * perMs);
        if (s.tx) { g.fillStyle = 'rgba(210,50,50,0.45)'; g.fillRect(x0, y, w, gh); }
        else if (s.sync) {
            var lvl = Math.max(0.06, Math.min(1, (s.snr - FDV_SNR[0]) / (FDV_SNR[1] - FDV_SNR[0])));
            g.fillStyle = '#3ccf8a'; g.fillRect(x0, y + gh - Math.round(lvl * gh), w, Math.round(lvl * gh));
        }
    }
    g.font = '13px ' + T.mono;
    g.fillStyle = T.dim;
    g.fillText('SNR ' + FDV_SNR[0] + '…' + FDV_SNR[1] + ' dB while in sync · red = my transmissions · last 60 s', gx + 8, y + 12);
    y += gh + 8;

    g.font = 'bold 16px ' + T.mono;
    g.fillStyle = T.dim; g.fillText('Heard', 12, y + 12);
    x = 84;
    for (i = 0; i < log.heard.length; i++) {
        g.fillStyle = T.dim; g.fillText(log.heard[i].t, x, y + 12);
        x += g.measureText(log.heard[i].t).width + 10;
        g.fillStyle = '#9ff2ff'; g.fillText(log.heard[i].call, x, y + 12);
        x += g.measureText(log.heard[i].call).width + 32;
    }
}

function drawScopeBody(g, cv, top, bottom) {
    // Spectrum over waterfall, at the page's 35/65 split. The passband, VFO
    // marker and frequency axis are already painted into those canvases.
    var sh = Math.round((bottom - top) * 0.35);
    drawSource(g, cv.spectrum, 0, top, VIDEO.w, sh);
    drawSource(g, cv.waterfall, 0, top + sh, VIDEO.w, bottom - top - sh);
}

function drawFrame(r) {
    var g = r.g, W = VIDEO.w, H = VIDEO.h, T = r.theme;
    var st = host.getState(), cv = host.getCanvases();
    var x, text;

    g.fillStyle = T.bg; g.fillRect(0, 0, W, H);
    g.fillStyle = T.bar; g.fillRect(0, 0, W, 36);
    g.textBaseline = 'middle';
    g.textAlign = 'left';

    // Top strip: RX/TX tile, mode, bandwidth, split — rig, callsign, UTC on the right.
    g.font = 'bold 15px ' + T.mono;
    tile(g, 12, 6, 48, 24, st.tx ? '#c00' : '#3a0a0a', st.tx ? '#fff' : '#e77', st.tx ? 'TX' : 'RX');

    g.font = 'bold 18px ' + T.mono;
    x = 76;
    g.fillStyle = T.text; g.fillText(st.mode || '', x, 19);
    x += g.measureText(st.mode || '').width + 18;
    text = bwLabel(st.filter);
    g.fillStyle = T.dim; g.fillText(text, x, 19);
    x += g.measureText(text).width + 18;
    if (st.split) { g.fillStyle = T.warn; g.fillText('SPLIT', x, 19); }

    var iso = new Date().toISOString();
    text = [st.rig, st.call, iso.substr(0, 10) + ' ' + iso.substr(11, 8) + ' UTC']
        .filter(Boolean).join('   ');
    g.textAlign = 'right';
    g.font = '15px ' + T.mono;
    g.fillStyle = T.dim; g.fillText(text, W - 12, 19);

    // Meter, copied from the page at its own proportions: fitted into the
    // row, never enlarged (the page's meter is wide on a desktop, narrow on a
    // phone). The frequency sits to its right at exactly twice the sprite's
    // size — the same scale the page uses, and the sharpest it can be drawn.
    var rowY = 36, rowH = HEADER_H - rowY, mw = 0, m = cv.meter;
    if (m && m.width && m.height) {
        var k = Math.min(METER_MAX_W / m.width, (rowH - 8) / m.height, 1);
        var mh = Math.round(m.height * k);
        mw = Math.round(m.width * k);
        g.imageSmoothingQuality = 'high';
        g.drawImage(m, 12, rowY + Math.round((rowH - mh) / 2), mw, mh);
    }
    x = 12 + mw + 32;
    g.textAlign = 'left';
    g.font = 'bold 16px ' + T.mono;
    g.fillStyle = T.dim; g.fillText(st.vfo || '', x, 54);
    drawFreq(r, formatFreq(st.freq), x, 70, DIGITS.h * 2);

    // Credit, so a shared clip says what made it and where to find it.
    g.textAlign = 'right';
    g.font = 'bold 22px ' + T.mono;
    g.fillStyle = T.text; g.fillText(CREDIT.name, W - 12, 78);
    g.font = '15px ' + T.mono;
    g.fillStyle = T.dim; g.fillText(CREDIT.url, W - 12, 104);
    g.textAlign = 'left';

    // Body. The FT8, JS8 and packet panels replace the scope, as they do on
    // the page; CW and FreeDV add a panel under it; otherwise the scope fills
    // the frame. The page keeps these modes mutually exclusive.
    var view;
    if ((view = ft8View())) return drawFt8Body(r, view);
    if ((view = js8View())) return drawJs8Body(r, view);
    if ((view = packetView())) return drawPacketBody(r, view);
    var cw = cwView(), fdv = cw ? null : freedvView();
    var panelH = cw ? CW_PANEL_H : fdv ? FDV_PANEL_H : 0;
    drawScopeBody(g, cv, HEADER_H, H - panelH);
    if (cw) drawCwPanel(r, cw, H - panelH, st);
    else if (fdv) drawFreedvPanel(r, fdv, H - panelH, st);
}

function startVideo(r) {
    var type = videoType();
    if (!type) return Promise.reject(new Error('This browser cannot record video'));

    return loadDigits().then(function () {
        var c = document.createElement('canvas');
        c.width = VIDEO.w; c.height = VIDEO.h;
        r.g = c.getContext('2d');
        r.theme = {
            bg: '#05070c',                       // the scope's own background
            bar: cssVar('--bg-bar', '#111'),
            text: cssVar('--text', '#e0e0e0'),
            dim: cssVar('--text-dim', '#aaa'),
            warn: cssVar('--state-warn', '#ee0'),
            mono: cssVar('--font-mono', 'monospace')
        };
        r.ext = videoExt(type);
        r.useSprite = digitSprite.complete && digitSprite.naturalWidth > 0;
        drawFrame(r);
        r.drawTimer = setInterval(function () { drawFrame(r); }, 1000 / VIDEO.fps);

        var stream = r.stream = c.captureStream(VIDEO.fps);
        stream.addTrack(r.dest.stream.getAudioTracks()[0]);
        var mr = new MediaRecorder(stream, {
            mimeType: type, videoBitsPerSecond: VIDEO.bps, audioBitsPerSecond: 96000
        });
        mr.ondataavailable = function (e) { if (e.data && e.data.size) r.parts.push(e.data); };
        mr.onerror = function (e) { if (rec === r) abort(e.error || new Error('Video recorder failed')); };
        r.finish = function () {
            return new Promise(function (res) {
                mr.onstop = function () { res(new Blob(r.parts, { type: type.split(';')[0] })); };
                if (mr.state !== 'inactive') mr.stop(); else mr.onstop();
            });
        };
        mr.start(1000);
    });
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
function supported() {
    return !!((global.AudioContext || global.webkitAudioContext) &&
              global.AudioWorkletNode && global.Worker);
}

function slug(s) {
    return String(s || '').toUpperCase().replace(/[^A-Z0-9]+/g, '-').replace(/^-+|-+$/g, '');
}

// wfweb_<CALL>_<YYYYMMDD_HHMMSS>_<kHz>kHz_<MODE> — UTC, same stamp as the ADIF export.
function fileStem() {
    var st = host.getState(), iso = new Date().toISOString();
    var parts = ['wfweb'];
    if (slug(st.call)) parts.push(slug(st.call));
    parts.push(iso.substr(0, 10).replace(/-/g, '') + '_' + iso.substr(11, 8).replace(/:/g, ''));
    if (st.freq > 0) parts.push(Math.round(st.freq / 1000) + 'kHz');
    if (slug(st.mode)) parts.push(slug(st.mode));
    return parts.join('_');
}

function saveBlob(blob, name) {
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url; a.download = name; a.style.display = 'none';
    document.body.appendChild(a);
    a.click();
    setTimeout(function () { document.body.removeChild(a); URL.revokeObjectURL(url); }, 60000);
}

function teardown(r) {
    clearInterval(r.drawTimer);
    if (r.worker) r.worker.terminate();
    if (r.stream) r.stream.getTracks().forEach(function (t) { t.stop(); });
    if (r.ctx && r.ctx.state !== 'closed') r.ctx.close();
}

function onUnload(e) { e.preventDefault(); e.returnValue = ''; }

function setIdle() {
    clearInterval(uiTimer); uiTimer = null;
    global.removeEventListener('beforeunload', onUnload);
}

function fail(err) {
    console.warn('[recorder]', err);
    var note = document.createElement('div');
    note.className = 'rec-note';
    note.textContent = 'Recording failed: ' + ((err && err.message) || err);
    popup([note]);
}

function start(kind) {
    if (rec || busy || !host || !supported()) return Promise.resolve(false);
    closeMenu();
    var AC = global.AudioContext || global.webkitAudioContext;
    var r = { kind: kind, parts: [], rxRs: { pos: 0, prev: 0 }, txRs: { pos: 0, prev: 0 } };
    busy = true; render();
    try { r.ctx = new AC({ sampleRate: DEFAULT_RATE }); } catch (e) { r.ctx = new AC(); }
    var url = blobUrl(mixerWorklet);
    return r.ctx.audioWorklet.addModule(url).then(function () {
        URL.revokeObjectURL(url);
        r.node = new AudioWorkletNode(r.ctx, 'wfweb-rec-mixer',
            { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [1] });
        // Always connected: it keeps the worklet pulled, and it is the audio
        // track of a video recording. Nothing goes to the speakers.
        r.dest = r.ctx.createMediaStreamDestination();
        r.node.connect(r.dest);
        if (r.ctx.state === 'suspended') r.ctx.resume();
        return kind === 'video' ? startVideo(r) : startMp3(r);
    }).then(function () {
        r.t0 = Date.now();
        r.name = fileStem();
        rec = r; busy = false;
        uiTimer = setInterval(tick, 1000);
        global.addEventListener('beforeunload', onUnload);
        render();
        return true;
    }).catch(function (err) {
        teardown(r);
        busy = false; render();
        fail(err);
        return false;
    });
}

// Resolves with { name, blob } once the file has been handed to the browser.
function stop() {
    if (!rec || busy) return Promise.resolve(null);
    var r = rec;
    rec = null; busy = true;
    setIdle(); render();
    return r.finish().then(function (blob) {
        teardown(r);
        var name = r.name + '.' + r.ext;
        if (blob.size) saveBlob(blob, name);
        busy = false; render();
        return { name: name, blob: blob };
    });
}

function abort(err) {
    var r = rec;
    rec = null;
    setIdle();
    if (r) teardown(r);
    render();
    fail(err);
}

function tick() {
    if (!rec) return;
    render();
    if (Date.now() - rec.t0 >= MAX_MS[rec.kind]) stop();
}

// ---------------------------------------------------------------------------
// Top-bar button + type menu
// ---------------------------------------------------------------------------
function render() {
    if (!btn) return;
    btn.classList.toggle('recording', !!rec);
    // Lets theme.css make room for the elapsed time on phones.
    document.body.classList.toggle('rec-on', !!rec);
    btn.disabled = busy;
    if (rec) {
        var s = Math.floor((Date.now() - rec.t0) / 1000);
        timeEl.textContent = Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
        btn.title = 'Stop recording and save';
    } else {
        btn.title = 'Record';
    }
}

function onOutside(e) {
    if (menu && !menu.contains(e.target) && !btn.contains(e.target)) closeMenu();
}
function onKey(e) { if (e.key === 'Escape') closeMenu(); }

function closeMenu() {
    if (!menu) return;
    document.removeEventListener('pointerdown', onOutside, true);
    document.removeEventListener('keydown', onKey, true);
    menu.parentNode.removeChild(menu);
    menu = null;
}

// Small popover anchored under the button: the type menu, or a failure note.
function popup(children) {
    closeMenu();
    if (!btn) return;
    menu = document.createElement('div');
    menu.className = 'rec-menu';
    children.forEach(function (c) { menu.appendChild(c); });
    document.body.appendChild(menu);
    var r = btn.getBoundingClientRect();
    menu.style.top = (r.bottom + 4) + 'px';
    menu.style.left = Math.max(4, Math.min(r.left, global.innerWidth - menu.offsetWidth - 4)) + 'px';
    document.addEventListener('pointerdown', onOutside, true);
    document.addEventListener('keydown', onKey, true);
}

function openMenu() {
    function item(label, kind) {
        var b = document.createElement('button');
        b.className = 'wf-btn';
        b.textContent = label;
        b.addEventListener('click', function () { start(kind); });
        return b;
    }
    var items = [item('Audio (MP3)', 'audio')];
    var type = videoType();
    if (type) items.push(item('Video (' + videoExt(type).toUpperCase() + ')', 'video'));
    popup(items);
}

function onButton() {
    if (busy) return;
    if (rec) stop();
    else if (menu) closeMenu();
    else openMenu();
}

// opts: { button, isTx(), getState(), getCanvases() }
//   getState()    → { freq, mode, filter, vfo, split, tx, rig, call }
//   getCanvases() → { meter, spectrum, waterfall }
function init(opts) {
    host = opts;
    btn = opts.button || null;
    if (!btn) return;
    // AudioWorklet needs a secure context; without it there is nothing to offer.
    if (!supported()) { btn.style.display = 'none'; return; }
    // Two labels, switched by theme.css: "REC" when idle; while recording, a
    // stop square (drawn by the stylesheet) and the elapsed time.
    var idle = document.createElement('span');
    idle.className = 'rec-idle';
    idle.textContent = btn.textContent;
    timeEl = document.createElement('span');
    timeEl.className = 'rec-time';
    btn.textContent = '';
    btn.appendChild(idle);
    btn.appendChild(timeEl);
    btn.addEventListener('click', onButton);
    render();
}

global.Recorder = {
    init: init,
    feedRx: feedRx,
    feedTx: feedTx,
    feedTxPcm: feedTxPcm,
    start: start,
    stop: stop,
    isRecording: function () { return !!rec; }
};

})(window);
