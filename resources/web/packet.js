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
        txActiveUntilMs: 0,    // waterfall tints columns red while Date.now() < this
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
    // 2048 pts balances temporal vs. frequency detail: ~43 ms window at
    // 48 kHz (short enough that 9600-bd bursts line up roughly with their
    // red stripe) and 23 Hz/bin — plenty of structure inside 300-AFSK's
    // 200 Hz mark/space shift without smearing short bursts across the
    // whole panel.  Paired with bin interpolation in paintScope(), the
    // on-screen gradient looks sharper than the raw bin count suggests.
    var FFT_SIZE = 2048;
    var MIN_DB = -120;
    var MAX_DB = -20;
    // AGC keeps the column MEAN (noise floor proxy) at a dim-blue level so
    // actual signals pop above it.  Max-tracking (previous algorithm) made
    // noise grow brighter over idle RX because only a handful of bins
    // drove the target — mean is dominated by the noise floor.
    var AGC_GAIN_MIN = 0.2;
    var AGC_GAIN_MAX = 20;
    var AGC_TARGET_HI = 60;    // if EMA(mean) exceeds this, turn gain down
    var AGC_TARGET_LO = 30;    // if EMA(mean) drops below this, turn gain up

    // Audio-graph nodes owned by this panel.  These are inserted between
    // the webserver's AudioWorklet and audioGainNode — see cw-decoder.js
    // for the same pattern.
    var audioContext = null;
    var analyserNode = null;
    var boostGain = null;       // AGC-controlled — RX path only
    var txFixedGain = null;     // constant attenuator on the TX-echo path to the same analyser
    var txNextStart = 0;        // next playback time for TX-echo buffers on the shared analyser
    var rafId = null;
    var lastPaintMs = 0;
    var scopeColorLUT = null;
    var agcEmaMean = 40;       // EMA of per-column mean byte value — the AGC's reference
    var agcFrame = 0;

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
                '<button id="packetClearBtn" class="packet-action-btn" title="Clear frame list">Clear</button>' +
                '<button id="packetCloseBtn" class="packet-close-btn">&#x2715;</button>' +
            '</div>' +
            '<canvas id="packetScopeCanvas" class="packet-scope"></canvas>' +
            '<div class="packet-monitor">' +
                '<div class="packet-monitor-label">MONITOR</div>' +
                '<div id="packetFrames" class="packet-frames"></div>' +
            '</div>' +
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
                '<div id="termXferBar" class="term-xfer-bar hidden">' +
                    '<span id="termXferLabel" class="term-xfer-label"></span>' +
                    '<div class="term-xfer-track"><div id="termXferFill" class="term-xfer-fill"></div></div>' +
                    '<span id="termXferPct" class="term-xfer-pct">0%</span>' +
                    '<button id="termXferAbortBtn" class="packet-action-btn term-xfer-abort">Abort</button>' +
                '</div>' +
                '<div id="termScrollback" class="term-scrollback"></div>' +
                '<div class="term-input-row">' +
                    '<input id="termInput" class="term-input" spellcheck="false" placeholder="message — Enter to send">' +
                    '<button id="termSendBtn" class="packet-send-btn" disabled>Send</button>' +
                    '<button id="termSendFileBtn" class="packet-action-btn" disabled title="Transfer a file (YAPP)">Send File</button>' +
                    '<input id="termFileInput" type="file" style="display:none">' +
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

        var termFileBtn   = document.getElementById('termSendFileBtn');
        var termFileInput = document.getElementById('termFileInput');
        if (termFileBtn)   termFileBtn.onclick   = function() { termFileInput.click(); };
        if (termFileInput) termFileInput.onchange = termSendFilePicked;

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
        var abortBtn = document.getElementById('termXferAbortBtn');
        if (abortBtn) abortBtn.onclick = termAbortTransfer;
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
            '.packet-scope { width: 100%; height: 13%; min-height: 40px; background: #000; border: 1px solid #0a0; border-radius: 3px; margin-bottom: 6px; display: block; }' +
            '.packet-monitor { display: flex; flex-direction: column; min-height: 0; flex: 1 1 0; margin-bottom: 6px; }' +
            '.packet-monitor-label { color: #0f0; font-weight: bold; letter-spacing: 1px; font-size: 10px; margin-bottom: 2px; }' +
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
            '.packet-frame .ftype { color: #8cf; font-weight: bold; margin-left: 4px; }' +
            '.packet-frame.tx { background: #1a0a00; }' +
            '.packet-frame.tx .chan { color: #f80; font-weight: bold; }' +
            '.packet-frame.tx .ftype { color: #fc8; }' +
            '.packet-frame .info { color: #cfc; margin-left: 6px; }' +
            '.packet-frame .info.binary { color: #888; font-style: italic; }' +
            '.packet-empty { color: #666; padding: 8px; text-align: center; }' +
            '.flex-space { flex: 1; }' +
            // Tab strip
            '.packet-tabs { display: flex; gap: 2px; margin-bottom: 4px; border-bottom: 1px solid #0a0; }' +
            '.packet-tab-btn { background: #001a00; border: 1px solid #0a0; border-bottom: none; color: #0a0; padding: 4px 14px; font-family: monospace; font-size: 11px; font-weight: bold; cursor: pointer; border-radius: 3px 3px 0 0; }' +
            '.packet-tab-btn.active { background: #0a0; color: #000; }' +
            '.packet-tab-btn:hover:not(.active) { background: #0a0; color: #000; }' +
            '.packet-pane { flex: 3 1 0; min-height: 0; display: flex; flex-direction: column; }' +
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
            '.term-force-btn:hover:not(:disabled) { background: #4a0000 !important; color: #fb0 !important; }' +
            '.term-dl-link { color: #0cf; text-decoration: underline; cursor: pointer; }' +
            '.term-dl-link:hover { color: #0ff; }' +
            // Transfer progress widget
            '.term-xfer-bar { display: flex; align-items: center; gap: 8px; margin: 4px 0 6px 0; padding: 4px 6px; background: #001a00; border: 1px solid #0a0; border-radius: 3px; }' +
            '.term-xfer-bar.hidden { display: none; }' +
            '.term-xfer-label { color: #cfc; font-size: 11px; min-width: 0; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 260px; }' +
            '.term-xfer-track { flex: 1; height: 10px; background: #000; border: 1px solid #0a0; border-radius: 2px; overflow: hidden; min-width: 60px; }' +
            '.term-xfer-fill { height: 100%; background: #0a0; width: 0%; transition: width 100ms linear; }' +
            '.term-xfer-pct { color: #0f0; font-size: 11px; min-width: 36px; text-align: right; }' +
            '.term-xfer-abort { background: #2a0000 !important; border-color: #a40 !important; color: #f80 !important; }' +
            '.term-xfer-abort:hover { background: #4a0000 !important; color: #fb0 !important; }' +
            // Inbound-file modal prompt
            '.term-file-prompt { position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.7); z-index: 2000; display: flex; align-items: center; justify-content: center; font-family: monospace; }' +
            '.term-file-prompt-box { background: #001a00; border: 2px solid #0a0; border-radius: 6px; padding: 20px 24px; min-width: 320px; max-width: 500px; box-shadow: 0 0 40px rgba(0,255,0,0.2); }' +
            '.term-file-prompt-title { color: #0f0; font-weight: bold; font-size: 14px; letter-spacing: 2px; margin-bottom: 10px; }' +
            '.term-file-prompt-body { color: #cfc; font-size: 14px; margin-bottom: 6px; word-break: break-all; }' +
            '.term-file-prompt-size { color: #888; font-size: 12px; }' +
            '.term-file-prompt-note { color: #888; font-size: 11px; font-style: italic; margin-bottom: 14px; }' +
            '.term-file-prompt-btns { display: flex; gap: 8px; justify-content: flex-end; }';
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
            // Don't extend txActiveUntilMs here — onTxAudio() is the one
            // authoritative source, driven by the actual PCM being played
            // into the analyser.  Bumping it here left the stripe lingering
            // for up to 2.5 s past the real TX end.
        } else if (msg.type === 'packetStatus') {
            state.enabled = !!msg.enabled;
            if (typeof msg.mode === 'number') {
                state.mode = msg.mode;
                updateModeButtons();
            }
        } else if (msg.type === 'packetTxStarted') {
            state.txBusy = true;
            // Stripe window is driven by onTxAudio(), not by this event.
            setTxStatus('transmitting…', false);
            setTxButtonEnabled(false);
        } else if (msg.type === 'packetTxComplete') {
            state.txBusy = false;
            setTxStatus('sent', false);
            setTxButtonEnabled(true);
        } else if (msg.type === 'packetTxFailed') {
            state.txBusy = false;
            state.txActiveUntilMs = 0;
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
        } else if (msg.type === 'termFile') {
            termHandleReceivedFile(msg);
        } else if (msg.type === 'termXferStart') {
            console.log('[xfer]', new Date().toISOString(), 'START', msg);
            var sx = state.terminal.sessions[msg.sid];
            if (!sx) { sx = { sid: msg.sid, scrollback: [] }; state.terminal.sessions[msg.sid] = sx; }
            sx.xfer = { active: true, dir: msg.dir, name: msg.name,
                        total: msg.total || 0, done: 0,
                        phase: msg.phase || 'active' };
            if (!state.terminal.activeSid) state.terminal.activeSid = msg.sid;
            renderTerm();
        } else if (msg.type === 'termXferRequest') {
            // Peer wants to send us a file (YAPP SI received).  Block data
            // until the operator Accepts or Rejects; no bytes have been
            // transmitted yet — Reject genuinely prevents the transfer.
            showInboundFilePrompt(msg.sid, msg.senderInfo);
        } else if (msg.type === 'termXferProgress') {
            var sx = state.terminal.sessions[msg.sid];
            if (sx && sx.xfer) {
                sx.xfer.done  = msg.done  || 0;
                sx.xfer.total = msg.total || sx.xfer.total;
                if (state.terminal.activeSid === msg.sid) renderTermXfer();
            }
        } else if (msg.type === 'termXferEnd') {
            console.log('[xfer]', new Date().toISOString(), 'END', msg);
            var sx = state.terminal.sessions[msg.sid];
            if (sx) { sx.xfer = null; }
            if (state.terminal.activeSid === msg.sid) renderTerm();
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

    function infoLooksBinary(s) {
        // The backend delivers info as Latin-1 (each JS char == one raw byte).
        // "Looks binary" = any byte outside printable ASCII, excluding the
        // usual CR/LF/TAB that show up in AX.25 I-frames.  YAPP file-transfer
        // payloads (0x01 headers, 0x02 data, random bytes) trip the first
        // non-printable char almost immediately; don't even try to render.
        if (!s) return false;
        var n = Math.min(s.length, 64);
        for (var i = 0; i < n; i++) {
            var c = s.charCodeAt(i);
            if (c === 0x09 || c === 0x0a || c === 0x0d) continue;
            if (c < 0x20 || c > 0x7e) return true;
        }
        return false;
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
            var ftypeStr = f.ftype
                ? ' <span class="ftype">[' + escapeHtml(f.ftype) + ']</span>'
                : '';
            var infoStr = '';
            if (f.info) {
                if (infoLooksBinary(f.info)) {
                    infoStr = ' <span class="info binary">&lt;' + f.info.length + ' bytes binary&gt;</span>';
                } else {
                    infoStr = ' <span class="info">' + escapeHtml(f.info) + '</span>';
                }
            }
            html +=
                '<div class="packet-frame' + (f.tx ? ' tx' : '') + '">' +
                    '<span class="ts">' + escapeHtml(formatTs(f.ts)) + '</span>' +
                    '<span class="chan">' + (f.tx ? 'TX' : 'ch' + escapeHtml(f.chan)) + '</span>' +
                    '<span class="src">' + escapeHtml(f.src) + '</span>' +
                    ' &gt; ' +
                    '<span class="dst">' + escapeHtml(f.dst) + '</span>' +
                    '<span class="path">' + pathStr + '</span>' +
                    ftypeStr +
                    infoStr +
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
    // spectrogram.  No decoding happens here — demod runs on the backend.
    // This is purely a diagnostic view.
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
            // No smoothing — each painted column should reflect the instant
            // spectrum so short 9600-bd bursts line up with the TX stripe.
            analyserNode.smoothingTimeConstant = 0;
            analyserNode.minDecibels = MIN_DB;
            analyserNode.maxDecibels = MAX_DB;

            // RX pre-analyser gain — AGC-controlled, so faint rig audio
            // lights up the LUT without strong signals saturating it.
            // Starts mid-range; paintScope() adjusts every ~100 ms.
            boostGain = audioContext.createGain();
            boostGain.gain.value = 5;

            // TX audio (tee'd from the server) skips AGC and uses a fixed
            // attenuator so a TX burst doesn't knock the AGC off the RX
            // calibration, and so the TX spectrum is visible without
            // saturating the analyser top-end.
            txFixedGain = audioContext.createGain();
            txFixedGain.gain.value = 0.05;   // ~-26 dB; keeps TX tone bins off the LUT's saturation wall
            txFixedGain.connect(analyserNode);

            // Splice the RX path: worklet -> boost -> analyser.  Keep the
            // clean (unboosted) edge to audioGainNode so the speaker path
            // is unaffected.
            window.audioWorkletNode.disconnect();
            window.audioWorkletNode.connect(boostGain);
            boostGain.connect(analyserNode);
            window.audioWorkletNode.connect(window.audioGainNode);
        } catch (e) {
            console.warn('[Packet] analyser attach failed:', e);
            return;
        }
        lastPaintMs = performance.now();
        rafId = requestAnimationFrame(paintScope);
    }

    function stopScope() {
        if (rafId) { cancelAnimationFrame(rafId); rafId = null; }
        if (analyserNode || boostGain || txFixedGain) {
            try {
                if (analyserNode) analyserNode.disconnect();
                if (boostGain)    boostGain.disconnect();
                if (txFixedGain)  txFixedGain.disconnect();
                if (window.audioWorkletNode && window.audioGainNode) {
                    window.audioWorkletNode.disconnect();
                    window.audioWorkletNode.connect(window.audioGainNode);
                }
            } catch (e) { /* graph already torn down */ }
            analyserNode = null;
            boostGain = null;
            txFixedGain = null;
        }
        audioContext = null;
        txNextStart = 0;
    }

    // Server teed TX audio via binary msgType 0x04.  Push it through a
    // BufferSource that feeds boostGain/analyser *only* — not connected to
    // destination, so the operator doesn't hear their own outgoing audio.
    function onTxAudio(buffer) {
        if (!audioContext || !txFixedGain) return;  // scope not attached yet
        if (!buffer || buffer.byteLength < 6) return;
        var view = new DataView(buffer);
        if (view.getUint8(0) !== 0x04) return;
        var rateDiv = view.getUint16(4, true);
        var sampleRate = rateDiv > 0 ? rateDiv * 1000 : 48000;
        var pcmBytes = buffer.byteLength - 6;
        if (pcmBytes <= 0 || (pcmBytes & 1)) return;
        var samples = pcmBytes / 2;
        var pcm = new Int16Array(buffer, 6, samples);

        var buf;
        try {
            buf = audioContext.createBuffer(1, samples, sampleRate);
        } catch (e) {
            // Some browsers reject buffer sampleRates that don't match the
            // context; fall back to the context rate and accept pitch shift.
            buf = audioContext.createBuffer(1, samples, audioContext.sampleRate);
        }
        var chan = buf.getChannelData(0);
        for (var i = 0; i < samples; i++) chan[i] = pcm[i] / 32768;

        var src = audioContext.createBufferSource();
        src.buffer = buf;
        src.connect(txFixedGain);     // → analyser only (NOT audioGainNode, NOT boostGain)
        var now = audioContext.currentTime;
        if (txNextStart < now) txNextStart = now;
        try {
            src.start(txNextStart);
        } catch (e) {
            try { src.start(); } catch (_) { return; }
        }
        txNextStart += buf.duration;

        // Set the stripe window to the exact wall-clock end of the last
        // scheduled buffer — no fudge tail, so the stripe stops the frame
        // after the TX audio stops hitting the analyser.
        var endMs = Date.now() + Math.max(0, (txNextStart - now) * 1000);
        if (endMs > state.txActiveUntilMs) state.txActiveUntilMs = endMs;
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
        // TX-active columns keep the normal spectrogram colours so the
        // actual TX audio shape stays visible; the TX window is marked by
        // a thin red stripe painted across the top rows afterwards.
        var txActive = Date.now() < state.txActiveUntilMs;
        var colSum = 0;
        // Visibility floor — radios mute RX for a brief window after unkey
        // so the analyser genuinely receives silence there.  Mapping that
        // to pure black reads as a visible "gap" after every TX; lifting
        // everything by a few LUT steps paints the gap as a dim blue floor
        // instead, matching the surrounding background colour.
        var FLOOR = 18;
        var Hm1 = Math.max(1, H - 1);
        for (var y = 0; y < H; y++) {
            // Linear interpolation between adjacent FFT bins.  Without this
            // each bin maps to a stepped block of pixels and the LUT's
            // quantised colour transitions are very visible — especially in
            // narrow panels where binRange is much smaller than H.
            var frac = (H - 1 - y) / Hm1;
            var idxF = minBin + frac * binRange;
            var i0 = Math.floor(idxF);
            var i1 = i0 + 1;
            if (i1 > maxBin) i1 = maxBin;
            var t = idxF - i0;
            var v = spectrum[i0] * (1 - t) + spectrum[i1] * t;
            if (v < FLOOR) v = FLOOR;
            v = v | 0;     // back to integer for the LUT lookup
            if (v > 255) v = 255;
            colSum += v;
            var rgb = scopeColorLUT[v];
            var q = y * 4;
            buf[q] = rgb[0]; buf[q + 1] = rgb[1]; buf[q + 2] = rgb[2];
            buf[q + 3] = 255;
        }
        var colMean = colSum / H;
        for (var i = 0; i < step; i++) {
            scopeCtx.putImageData(scopeColumn, scopeCanvas.width - step + i, 0);
        }

        if (txActive) {
            // Top-of-canvas red stripe (thicker than 1 px so it survives
            // the DPR scaling on hi-dpi displays).
            var stripeH = Math.max(2, Math.floor(H / 24));
            scopeCtx.fillStyle = 'rgba(255, 50, 50, 0.9)';
            scopeCtx.fillRect(scopeCanvas.width - step, 0, step, stripeH);
        }

        // AGC: drive the per-column mean (a good proxy for noise floor since
        // most bins don't hold signal) toward a dim-blue target, so real
        // signals pop above it.  Frozen during TX — the tee'd TX PCM would
        // otherwise bias the EMA and leave RX dim once the burst ends.
        if (!txActive) {
            agcEmaMean = agcEmaMean * 0.92 + colMean * 0.08;
            agcFrame++;
            if (boostGain && (agcFrame % 6) === 0) {
                var g = boostGain.gain.value;
                if (agcEmaMean > AGC_TARGET_HI && g > AGC_GAIN_MIN) {
                    boostGain.gain.value = Math.max(AGC_GAIN_MIN, g * 0.9);
                } else if (agcEmaMean < AGC_TARGET_LO && g < AGC_GAIN_MAX) {
                    boostGain.gain.value = Math.min(AGC_GAIN_MAX, g * 1.1);
                }
            }
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
        renderTermXfer();
        var sid = state.terminal.activeSid;
        var s = sid && state.terminal.sessions[sid];
        var connected = s && s.state === 'connected';
        var isDisconnecting = s && s.state === 'disconnecting';
        var canDisconnect = s && (s.state === 'connected'
                                  || s.state === 'connecting'
                                  || isDisconnecting);
        var xferActive = s && s.xfer && s.xfer.active;
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
        // Text send and file pick are both disabled during an active
        // transfer — the link is busy, and the Abort button (in the
        // progress bar) is the only way forward besides waiting.
        if (sendBtn) sendBtn.disabled = !connected || xferActive;
        if (input)   input.disabled   = !connected || xferActive;
        var fileBtn = document.getElementById('termSendFileBtn');
        if (fileBtn) fileBtn.disabled = !connected || xferActive;
    }

    function renderTermXfer() {
        var bar = document.getElementById('termXferBar');
        if (!bar) return;
        var sid = state.terminal.activeSid;
        var s   = sid && state.terminal.sessions[sid];
        var x   = s && s.xfer && s.xfer.active ? s.xfer : null;
        if (!x) { bar.classList.add('hidden'); return; }

        bar.classList.remove('hidden');
        var pct = x.total > 0 ? Math.floor((x.done / x.total) * 100) : 0;
        if (pct > 100) pct = 100;
        var arrow = x.dir === 'tx' ? '↑' : '↓';
        var label;
        if (x.phase === 'awaiting_rr') {
            // Sender side, pre-accept — no payload on the wire yet.
            label = arrow + ' ' + (x.name || '(file)') + '  — awaiting peer...';
            pct = 0;
        } else {
            label = arrow + ' ' + (x.name || '(file)')
                    + '  ' + x.done + ' / ' + x.total + ' B';
        }
        document.getElementById('termXferLabel').textContent = label;
        document.getElementById('termXferFill').style.width = pct + '%';
        document.getElementById('termXferPct').textContent  = pct + '%';
    }

    function termAbortTransfer() {
        var sid = state.terminal.activeSid;
        if (!sid) return;
        if (window.send) window.send({ cmd: 'termFileAbort', sid: sid });
    }

    function showInboundFilePrompt(sid, senderInfo) {
        // Avoid stacking prompts if a second request arrives before the
        // first is answered.
        var existing = document.getElementById('termFilePrompt');
        if (existing) existing.remove();

        var modal = document.createElement('div');
        modal.id = 'termFilePrompt';
        modal.className = 'term-file-prompt';
        modal.innerHTML =
            '<div class="term-file-prompt-box">' +
                '<div class="term-file-prompt-title">Incoming file request</div>' +
                '<div class="term-file-prompt-body">' +
                    escapeHtml(senderInfo || '(unknown)') +
                    ' wants to send you a file.' +
                '</div>' +
                '<div class="term-file-prompt-note">The transfer will only start if you accept.</div>' +
                '<div class="term-file-prompt-btns">' +
                    '<button id="termFilePromptReject" class="packet-action-btn term-xfer-abort">Reject</button>' +
                    '<button id="termFilePromptAccept" class="packet-send-btn">Accept</button>' +
                '</div>' +
            '</div>';
        document.body.appendChild(modal);
        document.getElementById('termFilePromptAccept').onclick = function() {
            if (window.send) window.send({ cmd: 'termXferAccept', sid: sid });
            modal.remove();
        };
        document.getElementById('termFilePromptReject').onclick = function() {
            if (window.send) window.send({ cmd: 'termXferReject', sid: sid });
            modal.remove();
        };
    }

    function termSendFilePicked(e) {
        var sid = state.terminal.activeSid;
        if (!sid) return;
        var s = state.terminal.sessions[sid];
        if (!s || s.state !== 'connected') return;
        var f = e.target.files && e.target.files[0];
        if (!f) return;
        // Cap at a few MB so the base64 round-trip doesn't flood the WS.
        if (f.size > 4 * 1024 * 1024) {
            alert('File too large (max 4 MB for this build).');
            e.target.value = '';
            return;
        }
        var reader = new FileReader();
        reader.onload = function() {
            var b64 = bufToBase64(new Uint8Array(reader.result));
            if (window.send) {
                window.send({ cmd: 'termFileSend', sid: sid, name: f.name, dataB64: b64 });
            }
        };
        reader.readAsArrayBuffer(f);
        e.target.value = '';  // reset so re-picking same file fires change
    }

    function bufToBase64(bytes) {
        var bin = '';
        var chunk = 0x8000;
        for (var i = 0; i < bytes.length; i += chunk) {
            bin += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
        }
        return btoa(bin);
    }

    function termHandleReceivedFile(msg) {
        // Convert base64 → Blob → auto-download.
        var bin = atob(msg.dataB64 || '');
        var buf = new Uint8Array(bin.length);
        for (var i = 0; i < bin.length; i++) buf[i] = bin.charCodeAt(i);
        var blob = new Blob([buf], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob);
        var fname = msg.name || 'received.bin';

        // Surface the save link inline with the completion notice so the
        // user can retrieve the file again if the auto-download was blocked.
        var s = state.terminal.sessions[msg.sid];
        if (s) {
            s.scrollback = s.scrollback || [];
            s.scrollback.push({
                ts: Date.now(),
                dir: 'info',
                data: '⤓ Saved: ' + fname,
                __download: { href: url, name: fname }
            });
            if (state.terminal.activeSid === msg.sid) renderTermScrollback();
        }

        // Auto-click so the browser prompts the user right away.  If it's
        // blocked (background tab, strict site setting), the scrollback
        // link is the fallback.
        var a = document.createElement('a');
        a.href = url;
        a.download = fname;
        a.style.display = 'none';
        document.body.appendChild(a);
        a.click();
        setTimeout(function() { document.body.removeChild(a); }, 100);
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
                    '<span class="' + (e.dir || 'info') + '">' + escapeHtml(e.data || '') + '</span>';
            if (e.__download) {
                // Render a visible "save again" link — the browser was also
                // auto-clicked when the file arrived, so the file is already
                // downloaded; this is a re-save affordance.
                html += ' <a class="term-dl-link" href="' + e.__download.href +
                        '" download="' + escapeHtml(e.__download.name) + '">[save again]</a>';
            }
            if (e.dir === 'info') html += '\n';
        }
        pane.innerHTML = html;
        pane.scrollTop = pane.scrollHeight;
    }

    window.Packet = {
        init: init,
        onMessage: onMessage,
        onTxAudio: onTxAudio,
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
