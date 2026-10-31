// RTTY bar — the RTTY companion of the CW bar, one copy for both builds.
// Opens when the rig enters RTTY / RTTY-R (updateMode -> RttyPanel.onModeChange),
// docks at the bottom of the scope area like #cwBar and holds:
//   - the DeepRTTY decoder (rtty-decoder-worker.js): the rig's RX PCM from
//     handleAudioData goes to the worker as it is (RttyPanel.feedRx); the
//     worker moves the tone pair at MARK / MARK + 170 Hz onto the model's
//     timeline, so nothing here depends on the audio rate;
//   - a tuning strip (the translated spectrum, centre +/- 300 Hz, mark and
//     space lines): a click moves the dial so the clicked tone lands on
//     the pair, the way the CW scope does;
//   - the decoded text, teleprinter style, committed bright and the
//     worker's pending tail dim; callsigns are links into the QSO field;
//   - CALL / QSO / RST fields and LOG QSO (addQsoLogEntry, mode RTTY);
//   - AFSK transmit: macros and a compose line keyed through the FT8 TX
//     path (streamDigiAudio, setPTT {digital: true}).  The rig's own RTTY
//     mode keys FSK from its key jack, which CI-V cannot drive, so TX
//     needs the rig on a sideband: the AFSK button puts it on LSB (the
//     standard 2125 / 2295 polarity) and closing the bar puts it back.
//
// Polarity: mark is the lower audio tone on LSB and in the rig's RTTY /
// RTTY-R modes, the upper one on USB; REV flips it for a station sending
// the other way round.  Settings: wfRttyMark (2125 / 1615 / 1275),
// wfRttyRev, wfRttyMacros, wfRttyDecoder ('deeprtty' | 'classic' pins
// an engine; the default is DeepRTTY where WebAssembly SIMD exists and
// the classic FSK demodulator otherwise, or when the model fails to load
// or cannot keep up).
(function () {
    'use strict';

    var MARKS = [2125, 1615, 1275];
    var SHIFT = 170;
    var SPEC_BINS = 80, SPEC_LO = 550, SPEC_BIN_HZ = 6.25;   // what the worker sends (rtty-decoder-worker.js): centre +/- 250 Hz
    var MODEL_CENTER = 800;
    var ENGINE_KEY = 'wfRttyDecoder';
    var SLOW_TICKS = 4, SLOW_TICK_MS = 2500;
    var MODEL_LOAD_TIMEOUT_MS = 180000;
    var MAX_LINES = 120;
    var DEFAULT_MACROS = [
        { label: 'CQ',  text: 'CQ CQ CQ DE [CALL] [CALL] [CALL] PSE K' },
        { label: 'ANS', text: '[QSO] DE [CALL] [CALL] [CALL] K' },
        { label: 'RPT', text: '[QSO] DE [CALL] UR RST [RST] [RST] [RST] [QSO] DE [CALL] K' },
        { label: 'TU',  text: '[QSO] DE [CALL] TU 73 SK' },
        { label: 'QRZ', text: 'QRZ? DE [CALL] [CALL] K' },
    ];

    var S = {
        visible: false, suspended: false,
        afsk: false, afskFrom: null,        // { mode, filter } to put back when the bar closes
        savedModLevel: -1,
        decoding: false, loading: false, loaded: false, loadProgress: 0,
        engine: 'deeprtty', engineReason: '',
        mark: 2125, rev: false,
        lines: [], pending: '',             // [{ dir: 'rx' | 'tx', text }]
        txBusy: false,
        macros: null,
        editMacros: false,
        suppressOpenUntil: 0,
    };
    var worker = null, loadTimer = null, tickMs = [];
    var bar = null, scope = null, scopeCtx = null, column = null;
    var colorLUT = null;
    var host = function (name) { return window[name]; };

    // ------------------------------------------------------------------
    // Markup
    // ------------------------------------------------------------------
    function build() {
        var cwBar = document.getElementById('cwBar');
        if (!cwBar || document.getElementById('rttyBar')) return;
        var el = document.createElement('div');
        el.id = 'rttyBar';
        el.className = 'rtty-bar mode-rtty wf-panel hidden';
        el.innerHTML =
            '<div class="rtty-bar-header wf-panel-header">' +
              '<span class="rtty-label wf-badge">RTTY</span>' +
              '<button id="rttyDecodeBtn" class="wf-btn toggle" title="Decode RTTY with DeepRTTY (neural, 45.45 Bd / 170 Hz)">DECODE</button>' +
              '<select id="rttyMarkSel" class="wf-field rtty-mark-sel" title="Mark tone: where the rig puts the mark (its RTTY MARK FREQUENCY setting; on a sideband, the AFSK tone pair). Space is 170 Hz away."></select>' +
              '<button id="rttyRevBtn" class="wf-btn toggle" title="Reverse: swap mark and space. Mark is the lower tone on LSB and in the rig\'s RTTY modes, the upper one on USB; press this when a station comes out as garbage.">REV</button>' +
              '<button id="rttyAfskBtn" class="wf-btn toggle" title="AFSK: put the rig on LSB and send RTTY as audio tones through the data input. The rig\'s RTTY mode keys FSK from its key jack, which wfweb cannot drive. Closing the bar puts the mode back.">AFSK</button>' +
              '<button id="rttyLogBtn" class="wf-btn">LOG<span class="rtty-wide"> QSO</span></button>' +
              '<button id="rttyClearBtn" class="wf-btn" title="Clear the text">CLR</button>' +
              '<button id="rttyStopBtn" class="wf-btn danger" title="Stop transmitting">STOP</button>' +
              '<button id="rttyCloseBtn" class="wf-btn sq">✕</button>' +
            '</div>' +
            '<div class="rtty-scope-row">' +
              '<canvas id="rttyScopeCanvas" title="Click a signal to tune it onto the mark / space pair"></canvas>' +
              '<div class="rtty-tone-line rtty-mark-line"><span>MARK</span></div>' +
              '<div class="rtty-tone-line rtty-space-line"><span>SPACE</span></div>' +
              '<div class="rtty-scale"><span class="rtty-scale-hi"></span><span class="rtty-scale-lo"></span></div>' +
              '<span id="rttyEngineTag" class="rtty-engine-tag" hidden>CLASSIC</span>' +
            '</div>' +
            '<div id="rttyText" class="rtty-text"></div>' +
            '<div class="rtty-callsign-row">' +
              '<span class="rtty-callsign-label">CALL:</span>' +
              '<input type="text" id="rttyCallInput" class="rtty-callsign-input wf-field" maxlength="10" autocomplete="off">' +
              '<span class="rtty-callsign-label">QSO:</span>' +
              '<input type="text" id="rttyQsoInput" class="rtty-callsign-input wf-field" maxlength="10" autocomplete="off">' +
              '<span class="rtty-callsign-label">RST:</span>' +
              '<input type="text" id="rttyRstSent" class="rtty-rst-input wf-field" maxlength="3" value="599" title="RST sent" autocomplete="off">' +
              '<input type="text" id="rttyRstRcvd" class="rtty-rst-input wf-field" maxlength="3" value="599" title="RST received" autocomplete="off">' +
            '</div>' +
            '<div class="rtty-macro-grid" id="rttyMacroGrid"></div>' +
            '<div class="rtty-compose-row">' +
              '<input type="text" id="rttyInput" class="rtty-input wf-field" placeholder="TYPE AND PRESS ENTER TO SEND..." autocomplete="off" autocapitalize="characters">' +
              '<button id="rttySendBtn" class="wf-btn">SEND</button>' +
            '</div>';
        cwBar.parentNode.insertBefore(el, cwBar.nextSibling);
        bar = el;
        scope = document.getElementById('rttyScopeCanvas');
        scopeCtx = scope.getContext('2d');

        var sel = document.getElementById('rttyMarkSel');
        MARKS.forEach(function (m) {
            var o = document.createElement('option');
            o.value = m; o.textContent = m + ' Hz';
            sel.appendChild(o);
        });
        loadPrefs();
        sel.value = S.mark;
        sel.addEventListener('change', function () { S.mark = parseInt(sel.value, 10) || 2125; savePrefs(); configureWorker(); });
        document.getElementById('rttyRevBtn').addEventListener('click', function () { S.rev = !S.rev; savePrefs(); configureWorker(); updateButtons(); });
        document.getElementById('rttyDecodeBtn').addEventListener('click', function () { if (S.decoding) stopDecoder(); else startDecoder(); });
        document.getElementById('rttyAfskBtn').addEventListener('click', toggleAfsk);
        document.getElementById('rttyLogBtn').addEventListener('click', logQso);
        document.getElementById('rttyClearBtn').addEventListener('click', clearText);
        document.getElementById('rttyStopBtn').addEventListener('click', stopTx);
        document.getElementById('rttyCloseBtn').addEventListener('click', function () { hide({ restore: true }); });
        document.getElementById('rttySendBtn').addEventListener('click', sendCompose);
        document.getElementById('rttyInput').addEventListener('keydown', function (e) {
            if (e.key === 'Enter') { e.preventDefault(); sendCompose(); }
            e.stopPropagation();           // the page's shortcuts must not see typing
        });
        var callEl = document.getElementById('rttyCallInput');
        callEl.value = myCall();
        callEl.addEventListener('input', function () {
            if (window.App && window.App.callsign) window.App.callsign.set(callEl.value);
            renderMacros();
        });
        window.addEventListener('appCallsignChanged', function (e) {
            if (callEl.value !== e.detail) { callEl.value = e.detail; renderMacros(); }
        });
        document.getElementById('rttyQsoInput').addEventListener('input', renderMacros);
        document.getElementById('rttyRstSent').addEventListener('input', renderMacros);
        document.getElementById('rttyText').addEventListener('click', function (e) {
            var span = e.target.closest('.rtty-call');
            if (!span) return;
            document.getElementById('rttyQsoInput').value = span.dataset.call;
            renderMacros();
        });
        scope.addEventListener('click', scopeClick);
        scope.addEventListener('wheel', function (e) {
            e.preventDefault();
            var f = host('currentFreq') || 0;
            if (f > 0) hostSend({ cmd: 'setFrequency', value: Math.round(f + (e.deltaY < 0 ? 50 : -50)) });
        }, { passive: false });
        if (window.ResizeObserver) {
            new ResizeObserver(function () {
                sizeScope();
                if (typeof window.resizeCanvases === 'function') window.resizeCanvases();
            }).observe(el);
        }
        renderMacros();
        updateButtons();
        colorLUT = buildColorLUT();
        // The page's first status can land before this script has loaded
        // (the parser yields between external scripts): a rig already in
        // RTTY at page load would never reach onModeChange.  Catch up now.
        if (isRttyMode(mode())) onModeChange(mode(), '');
    }

    function loadPrefs() {
        try {
            var m = parseInt(localStorage.getItem('wfRttyMark'), 10);
            if (MARKS.indexOf(m) >= 0) S.mark = m;
            S.rev = localStorage.getItem('wfRttyRev') === '1';
            var mac = JSON.parse(localStorage.getItem('wfRttyMacros') || 'null');
            if (Array.isArray(mac) && mac.length === DEFAULT_MACROS.length) S.macros = mac;
        } catch (e) { /* no storage */ }
        if (!S.macros) S.macros = DEFAULT_MACROS.map(function (m) { return { label: m.label, text: m.text }; });
    }
    function savePrefs() {
        try {
            localStorage.setItem('wfRttyMark', String(S.mark));
            localStorage.setItem('wfRttyRev', S.rev ? '1' : '0');
            localStorage.setItem('wfRttyMacros', JSON.stringify(S.macros));
        } catch (e) { /* no storage */ }
    }

    // ------------------------------------------------------------------
    // Mode and polarity
    // ------------------------------------------------------------------
    function mode() { return String(host('currentMode') || '').toUpperCase(); }
    function isRttyMode(m) { return m === 'RTTY' || m === 'RTTY-R'; }
    function isSideband(m) { return m === 'LSB' || m === 'USB'; }
    // Mark on the upper tone?  USB puts it there; REV flips whatever the mode says.
    function markHigh() { return (mode() === 'USB') !== S.rev; }
    function centerHz() { return S.mark + SHIFT / 2; }
    // Dial direction for a click on the strip: on the LSB side a higher
    // audio tone is a lower RF, so the dial goes up to bring it down.
    function dialSign() { var m = mode(); return (m === 'USB' || m === 'RTTY-R') ? -1 : 1; }
    function afskAllowed() { return isSideband(mode()); }
    function myCall() { return (window.App && window.App.callsign && window.App.callsign.get()) || ''; }
    function hostSend(msg) { var s = host('send'); if (typeof s === 'function') s(msg); }

    // ------------------------------------------------------------------
    // Open / close
    // ------------------------------------------------------------------
    function show() {
        if (!bar) build();
        if (!bar) return;
        if (typeof window.closeOtherModes === 'function') window.closeOtherModes('rtty');
        S.visible = true;
        S.suspended = false;
        bar.classList.remove('hidden');
        sizeScope();
        updateButtons();
        if (typeof window.renderBottomBar === 'function') window.renderBottomBar();
        if (typeof window.resizeCanvases === 'function') window.resizeCanvases();
        if (!S.decoding && !S.loading) startDecoder();
    }

    // opts.restore: put the rig back where AFSK found it (the bar's own
    // close button).  closeOtherModes and a mode the user changed by hand
    // pass false: the mode is somebody else's business then.
    function hide(opts) {
        opts = opts || {};
        var wasVisible = S.visible || S.suspended;
        S.visible = false;
        S.suspended = false;
        if (bar) bar.classList.add('hidden');
        if (!wasVisible) return;
        stopTx();
        stopDecoder();
        if (S.afsk) endAfsk(opts.restore !== false);   // a mode put back there must not reopen us (suppressOpenUntil)
        if (typeof window.renderBottomBar === 'function') window.renderBottomBar();
        if (typeof window.resizeCanvases === 'function') window.resizeCanvases();
    }

    // The page's setPanel hides the bars while an overlay is up.
    function suspend() {
        if (!S.visible) return;
        S.visible = false; S.suspended = true;
        bar.classList.add('hidden');
    }
    function resume() {
        if (!S.suspended) return;
        S.suspended = false;
        if (isRttyMode(mode()) || S.afsk) show();
    }

    function onModeChange(m, prev) {
        m = String(m || '').toUpperCase();
        if (isRttyMode(m)) {
            if (!S.visible && !S.suspended && Date.now() >= S.suppressOpenUntil) {
                if (host('currentPanel') === 'default') show();
                else { if (!bar) build(); S.suspended = true; if (!S.decoding) startDecoder(); }
            }
        } else if (S.afsk && isSideband(m) && S.afskPending) {
            S.afskPending = false;          // our own switch to LSB
        } else if (S.visible || S.suspended) {
            hide({ restore: false });       // the user left on their own
        }
        if (S.visible) { configureWorker(); updateButtons(); }
    }

    function toggleAfsk() {
        if (S.afsk) { endAfsk(true); updateButtons(); return; }
        var m = mode();
        if (!isRttyMode(m)) return;
        S.afsk = true;
        S.afskPending = true;
        S.afskFrom = { mode: m, filter: host('currentFilter') };
        var slider = document.getElementById('micGainSlider');
        S.savedModLevel = slider ? parseInt(slider.value, 10) : -1;
        if (typeof window.throttledSend === 'function' && typeof window.DIGI_MOD_LEVEL === 'number') {
            window.throttledSend('micGain', 'setMicGain', window.DIGI_MOD_LEVEL);
            if (typeof window.updateSlider === 'function') window.updateSlider('micGain', window.DIGI_MOD_LEVEL);
        }
        if (typeof window.setMode === 'function') window.setMode('LSB');
        updateButtons();
    }
    function endAfsk(restore) {
        if (!S.afsk) return;
        S.afsk = false; S.afskPending = false;
        if (S.savedModLevel >= 0 && typeof window.throttledSend === 'function') {
            window.throttledSend('micGain', 'setMicGain', S.savedModLevel);
            if (typeof window.updateSlider === 'function') window.updateSlider('micGain', S.savedModLevel);
        }
        S.savedModLevel = -1;
        var from = S.afskFrom; S.afskFrom = null;
        if (restore && from && isSideband(mode())) {
            S.suppressOpenUntil = Date.now() + 1500;
            hostSend({ cmd: 'setMode', value: from.mode, filter: from.filter });
        }
    }

    // ------------------------------------------------------------------
    // Decoder
    // ------------------------------------------------------------------
    function enginePreference() { try { return localStorage.getItem(ENGINE_KEY) || 'auto'; } catch (e) { return 'auto'; } }
    function wasmSimdSupported() {
        try {
            return typeof WebAssembly === 'object' && WebAssembly.validate(new Uint8Array([
                0, 97, 115, 109, 1, 0, 0, 0, 1, 5, 1, 96, 0, 1, 123, 3, 2, 1, 0,
                10, 10, 1, 8, 0, 65, 0, 253, 15, 253, 98, 11]));
        } catch (e) { return false; }
    }
    function chooseEngine() {
        var pref = enginePreference();
        if (pref === 'classic') return ['classic', 'set by ' + ENGINE_KEY];
        if (pref === 'deeprtty') return ['deeprtty', ''];
        if (!wasmSimdSupported()) return ['classic', 'no WebAssembly SIMD in this browser'];
        return ['deeprtty', ''];
    }
    function setEngine(engine, reason) {
        S.engine = engine; S.engineReason = reason || '';
        var tag = document.getElementById('rttyEngineTag');
        if (tag) { tag.hidden = engine !== 'classic'; tag.title = reason || ''; }
        if (engine === 'classic') console.log('[RTTY] engine: classic FSK (' + (reason || 'chosen') + ')');
    }

    function startDecoder() {
        if (S.decoding || S.loading) return;
        S.loading = true; S.loaded = false; S.loadProgress = 0; tickMs = [];
        updateButtons();
        var pick = chooseEngine();
        setEngine(pick[0], pick[1]);
        try {
            worker = new Worker('rtty-decoder-worker.js' + (window.__WFWEB_V__ ? '?v=' + window.__WFWEB_V__ : ''), { type: 'module' });
        } catch (e) {
            console.error('[RTTY] cannot start the decoder worker: ' + (e.message || e));
            S.loading = false; updateButtons();
            return;
        }
        worker.onmessage = onWorkerMessage;
        worker.onerror = function (e) {
            console.error('[RTTY Decoder Worker] ' + (e.message || e));
            if (!S.loaded && S.engine === 'deeprtty' && enginePreference() !== 'deeprtty') fallbackToClassic('worker failed: ' + (e.message || 'error'));
            else if (!S.loaded) { stopDecoder(); }
        };
        configureWorker();
        worker.postMessage({ type: 'loadModel' });
        loadTimer = setTimeout(function () {
            loadTimer = null;
            if (S.loaded) return;
            if (S.engine === 'deeprtty' && enginePreference() !== 'deeprtty') fallbackToClassic('model load timed out');
            else { console.error('[RTTY] decoder load timed out'); stopDecoder(); }
        }, MODEL_LOAD_TIMEOUT_MS);
    }

    function configureWorker() {
        if (!worker) return;
        S.inputRate = host('audioSampleRate') || 48000;
        worker.postMessage({ type: 'configure', inputRate: S.inputRate, centerHz: centerHz(), invert: markHigh(), engine: S.engine });
        S.pending = '';
        renderText();
    }

    function stopDecoder() {
        if (loadTimer) { clearTimeout(loadTimer); loadTimer = null; }
        if (worker) { worker.terminate(); worker = null; }
        S.decoding = false; S.loading = false; S.loaded = false; S.loadProgress = 0;
        S.pending = '';
        tickMs = [];
        renderText();
        updateButtons();
    }

    function fallbackToClassic(reason) {
        if (S.engine === 'classic') return;
        console.warn('[RTTY] falling back to the classic FSK demodulator: ' + reason);
        if (S.pending) { appendRx(S.pending); S.pending = ''; }
        setEngine('classic', reason);
        tickMs = [];
        if (worker) { configureWorker(); worker.postMessage({ type: 'loadModel' }); }
    }

    function onWorkerMessage(e) {
        var m = e.data;
        switch (m.type) {
            case 'loadProgress':
                S.loadProgress = m.progress || 0;
                updateButtons();
                break;
            case 'modelLoaded':
                if (loadTimer) { clearTimeout(loadTimer); loadTimer = null; }
                S.loaded = true; S.loading = false; S.decoding = true;
                updateButtons();
                break;
            case 'spectrum':
                drawColumn(m.col);
                break;
            case 'result':
                if (m.engine === 'deeprtty' && m.ms !== undefined) {
                    tickMs.push(m.ms);
                    if (tickMs.length > SLOW_TICKS) tickMs.shift();
                    var avg = tickMs.reduce(function (a, b) { return a + b; }, 0) / tickMs.length;
                    if (tickMs.length === SLOW_TICKS && avg > SLOW_TICK_MS && enginePreference() !== 'deeprtty') {
                        fallbackToClassic('too slow: ' + avg.toFixed(0) + ' ms per pass');
                        break;
                    }
                }
                if (m.engine !== S.engine) break;     // a late answer after a fallback
                if (m.committed) appendRx(m.committed);
                S.pending = (m.pending || '').replace(/[\r\x07]/g, '');
                renderText();
                break;
            case 'error':
                console.error('[RTTY Decoder Worker] ' + m.error);
                if (!S.loaded && S.engine === 'deeprtty' && enginePreference() !== 'deeprtty') fallbackToClassic('load failed: ' + m.error);
                break;
        }
    }

    // RX PCM from handleAudioData (Int16 at audioSampleRate).  While we
    // transmit, the rig's monitor would feed our own tones back: the
    // worker gets silence of the same length so its timeline stays whole.
    function feedRx(int16) {
        if (!worker || !S.loaded) return;
        // The decoder may start before the audio does (a rig already in
        // RTTY at page load): the stream's rate is only known once it runs.
        if ((host('audioSampleRate') || 48000) !== S.inputRate) configureWorker();
        if (S.txBusy) worker.postMessage({ type: 'gap', samples: int16.length });
        else worker.postMessage({ type: 'audio', samples: int16.slice() });
    }

    // ------------------------------------------------------------------
    // Text
    // ------------------------------------------------------------------
    function appendRx(text) {
        text = text.replace(/[\r\x07]/g, '');
        if (!text) return;
        var last = S.lines[S.lines.length - 1];
        if (!last || last.dir !== 'rx') { last = { dir: 'rx', text: '' }; S.lines.push(last); }
        var parts = text.split('\n');
        last.text += parts[0];
        for (var i = 1; i < parts.length; i++) { last = { dir: 'rx', text: parts[i] }; S.lines.push(last); }
        if (S.lines.length > MAX_LINES) S.lines.splice(0, S.lines.length - MAX_LINES);
    }
    function appendSys(text) {
        S.lines.push({ dir: 'sys', text: text });
        if (S.lines.length > MAX_LINES) S.lines.splice(0, S.lines.length - MAX_LINES);
        renderText();
    }
    function appendTx(text) {
        S.lines.push({ dir: 'tx', text: text });
        if (S.lines.length > MAX_LINES) S.lines.splice(0, S.lines.length - MAX_LINES);
        renderText();
    }
    function clearText() { S.lines = []; S.pending = ''; renderText(); if (worker) worker.postMessage({ type: 'reset' }); }

    function isCallsign(word) {
        if (word.length === 3) return /^[KNW][0-9][A-Z]$/.test(word);
        if (word.length < 4 || word.length > 8) return false;
        return /^[A-Z0-9]{1,3}[0-9][A-Z]{1,4}$/.test(word);
    }
    function escapeHtml(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
    function renderRx(text) {
        var mine = myCall();
        return text.split(/([A-Z0-9]+)/).map(function (part) {
            if (/^[A-Z0-9]+$/.test(part) && isCallsign(part) && part !== mine) return '<span class="rtty-call" data-call="' + part + '">' + part + '</span>';
            return escapeHtml(part);
        }).join('');
    }
    function renderText() {
        var el = document.getElementById('rttyText');
        if (!el) return;
        var html = '';
        var lines = S.lines;
        for (var i = 0; i < lines.length; i++) {
            var ln = lines[i];
            html += '<div class="rtty-line rtty-' + ln.dir + '">' + (ln.dir === 'rx' ? renderRx(ln.text) : escapeHtml(ln.text));
            if (i === lines.length - 1 && ln.dir === 'rx' && S.pending) html += '<span class="rtty-pending">' + escapeHtml(S.pending) + '</span>';
            html += '</div>';
        }
        if (S.pending && (!lines.length || lines[lines.length - 1].dir !== 'rx')) {
            html += '<div class="rtty-line rtty-rx"><span class="rtty-pending">' + escapeHtml(S.pending) + '</span></div>';
        }
        el.innerHTML = html;
        el.scrollTop = el.scrollHeight;
    }

    // ------------------------------------------------------------------
    // Tuning strip
    // ------------------------------------------------------------------
    function buildColorLUT() {
        var lut = new Array(256);
        for (var v = 0; v < 256; v++) {
            var t = Math.pow(v / 255, 2.2);
            lut[v] = hslToRgb((220 * (1 - t)) / 360, 1.0, 0.15 + 0.75 * t);
        }
        return lut;
    }
    function hslToRgb(h, s, l) {
        var hue2rgb = function (p, q, t) {
            if (t < 0) t += 1; if (t > 1) t -= 1;
            if (t < 1 / 6) return p + (q - p) * 6 * t;
            if (t < 1 / 2) return q;
            if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
            return p;
        };
        var q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        return [Math.round(hue2rgb(p, q, h + 1 / 3) * 255), Math.round(hue2rgb(p, q, h) * 255), Math.round(hue2rgb(p, q, h - 1 / 3) * 255)];
    }
    function sizeScope() {
        if (!scope) return;
        var rect = scope.getBoundingClientRect();
        var dpr = window.devicePixelRatio || 1;
        var w = Math.round((rect.width || 300) * dpr), h = Math.round((rect.height || 48) * dpr);
        if (scope.width !== w || scope.height !== h) { scope.width = w; scope.height = h; column = null; }
        // the mark / space lines: audio centre +/- 300 Hz over the strip, mark below space unless inverted
        var span = SPEC_BINS * SPEC_BIN_HZ, lo = MODEL_CENTER - span / 2;
        var markT = MODEL_CENTER - SHIFT / 2, spaceT = MODEL_CENTER + SHIFT / 2;   // translated, mark low
        var pct = function (fT) { var a = markHigh() ? MODEL_CENTER - (fT - MODEL_CENTER) : fT; return (1 - (a - lo) / span) * 100; };
        var ml = bar.querySelector('.rtty-mark-line'), sl = bar.querySelector('.rtty-space-line');
        if (ml) ml.style.top = pct(markT) + '%';
        if (sl) sl.style.top = pct(spaceT) + '%';
        var hi = bar.querySelector('.rtty-scale-hi'), lo = bar.querySelector('.rtty-scale-lo');
        if (hi) hi.textContent = Math.round(centerHz() + span / 2) + ' Hz';
        if (lo) lo.textContent = Math.round(centerHz() - span / 2) + ' Hz';
    }
    function drawColumn(col) {
        if (!scope || !scopeCtx || !S.visible) return;
        var w = scope.width, h = scope.height;
        if (!w || !h) return;
        scopeCtx.drawImage(scope, 0, 0, w, h, -1, 0, w, h);
        if (!column || column.height !== h) column = scopeCtx.createImageData(1, h);
        var buf = column.data, inv = markHigh();
        for (var y = 0; y < h; y++) {
            var frac = 1 - y / Math.max(1, h - 1);                // bottom = lowest audio frequency
            var bin = Math.min(SPEC_BINS - 1, Math.floor(frac * SPEC_BINS));
            if (inv) bin = SPEC_BINS - 1 - bin;                   // the worker's bins are translated, mark low
            var c = colorLUT[col[bin]], p = y * 4;
            buf[p] = c[0]; buf[p + 1] = c[1]; buf[p + 2] = c[2]; buf[p + 3] = 255;
        }
        scopeCtx.putImageData(column, w - 1, 0);
    }
    function scopeClick(e) {
        var rect = scope.getBoundingClientRect();
        var frac = 1 - (e.clientY - rect.top) / rect.height;      // 0 = bottom
        var span = SPEC_BINS * SPEC_BIN_HZ;
        var offset = (frac - 0.5) * span;                         // Hz from the centre, in audio terms
        var dial = host('currentFreq') || 0;
        if (dial <= 0) return;
        hostSend({ cmd: 'setFrequency', value: Math.round(dial - dialSign() * offset) });
    }

    // ------------------------------------------------------------------
    // TX
    // ------------------------------------------------------------------
    function substitute(t) {
        var qso = (document.getElementById('rttyQsoInput').value || '').trim().toUpperCase();
        var rst = (document.getElementById('rttyRstSent').value || '599').trim();
        return t.replace(/\[CALL\]/g, myCall()).replace(/\[QSO\]/g, qso).replace(/\[RST\]/g, rst);
    }
    function renderMacros() {
        var grid = document.getElementById('rttyMacroGrid');
        if (!grid) return;
        grid.innerHTML = '';
        S.macros.forEach(function (m, i) {
            var b = document.createElement('button');
            b.className = 'rtty-macro-btn wf-btn' + (S.editMacros ? ' editable' : '');
            b.textContent = m.label;
            b.title = substitute(m.text);
            b.addEventListener('click', function () {
                if (S.editMacros) { editMacro(i); return; }
                sendText(substitute(m.text));
            });
            grid.appendChild(b);
        });
        var edit = document.createElement('button');
        edit.className = 'rtty-macro-btn wf-btn toggle' + (S.editMacros ? ' active' : '');
        edit.textContent = S.editMacros ? 'DONE' : 'EDIT';
        edit.title = 'Edit the macros: [CALL] = your call, [QSO] = the QSO field, [RST] = the RST sent';
        edit.addEventListener('click', function () { S.editMacros = !S.editMacros; renderMacros(); });
        grid.appendChild(edit);
    }
    function editMacro(i) {
        var m = S.macros[i];
        var label = prompt('Button label:', m.label);
        if (label === null) return;
        var text = prompt('Text ([CALL] = your call, [QSO] = the QSO field, [RST] = RST sent):', m.text);
        if (text === null) return;
        m.label = label.trim().toUpperCase().slice(0, 8) || m.label;
        m.text = text;
        savePrefs();
        renderMacros();
    }
    function sendCompose() {
        var inp = document.getElementById('rttyInput');
        var text = (inp.value || '').trim();
        if (!text) return;
        if (sendText(text)) inp.value = '';
    }
    function sendText(text) {
        text = String(text || '').toUpperCase();
        if (!text) return false;
        if (!afskAllowed()) {
            // The rig's RTTY mode keys FSK from its key jack; CI-V has no way
            // in.  Say so where it is read, and point at the way out.
            appendSys('NO TX IN THE RIG\'S RTTY MODE: IT KEYS FSK FROM ITS KEY JACK. PRESS AFSK (LSB) TO SEND AUDIO TONES.');
            attention('rttyAfskBtn');
            flashHint('Press AFSK to transmit');
            return false;
        }
        if (S.txBusy) { flashHint('TX busy — STOP first'); return false; }
        if (typeof window.streamDigiAudio !== 'function' || !window.WfRtty) return false;
        if (typeof window.initDigiTxCtx === 'function') window.initDigiTxCtx();
        var synth = new window.WfRtty.WfRttySynth({ sampleRate: 12000, centerHz: centerHz(), markLow: !markHigh() });
        var samples = synth.render(text + ' ');
        S.txBusy = true;
        appendTx(text);
        updateButtons();
        // Like FT8 / JS8: the TX audio path and the rig's MOD input (DATA OFF
        // MOD -> USB) are set up by enableMic; without it the rig keys with
        // nothing on the modulator.  Put the mic back the way it was after.
        S.micWasOn = !!host('micEnabled');
        hostSend({ cmd: 'enableMic', value: true });
        var done = false;
        window.streamDigiAudio(samples,
            function () { if (done) return; done = true; unkey(); },
            function () { hostSend({ cmd: 'setPTT', value: true, digital: true }); });
        return true;
    }
    function unkey() {
        hostSend({ cmd: 'setPTT', value: false });
        if (!S.micWasOn) hostSend({ cmd: 'enableMic', value: false });
        S.txBusy = false;
        updateButtons();
    }
    function stopTx() {
        if (!S.txBusy) return;
        if (typeof window.haltDigiTx === 'function') window.haltDigiTx();
        unkey();
    }
    var attnTimer = null;
    function attention(id) {
        var el = document.getElementById(id);
        if (!el) return;
        el.classList.add('rtty-attn');
        if (attnTimer) clearTimeout(attnTimer);
        attnTimer = setTimeout(function () { el.classList.remove('rtty-attn'); attnTimer = null; }, 3000);
    }
    var hintTimer = null;
    function flashHint(text) {
        var inp = document.getElementById('rttyInput');
        if (!inp) return;
        var old = inp.placeholder;
        inp.placeholder = text;
        inp.classList.add('rtty-hint');
        if (hintTimer) clearTimeout(hintTimer);
        hintTimer = setTimeout(function () { inp.placeholder = 'TYPE AND PRESS ENTER TO SEND...'; inp.classList.remove('rtty-hint'); hintTimer = null; }, 2500);
    }

    // ------------------------------------------------------------------
    // Log
    // ------------------------------------------------------------------
    function logQso() {
        var el = document.getElementById('rttyQsoInput');
        var call = (el.value || '').trim().toUpperCase();
        if (!call) {
            var entered = prompt('DX Callsign:');
            if (!entered || !entered.trim()) return;
            call = entered.trim().toUpperCase();
            el.value = call;
        }
        if (typeof window.addQsoLogEntry !== 'function') return;
        window.addQsoLogEntry({
            call: call, mode: 'RTTY',
            rstSent: (document.getElementById('rttyRstSent').value || '599').trim(),
            rstRcvd: (document.getElementById('rttyRstRcvd').value || '599').trim(),
        });
        if (typeof window.openLogModal === 'function') window.openLogModal();
    }

    // ------------------------------------------------------------------
    // Buttons
    // ------------------------------------------------------------------
    function updateButtons() {
        if (!bar) return;
        var dec = document.getElementById('rttyDecodeBtn');
        var narrow = window.matchMedia && window.matchMedia('(orientation: portrait) and (max-width: 600px)').matches;
        if (S.loading) {
            var pct = Math.round(S.loadProgress * 100);
            dec.textContent = pct > 0 && pct < 100 ? (narrow ? pct + '%' : 'LOADING ' + pct + '%') : (narrow ? 'LOAD' : 'LOADING...');
            dec.className = 'wf-btn toggle loading';
        } else {
            dec.textContent = 'DECODE';
            dec.className = 'wf-btn toggle' + (S.decoding ? ' active' : '');
        }
        document.getElementById('rttyRevBtn').classList.toggle('active', S.rev);
        var afsk = document.getElementById('rttyAfskBtn');
        afsk.classList.toggle('active', S.afsk);
        afsk.disabled = !S.afsk && !isRttyMode(mode());
        var stop = document.getElementById('rttyStopBtn');
        stop.style.display = S.txBusy ? '' : 'none';
        bar.classList.toggle('rtty-tx', S.txBusy);
        bar.classList.toggle('rtty-no-tx', !afskAllowed());
        var sendBtn = document.getElementById('rttySendBtn');
        sendBtn.disabled = !afskAllowed();
        sendBtn.title = afskAllowed() ? 'Send the line as AFSK' : 'AFSK transmit needs the rig on LSB / USB: press AFSK';
        sizeScope();
    }

    window.RttyPanel = {
        onModeChange: onModeChange,
        show: show,
        hide: function (opts) { hide(opts || { restore: true }); },
        suspend: suspend,
        resume: resume,
        feedRx: feedRx,
        visible: function () { return S.visible; },
        afsk: function () { return S.afsk; },
        get state() { return S; },
        // 'auto' | 'deeprtty' | 'classic'; takes effect the next time the decoder starts.
        preferEngine: function (p) { try { if (p === 'auto') localStorage.removeItem(ENGINE_KEY); else localStorage.setItem(ENGINE_KEY, p); } catch (e) {} },
    };

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', build);
    else build();
})();
