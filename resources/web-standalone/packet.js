// Packet (Dire Wolf) decoded-frame panel for wfweb.
// Follows the cw-decoder.js pattern: classic script, self-inits on DOMContentLoaded,
// reaches out to window.send for command dispatch, exposes window.Packet.

(function() {
    'use strict';

    var FRAME_BUFFER_MAX = 200; // retain last N decoded frames

    // Common APRS primary-table symbols.  `key` is our internal id; `table`
    // is `/` (primary) or `\` (alternate); `code` is the single-char symbol.
    // Kept short on purpose — operators who want exotic symbols can edit
    // their lat/lon/comment by hand and we pick the icon by other means.
    var APRS_SYMBOLS = [
        { key: 'house',     label: 'House (QTH)',          table: '/', code: '-' },
        { key: 'car',       label: 'Car',                  table: '/', code: '>' },
        { key: 'pickup',    label: 'Pickup truck',         table: '/', code: 'U' },
        { key: 'truck',     label: 'Truck (18-wheeler)',   table: '/', code: 'k' },
        { key: 'van',       label: 'Van',                  table: '/', code: 'v' },
        { key: 'jeep',      label: 'Jeep',                 table: '/', code: 'j' },
        { key: 'motorcycle',label: 'Motorcycle',           table: '/', code: '<' },
        { key: 'bicycle',   label: 'Bicycle',              table: '/', code: 'b' },
        { key: 'runner',    label: 'Runner',               table: '/', code: '[' },
        { key: 'antenna',   label: 'Antenna',              table: '/', code: 'r' },
        { key: 'aircraft',  label: 'Aircraft',             table: '/', code: '\'' },
        { key: 'balloon',   label: 'Balloon',              table: '/', code: 'O' },
        { key: 'boat',      label: 'Boat',                 table: '/', code: 'Y' },
        { key: 'ship',      label: 'Ship',                 table: '/', code: 's' },
        { key: 'digi',      label: 'Digipeater',           table: '/', code: '#' },
        { key: 'wx',        label: 'Weather station',      table: '/', code: '_' },
        { key: 'phone',     label: 'Phone',                table: '/', code: '$' }
    ];

    function symbolFor(key) {
        for (var i = 0; i < APRS_SYMBOLS.length; i++) {
            if (APRS_SYMBOLS[i].key === key) return APRS_SYMBOLS[i];
        }
        return APRS_SYMBOLS[0];
    }
    function symbolKeyFor(table, code) {
        for (var i = 0; i < APRS_SYMBOLS.length; i++) {
            var s = APRS_SYMBOLS[i];
            if (s.table === table && s.code === code) return s.key;
        }
        return null;
    }

    var state = {
        visible: false,
        enabled: false,        // master packet modem enable
        mode: 300,             // 300 (HF AFSK), 1200 (VHF AFSK), or 9600 (VHF G3RUH)
        frames: [],
        txBusy: false,         // true between packetTxStarted and packetTxComplete/Failed
        txActiveUntilMs: 0,    // waterfall tints columns red while Date.now() < this
        aprs: {
            stations: {},      // src -> {src, lat, lon, symTable, symCode, comment, lastHeard, count, path[], _new}
            sortBy: 'lastHeard',  // column key
            sortDir: 'desc',
            beacon: {           // last-used TX fields (persisted to localStorage)
                src: '',        // derived from app callsign + settings.aprsSsid
                lat: 0,
                lon: 0,
                symKey: 'house',  // index into APRS_SYMBOLS
                comment: 'wfweb',
                path: 'WIDE1-1',
                intervalMin: 10,
                enabled: false
            }
        },
        activeTab: 'aprs',     // "aprs" or "term"
        terminal: {
            sessions: {},      // sid -> {sid, ownCall, peerCall, digis, state, scrollback[], chan, hasUnread}
            activeSid: null,   // which session's scrollback we're showing
            pendingOutbound: null,  // {ownCall, peerCall, chan} — set by termConnect, matched
                                    // against the next termSession we see so the tab the
                                    // *user* just asked for becomes active.  Inbound
                                    // sessions never set activeSid on their own.
            compose: {         // last-used connect fields
                ownCall:  '',  // derived from app callsign + settings.termSsid
                peerCall: 'N0CALL-1',
                digis:    ''
            }
        },
        // PKT-specific settings exposed via the gear dialog.  Callsign lives
        // in the shared window.App.callsign store, but the AX.25/APRS world
        // needs per-purpose SSIDs appended to it, so those live here.
        settings: {
            aprsSsid: '',      // "" or "0".."15"
            termSsid: ''       // "" or "0".."15"
        }
    };

    // Compose "K1FM" + "9" -> "K1FM-9"; blank/zero SSID leaves the bare call.
    function composeCallWithSsid(call, ssid) {
        var c = String(call || '').trim().toUpperCase();
        if (!c) return '';
        var s = String(ssid || '').trim();
        if (s === '' || s === '0') return c;
        var n = parseInt(s, 10);
        if (!isFinite(n) || n < 0 || n > 15) return c;
        return c + '-' + n;
    }

    // Re-derive the APRS beacon src and terminal own-call from the shared
    // app callsign and PKT-specific SSIDs.  Call after either changes.
    function recomputeDerivedCalls() {
        var base = (window.App && window.App.callsign) ? window.App.callsign.get() : '';
        state.aprs.beacon.src = composeCallWithSsid(base, state.settings.aprsSsid);
        state.terminal.compose.ownCall = composeCallWithSsid(base, state.settings.termSsid);
    }

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
    var AGC_TARGET_HI = 70;    // if EMA(mean) exceeds this, turn gain down
    var AGC_TARGET_LO = 45;    // if EMA(mean) drops below this, turn gain up — sits a hair above FLOOR so idle noise lands in the dark-blue band

    // Audio-graph nodes owned by this panel.  These are inserted between
    // the webserver's AudioWorklet and audioGainNode — see cw-decoder.js
    // for the same pattern.
    var audioContext = null;
    var analyserNode = null;
    var boostGain = null;       // AGC-controlled — RX path only
    var txFixedGain = null;     // constant attenuator on the TX-echo path to the same analyser
    var txMonitorGain = null;   // low-volume tap from TX-echo to speakers (operator monitor)
    var txNextStart = 0;        // next playback time for TX-echo buffers on the shared analyser
    var rafId = null;
    var lastPaintMs = 0;
    var scopeColorLUT = null;
    var agcEmaMean = 55;       // EMA of per-column mean byte value — the AGC's reference (sits between TARGET_LO and TARGET_HI on init)
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
                '<button id="packetSettingsBtn" class="packet-settings-btn" title="PKT settings" aria-label="PKT settings">&#x2699;</button>' +
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
                '<div class="aprs-split">' +
                    '<div class="aprs-stations-wrap">' +
                        '<div class="aprs-section-header">' +
                            '<span>HEARD STATIONS</span>' +
                            '<span id="aprsStationCount" class="aprs-count">0</span>' +
                            '<div class="flex-space"></div>' +
                            '<button id="aprsClearBtn" class="packet-action-btn" title="Forget all heard stations">Clear</button>' +
                        '</div>' +
                        '<div id="aprsStations" class="aprs-stations"></div>' +
                    '</div>' +
                    '<div class="aprs-beacon-wrap">' +
                        '<div class="aprs-section-header">' +
                            '<span>MY BEACON</span>' +
                            '<span id="aprsBeaconState" class="aprs-beacon-state">idle</span>' +
                        '</div>' +
                        '<div class="aprs-beacon-grid">' +
                            '<span id="aprsSrcDisplay" class="packet-call-display" title="Set in PKT settings">—</span>' +
                            '<label>Symbol <select id="aprsSym"   class="aprs-sym-select"></select></label>' +
                            '<label>Lat  <input id="aprsLat"     class="packet-tx-field" size="10" spellcheck="false" placeholder="40.6892"></label>' +
                            '<label>Lon  <input id="aprsLon"     class="packet-tx-field" size="10" spellcheck="false" placeholder="-74.0445"></label>' +
                            '<button id="aprsGeoBtn"  class="packet-action-btn" title="Use this device\'s location">Use my location</button>' +
                            '<label class="aprs-comment-label">Comment <input id="aprsComment" class="packet-tx-info" maxlength="43" spellcheck="false" placeholder="wfweb"></label>' +
                            '<label>Path <input id="aprsPath"    class="packet-tx-field" size="14" spellcheck="false" placeholder="WIDE1-1"></label>' +
                            '<label class="aprs-interval-label">Every <input id="aprsInterval" class="packet-tx-field" size="4" spellcheck="false" value="10"> min</label>' +
                            '<button id="aprsTxNowBtn"   class="packet-send-btn" title="Send one position report now">TX now</button>' +
                            '<button id="aprsBeaconBtn"  class="packet-action-btn" title="Toggle periodic beacon">Beacon: OFF</button>' +
                            '<span id="packetTxStatus" class="packet-tx-status"></span>' +
                        '</div>' +
                    '</div>' +
                '</div>' +
            '</div>' +
            '<div id="packetTermPane" class="packet-pane hidden">' +
                '<div class="term-bar">' +
                    '<span id="termOwnCallDisplay" class="packet-call-display" title="Set in PKT settings">—</span>' +
                    '<label>Peer <input id="termPeerCall" class="packet-tx-field" maxlength="9" size="9" spellcheck="false"></label>' +
                    '<label>Digi <input id="termDigis"    class="packet-tx-field" size="20" spellcheck="false" placeholder="DIGI1,DIGI2"></label>' +
                    '<button id="termConnectBtn"    class="packet-action-btn" title="Open AX.25 connected-mode link">Connect</button>' +
                    '<button id="termDisconnectBtn" class="packet-action-btn" title="Disconnect this session" disabled>Disconnect</button>' +
                    '<span   id="termStateChip"     class="term-state-chip">DISCONNECTED</span>' +
                '</div>' +
                '<div id="termSessionTabs" class="term-session-tabs"></div>' +
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
        if (framesEl) {
            framesEl.addEventListener('click', function(e) {
                var link = e.target.closest && e.target.closest('.callsign-link');
                if (!link) return;
                var call = link.getAttribute('data-call');
                if (call) setPeerFromMonitor(call);
            });
        }
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
        document.getElementById('packetSettingsBtn').onclick = openSettingsDialog;
        document.getElementById('packetCloseBtn').onclick = hide;

        loadSettingsFromStorage();
        loadAprsFromStorage();
        recomputeDerivedCalls();
        populateSymbolSelect();
        bindAprsControls();
        renderAprsStations();
        renderAprsBeaconButton();
        // Periodic redraw so "X min ago" ages stay current without
        // round-trips to the server.
        setInterval(function() { if (state.visible) renderAprsStations(); }, 5000);

        // Tab buttons.
        var tabBtns = barEl.querySelectorAll('.packet-tab-btn');
        for (var t = 0; t < tabBtns.length; t++) {
            (function(btn) {
                btn.onclick = function() { setActiveTab(btn.getAttribute('data-tab')); };
            })(tabBtns[t]);
        }

        // Terminal pane controls.
        loadTermComposeFromStorage();
        // Derived calls need to be recomputed after compose/settings load
        // and peer/digi state is restored.
        recomputeDerivedCalls();
        var peerEl = document.getElementById('termPeerCall');
        var digisEl = document.getElementById('termDigis');
        if (peerEl)  peerEl.value  = state.terminal.compose.peerCall;
        if (digisEl) digisEl.value = state.terminal.compose.digis;
        updateCallDisplays();

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
        // Tab strip click delegation — body selects, × closes.
        document.getElementById('termSessionTabs').addEventListener('click', function(e) {
            var closeBtn = e.target.closest && e.target.closest('.term-tab-close');
            if (closeBtn) {
                e.stopPropagation();
                var sidC = closeBtn.getAttribute('data-sid');
                if (sidC && window.send) window.send({ cmd: 'termClose', sid: sidC });
                return;
            }
            var tab = e.target.closest && e.target.closest('.term-tab');
            if (!tab) return;
            var sid = tab.getAttribute('data-sid');
            if (sid) termSetActive(sid);
        });
        var abortBtn = document.getElementById('termXferAbortBtn');
        if (abortBtn) abortBtn.onclick = termAbortTransfer;

        // Shared-callsign change: recompute derived own-call / aprs src,
        // refresh the visible chips, and re-register the terminal so the
        // server accepts inbound SABMs under the new call.
        window.addEventListener('appCallsignChanged', function() {
            recomputeDerivedCalls();
            updateCallDisplays();
            saveTermComposeToStorage();
            saveAprsToStorage();
            if (state.activeTab === 'term') termRegisterOwn();
            if (state.aprs.beacon.enabled) aprsSendBeaconConfig(true);
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
            /* absolute (not fixed) so it letterboxes with #scopeArea (max-width 16:9):
               fixed bars peek past the overlays on viewports wider than 16:9 — issue #39 */
            '.packet-bar { position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: #001008; border-top: 2px solid #0a0; z-index: 200; padding: 6px 8px; color: #cfc; font-family: monospace; font-size: 12px; display: flex; flex-direction: column; min-height: 0; box-sizing: border-box; }' +
            'body.packet-open #spectrumCanvas, body.packet-open #waterfallCanvas { display: none !important; }' +
            '.packet-scope { width: 100%; height: 13%; min-height: 40px; background: #000; border: 1px solid #0a0; border-radius: 3px; margin-bottom: 6px; display: block; }' +
            '.packet-monitor { display: flex; flex-direction: column; min-height: 0; flex: 1 1 0; margin-bottom: 6px; }' +
            '.packet-monitor-label { color: #0f0; font-weight: bold; letter-spacing: 1px; font-size: 10px; margin-bottom: 2px; }' +
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
            '.packet-frame .digi { color: #888; }' +
            '.packet-frame .callsign-link { cursor: pointer; text-decoration: underline; text-decoration-style: dotted; text-underline-offset: 2px; }' +
            '.packet-frame .callsign-link:hover { background: #003300; color: #fff; }' +
            '.packet-empty { color: #666; padding: 8px; text-align: center; }' +
            '.flex-space { flex: 1; }' +
            // Tab strip
            '.packet-tabs { display: flex; gap: 2px; margin-bottom: 4px; border-bottom: 1px solid #0a0; }' +
            '.packet-tab-btn { background: #001a00; border: 1px solid #0a0; border-bottom: none; color: #0a0; padding: 4px 14px; font-family: monospace; font-size: 11px; font-weight: bold; cursor: pointer; border-radius: 3px 3px 0 0; }' +
            '.packet-tab-btn.active { background: #0a0; color: #000; }' +
            '.packet-tab-btn:hover:not(.active) { background: #0a0; color: #000; }' +
            '.packet-pane { flex: 3 1 0; min-height: 0; display: flex; flex-direction: column; }' +
            '.packet-pane.hidden { display: none; }' +
            // APRS pane
            '.aprs-split { display: flex; flex-direction: column; gap: 6px; flex: 1 1 0; min-height: 0; }' +
            '.aprs-stations-wrap { display: flex; flex-direction: column; flex: 1 1 0; min-height: 0; }' +
            '.aprs-beacon-wrap { display: flex; flex-direction: column; flex-shrink: 0; }' +
            '.aprs-section-header { display: flex; align-items: center; gap: 8px; color: #0f0; font-weight: bold; letter-spacing: 1px; font-size: 10px; margin-bottom: 2px; }' +
            '.aprs-count { color: #8c8; font-weight: normal; letter-spacing: 0; font-size: 10px; }' +
            '.aprs-beacon-state { color: #8c8; font-weight: normal; letter-spacing: 0; font-size: 10px; }' +
            '.aprs-beacon-state.on { color: #0f0; }' +
            '.aprs-stations { background: #000; border: 1px solid #0a0; border-radius: 3px; flex: 1; min-height: 80px; overflow-y: auto; font-size: 11px; line-height: 1.5; }' +
            '.aprs-stations table { width: 100%; border-collapse: collapse; }' +
            '.aprs-stations th { position: sticky; top: 0; background: #001a00; color: #0f0; padding: 3px 6px; text-align: left; font-size: 10px; letter-spacing: 1px; border-bottom: 1px solid #0a0; cursor: pointer; user-select: none; }' +
            '.aprs-stations th:hover { color: #fff; background: #0a0; }' +
            '.aprs-stations th.sort-active::after { content: ""; display: inline-block; width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; margin-left: 4px; vertical-align: middle; }' +
            '.aprs-stations th.sort-asc::after  { border-bottom: 4px solid currentColor; }' +
            '.aprs-stations th.sort-desc::after { border-top: 4px solid currentColor; }' +
            '.aprs-stations td { padding: 2px 6px; border-bottom: 1px dotted #1a3a1a; white-space: nowrap; }' +
            '.aprs-stations tr.fresh { background: #001a00; }' +
            '.aprs-stations td.aprs-call { color: #ff0; font-weight: bold; cursor: pointer; }' +
            '.aprs-stations td.aprs-call:hover { color: #fff; background: #003300; }' +
            '.aprs-stations td.aprs-sym { color: #0ff; font-family: ui-monospace, monospace; text-align: center; width: 22px; }' +
            '.aprs-stations td.aprs-pos { color: #cfc; font-family: ui-monospace, monospace; }' +
            '.aprs-stations td.aprs-comment { color: #cfc; max-width: 280px; overflow: hidden; text-overflow: ellipsis; }' +
            '.aprs-stations td.aprs-age { color: #8c8; text-align: right; }' +
            '.aprs-stations td.aprs-count { color: #888; text-align: right; }' +
            '.aprs-empty { color: #666; padding: 12px; text-align: center; font-style: italic; }' +
            // Beacon compose grid
            '.aprs-beacon-grid { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; padding: 6px; background: #001a00; border: 1px solid #0a0; border-radius: 3px; font-size: 10px; color: #8c8; }' +
            '.aprs-beacon-grid label { display: inline-flex; align-items: center; gap: 3px; }' +
            '.aprs-comment-label { flex: 1 1 200px; }' +
            '.aprs-comment-label .packet-tx-info { flex: 1; min-width: 100px; }' +
            '.aprs-interval-label { font-size: 10px; }' +
            '.aprs-sym-select { background: #001a00; border: 1px solid #0a0; color: #cfc; font-family: monospace; font-size: 11px; padding: 2px; border-radius: 2px; }' +
            '.aprs-sym-select:focus { border-color: #0f0; outline: none; }' +
            '#aprsBeaconBtn.on { background: #0a0; color: #000; border-color: #0f0; box-shadow: 0 0 6px rgba(0,255,0,0.5); }' +
            // Terminal pane
            '.term-bar { display: flex; align-items: center; gap: 6px; margin-bottom: 6px; font-size: 10px; color: #8c8; flex-wrap: wrap; }' +
            '.term-bar label { display: flex; align-items: center; gap: 3px; }' +
            '.term-state-chip { padding: 3px 10px; border: 1px solid #555; border-radius: 3px; font-size: 11px; font-weight: bold; color: #888; background: #111; min-width: 96px; text-align: center; letter-spacing: 1px; transition: box-shadow 0.2s; }' +
            '.term-state-chip.connecting    { color: #ff3; border-color: #ee0; background: #221d00; animation: term-pulse 1.2s ease-in-out infinite; }' +
            '.term-state-chip.connected     { color: #0f0; border-color: #0f0; background: #002a00; box-shadow: 0 0 8px rgba(0,255,0,0.5), inset 0 0 6px rgba(0,255,0,0.15); }' +
            '.term-state-chip.disconnecting { color: #fb5; border-color: #f80; background: #2a1500; animation: term-pulse 1.2s ease-in-out infinite; }' +
            '.term-state-chip.disconnected  { color: #888; border-color: #555; background: #111; }' +
            '@keyframes term-pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.55; } }' +
            '.term-session-tabs { display: flex; flex-wrap: wrap; gap: 2px; margin-bottom: 4px; }' +
            '.term-session-tabs:empty { display: none; }' +
            '.term-tab { display: inline-flex; align-items: center; gap: 6px; background: #181818; border: 1px solid #555; border-bottom: none; color: #888; font-family: monospace; font-size: 11px; padding: 3px 4px 3px 8px; border-radius: 3px 3px 0 0; cursor: pointer; user-select: none; transition: box-shadow 0.2s; }' +
            '.term-tab:hover { filter: brightness(1.25); }' +
            // Connection-state colours (apply to all tabs, active or not).
            '.term-tab.s-connecting    { background: #221d00; border-color: #ee0; color: #ff3; animation: term-pulse 1.2s ease-in-out infinite; }' +
            '.term-tab.s-connected     { background: #002a00; border-color: #0f0; color: #0f0; }' +
            '.term-tab.s-disconnecting { background: #2a1500; border-color: #f80; color: #fb5; animation: term-pulse 1.2s ease-in-out infinite; }' +
            '.term-tab.s-disconnected  { background: #181818; border-color: #555; color: #888; }' +
            // Active tab gets a glow + bottom-edge highlight on top of state colour.
            '.term-tab.active            { box-shadow: 0 -2px 0 currentColor inset, 0 0 10px rgba(0,255,0,0.25); filter: brightness(1.15); }' +
            '.term-tab.active.s-connecting    { box-shadow: 0 -2px 0 #ee0 inset, 0 0 10px rgba(238,238,0,0.35); }' +
            '.term-tab.active.s-disconnecting { box-shadow: 0 -2px 0 #f80 inset, 0 0 10px rgba(255,136,0,0.35); }' +
            '.term-tab.active.s-disconnected  { box-shadow: 0 -2px 0 #888 inset; }' +
            '.term-tab .term-tab-state { font-size: 9px; opacity: 0.85; margin-left: 2px; text-transform: uppercase; letter-spacing: 0.5px; }' +
            '.term-tab.unread::before { content: "●"; color: #ff0; margin-right: 4px; font-size: 10px; }' +
            '.term-tab-close { background: transparent; border: none; color: inherit; font-family: monospace; font-size: 14px; line-height: 1; padding: 0 2px; cursor: pointer; opacity: 0.6; border-radius: 2px; }' +
            '.term-tab-close:hover { opacity: 1; background: #300; color: #f88; }' +
            '.term-scrollback { background: #000; border: 1px solid #0a0; border-radius: 3px; padding: 6px; flex: 1; min-height: 0; overflow-y: auto; font-family: ui-monospace, "SFMono-Regular", "Menlo", "Consolas", "DejaVu Sans Mono", monospace; font-size: 12px; line-height: 1.4; color: #cfc; white-space: pre-wrap; word-wrap: break-word; user-select: text; -webkit-user-select: text; cursor: text; }' +
            '.term-scrollback .rx { color: #cfc; }' +
            '.term-scrollback .tx { color: #ff0; }' +
            // Pending-ack TX: italic + dimmed.  Once peer responds with
            // any byte, termData rewrites .pending → plain .tx and the
            // scrollback re-renders, transitioning the line to normal.
            '.term-scrollback .tx.pending { font-style: italic; opacity: 0.55; transition: opacity 0.15s, font-style 0.15s; }' +
            '.term-scrollback .info { color: #888; font-style: italic; }' +
            '.term-input-row { display: flex; gap: 6px; margin-top: 6px; }' +
            '.term-input { flex: 1; background: #001a00; border: 1px solid #0a0; color: #cfc; font-family: monospace; font-size: 12px; padding: 4px 6px; border-radius: 2px; outline: none; }' +
            '.term-input:focus { border-color: #0f0; }' +
            '.term-input:disabled { opacity: 0.5; }' +
            '.term-force-btn { background: #2a0000 !important; border-color: #a40 !important; color: #f80 !important; }' +
            '.term-force-btn:hover:not(:disabled) { background: #4a0000 !important; color: #fb0 !important; }' +
            // Disconnect button when there is a LIVE connection — bright red
            // border + glow so the operator can see "click here to drop the
            // active link" at a glance.
            '.term-live-btn { background: #1a0000 !important; border: 1px solid #f33 !important; color: #f55 !important; font-weight: bold !important; box-shadow: 0 0 6px rgba(255,68,68,0.45) !important; }' +
            '.term-live-btn:hover:not(:disabled) { background: #330000 !important; color: #f88 !important; }' +
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
            '.term-file-prompt-btns { display: flex; gap: 8px; justify-content: flex-end; }' +
            // Gear button + settings dialog.
            '.packet-settings-btn { background: #111; border: 1px solid #555; color: #ccc; padding: 2px 6px; font-family: monospace; font-size: 13px; line-height: 1; cursor: pointer; border-radius: 3px; }' +
            '.packet-settings-btn:hover { background: #333; color: #fff; }' +
            '.packet-call-display { background: #001a00; border: 1px dashed #0a0; color: #0f0; font-family: monospace; font-weight: bold; font-size: 11px; padding: 2px 8px; border-radius: 2px; letter-spacing: 1px; }' +
            '.packet-call-display.unset { color: #f66; border-color: #a33; font-weight: normal; font-style: italic; }' +
            '.pkt-settings-modal { position: fixed; inset: 0; background: rgba(0,0,0,0.7); z-index: 2100; display: flex; align-items: center; justify-content: center; font-family: monospace; }' +
            '.pkt-settings-modal.hidden { display: none; }' +
            '.pkt-settings-box { background: #001a00; border: 2px solid #0a0; border-radius: 6px; padding: 16px 20px; min-width: 320px; max-width: 420px; box-shadow: 0 0 40px rgba(0,255,0,0.25); color: #cfc; }' +
            '.pkt-settings-title { color: #0f0; font-weight: bold; font-size: 13px; letter-spacing: 2px; margin: 0 0 12px 0; display: flex; align-items: center; justify-content: space-between; }' +
            '.pkt-settings-row { display: flex; align-items: center; gap: 8px; margin: 6px 0; font-size: 11px; }' +
            '.pkt-settings-row label { color: #8c8; min-width: 110px; }' +
            '.pkt-settings-row input { background: #000; border: 1px solid #0a0; color: #cfc; font-family: monospace; font-size: 12px; padding: 3px 6px; border-radius: 2px; outline: none; text-transform: uppercase; }' +
            '.pkt-settings-row input:focus { border-color: #0f0; }' +
            '.pkt-settings-hint { color: #668; font-size: 10px; margin: 2px 0 10px 118px; }' +
            '.pkt-settings-note { color: #888; font-size: 10px; margin-top: 10px; font-style: italic; }' +
            '.pkt-settings-btns { display: flex; gap: 8px; justify-content: flex-end; margin-top: 10px; }';
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

    // PKT transports a VFT/FSK signal through the mic path, so the radio
    // must be in a voice mode.  Digital modes (CW, RTTY, etc.) either
    // gate mic audio or impose a narrow filter that wipes out the tones.
    var PKT_VOICE_MODES = ['LSB', 'USB', 'AM', 'FM'];

    function setEnabled(on) {
        if (on) {
            // PKT and FreeDV are mutually exclusive — turning PKT on
            // must kill any active FreeDV/RADE session.  The server will
            // echo freedvStatus back, but we also fire the command here
            // so the two panels never appear active at the same time.
            if (window.freedvEnabled && window.send) {
                window.send({ cmd: 'setFreeDV', enabled: false });
            }
            // If the radio is on CW / RTTY / etc., switch to a sensible
            // voice mode for the current packet baud: FM for VHF
            // (1200 AFSK / 9600 G3RUH), USB for HF (300 AFSK).
            var cm = window.currentMode || '';
            if (PKT_VOICE_MODES.indexOf(cm) < 0 && window.send) {
                var target = (state.mode === 300) ? 'USB' : 'FM';
                window.send({ cmd: 'setMode', value: target });
            }
        }
        state.enabled = !!on;
        if (window.send) window.send({ cmd: 'packetEnable', value: state.enabled });
        // Do NOT push state.mode back to the server here.  state.mode is
        // the client-side default (300) until the first packetStatus from
        // the server fills it in; sending it on enable would clobber the
        // backend's persisted mode every time the panel reopens.
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
        if (msg.type === 'aprsSnapshot') {
            state.aprs.stations = {};
            if (Array.isArray(msg.stations)) {
                for (var ai = 0; ai < msg.stations.length; ai++) {
                    var st = msg.stations[ai];
                    if (st && st.src) state.aprs.stations[st.src] = st;
                }
            }
            renderAprsStations();
            return;
        } else if (msg.type === 'aprsStation') {
            applyAprsStation(msg);
            return;
        }
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
            maybeAutoActivateSole();
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
            var entry = { ts: msg.ts, dir: msg.dir, data: msg.data };
            // Mark TX entries as pending-ack until an AX.25 N(R) advance
            // (delivered via termAck below) flips them to delivered.
            if (msg.dir === 'tx') entry.pending = true;
            s.scrollback.push(entry);
            if (state.terminal.activeSid === msg.sid) {
                renderTermScrollback();
            } else {
                // Background tab got traffic — surface it via the unread dot.
                s.hasUnread = true;
                renderTermTabs();
            }
        } else if (msg.type === 'termAck') {
            // Real AX.25 ack: peer's N(R) advanced by msg.count, so the
            // oldest msg.count pending TX entries are now delivered.
            // Counts include text-bearing I-frames AND any internal
            // YAPP framing we sent — but YAPP frames don't appear in
            // scrollback, so we just clear up to count *visible* pending
            // TX lines and let the rest be no-op.
            var sa = state.terminal.sessions[msg.sid];
            if (sa && Array.isArray(sa.scrollback)) {
                var remaining = msg.count || 0;
                var changed = false;
                for (var ai = 0; ai < sa.scrollback.length && remaining > 0; ai++) {
                    var ea = sa.scrollback[ai];
                    if (ea.dir === 'tx' && ea.pending) {
                        ea.pending = false;
                        remaining--;
                        changed = true;
                    }
                }
                if (changed && state.terminal.activeSid === msg.sid) {
                    renderTermScrollback();
                }
            }
        } else if (msg.type === 'termList') {
            // Replace local view with server snapshot.  Keep activeSid if it
            // still exists; never auto-pick a new one — the operator selects.
            state.terminal.sessions = {};
            if (Array.isArray(msg.sessions)) {
                for (var i = 0; i < msg.sessions.length; i++) {
                    var ss = msg.sessions[i];
                    state.terminal.sessions[ss.sid] = ss;
                    state.terminal.sessions[ss.sid].scrollback = [];
                }
            }
            if (state.terminal.activeSid && !state.terminal.sessions[state.terminal.activeSid]) {
                state.terminal.activeSid = null;
            }
            maybeAutoActivateSole();
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
            // A transfer starting in a background tab is unread-worthy.
            if (state.terminal.activeSid !== msg.sid) sx.hasUnread = true;
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
        var prevState = existing ? existing.state : null;
        var s = existing || { sid: msg.sid, scrollback: [] };
        s.sid      = msg.sid;
        s.chan     = msg.chan;
        s.ownCall  = msg.ownCall;
        s.peerCall = msg.peerCall;
        s.digis    = msg.digis || [];
        s.state    = msg.state;
        s.incoming = msg.incoming;
        if (!existing) state.terminal.sessions[msg.sid] = s;

        // Activate only when this is the session the user just asked to
        // connect to (pendingOutbound).  Inbound or concurrent sessions
        // never steal focus on creation — they light up an unread marker
        // instead.  (The "became connected" and "sole session" rules
        // below may still promote them.)
        if (!existing) {
            var po = state.terminal.pendingOutbound;
            var matchesPending = po
                && po.ownCall  === msg.ownCall
                && po.peerCall === msg.peerCall
                && (typeof po.chan !== 'number' || po.chan === msg.chan);
            if (matchesPending) {
                state.terminal.pendingOutbound = null;
                termSetActive(msg.sid);
            } else if (state.terminal.activeSid !== msg.sid) {
                s.hasUnread = true;
            }
        }

        // Promote on transition to 'connected' (covers inbound SABM/UA
        // races where the first message we see is already 'connected',
        // and outbound 'connecting' → 'connected').
        if (msg.state === 'connected' && prevState !== 'connected') {
            termSetActive(msg.sid);
        }

        // If this is now the only session, focus it.
        maybeAutoActivateSole();

        renderTerm();
    }

    // Whenever the session set transitions to exactly one entry, that
    // entry should be the active selection — there's no other choice
    // for the operator to make.  Called from handleTermSession (add),
    // termClose (remove), and termList (server snapshot replace).
    function maybeAutoActivateSole() {
        var sids = Object.keys(state.terminal.sessions);
        if (sids.length !== 1) return;
        if (state.terminal.activeSid === sids[0]) return;
        termSetActive(sids[0]);
    }

    function setTxStatus(text, isError) {
        var el = document.getElementById('packetTxStatus');
        if (!el) return;
        el.textContent = text;
        el.classList.toggle('err', !!isError);
    }

    function setTxButtonEnabled(enabled) {
        var btn = document.getElementById('aprsTxNowBtn');
        if (btn) btn.disabled = !enabled;
    }

    // ---------------------------------------------------------------------
    // APRS pane
    // ---------------------------------------------------------------------

    function loadAprsFromStorage() {
        try {
            var raw = localStorage.getItem('wfweb.aprs.beacon');
            if (!raw) return;
            var obj = JSON.parse(raw);
            if (obj && typeof obj === 'object') {
                var b = state.aprs.beacon;
                // b.src is derived from app callsign + settings.aprsSsid;
                // legacy persisted values are intentionally ignored.
                if (typeof obj.lat     === 'number') b.lat      = obj.lat;
                if (typeof obj.lon     === 'number') b.lon      = obj.lon;
                if (typeof obj.symKey  === 'string') b.symKey   = obj.symKey;
                if (typeof obj.comment === 'string') b.comment  = obj.comment;
                if (typeof obj.path    === 'string') b.path     = obj.path;
                if (typeof obj.intervalMin === 'number') b.intervalMin = obj.intervalMin;
                // beacon.enabled is intentionally NOT restored — server owns
                // the periodic-beacon state, and we never want a page reload
                // to silently start TXing.
            }
        } catch (e) { /* ignore */ }
    }

    function saveAprsToStorage() {
        try {
            var b = state.aprs.beacon;
            var obj = { lat: b.lat, lon: b.lon, symKey: b.symKey,
                        comment: b.comment, path: b.path,
                        intervalMin: b.intervalMin };
            localStorage.setItem('wfweb.aprs.beacon', JSON.stringify(obj));
        } catch (e) { /* quota — ignore */ }
    }

    function populateSymbolSelect() {
        var sel = document.getElementById('aprsSym');
        if (!sel) return;
        var html = '';
        for (var i = 0; i < APRS_SYMBOLS.length; i++) {
            var s = APRS_SYMBOLS[i];
            html += '<option value="' + s.key + '">'
                  + escapeHtml(s.code + '  ' + s.label) + '</option>';
        }
        sel.innerHTML = html;
        sel.value = state.aprs.beacon.symKey;
    }

    function bindAprsControls() {
        var b = state.aprs.beacon;
        var symEl     = document.getElementById('aprsSym');
        var latEl     = document.getElementById('aprsLat');
        var lonEl     = document.getElementById('aprsLon');
        var commentEl = document.getElementById('aprsComment');
        var pathEl    = document.getElementById('aprsPath');
        var intervalEl= document.getElementById('aprsInterval');

        if (symEl)     symEl.value     = b.symKey;
        if (latEl)     latEl.value     = b.lat ? b.lat.toFixed(5) : '';
        if (lonEl)     lonEl.value     = b.lon ? b.lon.toFixed(5) : '';
        if (commentEl) commentEl.value = b.comment;
        if (pathEl)    pathEl.value    = b.path;
        if (intervalEl)intervalEl.value= b.intervalMin;

        document.getElementById('aprsTxNowBtn').onclick  = aprsTxNow;
        document.getElementById('aprsBeaconBtn').onclick = aprsToggleBeacon;
        document.getElementById('aprsGeoBtn').onclick    = aprsUseMyLocation;
        document.getElementById('aprsClearBtn').onclick  = aprsClearStations;

        // Persist on blur so partial typing doesn't churn localStorage.
        var commit = function() { aprsCommitForm(); };
        [latEl, lonEl, commentEl, pathEl, intervalEl, symEl].forEach(function(el) {
            if (!el) return;
            el.addEventListener('change', commit);
            el.addEventListener('blur',   commit);
        });

        // Sortable column headers — delegated on the stations container.
        // Clicking a callsign cell sets it as the terminal peer so the
        // operator can quickly open an AX.25 link to a heard station.
        var st = document.getElementById('aprsStations');
        if (st) {
            st.addEventListener('click', function(e) {
                var th = e.target.closest && e.target.closest('th');
                if (th && th.dataset.col) { aprsSetSort(th.dataset.col); return; }
                var call = e.target.closest && e.target.closest('td.aprs-call');
                if (call && call.dataset.call) setPeerFromMonitor(call.dataset.call);
            });
        }
    }

    function aprsCommitForm() {
        var b = state.aprs.beacon;
        var symEl     = document.getElementById('aprsSym');
        var latEl     = document.getElementById('aprsLat');
        var lonEl     = document.getElementById('aprsLon');
        var commentEl = document.getElementById('aprsComment');
        var pathEl    = document.getElementById('aprsPath');
        var intervalEl= document.getElementById('aprsInterval');

        if (symEl)     b.symKey      = symEl.value;
        if (latEl)     b.lat         = parseFloat(latEl.value) || 0;
        if (lonEl)     b.lon         = parseFloat(lonEl.value) || 0;
        if (commentEl) b.comment     = commentEl.value || '';
        if (pathEl)    b.path        = (pathEl.value || '').trim().toUpperCase();
        if (intervalEl)b.intervalMin = Math.max(1, parseInt(intervalEl.value, 10) || 10);
        saveAprsToStorage();

        // If the periodic beacon is currently on, push the new config so the
        // next firing uses the latest fields.
        if (b.enabled) aprsSendBeaconConfig(true);
    }

    function aprsValidateBeacon() {
        var b = state.aprs.beacon;
        if (!b.src || b.src === 'N0CALL') return 'set your callsign';
        if (!isFinite(b.lat) || !isFinite(b.lon)) return 'lat/lon required';
        if (b.lat === 0 && b.lon === 0)           return 'lat/lon required';
        if (b.lat < -90  || b.lat > 90)           return 'lat out of range';
        if (b.lon < -180 || b.lon > 180)          return 'lon out of range';
        return '';
    }

    function aprsBeaconPayload() {
        var b = state.aprs.beacon;
        var sym = symbolFor(b.symKey);
        var pathList = [];
        if (b.path) {
            var parts = b.path.split(/[,\s]+/);
            for (var i = 0; i < parts.length; i++) if (parts[i]) pathList.push(parts[i]);
        }
        return {
            src: b.src, lat: b.lat, lon: b.lon,
            symTable: sym.table, symCode: sym.code,
            comment: b.comment, path: pathList
        };
    }

    function aprsTxNow() {
        aprsCommitForm();
        if (state.txBusy) return;
        if (!state.enabled) { setEnabled(true); }
        var err = aprsValidateBeacon();
        if (err) { setTxStatus(err, true); return; }
        setTxStatus('beacon queued…', false);
        setTxButtonEnabled(false);
        if (window.send) {
            var p = aprsBeaconPayload();
            p.cmd = 'aprsTxBeacon';
            window.send(p);
        }
    }

    function aprsToggleBeacon() {
        aprsCommitForm();
        var b = state.aprs.beacon;
        if (!b.enabled) {
            var err = aprsValidateBeacon();
            if (err) { setTxStatus(err, true); return; }
            if (!state.enabled) setEnabled(true);
            b.enabled = true;
            aprsSendBeaconConfig(true);
        } else {
            b.enabled = false;
            aprsSendBeaconConfig(false);
        }
        renderAprsBeaconButton();
    }

    function aprsSendBeaconConfig(enabled) {
        if (!window.send) return;
        var p = aprsBeaconPayload();
        p.cmd = 'aprsBeaconConfig';
        p.enabled = !!enabled;
        p.intervalSec = state.aprs.beacon.intervalMin * 60;
        window.send(p);
    }

    function aprsUseMyLocation() {
        if (!navigator.geolocation) {
            setTxStatus('geolocation not available', true);
            return;
        }
        setTxStatus('locating…', false);
        navigator.geolocation.getCurrentPosition(function(pos) {
            var latEl = document.getElementById('aprsLat');
            var lonEl = document.getElementById('aprsLon');
            if (latEl) latEl.value = pos.coords.latitude.toFixed(5);
            if (lonEl) lonEl.value = pos.coords.longitude.toFixed(5);
            aprsCommitForm();
            setTxStatus('location set', false);
        }, function(err) {
            setTxStatus('location: ' + (err && err.message ? err.message : 'denied'), true);
        }, { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 });
    }

    function aprsClearStations() {
        var existing = document.getElementById('aprsClearConfirm');
        if (existing) existing.remove();

        var modal = document.createElement('div');
        modal.id = 'aprsClearConfirm';
        modal.className = 'term-file-prompt';
        modal.innerHTML =
            '<div class="term-file-prompt-box">' +
                '<div class="term-file-prompt-title">Forget heard stations?</div>' +
                '<div class="term-file-prompt-body">' +
                    'This will clear the APRS heard-stations list.' +
                '</div>' +
                '<div class="term-file-prompt-note">Stations will reappear as they are heard again.</div>' +
                '<div class="term-file-prompt-btns">' +
                    '<button id="aprsClearCancel" class="packet-action-btn">Cancel</button>' +
                    '<button id="aprsClearOk" class="packet-action-btn term-xfer-abort">Forget all</button>' +
                '</div>' +
            '</div>';
        document.body.appendChild(modal);
        document.getElementById('aprsClearCancel').onclick = function() { modal.remove(); };
        document.getElementById('aprsClearOk').onclick = function() {
            if (window.send) window.send({ cmd: 'aprsClearStations' });
            modal.remove();
        };
        modal.addEventListener('click', function(e) {
            if (e.target === modal) modal.remove();
        });
    }

    function renderAprsBeaconButton() {
        var btn = document.getElementById('aprsBeaconBtn');
        var st  = document.getElementById('aprsBeaconState');
        if (!btn) return;
        var on = !!state.aprs.beacon.enabled;
        btn.classList.toggle('on', on);
        btn.textContent = on ? ('Beacon: ON (' + state.aprs.beacon.intervalMin + ' min)')
                             : 'Beacon: OFF';
        if (st) {
            st.classList.toggle('on', on);
            st.textContent = on
                ? ('every ' + state.aprs.beacon.intervalMin + ' min')
                : 'idle';
        }
    }

    function aprsSetSort(col) {
        if (state.aprs.sortBy === col) {
            state.aprs.sortDir = state.aprs.sortDir === 'asc' ? 'desc' : 'asc';
        } else {
            state.aprs.sortBy = col;
            // Default direction per column: alphabetic ASC; numerics DESC
            // (largest/most-recent first).
            state.aprs.sortDir = (col === 'src') ? 'asc' : 'desc';
        }
        renderAprsStations();
    }

    function aprsAge(ms) {
        if (!ms) return '';
        var dt = (Date.now() - ms) / 1000;
        if (dt < 60)        return Math.floor(dt) + 's';
        if (dt < 3600)      return Math.floor(dt / 60) + 'm';
        if (dt < 86400)     return Math.floor(dt / 3600) + 'h';
        return Math.floor(dt / 86400) + 'd';
    }

    function fmtCoord(v, isLat) {
        if (!isFinite(v)) return '';
        var hem = isLat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
        return Math.abs(v).toFixed(4) + hem;
    }

    function renderAprsStations() {
        var host = document.getElementById('aprsStations');
        var countEl = document.getElementById('aprsStationCount');
        if (!host) return;

        var stations = Object.keys(state.aprs.stations).map(function(k) {
            return state.aprs.stations[k];
        });
        if (countEl) countEl.textContent = stations.length;

        if (stations.length === 0) {
            host.innerHTML = '<div class="aprs-empty">No APRS stations heard yet. '
                           + 'Tune to 144.390 (or your local APRS frequency), enable the modem, and wait for a beacon.</div>';
            return;
        }

        var sortBy  = state.aprs.sortBy;
        var sortDir = state.aprs.sortDir;
        var sign = sortDir === 'asc' ? 1 : -1;
        stations.sort(function(a, b) {
            var av = a[sortBy], bv = b[sortBy];
            if (av == null && bv == null) return 0;
            if (av == null) return 1;
            if (bv == null) return -1;
            if (typeof av === 'string') return sign * av.localeCompare(bv);
            return sign * (av - bv);
        });

        var cols = [
            { key: 'src',       label: 'Callsign' },
            { key: '_sym',      label: 'Sym',      sortable: false },
            { key: 'lat',       label: 'Lat'      },
            { key: 'lon',       label: 'Lon'      },
            { key: 'comment',   label: 'Comment'  },
            { key: 'lastHeard', label: 'Heard'    },
            { key: 'count',     label: '#'        }
        ];

        var html = '<table><thead><tr>';
        for (var c = 0; c < cols.length; c++) {
            var col = cols[c];
            var sortable = col.sortable !== false;
            var classes = '';
            if (sortable && col.key === sortBy) {
                classes = 'sort-active sort-' + sortDir;
            }
            html += '<th' + (sortable ? ' data-col="' + col.key + '"' : '')
                  + (classes ? ' class="' + classes + '"' : '') + '>'
                  + escapeHtml(col.label) + '</th>';
        }
        html += '</tr></thead><tbody>';

        for (var i = 0; i < stations.length; i++) {
            var s = stations[i];
            var freshCls = s._fresh ? ' class="fresh"' : '';
            html += '<tr' + freshCls + '>'
                  + '<td class="aprs-call" data-call="' + escapeHtml(s.src) + '" title="Click to use as beacon source">'
                  +   escapeHtml(s.src) + '</td>'
                  + '<td class="aprs-sym" title="' + escapeHtml(s.symTable + s.symCode) + '">'
                  +   escapeHtml(s.symCode || '?') + '</td>'
                  + '<td class="aprs-pos">' + fmtCoord(s.lat, true)  + '</td>'
                  + '<td class="aprs-pos">' + fmtCoord(s.lon, false) + '</td>'
                  + '<td class="aprs-comment" title="' + escapeHtml(s.comment || '') + '">'
                  +   escapeHtml(s.comment || '') + '</td>'
                  + '<td class="aprs-age">'  + escapeHtml(aprsAge(s.lastHeard)) + '</td>'
                  + '<td class="aprs-count">' + (s.count || 0) + '</td>'
                  + '</tr>';
        }
        html += '</tbody></table>';
        host.innerHTML = html;
    }

    function applyAprsStation(st) {
        var src = st.src;
        if (!src) return;
        var existing = state.aprs.stations[src];
        st._fresh = true;
        state.aprs.stations[src] = st;
        // Drop the freshness highlight a couple seconds later.
        setTimeout(function() {
            var s = state.aprs.stations[src];
            if (s) s._fresh = false;
            if (state.visible) renderAprsStations();
        }, 2500);
        if (state.visible) renderAprsStations();
        if (!existing && state.activeTab === 'aprs') {
            // Subtle visual cue handled by `_fresh`.
        }
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

    // Real amateur callsigns always contain at least one digit (ITU rule).
    // Service identifiers in the AX.25 destination field — BEACON, MAIL,
    // APRS, ID, FBB, MBX — are letters only and shouldn't be offered as
    // connect targets, so we skip the click affordance for those.
    function looksLikeCallsign(s) {
        if (!s) return false;
        if (!/^[A-Z0-9]{1,6}(-[0-9]{1,2})?$/i.test(s)) return false;
        return /[0-9]/.test(s.split('-')[0]);
    }

    // Treat "K1FM" and "K1FM-0" as the same station (AX.25 SSID 0 == bare).
    function normalizeCall(s) {
        var u = String(s || '').trim().toUpperCase();
        return u.replace(/-0$/, '');
    }

    function isOwnCallsign(s) {
        var own = normalizeCall(state.terminal && state.terminal.compose && state.terminal.compose.ownCall);
        if (!own) return false;
        return normalizeCall(s) === own;
    }

    function callsignSpan(s, cssClass) {
        var safe = escapeHtml(s);
        if (looksLikeCallsign(s) && !isOwnCallsign(s)) {
            return '<span class="' + cssClass + ' callsign-link"'
                 + ' data-call="' + safe + '"'
                 + ' title="Click to set as terminal peer">' + safe + '</span>';
        }
        return '<span class="' + cssClass + '">' + safe + '</span>';
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
            var pathStr = '';
            if (Array.isArray(f.path) && f.path.length > 0) {
                pathStr = ' via ';
                for (var p = 0; p < f.path.length; p++) {
                    if (p > 0) pathStr += ',';
                    // Strip any "*" used-digi marker before the callsign test.
                    var digi = String(f.path[p] || '');
                    var bare = digi.replace(/\*$/, '');
                    pathStr += callsignSpan(bare, 'digi') + (digi.endsWith('*') ? '*' : '');
                }
            }
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
                    callsignSpan(f.src, 'src') +
                    ' &gt; ' +
                    callsignSpan(f.dst, 'dst') +
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
        // Mirrors initWfColorTable() in index.html (FT8/FT4 palette) so the
        // packet WF reads the same as DIGI: black → dark blue → bright blue →
        // cyan → yellow → red.  Combined with FLOOR in paintScope(), idle
        // RX (S0) renders as a clear dark blue, not near-black.
        for (var i = 0; i < 256; i++) {
            var r, g, b;
            if (i < 85) {
                r = 0; g = 0; b = Math.round(i * 3);
            } else if (i < 150) {
                var t1 = (i - 85) / 64;
                r = 0; g = Math.round(t1 * 60); b = 255;
            } else if (i < 190) {
                var t2 = (i - 150) / 39;
                r = 0; g = 60 + Math.round(t2 * 195); b = 255;
            } else if (i < 225) {
                var t3 = (i - 190) / 34;
                r = Math.round(t3 * 255); g = 255; b = Math.round(255 * (1 - t3));
            } else {
                var t4 = (i - 225) / 30;
                r = 255; g = Math.round(255 * (1 - t4)); b = 0;
            }
            scopeColorLUT[i] = [r, g, b];
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

            // Local TX monitor — operator hears their own outgoing audio at
            // a low fixed level (matches FT8/FT4 monitor in index.html).
            txMonitorGain = audioContext.createGain();
            txMonitorGain.gain.value = 0.025;
            txMonitorGain.connect(audioContext.destination);

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
        if (analyserNode || boostGain || txFixedGain || txMonitorGain) {
            try {
                if (analyserNode)   analyserNode.disconnect();
                if (boostGain)      boostGain.disconnect();
                if (txFixedGain)    txFixedGain.disconnect();
                if (txMonitorGain)  txMonitorGain.disconnect();
                if (window.audioWorkletNode && window.audioGainNode) {
                    window.audioWorkletNode.disconnect();
                    window.audioWorkletNode.connect(window.audioGainNode);
                }
            } catch (e) { /* graph already torn down */ }
            analyserNode = null;
            boostGain = null;
            txFixedGain = null;
            txMonitorGain = null;
        }
        audioContext = null;
        txNextStart = 0;
    }

    // Server teed TX audio via binary msgType 0x04.  Push it through a
    // BufferSource that feeds boostGain/analyser for the waterfall and a
    // separate low-gain tap to the speakers so the operator hears their own
    // outgoing audio (same approach as the FT8/FT4 TX monitor).
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
        src.connect(txFixedGain);                        // → analyser (waterfall)
        if (txMonitorGain) src.connect(txMonitorGain);   // → speakers (operator monitor)
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

        // 1 px every ~16 ms (paint roughly every RAF tick at 60 Hz) — fast
        // enough that a typical AX.25 burst lights up tens of pixels of
        // streak.  The 43 ms FFT window means adjacent columns overlap, but
        // that just means features look like wider streaks, not smeared.
        var now = performance.now();
        if (now - lastPaintMs < 16) {
            rafId = requestAnimationFrame(paintScope);
            return;
        }
        lastPaintMs = now;
        var step = 1;

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
        // everything to LUT step ~40 paints S0 as a clear dark blue, matching
        // the FT8 waterfall's idle background.
        var FLOOR = 40;
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

    // Click target for callsigns rendered in the monitor pane.  Sets the
    // terminal peer field, persists the compose state, and switches to the
    // terminal tab so the operator can review channel/digis and hit Connect.
    // Deliberately does NOT auto-connect — clicking should never key the
    // radio without an explicit second action.
    function setPeerFromMonitor(call) {
        var peer = String(call || '').trim().toUpperCase();
        if (!peer) return;
        state.terminal.compose.peerCall = peer;
        var peerEl = document.getElementById('termPeerCall');
        if (peerEl) peerEl.value = peer;
        saveTermComposeToStorage();
        setActiveTab('term');
    }

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
        var own = state.terminal.compose.ownCall;
        if (!own || own === 'N0CALL') return;
        if (window.send) window.send({ cmd: 'termRegister', ownCall: own, chan: 0 });
    }

    function termConnect() {
        if (!state.enabled) {
            // Auto-enable so the operator doesn't have to flip two switches.
            setEnabled(true);
        }
        var own  = state.terminal.compose.ownCall;
        var peer = (document.getElementById('termPeerCall').value || '').trim().toUpperCase();
        var digisRaw = (document.getElementById('termDigis').value || '').trim().toUpperCase();
        if (!own) {
            setTxStatus('set your callsign (gear icon)', true);
            return;
        }
        if (!peer) return;

        var digis = [];
        if (digisRaw.length > 0) {
            var parts = digisRaw.split(/[,\s]+/);
            for (var i = 0; i < parts.length; i++) {
                if (parts[i]) digis.push(parts[i]);
            }
        }

        state.terminal.compose.peerCall = peer;
        state.terminal.compose.digis    = digisRaw;
        saveTermComposeToStorage();

        // Record the user's intent so handleTermSession activates the
        // matching session when it arrives from the server, rather than
        // whichever session happened to be first.
        state.terminal.pendingOutbound = { ownCall: own, peerCall: peer, chan: 0 };
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
                // ownCall is derived from app callsign + settings.termSsid;
                // legacy persisted values are intentionally ignored.
                if (typeof obj.peerCall === 'string') state.terminal.compose.peerCall = obj.peerCall;
                if (typeof obj.digis    === 'string') state.terminal.compose.digis    = obj.digis;
            }
        } catch (e) { /* ignore */ }
    }

    function saveTermComposeToStorage() {
        try {
            // ownCall is persisted too so legacy readers see it, but the
            // authoritative source on load is the shared app callsign.
            var c = state.terminal.compose;
            localStorage.setItem('wfweb.term.compose', JSON.stringify({
                peerCall: c.peerCall,
                digis: c.digis
            }));
        } catch (e) { /* quota — ignore */ }
    }

    function renderTerm() {
        renderTermTabs();
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
            // When the link is live (connected), highlight the Disconnect
            // button so it's obvious this is the ONE thing to press to drop
            // the link.  Hidden under term-force-btn during the Force state.
            dcBtn.classList.toggle('term-live-btn',
                                   connected && !isDisconnecting);
            dcBtn.title = isDisconnecting
                ? 'Send DM and tear down the link immediately'
                : 'Disconnect this session';
        }
        var conBtn = document.getElementById('termConnectBtn');
        if (conBtn) {
            // Dim Connect while a session is alive — the operator should
            // use Disconnect first.  Still enabled so a second connect
            // attempt to a different peer works after closing the tab.
            conBtn.style.opacity = (connected || s && s.state === 'connecting')
                                   ? '0.5' : '';
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

    function renderTermTabs() {
        var host = document.getElementById('termSessionTabs');
        if (!host) return;
        var sids = Object.keys(state.terminal.sessions);
        var html = '';
        for (var i = 0; i < sids.length; i++) {
            var sid = sids[i];
            var s = state.terminal.sessions[sid];
            var isActive = sid === state.terminal.activeSid;
            var stateClass = 's-' + (s.state || 'disconnected');
            var cls = 'term-tab'
                    + ' ' + stateClass
                    + (isActive ? ' active' : '')
                    + (!isActive && s.hasUnread ? ' unread' : '');
            html += '<div class="' + cls + '" data-sid="' + escapeHtml(sid) + '"'
                  + ' title="' + escapeHtml(s.ownCall + ' ↔ ' + s.peerCall
                                            + (s.digis && s.digis.length ? ' via ' + s.digis.join(',') : '')) + '">'
                  +   '<span>' + escapeHtml(s.peerCall || '(unknown)') + '</span>'
                  +   '<span class="term-tab-state">' + escapeHtml(s.state || 'disconnected') + '</span>'
                  +   '<button type="button" class="term-tab-close" data-sid="' + escapeHtml(sid) + '" title="Close session">×</button>'
                  + '</div>';
        }
        host.innerHTML = html;
    }

    // Explicit activation — the only place activeSid changes, aside from
    // close-and-null-out.  Prefetch history and clear the unread marker.
    function termSetActive(sid) {
        if (!sid || !state.terminal.sessions[sid]) return;
        if (state.terminal.activeSid === sid) return;
        state.terminal.activeSid = sid;
        var s = state.terminal.sessions[sid];
        if (s) s.hasUnread = false;
        if (window.send) window.send({ cmd: 'termHistory', sid: sid });
        renderTerm();
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
            var cls = e.dir || 'info';
            if (e.dir === 'tx' && e.pending) cls += ' pending';
            html += '<span class="' + cls + '">' + escapeHtml(e.data || '') + '</span>';
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

    // -----------------------------------------------------------------
    // PKT settings (gear dialog)
    // -----------------------------------------------------------------

    function loadSettingsFromStorage() {
        try {
            var raw = localStorage.getItem('wfweb.pkt.settings');
            if (!raw) return;
            var obj = JSON.parse(raw);
            if (obj && typeof obj === 'object') {
                if (typeof obj.aprsSsid === 'string') state.settings.aprsSsid = obj.aprsSsid;
                if (typeof obj.termSsid === 'string') state.settings.termSsid = obj.termSsid;
            }
        } catch (e) { /* ignore */ }
    }

    function saveSettingsToStorage() {
        try {
            localStorage.setItem('wfweb.pkt.settings', JSON.stringify(state.settings));
        } catch (e) { /* quota — ignore */ }
    }

    // Refresh the small non-editable callsign chips on the APRS and Term panes.
    function updateCallDisplays() {
        var aprsEl = document.getElementById('aprsSrcDisplay');
        if (aprsEl) {
            var a = state.aprs.beacon.src;
            aprsEl.textContent = a || '(set callsign)';
            aprsEl.classList.toggle('unset', !a);
        }
        var termEl = document.getElementById('termOwnCallDisplay');
        if (termEl) {
            var t = state.terminal.compose.ownCall;
            termEl.textContent = t || '(set callsign)';
            termEl.classList.toggle('unset', !t);
        }
    }

    function openSettingsDialog() {
        var modal = document.getElementById('pktSettingsModal');
        if (!modal) modal = buildSettingsDialog();
        var callEl = document.getElementById('pktSettingsCall');
        var aprsEl = document.getElementById('pktSettingsAprsSsid');
        var termEl = document.getElementById('pktSettingsTermSsid');
        if (callEl) callEl.value = (window.App && window.App.callsign) ? window.App.callsign.get() : '';
        if (aprsEl) aprsEl.value = state.settings.aprsSsid;
        if (termEl) termEl.value = state.settings.termSsid;
        modal.classList.remove('hidden');
        if (callEl) callEl.focus();
    }

    function closeSettingsDialog() {
        var modal = document.getElementById('pktSettingsModal');
        if (modal) modal.classList.add('hidden');
    }

    // Normalise an SSID string: blank stays blank, else a number in [0,15] or blank.
    function normalizeSsid(s) {
        var t = String(s || '').trim();
        if (t === '') return '';
        var n = parseInt(t, 10);
        if (!isFinite(n) || n < 0 || n > 15) return '';
        return String(n);
    }

    function saveSettingsFromDialog() {
        var callEl = document.getElementById('pktSettingsCall');
        var aprsEl = document.getElementById('pktSettingsAprsSsid');
        var termEl = document.getElementById('pktSettingsTermSsid');
        if (callEl && window.App && window.App.callsign) {
            window.App.callsign.set(callEl.value);
        }
        state.settings.aprsSsid = aprsEl ? normalizeSsid(aprsEl.value) : '';
        state.settings.termSsid = termEl ? normalizeSsid(termEl.value) : '';
        saveSettingsToStorage();
        // appCallsignChanged already fires from callsign.set() when the call
        // changed, triggering the shared listener that recomputes derived
        // calls and updates displays.  Call the same logic explicitly here
        // to cover the case where only an SSID was edited.
        recomputeDerivedCalls();
        updateCallDisplays();
        saveTermComposeToStorage();
        saveAprsToStorage();
        if (state.activeTab === 'term') termRegisterOwn();
        if (state.aprs.beacon.enabled) aprsSendBeaconConfig(true);
        closeSettingsDialog();
    }

    function buildSettingsDialog() {
        var modal = document.createElement('div');
        modal.id = 'pktSettingsModal';
        modal.className = 'pkt-settings-modal hidden';
        modal.innerHTML =
            '<div class="pkt-settings-box">' +
                '<div class="pkt-settings-title">' +
                    '<span>PKT SETTINGS</span>' +
                    '<button id="pktSettingsClose" class="packet-close-btn" aria-label="Close">&#x2715;</button>' +
                '</div>' +
                '<div class="pkt-settings-row">' +
                    '<label for="pktSettingsCall">Callsign</label>' +
                    '<input id="pktSettingsCall" maxlength="10" size="10" spellcheck="false" autocomplete="off">' +
                '</div>' +
                '<div class="pkt-settings-hint">Shared with CW, FT8/FT4, FreeDV reporter, and logbook.</div>' +
                '<div class="pkt-settings-row">' +
                    '<label for="pktSettingsAprsSsid">APRS SSID</label>' +
                    '<input id="pktSettingsAprsSsid" maxlength="2" size="3" spellcheck="false" placeholder="none">' +
                '</div>' +
                '<div class="pkt-settings-hint">0–15, or blank for no SSID (e.g. 9 for mobile).</div>' +
                '<div class="pkt-settings-row">' +
                    '<label for="pktSettingsTermSsid">Terminal SSID</label>' +
                    '<input id="pktSettingsTermSsid" maxlength="2" size="3" spellcheck="false" placeholder="none">' +
                '</div>' +
                '<div class="pkt-settings-hint">Appended to your callsign for AX.25 connected-mode links.</div>' +
                '<div class="pkt-settings-btns">' +
                    '<button id="pktSettingsCancel" class="packet-action-btn">Cancel</button>' +
                    '<button id="pktSettingsSave" class="packet-send-btn">Save</button>' +
                '</div>' +
            '</div>';
        document.body.appendChild(modal);

        document.getElementById('pktSettingsClose').onclick  = closeSettingsDialog;
        document.getElementById('pktSettingsCancel').onclick = closeSettingsDialog;
        document.getElementById('pktSettingsSave').onclick   = saveSettingsFromDialog;
        modal.addEventListener('click', function(e) {
            if (e.target === modal) closeSettingsDialog();
        });
        modal.addEventListener('keydown', function(e) {
            if (e.key === 'Escape') closeSettingsDialog();
            else if (e.key === 'Enter') saveSettingsFromDialog();
        });
        return modal;
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
