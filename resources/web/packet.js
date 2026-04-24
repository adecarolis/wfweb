// Packet (Dire Wolf) decoded-frame panel for wfweb.
// Follows the cw-decoder.js pattern: classic script, self-inits on DOMContentLoaded,
// reaches out to window.send for command dispatch, exposes window.Packet.

(function() {
    'use strict';

    var FRAME_BUFFER_MAX = 200; // retain last N decoded frames

    var state = {
        visible: false,
        enabled: false,        // master packet modem enable
        mode: 300,             // 300 (HF AFSK), 1200 (VHF AFSK), or 9600 (VHF G3RUH)
        frames: [],
        txBusy: false,         // true between packetTxStarted and packetTxComplete/Failed
        compose: {             // last-used TX fields (persisted to localStorage)
            src:  'N0CALL',
            dst:  'APRS',
            path: 'WIDE1-1',
            info: 'hello from wfweb'
        },
        activeTab: 'aprs',     // "aprs" or "term"
        terminal: {
            sessions: {},      // sid -> {sid, ownCall, peerCall, digis, state, scrollback[], chan}
            activeSid: null,   // which session's scrollback we're showing
            compose: {         // last-used connect fields
                ownCall:  'N0CALL',
                peerCall: 'N0CALL-1',
                digis:    ''
            }
        }
    };

    var barEl = null;
    var framesEl = null;
    var scopeCanvas = null;
    var scopeCtx = null;
    var scopeColumn = null;

    // AFSK / G3RUH parameters per baud.  minHz/maxHz define the visible
    // frequency window for the spectrogram; mark/space are drawn as overlay
    // lines so the user can see the tone pair they're trying to demodulate.
    var MODE_PARAMS = {
        300:  { minHz: 1200, maxHz: 2200, mark: 1600, space: 1800 },
        1200: { minHz:  500, maxHz: 3000, mark: 1200, space: 2200 },
        9600: { minHz:    0, maxHz: 5000, mark:    0, space:    0 }  // baseband
    };
    var FFT_SIZE = 8192;   // ~6 Hz/bin at 48 kHz — resolves the 200 Hz shift
                           // of 300 AFSK (1600/1800) with clear separate ridges.
    var MIN_DB = -90;
    var MAX_DB = -30;

    // Audio-graph nodes owned by this panel.  These are inserted between
    // the webserver's AudioWorklet and audioGainNode — see cw-decoder.js
    // for the same pattern.
    var audioContext = null;
    var analyserNode = null;
    var rafId = null;
    var lastPaintMs = 0;
    var scopeColorLUT = null;

    function init() {
        buildUI();
    }

    function buildUI() {
        if (document.getElementById('packetBar')) return;
        if (!document.body) { setTimeout(buildUI, 50); return; }

        addStyles();

        barEl = document.createElement('div');
        barEl.id = 'packetBar';
        barEl.className = 'packet-bar hidden';
        barEl.innerHTML =
            '<div class="packet-header">' +
                '<span class="packet-label">PACKET</span>' +
                '<button id="packetMode300"  class="packet-mode-btn" data-mode="300"  title="300 bps AFSK (HF packet)">300 AFSK</button>' +
                '<button id="packetMode1200" class="packet-mode-btn" data-mode="1200" title="1200 bps AFSK Bell 202 (VHF / APRS)">1200 AFSK</button>' +
                '<button id="packetMode9600" class="packet-mode-btn" data-mode="9600" title="9600 bps G3RUH FSK (VHF)">9600 FSK</button>' +
                '<div class="flex-space"></div>' +
                '<button id="packetCaptureBtn" class="packet-action-btn" title="Save 10 seconds of incoming audio (as seen by the demodulator) to a WAV file">Save 10s</button>' +
                '<span id="packetCaptureStatus" class="packet-capture-status"></span>' +
                '<button id="packetClearBtn" class="packet-action-btn" title="Clear frame list">Clear</button>' +
                '<button id="packetCloseBtn" class="packet-close-btn">&#x2715;</button>' +
            '</div>' +
            '<canvas id="packetScopeCanvas" class="packet-scope"></canvas>' +
            '<div class="packet-tabs">' +
                '<button id="packetTabAprs" class="packet-tab-btn active" data-tab="aprs">APRS</button>' +
                '<button id="packetTabTerm" class="packet-tab-btn" data-tab="term">TERMINAL</button>' +
            '</div>' +
            '<div id="packetAprsPane" class="packet-pane">' +
                '<div class="packet-compose">' +
                    '<label>From <input id="packetTxSrc"  class="packet-tx-field" maxlength="9" size="9" spellcheck="false"></label>' +
                    '<label>To <input id="packetTxDst"  class="packet-tx-field" maxlength="9" size="9" spellcheck="false"></label>' +
                    '<label>Path <input id="packetTxPath" class="packet-tx-field" size="14" spellcheck="false" placeholder="WIDE1-1"></label>' +
                    '<input id="packetTxInfo" class="packet-tx-info" spellcheck="false" placeholder="payload"> ' +
                    '<button id="packetTxBtn" class="packet-send-btn" title="Transmit a single AX.25 UI frame">TX</button>' +
                    '<span id="packetTxStatus" class="packet-tx-status"></span>' +
                '</div>' +
                '<div id="packetFrames" class="packet-frames"></div>' +
            '</div>' +
            '<div id="packetTermPane" class="packet-pane hidden">' +
                '<div class="term-bar">' +
                    '<label>Own <input id="termOwnCall"  class="packet-tx-field" maxlength="9" size="9" spellcheck="false"></label>' +
                    '<label>Peer <input id="termPeerCall" class="packet-tx-field" maxlength="9" size="9" spellcheck="false"></label>' +
                    '<label>Digi <input id="termDigis"    class="packet-tx-field" size="20" spellcheck="false" placeholder="DIGI1,DIGI2"></label>' +
                    '<button id="termConnectBtn"    class="packet-send-btn" title="Open AX.25 connected-mode link">Connect</button>' +
                    '<button id="termDisconnectBtn" class="packet-action-btn" title="Disconnect this session" disabled>Disconnect</button>' +
                    '<span   id="termStateChip"     class="term-state-chip">DISCONNECTED</span>' +
                    '<select id="termSessionPicker" class="term-session-picker"></select>' +
                '</div>' +
                '<div id="termScrollback" class="term-scrollback"></div>' +
                '<div class="term-input-row">' +
                    '<input id="termInput" class="term-input" spellcheck="false" placeholder="message — Enter to send">' +
                    '<button id="termSendBtn" class="packet-send-btn" disabled>Send</button>' +
                '</div>' +
            '</div>';

        // Mount inside #scopeArea — its z-index:300 paints over anything in body,
        // so a sibling panel on body gets covered. The digi-bar lives inside it
        // for the same reason.
        var scopeArea = document.getElementById('scopeArea');
        if (scopeArea) {
            scopeArea.appendChild(barEl);
        } else {
            document.body.appendChild(barEl);
        }

        framesEl = document.getElementById('packetFrames');
        scopeCanvas = document.getElementById('packetScopeCanvas');
        if (scopeCanvas) {
            scopeCtx = scopeCanvas.getContext('2d');
            window.addEventListener('resize', resizeScope);
        }

        var modeBtns = barEl.querySelectorAll('.packet-mode-btn');
        for (var i = 0; i < modeBtns.length; i++) {
            (function(btn) {
                btn.onclick = function() {
                    setMode(parseInt(btn.getAttribute('data-mode'), 10));
                };
            })(modeBtns[i]);
        }
        document.getElementById('packetClearBtn').onclick = clearFrames;
        document.getElementById('packetCaptureBtn').onclick = captureAudio;
        document.getElementById('packetCloseBtn').onclick = hide;

        loadComposeFromStorage();
        var txSrcEl  = document.getElementById('packetTxSrc');
        var txDstEl  = document.getElementById('packetTxDst');
        var txPathEl = document.getElementById('packetTxPath');
        var txInfoEl = document.getElementById('packetTxInfo');
        if (txSrcEl)  txSrcEl.value  = state.compose.src;
        if (txDstEl)  txDstEl.value  = state.compose.dst;
        if (txPathEl) txPathEl.value = state.compose.path;
        if (txInfoEl) txInfoEl.value = state.compose.info;
        var txBtn = document.getElementById('packetTxBtn');
        if (txBtn) txBtn.onclick = sendFrame;
        if (txInfoEl) txInfoEl.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { e.preventDefault(); sendFrame(); }
        });

        // Tab buttons.
        var tabBtns = barEl.querySelectorAll('.packet-tab-btn');
        for (var t = 0; t < tabBtns.length; t++) {
            (function(btn) {
                btn.onclick = function() { setActiveTab(btn.getAttribute('data-tab')); };
            })(tabBtns[t]);
        }

        // Terminal pane controls.
        loadTermComposeFromStorage();
        var ownEl  = document.getElementById('termOwnCall');
        var peerEl = document.getElementById('termPeerCall');
        var digisEl = document.getElementById('termDigis');
        if (ownEl)   ownEl.value   = state.terminal.compose.ownCall;
        if (peerEl)  peerEl.value  = state.terminal.compose.peerCall;
        if (digisEl) digisEl.value = state.terminal.compose.digis;

        document.getElementById('termConnectBtn').onclick = termConnect;
        document.getElementById('termDisconnectBtn').onclick = termDisconnect;
        document.getElementById('termSendBtn').onclick = termSendLine;
        var termInput = document.getElementById('termInput');
        if (termInput) {
            termInput.addEventListener('keydown', function(e) {
                if (e.key === 'Enter' && !e.shiftKey) {
                    e.preventDefault();
                    termSendLine();
                }
            });
        }
        document.getElementById('termSessionPicker').onchange = function(e) {
            state.terminal.activeSid = e.target.value || null;
            renderTerm();
        };
        // Re-register on focus-out so changes to the Own input flow through
        // without the operator having to re-enter the tab.
        if (ownEl) ownEl.addEventListener('blur', function() {
            if (state.activeTab === 'term') termRegisterOwn();
        });

        updateModeButtons();
        renderTerm();
        // Re-open the packet panel (and its last tab) if the browser had it
        // open before refresh.  Done after all handlers are bound.  The
        // server keeps modem enable/mode in its own prefs, so this is a
        // pure UI-layout restore; packetStatus from the server will
        // re-sync once the WS connects.
        restoreLayoutFromStorage();
    }

    function addStyles() {
        if (document.getElementById('packetStyles')) return;
        var style = document.createElement('style');
        style.id = 'packetStyles';
        style.textContent =
            '.packet-bar { position: fixed; top: 134px; left: 0; right: 0; bottom: 48px; background: #001008; border-top: 2px solid #0a0; z-index: 200; padding: 6px 8px; color: #cfc; font-family: monospace; font-size: 12px; display: flex; flex-direction: column; min-height: 0; box-sizing: border-box; }' +
            'body.packet-open #spectrumCanvas, body.packet-open #waterfallCanvas { display: none !important; }' +
            '.packet-scope { width: 100%; height: 40%; min-height: 80px; background: #000; border: 1px solid #0a0; border-radius: 3px; margin-bottom: 6px; display: block; }' +
            '@media (max-height: 450px) { .packet-bar { top: 112px; bottom: 40px; } }' +
            '.packet-frames-wrap { flex: 1; min-height: 0; display: flex; flex-direction: column; }' +
            '.packet-bar.hidden { display: none; }' +
            '.packet-header { display: flex; align-items: center; gap: 6px; margin-bottom: 6px; }' +
            '.packet-label { color: #0f0; font-weight: bold; letter-spacing: 1px; }' +
            '.packet-mode-btn { background: #001a00; border: 1px solid #0a0; color: #0a0; padding: 2px 8px; font-family: monospace; font-size: 10px; font-weight: bold; cursor: pointer; border-radius: 3px; }' +
            '.packet-mode-btn.active { background: #0a0; color: #000; }' +
            '.packet-mode-btn:hover { background: #0a0; color: #000; }' +
            '.packet-action-btn, .packet-close-btn { background: #111; border: 1px solid #555; color: #ccc; padding: 2px 8px; font-family: monospace; font-size: 10px; cursor: pointer; border-radius: 3px; }' +
            '.packet-action-btn:hover, .packet-close-btn:hover { background: #333; color: #fff; }' +
            '.packet-action-btn:disabled { opacity: 0.5; cursor: default; }' +
            '.packet-capture-status { color: #8c8; font-size: 10px; margin-left: 4px; min-width: 120px; }' +
            '.packet-capture-status.err { color: #f66; }' +
            '.packet-compose { display: flex; align-items: center; gap: 6px; margin-bottom: 6px; font-size: 10px; color: #8c8; }' +
            '.packet-compose label { display: flex; align-items: center; gap: 3px; }' +
            '.packet-tx-field, .packet-tx-info { background: #001a00; border: 1px solid #0a0; color: #cfc; font-family: monospace; font-size: 11px; padding: 2px 4px; border-radius: 2px; outline: none; text-transform: uppercase; }' +
            '.packet-tx-info { flex: 1; min-width: 140px; text-transform: none; }' +
            '.packet-tx-field:focus, .packet-tx-info:focus { border-color: #0f0; }' +
            '.packet-send-btn { background: #0a0; color: #000; border: 1px solid #0f0; padding: 3px 12px; font-family: monospace; font-size: 11px; font-weight: bold; cursor: pointer; border-radius: 3px; }' +
            '.packet-send-btn:hover:not(:disabled) { background: #0f0; }' +
            '.packet-send-btn:disabled { opacity: 0.5; cursor: default; }' +
            '.packet-tx-status { color: #8c8; font-size: 10px; min-width: 120px; }' +
            '.packet-tx-status.err { color: #f66; }' +
            '.packet-frames { background: #000; border: 1px solid #0a0; border-radius: 3px; padding: 4px; flex: 1; min-height: 0; overflow-y: auto; font-size: 11px; line-height: 1.4; }' +
            '.packet-frame { padding: 2px 4px; border-bottom: 1px dotted #1a3a1a; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }' +
            '.packet-frame:last-child { border-bottom: none; }' +
            '.packet-frame .ts { color: #888; margin-right: 6px; }' +
            '.packet-frame .chan { color: #888; margin-right: 6px; }' +
            '.packet-frame .src { color: #ff0; }' +
            '.packet-frame .dst { color: #0ff; }' +
            '.packet-frame .path { color: #888; }' +
            '.packet-frame.tx { background: #001a00; }' +
            '.packet-frame.tx .chan { color: #f80; font-weight: bold; }' +
            '.packet-frame .info { color: #cfc; margin-left: 6px; }' +
            '.packet-empty { color: #666; padding: 8px; text-align: center; }' +
            '.flex-space { flex: 1; }' +
            // Tab strip
            '.packet-tabs { display: flex; gap: 2px; margin-bottom: 4px; border-bottom: 1px solid #0a0; }' +
            '.packet-tab-btn { background: #001a00; border: 1px solid #0a0; border-bottom: none; color: #0a0; padding: 4px 14px; font-family: monospace; font-size: 11px; font-weight: bold; cursor: pointer; border-radius: 3px 3px 0 0; }' +
            '.packet-tab-btn.active { background: #0a0; color: #000; }' +
            '.packet-tab-btn:hover:not(.active) { background: #0a0; color: #000; }' +
            '.packet-pane { flex: 1; min-height: 0; display: flex; flex-direction: column; }' +
            '.packet-pane.hidden { display: none; }' +
            // Terminal pane
            '.term-bar { display: flex; align-items: center; gap: 6px; margin-bottom: 6px; font-size: 10px; color: #8c8; flex-wrap: wrap; }' +
            '.term-bar label { display: flex; align-items: center; gap: 3px; }' +
            '.term-state-chip { padding: 2px 8px; border: 1px solid #555; border-radius: 3px; font-size: 10px; font-weight: bold; color: #888; background: #111; min-width: 90px; text-align: center; letter-spacing: 1px; }' +
            '.term-state-chip.connecting    { color: #ff0; border-color: #aa0; background: #110; }' +
            '.term-state-chip.connected     { color: #0f0; border-color: #0a0; background: #010; }' +
            '.term-state-chip.disconnecting { color: #f80; border-color: #a40; background: #110; }' +
            '.term-state-chip.disconnected  { color: #888; border-color: #555; background: #111; }' +
            '.term-session-picker { background: #001a00; border: 1px solid #0a0; color: #cfc; font-family: monospace; font-size: 11px; padding: 2px 4px; border-radius: 2px; outline: none; }' +
            '.term-session-picker:empty { display: none; }' +
            '.term-scrollback { background: #000; border: 1px solid #0a0; border-radius: 3px; padding: 6px; flex: 1; min-height: 0; overflow-y: auto; font-family: monospace; font-size: 12px; line-height: 1.4; color: #cfc; white-space: pre-wrap; word-wrap: break-word; }' +
            '.term-scrollback .rx { color: #cfc; }' +
            '.term-scrollback .tx { color: #ff0; }' +
            '.term-scrollback .info { color: #888; font-style: italic; }' +
            '.term-scrollback .ts { color: #555; margin-right: 6px; font-size: 10px; }' +
            '.term-input-row { display: flex; gap: 6px; margin-top: 6px; }' +
            '.term-input { flex: 1; background: #001a00; border: 1px solid #0a0; color: #cfc; font-family: monospace; font-size: 12px; padding: 4px 6px; border-radius: 2px; outline: none; }' +
            '.term-input:focus { border-color: #0f0; }' +
            '.term-input:disabled { opacity: 0.5; }' +
            '.term-force-btn { background: #2a0000 !important; border-color: #a40 !important; color: #f80 !important; }' +
            '.term-force-btn:hover:not(:disabled) { background: #4a0000 !important; color: #fb0 !important; }';
        document.head.appendChild(style);
    }

    function show() {
        state.visible = true;
        if (barEl) barEl.classList.remove('hidden');
        document.body.classList.add('packet-open');
        setEnabled(true);
        renderFrames();
        // Kick off the waterfall after the bar is visible so the canvas
        // has a non-zero clientWidth/Height for the DPR math.
        setTimeout(startScope, 0);
        saveLayoutToStorage();
    }

    function hide() {
        state.visible = false;
        if (barEl) barEl.classList.add('hidden');
        document.body.classList.remove('packet-open');
        setEnabled(false);
        stopScope();
        saveLayoutToStorage();
    }

    function toggle() {
        if (state.visible) hide(); else show();
    }

    function saveLayoutToStorage() {
        try {
            localStorage.setItem('wfweb.packet.layout', JSON.stringify({
                visible: state.visible,
                activeTab: state.activeTab
            }));
        } catch (e) { /* quota — ignore */ }
    }

    function restoreLayoutFromStorage() {
        try {
            var raw = localStorage.getItem('wfweb.packet.layout');
            if (!raw) return;
            var obj = JSON.parse(raw);
            if (!obj || typeof obj !== 'object') return;

            if (obj.activeTab === 'aprs' || obj.activeTab === 'term') {
                // Direct DOM toggle — don't fire the side-effects setActiveTab
                // does (termList / termRegister) until we're actually visible.
                state.activeTab = obj.activeTab;
                var aprs = document.getElementById('packetAprsPane');
                var term = document.getElementById('packetTermPane');
                var aBtn = document.getElementById('packetTabAprs');
                var tBtn = document.getElementById('packetTabTerm');
                if (aprs) aprs.classList.toggle('hidden', obj.activeTab !== 'aprs');
                if (term) term.classList.toggle('hidden', obj.activeTab !== 'term');
                if (aBtn) aBtn.classList.toggle('active', obj.activeTab === 'aprs');
                if (tBtn) tBtn.classList.toggle('active', obj.activeTab === 'term');
            }

            if (obj.visible === true) {
                // Reopen the panel and, if we landed on the TERMINAL tab,
                // run its per-tab activation (termList / termRegister).
                // setEnabled inside show() fires packetEnable, but the
                // server is the source of truth — if it's already enabled
                // the round-trip is a no-op; if WS isn't open yet, send()
                // silently drops and the later packetStatus sync corrects us.
                show();
                if (state.activeTab === 'term') setActiveTab('term');
            }
        } catch (e) { /* ignore */ }
    }

    function setEnabled(on) {
        state.enabled = !!on;
        if (window.send) window.send({ cmd: 'packetEnable', value: state.enabled });
        // Ensure the backend knows the current mode whenever we (re)enable.
        if (state.enabled) setMode(state.mode);
    }

    function setMode(baud) {
        if (baud !== 300 && baud !== 1200 && baud !== 9600) return;
        state.mode = baud;
        if (window.send) window.send({ cmd: 'packetSetMode', value: baud });
        updateModeButtons();
        // Repaint frequency-marker overlay with the new mark/space pair.
        if (scopeCtx) clearScope();
    }

    function updateModeButtons() {
        if (!barEl) return;
        var btns = barEl.querySelectorAll('.packet-mode-btn');
        for (var i = 0; i < btns.length; i++) {
            var m = parseInt(btns[i].getAttribute('data-mode'), 10);
            btns[i].classList.toggle('active', m === state.mode);
        }
    }

    function onMessage(msg) {
        if (!msg || !msg.type) return;
        if (msg.type === 'packetRxFrame') {
            appendFrame(msg);
        } else if (msg.type === 'packetStatus') {
            state.enabled = !!msg.enabled;
            if (typeof msg.mode === 'number') {
                state.mode = msg.mode;
                updateModeButtons();
            }
        } else if (msg.type === 'packetCaptureStarted') {
            setCaptureStatus('capturing ' + msg.seconds + 's…', false);
        } else if (msg.type === 'packetCaptureComplete') {
            setCaptureStatus('saved ' + msg.path +
                ' (' + msg.samples + ' samples @ ' + msg.sampleRate + ' Hz)', false);
            var btn = document.getElementById('packetCaptureBtn');
            if (btn) btn.disabled = false;
        } else if (msg.type === 'packetCaptureFailed') {
            setCaptureStatus('capture failed: ' + (msg.reason || 'unknown'), true);
            var btn = document.getElementById('packetCaptureBtn');
            if (btn) btn.disabled = false;
        } else if (msg.type === 'packetTxStarted') {
            state.txBusy = true;
            setTxStatus('transmitting…', false);
            setTxButtonEnabled(false);
            // Echo our own TX frame into the log so the operator sees it.
            var parts = parseMonitor(msg.monitor);
            if (parts) {
                appendFrame({
                    type: 'packetRxFrame',
                    chan: 0,
                    src: parts.src,
                    dst: parts.dst,
                    path: parts.path,
                    info: parts.info,
                    ts: Date.now(),
                    tx: true
                });
            }
        } else if (msg.type === 'packetTxComplete') {
            state.txBusy = false;
            setTxStatus('sent', false);
            setTxButtonEnabled(true);
        } else if (msg.type === 'packetTxFailed') {
            state.txBusy = false;
            setTxStatus('TX failed: ' + (msg.reason || 'unknown'), true);
            setTxButtonEnabled(true);
        } else if (msg.type === 'termSession') {
            handleTermSession(msg);
        } else if (msg.type === 'termClose') {
            delete state.terminal.sessions[msg.sid];
            if (state.terminal.activeSid === msg.sid) state.terminal.activeSid = null;
            renderTerm();
        } else if (msg.type === 'termData') {
            var s = state.terminal.sessions[msg.sid];
            if (!s) {
                // Server may emit data before our termSession arrived if we
                // missed it; create a stub so the entry isn't lost.
                s = { sid: msg.sid, scrollback: [], state: 'connected' };
                state.terminal.sessions[msg.sid] = s;
            }
            s.scrollback = s.scrollback || [];
            s.scrollback.push({ ts: msg.ts, dir: msg.dir, data: msg.data });
            if (state.terminal.activeSid === msg.sid) renderTermScrollback();
        } else if (msg.type === 'termList') {
            // Replace local view with server snapshot.  Keep activeSid if it
            // still exists.
            state.terminal.sessions = {};
            if (Array.isArray(msg.sessions)) {
                for (var i = 0; i < msg.sessions.length; i++) {
                    var ss = msg.sessions[i];
                    state.terminal.sessions[ss.sid] = ss;
                    state.terminal.sessions[ss.sid].scrollback = [];
                }
            }
            // If we have sessions but no active one, pick the first.
            var sids = Object.keys(state.terminal.sessions);
            if (!state.terminal.activeSid && sids.length > 0) {
                state.terminal.activeSid = sids[0];
                if (window.send) window.send({ cmd: 'termHistory', sid: state.terminal.activeSid });
            }
            renderTerm();
        } else if (msg.type === 'termHistory') {
            var sh = state.terminal.sessions[msg.sid];
            if (sh && Array.isArray(msg.entries)) {
                sh.scrollback = msg.entries.slice();
                if (state.terminal.activeSid === msg.sid) renderTermScrollback();
            }
        } else if (msg.type === 'termError') {
            // Show as info line in active session, or alert if none.
            var sa = state.terminal.activeSid && state.terminal.sessions[state.terminal.activeSid];
            if (sa) {
                sa.scrollback = sa.scrollback || [];
                sa.scrollback.push({ ts: Date.now(), dir: 'info', data: '*** ERROR: ' + (msg.reason || 'unknown') });
                renderTermScrollback();
            } else {
                console.warn('[term] error:', msg.reason);
            }
        }
    }

    function handleTermSession(msg) {
        var existing = state.terminal.sessions[msg.sid];
        var s = existing || { sid: msg.sid, scrollback: [] };
        s.sid      = msg.sid;
        s.chan     = msg.chan;
        s.ownCall  = msg.ownCall;
        s.peerCall = msg.peerCall;
        s.digis    = msg.digis || [];
        s.state    = msg.state;
        s.incoming = msg.incoming;
        if (!existing) state.terminal.sessions[msg.sid] = s;
        // First session we see becomes active.
        if (!state.terminal.activeSid) state.terminal.activeSid = msg.sid;
        // Pre-warm scrollback for fresh sessions; for reconnects, request history.
        if (!existing && state.terminal.activeSid === msg.sid) {
            if (window.send) window.send({ cmd: 'termHistory', sid: msg.sid });
        }
        renderTerm();
    }

    function parseMonitor(s) {
        // "SRC>DST[,PATH,...]:info"
        if (!s) return null;
        var colon = s.indexOf(':');
        if (colon < 0) return null;
        var header = s.substring(0, colon);
        var info   = s.substring(colon + 1);
        var gt = header.indexOf('>');
        if (gt < 0) return null;
        var src = header.substring(0, gt);
        var rest = header.substring(gt + 1).split(',');
        var dst = rest[0];
        var path = rest.slice(1);
        return { src: src, dst: dst, path: path, info: info };
    }

    function setTxStatus(text, isError) {
        var el = document.getElementById('packetTxStatus');
        if (!el) return;
        el.textContent = text;
        el.classList.toggle('err', !!isError);
    }

    function setTxButtonEnabled(enabled) {
        var btn = document.getElementById('packetTxBtn');
        if (btn) btn.disabled = !enabled;
    }

    function sendFrame() {
        if (state.txBusy) return;
        if (!state.enabled) {
            setTxStatus('enable packet modem first', true);
            return;
        }
        var src  = (document.getElementById('packetTxSrc').value  || '').trim().toUpperCase();
        var dst  = (document.getElementById('packetTxDst').value  || '').trim().toUpperCase();
        var path = (document.getElementById('packetTxPath').value || '').trim().toUpperCase();
        var info = (document.getElementById('packetTxInfo').value || '');
        if (!src || !dst || !info) {
            setTxStatus('From, To, and Info are required', true);
            return;
        }

        var pathList = [];
        if (path.length > 0) {
            var parts = path.split(/[,\s]+/);
            for (var i = 0; i < parts.length; i++) {
                if (parts[i]) pathList.push(parts[i]);
            }
        }

        state.compose.src = src;
        state.compose.dst = dst;
        state.compose.path = path;
        state.compose.info = info;
        saveComposeToStorage();

        setTxStatus('requesting…', false);
        setTxButtonEnabled(false);
        if (window.send) {
            window.send({ cmd: 'packetTx', src: src, dst: dst, path: pathList, info: info });
        }
    }

    function loadComposeFromStorage() {
        try {
            var raw = localStorage.getItem('wfweb.packet.compose');
            if (!raw) return;
            var obj = JSON.parse(raw);
            if (obj && typeof obj === 'object') {
                if (typeof obj.src  === 'string') state.compose.src  = obj.src;
                if (typeof obj.dst  === 'string') state.compose.dst  = obj.dst;
                if (typeof obj.path === 'string') state.compose.path = obj.path;
                if (typeof obj.info === 'string') state.compose.info = obj.info;
            }
        } catch (e) { /* ignore */ }
    }

    function saveComposeToStorage() {
        try {
            localStorage.setItem('wfweb.packet.compose', JSON.stringify(state.compose));
        } catch (e) { /* quota exceeded — ignore */ }
    }

    function captureAudio() {
        if (!state.enabled) {
            setCaptureStatus('enable packet modem first', true);
            return;
        }
        var btn = document.getElementById('packetCaptureBtn');
        if (btn) btn.disabled = true;
        setCaptureStatus('requesting…', false);
        if (window.send) window.send({ cmd: 'packetCaptureAudio', seconds: 10 });
    }

    function setCaptureStatus(text, isError) {
        var el = document.getElementById('packetCaptureStatus');
        if (!el) return;
        el.textContent = text;
        el.classList.toggle('err', !!isError);
    }

    function appendFrame(frame) {
        state.frames.push(frame);
        if (state.frames.length > FRAME_BUFFER_MAX) {
            state.frames.splice(0, state.frames.length - FRAME_BUFFER_MAX);
        }
        if (state.visible) renderFrames();
    }

    function clearFrames() {
        state.frames = [];
        renderFrames();
    }

    function formatTs(ms) {
        if (!ms) return '';
        var d = new Date(ms);
        var hh = String(d.getHours()).padStart(2, '0');
        var mm = String(d.getMinutes()).padStart(2, '0');
        var ss = String(d.getSeconds()).padStart(2, '0');
        return hh + ':' + mm + ':' + ss;
    }

    function escapeHtml(s) {
        return String(s == null ? '' : s)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');
    }

    function renderFrames() {
        if (!framesEl) return;
        if (state.frames.length === 0) {
            framesEl.innerHTML = '<div class="packet-empty">No frames decoded yet.</div>';
            return;
        }
        var html = '';
        for (var i = 0; i < state.frames.length; i++) {
            var f = state.frames[i];
            var pathStr = Array.isArray(f.path) && f.path.length > 0
                ? ' via ' + escapeHtml(f.path.join(','))
                : '';
            html +=
                '<div class="packet-frame' + (f.tx ? ' tx' : '') + '">' +
                    '<span class="ts">' + escapeHtml(formatTs(f.ts)) + '</span>' +
                    '<span class="chan">' + (f.tx ? 'TX' : 'ch' + escapeHtml(f.chan)) + '</span>' +
                    '<span class="src">' + escapeHtml(f.src) + '</span>' +
                    ' &gt; ' +
                    '<span class="dst">' + escapeHtml(f.dst) + '</span>' +
                    '<span class="path">' + pathStr + '</span>' +
                    '<span class="info">' + escapeHtml(f.info) + '</span>' +
                '</div>';
        }
        framesEl.innerHTML = html;
        framesEl.scrollTop = framesEl.scrollHeight;
    }

    // ---------------------------------------------------------------------
    // Dedicated packet waterfall
    //
    // Mirrors cw-decoder.js: taps the live RX audio graph (window.audioCtx
    // + window.audioWorkletNode) with an AnalyserNode and paints a scrolling
    // spectrogram.  Mark/space tones for the active baud are drawn as
    // horizontal marker lines so the operator can see they're on frequency.
    // No decoding happens here — demod runs on the backend.  This is purely
    // a diagnostic view.
    // ---------------------------------------------------------------------

    function buildScopeColorLUT() {
        if (scopeColorLUT) return scopeColorLUT;
        scopeColorLUT = new Array(256);
        for (var v = 0; v < 256; v++) {
            // Black -> dark blue -> cyan -> green -> yellow -> red (like wfview)
            var t = v / 255;
            var r, g, b;
            if (t < 0.25)      { r = 0;                     g = 0;                     b = Math.floor(128 * (t / 0.25)); }
            else if (t < 0.5)  { r = 0;                     g = Math.floor(255 * ((t - 0.25) / 0.25)); b = 128 + Math.floor(127 * ((t - 0.25) / 0.25)); }
            else if (t < 0.75) { r = Math.floor(255 * ((t - 0.5) / 0.25));  g = 255;                   b = Math.floor(255 * (1 - (t - 0.5) / 0.25)); }
            else               { r = 255;                   g = Math.floor(255 * (1 - (t - 0.75) / 0.25)); b = 0; }
            scopeColorLUT[v] = [r, g, b];
        }
        return scopeColorLUT;
    }

    function resizeScope() {
        if (!scopeCanvas) return;
        var dpr = window.devicePixelRatio || 1;
        var rect = scopeCanvas.getBoundingClientRect();
        var w = Math.max(1, Math.floor((rect.width  || scopeCanvas.clientWidth  || 300) * dpr));
        var h = Math.max(1, Math.floor((rect.height || scopeCanvas.clientHeight || 100) * dpr));
        if (scopeCanvas.width !== w || scopeCanvas.height !== h) {
            scopeCanvas.width = w;
            scopeCanvas.height = h;
            scopeColumn = null;     // drop stale column buffer
            clearScope();
        }
    }

    function clearScope() {
        if (!scopeCanvas || !scopeCtx) return;
        scopeCtx.fillStyle = '#000';
        scopeCtx.fillRect(0, 0, scopeCanvas.width, scopeCanvas.height);
        drawFreqMarkers();
    }

    function drawFreqMarkers() {
        if (!scopeCanvas || !scopeCtx) return;
        var p = MODE_PARAMS[state.mode];
        if (!p || !p.mark || !p.space) return;  // 9600 baseband: skip
        var range = p.maxHz - p.minHz;
        // Markers live on a small right-edge ruler that doesn't scroll —
        // the canvas is being shifted left each paint, so we can't draw
        // into it permanently.  Instead we paint a dashed indicator on
        // the rightmost 2 px column per frame inside painColumn().
        // (Kept as a helper so clearScope() can stamp the labels once.)
        scopeCtx.fillStyle = 'rgba(255,255,0,0.9)';
        scopeCtx.font = (Math.floor(scopeCanvas.height / 14)) + 'px monospace';
        var yMark  = yForHz(p.mark);
        var ySpace = yForHz(p.space);
        scopeCtx.fillText('M ' + p.mark,  4, yMark  + 4);
        scopeCtx.fillText('S ' + p.space, 4, ySpace + 4);
        // freq labels on the left stay, markers are redrawn each paint
    }

    function yForHz(hz) {
        var p = MODE_PARAMS[state.mode];
        if (!p) return 0;
        var frac = (hz - p.minHz) / (p.maxHz - p.minHz);
        return Math.floor((1 - frac) * (scopeCanvas.height - 1));
    }

    function startScope() {
        if (!scopeCanvas || !scopeCtx) return;
        resizeScope();
        clearScope();
        buildScopeColorLUT();

        // Require the shared audio graph.  If the user hasn't started RX
        // audio playback yet there's no context; poll briefly, then give up
        // gracefully — the backend modem will still decode either way.
        var tries = 0;
        var attach = function() {
            if (!state.visible) return;
            if (window.audioCtx && window.audioWorkletNode && window.audioGainNode) {
                attachAnalyser();
                return;
            }
            if (++tries < 50) setTimeout(attach, 100);
            else drawScopeMessage('Start RX audio for waterfall');
        };
        attach();
    }

    function attachAnalyser() {
        audioContext = window.audioCtx;
        try {
            analyserNode = audioContext.createAnalyser();
            analyserNode.fftSize = FFT_SIZE;
            analyserNode.smoothingTimeConstant = 0.2;
            analyserNode.minDecibels = MIN_DB;
            analyserNode.maxDecibels = MAX_DB;

            // Splice: worklet -> analyser -> gain (unchanged downstream).
            window.audioWorkletNode.disconnect();
            window.audioWorkletNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);
        } catch (e) {
            console.warn('[Packet] analyser attach failed:', e);
            return;
        }
        lastPaintMs = performance.now();
        rafId = requestAnimationFrame(paintScope);
    }

    function stopScope() {
        if (rafId) { cancelAnimationFrame(rafId); rafId = null; }
        if (analyserNode) {
            try {
                analyserNode.disconnect();
                if (window.audioWorkletNode && window.audioGainNode) {
                    window.audioWorkletNode.disconnect();
                    window.audioWorkletNode.connect(window.audioGainNode);
                }
            } catch (e) { /* graph already torn down */ }
            analyserNode = null;
        }
        audioContext = null;
    }

    function drawScopeMessage(text) {
        if (!scopeCtx || !scopeCanvas) return;
        scopeCtx.fillStyle = '#0a0';
        scopeCtx.font = Math.floor(scopeCanvas.height / 6) + 'px monospace';
        scopeCtx.textAlign = 'center';
        scopeCtx.fillText(text, scopeCanvas.width / 2, scopeCanvas.height / 2);
        scopeCtx.textAlign = 'left';
    }

    function paintScope() {
        rafId = null;
        if (!state.visible || !analyserNode || !scopeCanvas || !scopeCtx) return;

        // Advance by whole pixels so the scroll rate is stable across frame
        // rates.  30 px/s gives a visible packet burst that spans ~3 s at
        // typical panel widths.
        var now = performance.now();
        var dt = (now - lastPaintMs) / 1000;
        lastPaintMs = now;
        var pxPerSec = Math.max(30, Math.floor(scopeCanvas.width / 6));
        var step = Math.max(1, Math.floor(dt * pxPerSec));
        if (step > scopeCanvas.width) step = scopeCanvas.width;

        var bins = analyserNode.frequencyBinCount;
        var spectrum = new Uint8Array(bins);
        analyserNode.getByteFrequencyData(spectrum);

        // Scroll existing image left.
        scopeCtx.drawImage(
            scopeCanvas,
            0, 0, scopeCanvas.width, scopeCanvas.height,
            -step, 0, scopeCanvas.width, scopeCanvas.height
        );

        if (!scopeColumn || scopeColumn.height !== scopeCanvas.height) {
            scopeColumn = scopeCtx.createImageData(1, scopeCanvas.height);
        }

        var p = MODE_PARAMS[state.mode];
        var nyquist = audioContext.sampleRate / 2;
        var minBin = Math.floor((p.minHz / nyquist) * (bins - 1));
        var maxBin = Math.min(bins - 1, Math.ceil((p.maxHz / nyquist) * (bins - 1)));
        var binRange = Math.max(1, maxBin - minBin);
        var buf = scopeColumn.data;
        var H = scopeCanvas.height;
        for (var y = 0; y < H; y++) {
            // Top of canvas = high freq; invert so high freq appears at top.
            var frac = (H - 1 - y) / Math.max(1, H - 1);
            var idx = minBin + Math.floor(frac * binRange);
            var v = spectrum[idx];
            var rgb = scopeColorLUT[v];
            var q = y * 4;
            buf[q] = rgb[0]; buf[q + 1] = rgb[1]; buf[q + 2] = rgb[2]; buf[q + 3] = 255;
        }
        for (var i = 0; i < step; i++) {
            scopeCtx.putImageData(scopeColumn, scopeCanvas.width - step + i, 0);
        }

        // Mark/space frequency lines (repainted on top of the freshly
        // scrolled image).  Short ticks on the right edge + a faint
        // full-width guide so the operator can align TX frequency.
        if (p.mark && p.space) {
            var yMark  = yForHz(p.mark);
            var yOff   = yForHz(p.space);
            scopeCtx.strokeStyle = 'rgba(255,255,0,0.35)';
            scopeCtx.lineWidth = 1;
            scopeCtx.beginPath();
            scopeCtx.moveTo(0, yMark);  scopeCtx.lineTo(scopeCanvas.width, yMark);
            scopeCtx.moveTo(0, yOff);   scopeCtx.lineTo(scopeCanvas.width, yOff);
            scopeCtx.stroke();
        }

        rafId = requestAnimationFrame(paintScope);
    }

    // ---------------------------------------------------------------------
    // Tab + terminal handling

    function setActiveTab(name) {
        if (name !== 'aprs' && name !== 'term') return;
        state.activeTab = name;
        var aprsBtn = document.getElementById('packetTabAprs');
        var termBtn = document.getElementById('packetTabTerm');
        var aprsPane = document.getElementById('packetAprsPane');
        var termPane = document.getElementById('packetTermPane');
        if (aprsBtn)  aprsBtn.classList.toggle('active', name === 'aprs');
        if (termBtn)  termBtn.classList.toggle('active', name === 'term');
        if (aprsPane) aprsPane.classList.toggle('hidden', name !== 'aprs');
        if (termPane) termPane.classList.toggle('hidden', name !== 'term');
        saveLayoutToStorage();
        if (name === 'term') {
            // Pull any sessions the server might already be holding.
            if (window.send) window.send({ cmd: 'termList' });
            // Register our own callsign so the server accepts inbound SABMs
            // even before we make any outbound call.  Auto-enable so the
            // operator only flips one switch.
            if (!state.enabled) setEnabled(true);
            termRegisterOwn();
            renderTerm();
        }
    }

    function termRegisterOwn() {
        var ownEl = document.getElementById('termOwnCall');
        if (!ownEl) return;
        var own = (ownEl.value || '').trim().toUpperCase();
        if (!own || own === 'N0CALL') return;
        if (window.send) window.send({ cmd: 'termRegister', ownCall: own, chan: 0 });
    }

    function termConnect() {
        if (!state.enabled) {
            // Auto-enable so the operator doesn't have to flip two switches.
            setEnabled(true);
        }
        var own  = (document.getElementById('termOwnCall').value  || '').trim().toUpperCase();
        var peer = (document.getElementById('termPeerCall').value || '').trim().toUpperCase();
        var digisRaw = (document.getElementById('termDigis').value || '').trim().toUpperCase();
        if (!own || !peer) return;

        var digis = [];
        if (digisRaw.length > 0) {
            var parts = digisRaw.split(/[,\s]+/);
            for (var i = 0; i < parts.length; i++) {
                if (parts[i]) digis.push(parts[i]);
            }
        }

        state.terminal.compose.ownCall  = own;
        state.terminal.compose.peerCall = peer;
        state.terminal.compose.digis    = digisRaw;
        saveTermComposeToStorage();

        if (window.send) {
            window.send({ cmd: 'termConnect', ownCall: own, peerCall: peer, digis: digis, chan: 0 });
        }
    }

    function termDisconnect() {
        var sid = state.terminal.activeSid;
        if (!sid) return;
        if (window.send) window.send({ cmd: 'termDisconnect', sid: sid });
    }

    function termSendLine() {
        var sid = state.terminal.activeSid;
        if (!sid) return;
        var s = state.terminal.sessions[sid];
        if (!s || s.state !== 'connected') return;
        var input = document.getElementById('termInput');
        if (!input) return;
        var text = input.value;
        if (text.length === 0) return;
        // AX.25 BBSes expect CR line terminators (TSTHOST-era convention).
        if (window.send) window.send({ cmd: 'termSend', sid: sid, data: text + '\r' });
        input.value = '';
    }

    function loadTermComposeFromStorage() {
        try {
            var raw = localStorage.getItem('wfweb.term.compose');
            if (!raw) return;
            var obj = JSON.parse(raw);
            if (obj && typeof obj === 'object') {
                if (typeof obj.ownCall  === 'string') state.terminal.compose.ownCall  = obj.ownCall;
                if (typeof obj.peerCall === 'string') state.terminal.compose.peerCall = obj.peerCall;
                if (typeof obj.digis    === 'string') state.terminal.compose.digis    = obj.digis;
            }
        } catch (e) { /* ignore */ }
    }

    function saveTermComposeToStorage() {
        try {
            localStorage.setItem('wfweb.term.compose', JSON.stringify(state.terminal.compose));
        } catch (e) { /* quota — ignore */ }
    }

    function renderTerm() {
        renderTermPicker();
        renderTermStateChip();
        renderTermScrollback();
        var sid = state.terminal.activeSid;
        var s = sid && state.terminal.sessions[sid];
        var connected = s && s.state === 'connected';
        var isDisconnecting = s && s.state === 'disconnecting';
        var canDisconnect = s && (s.state === 'connected'
                                  || s.state === 'connecting'
                                  || isDisconnecting);
        var dcBtn = document.getElementById('termDisconnectBtn');
        var sendBtn = document.getElementById('termSendBtn');
        var input = document.getElementById('termInput');
        if (dcBtn) {
            dcBtn.disabled = !canDisconnect;
            // In Disconnecting, a second press asks ax25_link to send a DM
            // and go straight to disconnected — no waiting for the peer's UA.
            dcBtn.textContent = isDisconnecting ? 'Force' : 'Disconnect';
            dcBtn.classList.toggle('term-force-btn', !!isDisconnecting);
            dcBtn.title = isDisconnecting
                ? 'Send DM and tear down the link immediately'
                : 'Disconnect this session';
        }
        if (sendBtn) sendBtn.disabled = !connected;
        if (input)   input.disabled   = !connected;
    }

    function renderTermPicker() {
        var picker = document.getElementById('termSessionPicker');
        if (!picker) return;
        var sids = Object.keys(state.terminal.sessions);
        if (sids.length <= 1) {
            picker.innerHTML = '';
            picker.style.display = 'none';
            return;
        }
        picker.style.display = '';
        var html = '';
        for (var i = 0; i < sids.length; i++) {
            var s = state.terminal.sessions[sids[i]];
            var lbl = s.peerCall + ' (' + s.state + ')';
            html += '<option value="' + sids[i] + '"' +
                    (sids[i] === state.terminal.activeSid ? ' selected' : '') +
                    '>' + escapeHtml(lbl) + '</option>';
        }
        picker.innerHTML = html;
    }

    function renderTermStateChip() {
        var chip = document.getElementById('termStateChip');
        if (!chip) return;
        var sid = state.terminal.activeSid;
        var s = sid && state.terminal.sessions[sid];
        var st = s ? s.state : 'disconnected';
        chip.className = 'term-state-chip ' + st;
        chip.textContent = st.toUpperCase();
    }

    function renderTermScrollback() {
        var pane = document.getElementById('termScrollback');
        if (!pane) return;
        var sid = state.terminal.activeSid;
        var s = sid && state.terminal.sessions[sid];
        if (!s) {
            pane.innerHTML = '<div class="info">No active session. Enter a callsign and click Connect.</div>';
            return;
        }
        var html = '';
        var entries = s.scrollback || [];
        for (var i = 0; i < entries.length; i++) {
            var e = entries[i];
            html += '<span class="ts">' + escapeHtml(formatTs(e.ts)) + '</span>' +
                    '<span class="' + (e.dir || 'info') + '">' + escapeHtml(e.data || '') + '</span>' +
                    (e.dir === 'info' ? '\n' : '');
        }
        pane.innerHTML = html;
        pane.scrollTop = pane.scrollHeight;
    }

    window.Packet = {
        init: init,
        onMessage: onMessage,
        show: show,
        hide: hide,
        toggle: toggle,
        get state() { return state; }
    };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
