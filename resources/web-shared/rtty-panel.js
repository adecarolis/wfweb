// RTTY bar — the RTTY companion of the CW bar, one copy for both builds.
// Opens when the rig enters RTTY / RTTY-R (updateMode -> RttyPanel.onModeChange),
// docks at the bottom of the scope area like #cwBar and holds:
//   - the DeepRTTY decoder (rtty-decoder-worker.js): the rig's RX PCM from
//     handleAudioData goes to the worker as it is (RttyPanel.feedRx); the
//     worker moves the tone pair at MARK / MARK + 170 Hz onto the model's
//     timeline, so nothing here depends on the audio rate;
//   - a tuning strip: a waterfall of the input audio over the whole
//     passband (ZOOM: the pair +/- 250 Hz) with the mark and space lines;
//     a click moves the dial so the clicked tone lands on the pair, the
//     way the CW scope does, and AUTOTUNE finds the strongest pair of
//     tones 170 Hz apart and pulls it there (the CW decoder's AUTOTUNE);
//   - the decoded text, teleprinter style, committed bright and the
//     worker's pending tail dim; callsigns are links into the QSO field;
//   - CALL / QSO / RST fields and LOG QSO (addQsoLogEntry, mode RTTY);
//     ten macro slots laid out and edited like the CW bar's (EDIT/SET,
//     then a button opens the editor overlay; wfRttyMacros);
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
    // The strip: the worker posts a spectrum of the input audio over the
    // whole passband (lowHz / binHz come with each column); ZOOM shows the
    // pair +/- ZOOM_HALF_HZ of it instead.
    var ZOOM_HALF_HZ = 250;
    // Auto-tune: average the strip for a while, find the strongest pair of
    // tones 170 Hz apart in the passband, pull it onto the mark / space pair.
    var AUTOTUNE_WINDOW_MS = 1500;    // several characters at 45 Bd
    var AUTOTUNE_DEADBAND_HZ = 15;    // finer than the ear - don't nudge for nothing
    // The strip is dB relative to a slow peak AGC (0..255 = -40..0 dB), so
    // there is no absolute level to test; the weaker tone of the pair must
    // stand this far above the band's median instead.  A lone carrier has
    // only one tone up, and a quiet band has nothing up.
    var AUTOTUNE_MIN_ABOVE_DB = 6;
    var AUTOTUNE_HOLD_MS = 1600;      // how long the button shows its verdict
    var ENGINE_KEY = 'wfRttyDecoder';
    var SLOW_TICKS = 4, SLOW_TICK_MS = 2500;
    var MODEL_LOAD_TIMEOUT_MS = 180000;
    var MAX_LINES = 120;
    // Ten macro slots laid out like the CW bar's: two rows of five, slot 5
    // (top right) is EDIT/SET. A button shows its text with [CALL] [QSO]
    // [RST] filled in; an empty slot reads M<n>.
    var MACRO_SLOTS = 10, EDIT_SLOT = 5;
    // Its own set, not the CW one: an RTTY exchange is rubber-stamp text
    // (RTTY repeats the report, ends an over with K / BK / SK), plus a
    // contest row and an RY test line for setting the AFSK level.
    var DEFAULT_MACROS = {
        1: 'CQ CQ CQ DE [CALL] [CALL] [CALL] PSE K',
        2: '[QSO] [QSO] DE [CALL] [CALL] [CALL] K',
        3: '[QSO] DE [CALL] R R TNX FER CALL UR RST [RST] [RST] [RST] BTU [QSO] DE [CALL] K',
        4: '[QSO] DE [CALL] R R QSL TNX FER QSO 73 73 [QSO] DE [CALL] SK',
        6: 'QRZ? DE [CALL] [CALL] K',
        7: '[QSO] DE [CALL] AGN AGN PSE K',
        8: 'CQ TEST CQ TEST DE [CALL] [CALL] TEST',
        9: '[QSO] UR [RST] [RST] [RST] BK',
        10: 'RYRYRYRYRY RYRYRYRYRY RYRYRYRYRY DE [CALL]',
    };

    var S = {
        visible: false, suspended: false,
        afsk: false, afskFrom: null,        // { mode, filter } to put back when the bar closes
        savedModLevel: -1,
        decoding: false, loading: false, loaded: false, loadProgress: 0,
        engine: 'deeprtty', engineReason: '',
        mark: 2125, rev: false, zoom: false,
        afskDefault: true,                  // engage AFSK (LSB) when the bar opens; the AFSK button remembers
        spec: null,                         // { lowHz, binHz, bins } of the strip columns
        lines: [], pending: '',             // [{ dir: 'rx' | 'tx', text }]
        txBusy: false,
        macros: null,                       // { slot: text }
        editMacros: false, editingSlot: null,
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
              '<div class="rtty-scope-btns">' +
                '<button id="rttyAutoTune" class="wf-btn" title="Auto-tune: listen for a moment, find the strongest RTTY signal in the passband (two tones 170 Hz apart) and move the dial so it lands on the mark / space pair"></button>' +
                '<button id="rttyZoomBtn" class="wf-btn toggle" title="Zoom the strip onto the pair (\u00b1250 Hz) or show the whole passband">ZOOM</button>' +
              '</div>' +
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
        var ed = document.createElement('div');
        ed.id = 'rttyMacroEditor';
        ed.className = 'overlay mode-rtty rtty-macro-editor';
        ed.style.display = 'none';
        ed.innerHTML =
            '<div class="editor-dialog">' +
              '<div class="editor-title">Edit Macro Button</div>' +
              '<div class="editor-hint">Use [CALL], [QSO] and [RST] as placeholders</div>' +
              '<textarea id="rttyMacroText"></textarea>' +
              '<div class="editor-buttons">' +
                '<button id="rttyMacroSave">SAVE</button>' +
                '<button id="rttyMacroClear">CLEAR</button>' +
                '<button id="rttyMacroCancel">CANCEL</button>' +
              '</div>' +
            '</div>';
        el.parentNode.insertBefore(ed, el.nextSibling);
        document.getElementById('rttyMacroSave').addEventListener('click', function () {
            var text = document.getElementById('rttyMacroText').value.trim().toUpperCase();
            if (text) S.macros[S.editingSlot] = text; else delete S.macros[S.editingSlot];
            savePrefs(); renderMacros(); hideMacroEditor();
        });
        document.getElementById('rttyMacroClear').addEventListener('click', function () {
            delete S.macros[S.editingSlot];
            savePrefs(); renderMacros(); hideMacroEditor();
        });
        document.getElementById('rttyMacroCancel').addEventListener('click', hideMacroEditor);
        document.getElementById('rttyMacroText').addEventListener('input', function () {
            var a = this.selectionStart, b = this.selectionEnd;
            this.value = this.value.toUpperCase();
            this.setSelectionRange(a, b);
        });
        document.getElementById('rttyMacroText').addEventListener('keydown', function (e) { e.stopPropagation(); });
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
        document.getElementById('rttyZoomBtn').addEventListener('click', function () { S.zoom = !S.zoom; savePrefs(); updateButtons(); clearScope(); });
        document.getElementById('rttyAutoTune').addEventListener('click', autoTune);
        setAutoTuneState('idle');
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
            S.zoom = localStorage.getItem('wfRttyZoom') === '1';
            S.afskDefault = localStorage.getItem('wfRttyAfsk') !== '0';
            var mac = JSON.parse(localStorage.getItem('wfRttyMacros') || 'null');
            if (Array.isArray(mac)) {                   // the first layout: five { label, text }; the rest from the defaults
                S.macros = {};
                for (var d in DEFAULT_MACROS) S.macros[d] = DEFAULT_MACROS[d];
                var slots = [1, 2, 3, 4, 6];
                mac.forEach(function (m, i) { if (m && m.text && slots[i]) S.macros[slots[i]] = m.text; });
            } else if (mac && typeof mac === 'object') S.macros = mac;
        } catch (e) { /* no storage */ }
        if (!S.macros) { S.macros = {}; for (var k in DEFAULT_MACROS) S.macros[k] = DEFAULT_MACROS[k]; }
    }
    function savePrefs() {
        try {
            localStorage.setItem('wfRttyMark', String(S.mark));
            localStorage.setItem('wfRttyRev', S.rev ? '1' : '0');
            localStorage.setItem('wfRttyZoom', S.zoom ? '1' : '0');
            localStorage.setItem('wfRttyAfsk', S.afskDefault ? '1' : '0');
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
        // AFSK is the way to transmit, so it is on by default: the rig goes
        // to LSB as the bar opens.  The AFSK button turns it off and that
        // choice is remembered.
        if (S.afskDefault && !S.afsk && isRttyMode(mode())) startAfsk();
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
        if (S.editingSlot !== null) hideMacroEditor();
        if (!wasVisible) return;
        stopTx();
        stopDecoder();
        // Closing in AFSK goes back to the mode the rig was in before RTTY
        // (the AFSK button alone goes back to RTTY); a mode put back there
        // must not reopen us (suppressOpenUntil).
        if (S.afsk) endAfsk(opts.restore !== false, S.beforeRtty);
        S.beforeRtty = null;
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
            // A fresh entry into RTTY from another mode: remember where the
            // rig came from, so closing the bar in AFSK can go back there.
            if (prev && !isRttyMode(prev) && !S.afsk && !S.visible && !S.suspended) S.beforeRtty = prev;
            if (S.afsk && !S.afskPending) endAfsk(false);   // the user went back to the rig's RTTY by hand
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
        if (S.afsk) { S.afskDefault = false; savePrefs(); endAfsk(true); updateButtons(); return; }
        if (!isRttyMode(mode())) return;
        S.afskDefault = true; savePrefs();
        startAfsk();
        updateButtons();
    }
    function startAfsk() {
        var m = mode();
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
    function endAfsk(restore, toMode) {
        if (!S.afsk) return;
        S.afsk = false; S.afskPending = false;
        if (S.savedModLevel >= 0 && typeof window.throttledSend === 'function') {
            window.throttledSend('micGain', 'setMicGain', S.savedModLevel);
            if (typeof window.updateSlider === 'function') window.updateSlider('micGain', S.savedModLevel);
        }
        S.savedModLevel = -1;
        var from = S.afskFrom; S.afskFrom = null;
        if (typeof window.updateModeBtnLabel === 'function') window.updateModeBtnLabel();
        if (restore && from && isSideband(mode())) {
            S.suppressOpenUntil = Date.now() + 1500;
            if (toMode && !isRttyMode(toMode) && typeof window.setMode === 'function') window.setMode(toMode);
            else hostSend({ cmd: 'setMode', value: from.mode, filter: from.filter });
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
        cancelAutoTune();
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
                if (!S.spec || S.spec.lowHz !== m.lowHz || S.spec.binHz !== m.binHz || S.spec.bins !== m.col.length) {
                    S.spec = { lowHz: m.lowHz, binHz: m.binHz, bins: m.col.length };
                    sizeScope();
                }
                if (autoTuneAcc && !S.txBusy) autoTuneFeed(m.col);
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
    // The strip's frequency range, in audio Hz: the whole passband the
    // worker sends, or the pair +/- ZOOM_HALF_HZ.
    function stripRange() {
        if (S.zoom) return { lo: centerHz() - ZOOM_HALF_HZ, hi: centerHz() + ZOOM_HALF_HZ };
        var sp = S.spec;
        if (!sp) return { lo: 300, hi: 3000 };
        return { lo: sp.lowHz, hi: sp.lowHz + sp.binHz * (sp.bins - 1) };
    }
    function markHz() { return centerHz() + (markHigh() ? 1 : -1) * SHIFT / 2; }
    function spaceHz() { return centerHz() - (markHigh() ? 1 : -1) * SHIFT / 2; }
    function sizeScope() {
        if (!scope) return;
        var rect = scope.getBoundingClientRect();
        var dpr = window.devicePixelRatio || 1;
        var w = Math.round((rect.width || 300) * dpr), h = Math.round((rect.height || 48) * dpr);
        if (scope.width !== w || scope.height !== h) { scope.width = w; scope.height = h; column = null; }
        var r = stripRange(), span = r.hi - r.lo;
        var pct = function (f) { return (1 - (f - r.lo) / span) * 100; };
        var ml = bar.querySelector('.rtty-mark-line'), sl = bar.querySelector('.rtty-space-line');
        if (ml) ml.style.top = pct(markHz()) + '%';
        if (sl) sl.style.top = pct(spaceHz()) + '%';
        // Over the whole passband the two lines are a few pixels apart: one label then.
        var close = rect.height * SHIFT / span < 16;
        if (ml) ml.querySelector('span').textContent = close ? 'MARK \u00b7 SPACE' : 'MARK';
        if (sl) sl.querySelector('span').style.display = close ? 'none' : '';
        var hi = bar.querySelector('.rtty-scale-hi'), lo = bar.querySelector('.rtty-scale-lo');
        if (hi) hi.textContent = Math.round(r.hi) + ' Hz';
        if (lo) lo.textContent = Math.round(r.lo) + ' Hz';
    }
    function clearScope() {
        if (scope && scopeCtx) { scopeCtx.fillStyle = '#000'; scopeCtx.fillRect(0, 0, scope.width, scope.height); }
        sizeScope();
    }
    // Strip value at an audio frequency: linear interpolation between bins.
    function specAt(col, f) {
        var sp = S.spec;
        var x = (f - sp.lowHz) / sp.binHz;
        if (x <= 0) return col[0];
        if (x >= sp.bins - 1) return col[sp.bins - 1];
        var i = Math.floor(x), t = x - i;
        return col[i] * (1 - t) + col[i + 1] * t;
    }
    function drawColumn(col) {
        if (!scope || !scopeCtx || !S.visible || !S.spec) return;
        var w = scope.width, h = scope.height;
        if (!w || !h) return;
        scopeCtx.drawImage(scope, 0, 0, w, h, -1, 0, w, h);
        if (!column || column.height !== h) column = scopeCtx.createImageData(1, h);
        var buf = column.data, r = stripRange(), span = r.hi - r.lo;
        for (var y = 0; y < h; y++) {
            var f = r.lo + (1 - y / Math.max(1, h - 1)) * span;   // bottom = lowest audio frequency
            var c = colorLUT[Math.round(specAt(col, f))], p = y * 4;
            buf[p] = c[0]; buf[p + 1] = c[1]; buf[p + 2] = c[2]; buf[p + 3] = 255;
        }
        scopeCtx.putImageData(column, w - 1, 0);
    }
    // Move the dial so the audio tone at `f` lands on the centre of the pair.
    function tuneToTone(f) {
        var dial = host('currentFreq') || 0;
        if (dial <= 0) return null;
        var delta = dialSign() * (centerHz() - f);
        hostSend({ cmd: 'setFrequency', value: Math.round(dial + delta) });
        return delta;
    }
    function scopeClick(e) {
        var rect = scope.getBoundingClientRect();
        var frac = 1 - (e.clientY - rect.top) / rect.height;      // 0 = bottom
        var r = stripRange();
        tuneToTone(r.lo + frac * (r.hi - r.lo));
    }

    // ------------------------------------------------------------------
    // Auto-tune
    // ------------------------------------------------------------------
    var autoTuneAcc = null, autoTuneTimer = null, autoTuneHoldTimer = null;
    function autoTune() {
        if (autoTuneAcc) return;                                  // already listening
        if (!S.decoding || !S.spec) return;
        autoTuneAcc = { sum: new Float64Array(S.spec.bins), frames: 0 };
        setAutoTuneState('busy');
        autoTuneTimer = setTimeout(finishAutoTune, AUTOTUNE_WINDOW_MS);
    }
    function autoTuneFeed(col) {
        var a = autoTuneAcc;
        if (col.length !== a.sum.length) return;
        for (var k = 0; k < col.length; k++) a.sum[k] += col[k];
        a.frames++;
    }
    function cancelAutoTune() {
        if (autoTuneTimer) { clearTimeout(autoTuneTimer); autoTuneTimer = null; }
        if (autoTuneHoldTimer) { clearTimeout(autoTuneHoldTimer); autoTuneHoldTimer = null; }
        autoTuneAcc = null;
        setAutoTuneState('idle');
    }
    // The strongest pair: for every centre c in the passband, the strip at
    // c - 85 plus the strip at c + 85, averaged over the window.  A single
    // carrier scores only half; two RTTY tones score on both.
    function finishAutoTune() {
        autoTuneTimer = null;
        var a = autoTuneAcc; autoTuneAcc = null;
        var sp = S.spec;
        if (!a || !a.frames || !sp) { setAutoTuneState('warn', 'NO SIG'); return; }
        var avg = new Float64Array(sp.bins);
        for (var k = 0; k < sp.bins; k++) avg[k] = a.sum[k] / a.frames;
        var sorted = Array.prototype.slice.call(avg).sort(function (x, y) { return x - y; });
        var median = sorted[sorted.length >> 1];
        var toDb = function (v) { return v / 255 * 40 - 40; };
        var saved = S.spec; S.spec = sp;
        var half = SHIFT / 2, lo = sp.lowHz + half, hi = sp.lowHz + sp.binHz * (sp.bins - 1) - half;
        // The pair is scored by its weaker tone: both must be there.
        var scoreAt = function (c) { var p = specAt(avg, c - half), q = specAt(avg, c + half); return Math.min(p, q) + 0.1 * Math.max(p, q); };
        var best = -Infinity, bestC = 0, step = sp.binHz / 2;
        for (var c = lo; c <= hi; c += step) {
            var sc = scoreAt(c);
            if (sc > best) { best = sc; bestC = c; }
        }
        // Parabolic refinement across the peak: finer than the bin.
        var s0 = scoreAt(bestC - step), s1 = best, s2 = scoreAt(bestC + step);
        var denom = s0 - 2 * s1 + s2;
        if (denom !== 0) bestC += step * Math.max(-0.5, Math.min(0.5, 0.5 * (s0 - s2) / denom));
        var weaker = Math.min(specAt(avg, bestC - half), specAt(avg, bestC + half));
        S.spec = saved;
        var above = toDb(weaker) - toDb(median);
        if (above < AUTOTUNE_MIN_ABOVE_DB) {
            console.log('[RTTY] Auto-tune: no signal (best pair ' + above.toFixed(1) + ' dB above the band median)');
            setAutoTuneState('warn', 'NO SIG');
            return;
        }
        var delta = dialSign() * (centerHz() - bestC);
        if (Math.abs(delta) < AUTOTUNE_DEADBAND_HZ) {
            console.log('[RTTY] Auto-tune: already centred (' + bestC.toFixed(0) + ' Hz, ' + above.toFixed(0) + ' dB)');
            setAutoTuneState('ok', '\u2713 ' + Math.round(bestC) + ' Hz');
            return;
        }
        if (tuneToTone(bestC) === null) { setAutoTuneState('warn', 'NO VFO'); return; }
        console.log('[RTTY] Auto-tune: pair at ' + bestC.toFixed(0) + ' Hz (' + above.toFixed(0) + ' dB), dial ' + (delta > 0 ? '+' : '') + Math.round(delta) + ' Hz');
        setAutoTuneState('ok', (delta > 0 ? '+' : '\u2212') + Math.abs(Math.round(delta)) + ' Hz');
    }
    // Button label doubles as the auto-tune readout: what it heard, or why not.
    function setAutoTuneState(state, label) {
        var btn = document.getElementById('rttyAutoTune');
        if (!btn) return;
        if (autoTuneHoldTimer) { clearTimeout(autoTuneHoldTimer); autoTuneHoldTimer = null; }
        btn.className = 'wf-btn' + (state === 'idle' ? '' : ' ' + state);
        if (state === 'idle') btn.innerHTML = '\u2316 AUTO<span class="rtty-wide">TUNE</span>';
        else if (state === 'busy') btn.textContent = 'LISTENING';
        else {
            btn.textContent = label;
            autoTuneHoldTimer = setTimeout(function () { autoTuneHoldTimer = null; setAutoTuneState('idle'); }, AUTOTUNE_HOLD_MS);
        }
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
        for (var i = 1; i <= MACRO_SLOTS; i++) {
            var b = document.createElement('button');
            b.id = 'rttyMacro' + i;
            if (i === EDIT_SLOT) {
                b.className = 'rtty-macro-btn edit-set-btn wf-btn' + (S.editMacros ? ' editing' : '');
                b.textContent = 'EDIT/SET';
                b.title = 'Edit the macros: press, then press a button to change its text';
                b.addEventListener('click', toggleEditMode);
            } else {
                var template = S.macros[i] || '';
                b.className = 'rtty-macro-btn wf-btn' + (template ? '' : ' empty') + (S.editMacros ? ' editable' : '');
                b.textContent = template ? substitute(template) : 'M' + i;
                b.title = template;
                b.addEventListener('click', (function (slot) { return function () {
                    if (S.editMacros) { showMacroEditor(slot); return; }
                    var t = S.macros[slot];
                    if (t) sendText(substitute(t));
                }; })(i));
            }
            grid.appendChild(b);
        }
    }
    function toggleEditMode() { S.editMacros = !S.editMacros; renderMacros(); }
    // The editor is an overlay over the scope area, like the CW bar's: the
    // bar fades out under it so the two never overlap.
    function showMacroEditor(slot) {
        S.editingSlot = slot;
        var ta = document.getElementById('rttyMacroText');
        ta.value = S.macros[slot] || '';
        document.getElementById('rttyMacroEditor').style.display = 'flex';
        bar.style.opacity = '0'; bar.style.pointerEvents = 'none';
        ta.focus();
    }
    function hideMacroEditor() {
        document.getElementById('rttyMacroEditor').style.display = 'none';
        bar.style.opacity = '1'; bar.style.pointerEvents = 'auto';
        S.editingSlot = null;
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
        document.getElementById('rttyZoomBtn').classList.toggle('active', S.zoom);
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
