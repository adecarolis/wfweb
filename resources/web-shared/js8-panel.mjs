// JS8 messenger panel — shared between the server build
// (resources/web/index.html via web.qrc) and the standalone build
// (resources/web-standalone/index.html via tools/build-static.sh).
//
// What lives here:
//   • the WASM-backed codec import (js8.mjs)
//   • the full panel markup (template literal injected on init)
//   • the panel's state machine, RX decoder loop, TX queue, CMD palette,
//     QSO tab manager, settings/relay modals, auto-replies, HB scheduler
//
// What the panel reads off the host:
//   send(cmd)              — generic command channel
//   streamDigiAudio(...)   — TX audio sink (server: WebSocket; standalone: Web Serial)
//   haltDigiTx()           — abort an in-flight TX
//   initDigiTxCtx()        — ensure an AudioContext is alive for TX
//   startDigiTune(freq)    — tone generator for TUNE (used by ATU)
//   stopDigiTune()         — opposite of above
//   digiTuneActive         — boolean reflecting tune state
//   wfBuf / wfBufPos / WF_FFT_SIZE — circular sample buffer for waterfall
//   drawWfCanvas()         — shared waterfall renderer (web-shared/wf-canvas.js)
//   closeOtherModes('js8') — overlay mode arbiter
//   window._js8ProcessAudioChunk — RX tap installed by the panel; the
//                                  host's audio path is expected to call
//                                  it with int16 chunks
//
// Everything else stays local to this module (the JS8 panel's State,
// chains, QSO map, feed list, tabs, palette, modals, …).

// JS8 messenger — Phase 4. Loads the wasm codec, owns the JS8 panel
// state machine, and bridges the existing digi audio pipeline:
//   • RX: tapped from handleAudioData's int16 chunk via _js8ProcessAudioChunk
//   • TX: routed through streamDigiAudio (same path FT8 uses)
// The panel is an overlay peer of the FT8 DIGI Bar; opening one closes
// the other through closeOtherModes('js8' | 'digi' | ...).
import { js8Init, synthesize, getSubmode } from '/js8.mjs';

// Panel markup — appended to <body> exactly once on first import. Kept
// as a template literal so the JS that drives the panel and the HTML it
// drives ride together; editing them in sync is easier than chasing a
// markup file across the project.
const JS8_PANEL_MARKUP = `
        <!-- JS8 messenger panel — same overlay treatment as DIGI Bar, but
             the contents are a conversational feed instead of a CQ roster.
             Three zones: stations (left), feed (right), compose (bottom). -->
        <div id="js8Bar" class="js8-bar hidden">
            <div class="js8-header">
                <span class="js8-brand">JS8</span>
                <select id="js8SubmodeSel" class="js8-submode-sel"
                        title="TX speed — RX decodes all four speeds at once">
                    <option value="0">JS8 Normal · 15s</option>
                    <option value="1">JS8 Fast · 10s</option>
                    <option value="2">JS8 40 · 6s</option>
                    <option value="4">JS8 Slow · 30s</option>
                    <option value="8">JS8 60 · 4s</option>
                </select>
                <span class="js8-status-pill" id="js8Status">IDLE</span>
                <div class="js8-progress-wrap">
                    <div id="js8ProgressBar" class="js8-progress-fill"></div>
                </div>
                <span id="js8Clock" class="js8-clock">15</span>
                <span class="js8-field-label" title="TX audio frequency. Auto when 'Auto Freq' is on (Settings).">TX</span>
                <input type="number" id="js8TxFreq" class="js8-num-input"
                       min="200" max="2800" step="10" value="1500"
                       title="TX audio frequency in Hz">
                <span class="js8-field-label">Hz</span>
                <span id="js8AutoTag" class="js8-auto-tag" title="Auto-pick TX frequency">AUTO</span>
                <div class="flex-space"></div>
                <select id="js8BandSel" class="js8-band-sel"
                        title="Tune the rig to a JS8 dial frequency"></select>
                <button id="js8RxToggle" class="js8-rx-toggle active">RX ON</button>
                <button id="js8TuneBtn" class="js8-tune-btn"
                        title="Transmit a continuous carrier at the JS8 TX frequency (for ATU tuning)">TUNE</button>
                <button id="js8SettingsBtn" class="js8-icon-btn" title="Settings">&#9881;</button>
                <button id="js8CloseBtn" class="js8-icon-btn" title="Close">&#x2715;</button>
            </div>

            <div class="js8-content">
                <div class="js8-col js8-stations-col">
                    <div class="js8-col-header">Heard</div>
                    <div id="js8Stations" class="js8-stations"></div>
                </div>
                <div class="js8-main-col">
                    <!-- Tab strip: Monitor (catch-all feed) + one tab per
                         open QSO. Mirrors packet's term-session-tabs. -->
                    <div id="js8Tabs" class="js8-tabs"></div>
                    <!-- Monitor view = the original chronological feed -->
                    <div id="js8Feed" class="js8-feed"></div>
                    <!-- QSO view = chat-bubble layout for the active peer.
                         Path header above messages reflects the relay
                         chain the conversation uses. -->
                    <div id="js8QsoView" class="js8-qso-view hidden">
                        <div class="js8-qso-header">
                            <span id="js8QsoPath" class="js8-qso-path"></span>
                            <button id="js8QsoCloseBtn" class="js8-link-btn"
                                title="Close this QSO">close</button>
                        </div>
                        <div id="js8QsoMessages" class="js8-qso-messages"></div>
                    </div>
                </div>
            </div>

            <!-- Waterfall row — same FFT/blit core as the FT8/FT4 panel
                 (drawWfCanvas) with JS8-amber markers. Click sets TX freq. -->
            <div class="js8-waterfall-wrap" id="js8WaterfallWrap">
                <div class="js8-wf-bar" id="js8WfBar">
                    <canvas id="js8WfCanvas" class="js8-wf-canvas"></canvas>
                    <div id="js8WfTxMarker" class="js8-wf-marker js8-wf-tx"><span class="js8-wf-label">TX</span></div>
                </div>
                <div class="js8-wf-scale">
                    <span>300</span><span>800</span><span>1.3k</span><span>1.8k</span><span>2.3k</span><span>2.8k</span>
                </div>
            </div>

            <!-- CMD palette overlay. Position is recalculated on open
                 to anchor above the compose row. Backdrop catches
                 outside-clicks. -->
            <div id="js8CmdPaletteBackdrop" class="js8-cmd-palette-backdrop"></div>
            <div id="js8CmdPalette" class="js8-cmd-palette">
                <div class="js8-cmd-palette-section">
                    <div class="js8-cmd-palette-title">Ask</div>
                    <div class="js8-cmd-palette-grid">
                        <button class="js8-cmd-palette-btn" data-cmd="SNR?">SNR?<span class="desc">how do you hear me</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="GRID?">GRID?<span class="desc">their locator</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="INFO?">INFO?<span class="desc">their info</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="STATUS?">STATUS?<span class="desc">their status</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="HEARING?">HEARING?<span class="desc">who they hear</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="HW CPY?">HW CPY?<span class="desc">copy check</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="QSL?">QSL?<span class="desc">do you copy</span></button>
                        <button class="js8-cmd-palette-btn" data-cmd="AGN?">AGN?<span class="desc">please repeat</span></button>
                    </div>
                </div>
                <div class="js8-cmd-palette-section">
                    <div class="js8-cmd-palette-title">Reply</div>
                    <div class="js8-cmd-palette-grid">
                        <button class="js8-cmd-palette-btn reply" data-cmd="RR">RR<span class="desc">roger roger</span></button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="FB">FB<span class="desc">fine business</span></button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="QSL">QSL<span class="desc">I copy</span></button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="ACK">ACK<span class="desc">acknowledge</span></button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="YES">YES</button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="NO">NO</button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="73">73<span class="desc">best regards</span></button>
                        <button class="js8-cmd-palette-btn reply" data-cmd="SK">SK<span class="desc">end of contact</span></button>
                    </div>
                </div>
                <div class="js8-cmd-palette-section">
                    <div class="js8-cmd-palette-title">Action</div>
                    <div class="js8-cmd-palette-grid">
                        <button class="js8-cmd-palette-btn action" data-cmd="CQ">CQ<span class="desc">call CQ to @ALLCALL</span></button>
                        <button class="js8-cmd-palette-btn action" id="js8PaletteHbBtn" data-cmd="HB">HB<span class="desc">send heartbeat</span></button>
                        <button class="js8-cmd-palette-btn action" data-cmd="RELAY">&gt; RELAY<span class="desc">multi-hop message</span></button>
                    </div>
                </div>
            </div>

            <div class="js8-action-row">
                <span class="js8-field-label js8-to-label">TO</span>
                <input type="text" id="js8DxCall" class="js8-callsign-input"
                       maxlength="9" placeholder="TO…" autocomplete="off">
                <!-- Floating suggestion popover for the TO field. Opens
                     on hover/focus, lists @ALLCALL + @HB + most-recently
                     heard stations. Click a chip to fill the TO input. -->
                <div id="js8DxSuggest" class="js8-dx-suggest" aria-hidden="true">
                    <div class="js8-dx-suggest-title">SUGGESTIONS</div>
                    <div id="js8DxSuggestGrid" class="js8-dx-suggest-grid"></div>
                    <div class="js8-dx-suggest-help">Leave empty to broadcast to <b>@ALLCALL</b>.</div>
                </div>
                <button id="js8CmdChip" class="js8-cmd-chip"
                    title="Open the CMD palette (or press / in compose)"
                    aria-label="Open CMD palette">⌘</button>
                <input type="text" id="js8Compose" class="js8-compose"
                       placeholder="Type message…" maxlength="120" autocomplete="off">
                <button id="js8SendBtn" class="js8-big-btn js8-send-btn">SEND</button>
            </div>
        </div>

        <!-- JS8 Relay modal — compose a relay chain. The user fills in
             space-separated VIA hops, a final TO target, and a MESSAGE;
             the preview shows the exact wire form before sending. -->
        <div id="js8RelayModal" class="js8-modal hidden">
            <div class="js8-modal-body">
                <div class="js8-modal-title">Relay a Message</div>
                <p class="js8-modal-help">Send a message that travels through one or more relay stations on its way to the final recipient. Each station along the chain forwards it on automatically.</p>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Each station forwards the message to the next one. Order matters.">VIA (space-separated)</span>
                    <input type="text" id="js8RelayVia" class="js8-compose" placeholder="e.g. IZ6BYY VK2ITM" autocomplete="off">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="The station the message is for. It does not need to be in range of yours — only the last VIA station needs to reach it.">TO (final target)</span>
                    <input type="text" id="js8RelayTo" class="js8-compose" placeholder="e.g. IW6PBC" autocomplete="off">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label">MESSAGE</span>
                    <input type="text" id="js8RelayMsg" class="js8-compose" placeholder="e.g. HELLO" maxlength="80" autocomplete="off">
                </div>
                <div class="js8-relay-preview">
                    <span class="js8-field-label">PATH</span>
                    <span id="js8RelayPath" class="js8-relay-path"></span>
                </div>
                <div class="js8-relay-preview">
                    <span class="js8-field-label">ON-AIR</span>
                    <span id="js8RelayPreview" class="js8-relay-wire"></span>
                </div>
                <div class="js8-modal-actions">
                    <button id="js8RelayCancelBtn" class="js8-modal-close">Cancel</button>
                    <button id="js8RelaySendBtn" class="js8-big-btn js8-send-btn">SEND</button>
                </div>
            </div>
        </div>

        <!-- JS8 settings modal — minimal: station identity + audio base freq.
             Callsign + grid come from the app-wide shared widgets; this
             modal is for JS8-only knobs (submode default, base Hz). -->
        <div id="js8SettingsModal" class="js8-modal hidden">
            <div class="js8-modal-body">
                <div class="js8-modal-title">JS8 Settings</div>
                <div class="js8-settings-field">
                    <span class="js8-field-label">CALL</span>
                    <input type="text" id="js8SettingMycall" class="js8-callsign-input" maxlength="10" placeholder="CALL" autocomplete="off">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label">GRID</span>
                    <input type="text" id="js8SettingMygrid" class="js8-callsign-input" maxlength="6" placeholder="EM85" autocomplete="off">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label">DEFAULT SPEED</span>
                    <select id="js8SettingSubmode" class="js8-submode-sel">
                        <option value="0">JS8 Normal · 15s</option>
                        <option value="1">JS8 Fast · 10s</option>
                        <option value="2">JS8 40 · 6s</option>
                        <option value="4">JS8 Slow · 30s</option>
                        <option value="8">JS8 60 · 4s</option>
                    </select>
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Auto-pick a non-colliding TX frequency. HB uses the 500–1000 Hz sub-channel; regular traffic uses 1000–2500 Hz.">AUTO FREQ</span>
                    <input type="checkbox" id="js8SettingAutoFreq">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Periodically broadcast a heartbeat so other JS8 stations can discover you.">HB NETWORK</span>
                    <input type="checkbox" id="js8SettingHbNet">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Minutes between automatic heartbeats.">HB INTERVAL</span>
                    <input type="number" id="js8SettingHbInterval" class="js8-num-input" min="2" max="120" step="1" value="15">
                    <span class="js8-field-label">min</span>
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Automatically reply to received heartbeats with a signal-strength report.">HB ACK</span>
                    <input type="checkbox" id="js8SettingHbAck">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Minimum minutes between successive ACKs to the same station, to avoid ACK storms.">HB ACK COOLDOWN</span>
                    <input type="number" id="js8SettingHbAckCooldown" class="js8-num-input" min="0" max="60" step="1" value="2">
                    <span class="js8-field-label">min</span>
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Auto-reply to received SNR?, GRID?, INFO?, STATUS?, HEARING?, AGN?, and QSL? queries.">AUTO RESPOND</span>
                    <input type="checkbox" id="js8SettingAutoRespond">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Free-text reply sent when someone asks INFO?.">INFO TEXT</span>
                    <input type="text" id="js8SettingInfoText" class="js8-compose" maxlength="80" placeholder="e.g. RIG IC-7300 ANT DIPOLE">
                </div>
                <div class="js8-settings-field">
                    <span class="js8-field-label" title="Free-text reply sent when someone asks STATUS?.">STATUS TEXT</span>
                    <input type="text" id="js8SettingStatusText" class="js8-compose" maxlength="80" placeholder="e.g. WFWEB IDLE">
                </div>
                <button id="js8SettingsCloseBtn" class="js8-modal-close">Close</button>
            </div>
        </div>
`;

(async () => {
    if (!document.getElementById('js8Bar')) {
        // The panel's `.js8-bar` uses `position: absolute; inset: 0` and
        // expects its containing block to be the scope area (whose CSS
        // grid row is expanded by `body.js8-open #scopeArea { grid-row:
        // 3 / 5 }`). Injecting onto <body> instead leaves the panel
        // filling the entire viewport, while stray scope-area children
        // (audio/mic status, spectrum tick labels) bleed through above
        // and below it. Both builds provide #scopeArea — that's the
        // home position. Body fallback is for any future host that
        // doesn't have a scope area at all.
        var host = document.getElementById('scopeArea') || document.body;
        host.insertAdjacentHTML('beforeend', JS8_PANEL_MARKUP);
    }
    const js8 = await js8Init();
    window.js8 = js8;

    // ─── State ─────────────────────────────────────────────────────────
    // RX runs all five submodes simultaneously — each mode has its
    // own slot duration aligned to UTC seconds. lastSlot tracks the most
    // recent slot index we've seen per mode so we can detect a boundary
    // and trigger an *aligned* decode for the just-ended slot.
    const SUBMODE_BITS = { 0: 1, 1: 2, 2: 4, 4: 8, 8: 16 }; // SubmodeType ID → ksz bit
    const RX_MODES     = [0, 1, 2, 4, 8];                   // JS8 Normal/Fast/40/Slow/60
    const S = {
        visible: false,
        rxEnabled: false,
        submode: 0,             // TX submode (RX always tries all 4)
        baseHz: 1500,           // current TX audio freq (Hz), auto-picked or manual
        autoFreq: true,         // recompute baseHz at every enqueueTx
        hbNet: true,            // auto-broadcast HB every hbInterval minutes
        hbInterval: 15,         // minutes between auto HBs
        hbAck: true,            // auto-reply to received HBs with SNR
        hbAckCooldown: 2,       // minutes — per-station cooldown between ACKs
        autoRespond: true,      // auto-reply to SNR? / GRID? / INFO? / STATUS? / HEARING? / AGN? / QSL?
        infoText: '',           // free-text reply to INFO?
        statusText: 'WFWEB',    // free-text reply to STATUS?
        lastAutoReply: new Map(), // "fromCall:cmd" → ms timestamp (per-pair cooldown)
        lastTxText: '',         // most recent TX text — used to answer AGN?
        decoder: null,
        rxBuffer: null,
        rxWritePos: 0,
        rxPhase: 0,
        slotIndex: -1,                  // TX slot tracker (driven by S.submode)
        lastSlot: { 0: -1, 1: -1, 2: -1, 4: -1, 8: -1 }, // per-mode RX slot trackers
        chains: new Map(),              // freqKey → { from, parts, firstUtc, snr, freq }
        chainFlushTimer: null,
        lastHeardRenderMin: -1,         // wall-clock minute of last Heard-list re-render

        timer: null,
        stations: new Map(),
        feed: [],
        feedMax: 100,
        dxCall: '@ALLCALL',
        txBusy: false,
        txQueue: [],
        lastHbTx: 0,              // wall-clock ms of most recent auto HB
        lastAckTo: new Map(),     // call → ms timestamp of last ACK we sent
        // QSO-pause state: when a user-initiated directed message has
        // been sent to a real peer, we hold auto-TX (HB beacons, HB-ACKs
        // to third parties, auto-replies to non-peer stations) so a
        // courtesy ACK doesn't collide with the partner's slow
        // multi-frame reply. Cleared automatically after QSO_PENDING_MS
        // or when the partner's reply finishes.
        qsoPendingPeer: '',
        qsoPendingAt: 0,
        txIdCounter: 0,           // monotonic id for matching TX feed rows to queued frames
        activeTx: null,           // { entries: [{entry,count}], totalFrames, startMs, slotMs, timer }
        // ─── QSO tabs ──────────────────────────────────────────────────
        // Each direct conversation lives in its own QSO entry: messages
        // are entry objects (same shape as feed entries — usually the SAME
        // object reference, pushed into both feed and qso.messages). The
        // active tab decides what's rendered to the right of Heard.
        // 'monitor' = the catch-all feed; any other value is a peer call.
        qsos: new Map(),       // peer → { peer, path: [], messages: [], unread: 0, lastUtc, lastSnr }
        activeTab: 'monitor',  // 'monitor' or peer callsign
        tabOrder: [],          // [peer1, peer2, ...] (Monitor is implicit head)
    };

    // ─── Persistent settings (localStorage) ───────────────────────────
    // Each setting reads from localStorage on open() and writes when the
    // settings modal is closed. Booleans are stored as "0"/"1".
    // A version key (js8SettingsV) guards one-shot migrations against
    // stale data — older builds defaulted the submode to JS8 40 (then-
    // named "Turbo"), which is no longer the desired starting speed.
    var JS8_SETTINGS_VERSION = 2;
    function loadSettings() {
        try {
            var ls = window.localStorage;
            // One-shot migration: anyone whose stored settings predate v2
            // gets their saved submode wiped so the in-code default
            // (JS8 Normal) takes effect on the next load. Other settings
            // are preserved.
            var savedVer = parseInt(ls.getItem('js8SettingsV'), 10);
            if (isNaN(savedVer) || savedVer < JS8_SETTINGS_VERSION) {
                ls.removeItem('js8Submode');
                ls.setItem('js8SettingsV', String(JS8_SETTINGS_VERSION));
            }
            if (ls.getItem('js8AutoFreq')    != null) S.autoFreq    = ls.getItem('js8AutoFreq') === '1';
            if (ls.getItem('js8HbNet')       != null) S.hbNet       = ls.getItem('js8HbNet') === '1';
            if (ls.getItem('js8HbAck')       != null) S.hbAck       = ls.getItem('js8HbAck') === '1';
            var iv = parseInt(ls.getItem('js8HbInterval'), 10);
            if (!isNaN(iv) && iv > 0)                 S.hbInterval  = iv;
            var ac = parseInt(ls.getItem('js8HbAckCooldown'), 10);
            if (!isNaN(ac) && ac > 0)                 S.hbAckCooldown = ac;
            if (ls.getItem('js8AutoRespond')  != null) S.autoRespond = ls.getItem('js8AutoRespond') === '1';
            if (ls.getItem('js8InfoText')     != null) S.infoText    = ls.getItem('js8InfoText');
            if (ls.getItem('js8StatusText')   != null) S.statusText  = ls.getItem('js8StatusText');
            var sm = parseInt(ls.getItem('js8Submode'), 10);
            if (!isNaN(sm))                           S.submode     = sm;
            var bh = parseInt(ls.getItem('js8BaseHz'), 10);
            if (!isNaN(bh))                           S.baseHz      = bh;
        } catch (e) {}
    }
    function saveSettings() {
        try {
            var ls = window.localStorage;
            ls.setItem('js8SettingsV', String(JS8_SETTINGS_VERSION));
            ls.setItem('js8AutoFreq',   S.autoFreq ? '1' : '0');
            ls.setItem('js8HbNet',      S.hbNet    ? '1' : '0');
            ls.setItem('js8HbAck',      S.hbAck    ? '1' : '0');
            ls.setItem('js8HbInterval', String(S.hbInterval));
            ls.setItem('js8HbAckCooldown', String(S.hbAckCooldown));
            ls.setItem('js8AutoRespond',  S.autoRespond ? '1' : '0');
            ls.setItem('js8InfoText',     S.infoText || '');
            ls.setItem('js8StatusText',   S.statusText || '');
            ls.setItem('js8Submode',    String(S.submode));
            ls.setItem('js8BaseHz',     String(S.baseHz));
        } catch (e) {}
    }
    loadSettings();

    // ─── Helpers ──────────────────────────────────────────────────────
    function getMyCall() {
        return (window.App && window.App.callsign && window.App.callsign.get()) || '';
    }
    function getMyGridLocal() {
        var el = document.getElementById('digiGrid');
        return ((el && el.value) || '').toUpperCase().substr(0, 6);
    }
    function escapeHtml(s) {
        return String(s).replace(/[&<>"']/g, function(c) {
            return { '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;' }[c];
        });
    }
    function utcStamp() {
        var d = new Date(), p = (n) => n < 10 ? '0' + n : '' + n;
        return p(d.getUTCHours()) + ':' + p(d.getUTCMinutes()) + ':' + p(d.getUTCSeconds());
    }
    function setStatus(label, cls) {
        var el = document.getElementById('js8Status');
        if (!el) return;
        el.textContent = label;
        el.className = 'js8-status-pill' + (cls ? ' ' + cls : '');
    }

    // ─── RX pipeline ──────────────────────────────────────────────────
    // The decoder runs all four submodes at once. Slow's slot is 30 s
    // (longest); we want at least one full Slow slot at every Slow slot
    // boundary plus headroom for the older slot we just decoded. 60 s
    // of rolling buffer covers that comfortably and matches JS8Call's
    // own JS8_NTMAX (= 60). We slide when more than half the buffer is
    // used so writes never run off the end.
    var RX_BUFFER_SECONDS = 60;
    function resetRxBuffer() {
        S.rxBuffer = new Float32Array(RX_BUFFER_SECONDS * 12000);
        S.rxWritePos = 0;
        S.rxPhase = 0;
    }
    function maybeSlideRxBuffer() {
        var halfLen   = (RX_BUFFER_SECONDS / 2) * 12000;
        var maxKeep   = halfLen;  // keep at most the most-recent half
        if (S.rxWritePos > halfLen) {
            S.rxBuffer.copyWithin(0, S.rxWritePos - maxKeep, S.rxWritePos);
            S.rxWritePos = maxKeep;
        }
    }

    // Diagnostic counters for the RX pipeline. Easy to read from devtools:
    //   window.js8Diag()
    var diag = { audioChunks: 0, audioSamples: 0, decodePasses: 0,
                 syncCandidates: 0, decodes: 0, lastBufferSamples: 0 };
    window.js8Diag = function () { return Object.assign({}, diag,
        { rxEnabled: S.rxEnabled, txBusy: S.txBusy,
          submode: S.submode, rxWritePos: S.rxWritePos,
          rxBufferLen: S.rxBuffer ? S.rxBuffer.length : 0,
          audioSampleRate: typeof audioSampleRate !== 'undefined' ? audioSampleRate : '(undef)',
          decoder: !!S.decoder, visible: S.visible }); };

    // Called from handleAudioData via window._js8ProcessAudioChunk. Decimates
    // rig-rate Int16 audio down to 12 kHz Float32 into S.rxBuffer.
    window._js8ProcessAudioChunk = function (int16Chunk) {
        if (!S.rxEnabled || !S.rxBuffer || S.txBusy) return;
        diag.audioChunks++;
        diag.audioSamples += int16Chunk.length;
        if (diag.audioChunks === 1) {
            console.log('[JS8 RX] first audio chunk: '
                + int16Chunk.length + ' samples, audioSampleRate='
                + (typeof audioSampleRate !== 'undefined' ? audioSampleRate : '?'));
        }
        var D = Math.round(audioSampleRate / 12000); if (D < 1) D = 1;
        // Same 12 kHz decimation feeds JS8's RX buffer AND the shared
        // waterfall FFT buffer (drawWfCanvas reads from wfBuf). The
        // waterfall feed is gated on S.visible so the JS8 panel doesn't
        // overwrite FT8's wfBuf when FT8 is the active overlay.
        for (var i = 0; i < int16Chunk.length; i++) {
            S.rxPhase++;
            if (S.rxPhase >= D) {
                S.rxPhase = 0;
                var sample = int16Chunk[i] / 32768.0;
                if (S.rxWritePos < S.rxBuffer.length) {
                    S.rxBuffer[S.rxWritePos++] = sample;
                }
                if (S.visible && typeof wfBuf !== 'undefined') {
                    wfBuf[wfBufPos] = sample;
                    wfBufPos++;
                    if (wfBufPos >= WF_FFT_SIZE) { wfBufPos = 0; wfBufFull = true; }
                }
            }
        }
    };

    function slotInfo() {
        var p = getSubmode(S.submode).slotSeconds;
        var now = Date.now() / 1000;
        var phase = now % p;
        return { slotIndex: Math.floor(now / p), slotPhase: phase, remaining: p - phase, period: p };
    }

    // JS8 is 8-FSK. Tone n is at baseHz + n*baud, where baud = 12000/symbolSamples
    // (matches synthesize() in js8.mjs). Center of the tone span is 3.5 * baud
    // above baseHz — same convention as FT8's digiCenterOffset().
    function js8CenterOffset() {
        var sm = getSubmode(S.submode);
        return 3.5 * (12000 / sm.symbolSamples);
    }

    function drawJs8Waterfall() {
        var canvas = document.getElementById('js8WfCanvas');
        if (!canvas) return;
        var marker = document.getElementById('js8WfTxMarker');
        var bwHz = js8CenterOffset() * 2;
        var freqRange = WF_FREQ_HIGH - WF_FREQ_LOW;
        if (marker) {
            marker.style.left  = ((S.baseHz - WF_FREQ_LOW) / freqRange * 100).toFixed(3) + '%';
            marker.style.width = (bwHz / freqRange * 100).toFixed(3) + '%';
            marker.classList.toggle('armed', !!S.txBusy);
        }
        drawWfCanvas(canvas, null, {
            rxEnabled: S.rxEnabled,
            slotInfo: slotInfo(),
            labels: null,
            centerOffset: js8CenterOffset(),
        });
    }

    function tickTimer() {
        if (!S.visible) return;
        var nowSec = Date.now() / 1000;

        // Clock + progress bar — driven by the user's selected TX submode
        // so it shows the cadence of the speed they'll transmit at.
        var info = slotInfo();
        var clk = document.getElementById('js8Clock');
        if (clk) clk.textContent = Math.ceil(info.remaining);
        var bar = document.getElementById('js8ProgressBar');
        if (bar) {
            bar.style.width = ((info.slotPhase / info.period) * 100).toFixed(1) + '%';
            bar.className = 'js8-progress-fill' + (S.txBusy ? ' tx' : '');
        }

        // During TX, drain locally-generated samples into wfBuf so the
        // waterfall shows our own signal — same loopback pattern as the
        // FT8 panel (digiTxWfSamples is set from fireNextTxFrame).
        if (typeof digiTxWfSamples !== 'undefined' && digiTxWfSamples
            && digiTxWfPos < digiTxWfSamples.length) {
            var tickSamples = Math.round(12000 * 0.1); // 100 ms per tick
            var end = Math.min(digiTxWfPos + tickSamples, digiTxWfSamples.length);
            for (var wi = digiTxWfPos; wi < end; wi++) {
                wfBuf[wfBufPos] = digiTxWfSamples[wi];
                wfBufPos++;
                if (wfBufPos >= WF_FFT_SIZE) { wfBufPos = 0; wfBufFull = true; }
            }
            digiTxWfPos = end;
        }

        drawJs8Waterfall();

        // RX: detect each enabled submode's slot boundary independently
        // and decode that mode's slot as soon as it completes. Multiple
        // modes can complete at the same instant (every 30 s all four
        // align), so we collect them and run a single decode pass with
        // the right kpos/ksz per mode.
        var modesEnded = [];
        for (var i = 0; i < RX_MODES.length; ++i) {
            var sm = RX_MODES[i];
            var p  = getSubmode(sm).slotSeconds;
            var idx = Math.floor(nowSec / p);
            if (idx !== S.lastSlot[sm]) {
                if (S.lastSlot[sm] !== -1) {
                    modesEnded.push({ sm: sm, slotSeconds: p, bit: SUBMODE_BITS[sm] });
                }
                S.lastSlot[sm] = idx;
            }
        }
        if (modesEnded.length > 0 && S.rxEnabled && !S.txBusy) {
            triggerDecode(modesEnded);
        }

        // TX: fire at the user's selected TX submode's slot boundary.
        if (info.slotIndex !== S.slotIndex) {
            S.slotIndex = info.slotIndex;
            if (S.txQueue.length > 0 && !S.txBusy) {
                fireNextTxFrame();
            } else {
                // Slot boundary is also when the HB scheduler considers
                // queueing a new auto-beacon. We only queue at slot
                // boundaries because that's when fireNextTxFrame will
                // pick it up — queueing mid-slot would idle for almost
                // a whole period.
                maybeFireHb();
            }
        }

        // Re-render the Heard list once per wall-clock minute so the
        // age badges tick forward and stations past the TTL fall off
        // even during long quiet stretches with no decodes.
        var nowMin = Math.floor(Date.now() / 60000);
        if (nowMin !== S.lastHeardRenderMin) {
            S.lastHeardRenderMin = nowMin;
            renderStations();
        }

        maybeSlideRxBuffer();
    }

    function triggerDecode(modesEnded) {
        if (!S.decoder || !S.rxBuffer || S.rxWritePos < 1000) return;
        setStatus('DEC', 'dec');
        diag.decodePasses++;
        diag.lastBufferSamples = S.rxWritePos;

        // Each mode's window points at the LAST slotSeconds of staged audio.
        // We've just detected the slot ended at wall-clock T_now ≈ now, and
        // the audio we captured into the buffer up to S.rxWritePos covers
        // that slot's transmission interval. Setting kposX = rxWritePos -
        // slotSamples and kszX = slotSamples points the decoder at it.
        var nsubmodes = 0;
        var posA = 0, szA = 0, posB = 0, szB = 0, posC = 0, szC = 0;
        var posE = 0, szE = 0, posI = 0, szI = 0;
        for (var i = 0; i < modesEnded.length; ++i) {
            var m = modesEnded[i];
            var slotSamples = m.slotSeconds * 12000;
            var sz  = Math.min(S.rxWritePos, slotSamples);
            var pos = Math.max(0, S.rxWritePos - slotSamples);
            nsubmodes |= m.bit;
            switch (m.bit) {
                case 1:  posA = pos; szA = sz; break; // Normal
                case 2:  posB = pos; szB = sz; break; // Fast
                case 4:  posC = pos; szC = sz; break; // JS8 40 (Turbo)
                case 8:  posE = pos; szE = sz; break; // Slow
                case 16: posI = pos; szI = sz; break; // JS8 60 (Ultra)
            }
        }

        // The wasm decoder's push() is append-only with front-eviction. If
        // we kept reusing one long-lived decoder across passes, every push
        // would duplicate prior samples into its ring (we always slice from
        // rxBuffer[0]), so kpos/ksz coordinates — which are in SPA-buffer
        // space — would drift out of alignment with what's actually staged
        // in dec_data.d2. Recreating per pass keeps the two in lockstep;
        // the constructor is essentially free (just an initializer list
        // over dec_data). Same pattern the roundtrip tests use.
        if (S.decoder) S.decoder.free();
        S.decoder = js8.newDecoder(0);

        var samples = S.rxBuffer.slice(0, S.rxWritePos);
        S.decoder.push(samples);
        S.decoder.runModes(nsubmodes, posA, szA, posB, szB, posC, szC, posE, szE, posI, szI);

        var syncCands = 0, decoded = 0;
        while (true) {
            var ev = S.decoder.pop();
            if (!ev) break;
            if (ev.event === 'sync') {
                if (ev.kind === 'candidate') { syncCands++; diag.syncCandidates++; }
            } else if (ev.event === 'decoded') {
                decoded++; diag.decodes++;
                ingestDecode(ev);
            }
        }
        console.log('[JS8 RX] decode (modes='
            + modesEnded.map(function (m) { return m.sm; }).join(',')
            + ', nsubmodes=' + nsubmodes
            + ', samples=' + samples.length + ', cands=' + syncCands
            + ', decoded=' + decoded + ')');
        setStatus(S.rxEnabled ? 'RX' : 'IDLE', S.rxEnabled ? 'rx' : '');
    }

    // Multi-frame messages arrive across several slots: the first frame
    // has the JS8CallFirst bit (1), the last has JS8CallLast (2), middle
    // frames have neither (and contain the message continuation). We
    // group consecutive frames by transmit frequency (bucketed coarsely
    // so small dt jitter doesn't break the chain) and emit one merged
    // entry once the chain closes. Stale chains are flushed after 60 s.
    var JS8_BIT_FIRST = 1;
    var JS8_BIT_LAST  = 2;

    // Recognise only ":" as the FROM/MSG separator. Some decoded frame
    // texts contain "TARGET> …" (the relay marker), and the old form
    // `[:>]` mistakenly treated that as if TARGET were the sender —
    // which silently dropped the intermediate hop of a multi-hop relay
    // when the data continuation began with "<NEXT-HOP>>".
    function parseFrom(text) {
        var m = text.match(/^([@A-Z0-9\/]{2,})\s*:\s*(.*)$/);
        if (m) return { from: m[1], msg: m[2].trim() };
        // Compound de-compound frames where the sender has no grid set
        // come back as a bare "CALLSIGN  " — JS8Call's unpack
        // strategies bail because the trimmed text contains a space
        // (or is <12 chars), so message_ stays as the raw frame text
        // with no ": " sender suffix. Recognise that shape too,
        // otherwise the chain accumulator never learns who sent the
        // multi-frame TX and routing on isLast falls through to
        // Monitor instead of the QSO/group tab.
        //
        // Validator is shaped to the ITU callsign pattern (1-2 letter
        // prefix + 1-2 digits + 1-4 letter suffix, optional /SUFFIX),
        // THEN rejected if the same token also matches a Maidenhead
        // grid (4/6/8-char). The callsign regex alone can't tell
        // EM60RV (grid) from KE6ABC (callsign) — both are LL##LL —
        // but grids have positional constraints (first 2 letters in
        // A-R, sub-square in A-X) that real callsigns mostly violate.
        // Without this second pass, bare grids decoded out of context
        // showed up in the Heard list as phantom stations.
        var m2 = text.match(/^([A-Z0-9\/]{2,})\s*$/);
        if (m2 && /^[A-Z]{1,2}[0-9]{1,2}[A-Z]{1,4}(\/[A-Z0-9]{1,4})?$/.test(m2[1])) {
            var looksLikeGrid =
                /^[A-R]{2}[0-9]{2}$/.test(m2[1]) ||
                /^[A-R]{2}[0-9]{2}[A-X]{2}$/.test(m2[1]) ||
                /^[A-R]{2}[0-9]{2}[A-X]{2}[0-9]{2}$/.test(m2[1]);
            if (!looksLikeGrid) return { from: m2[1], msg: '' };
        }
        return { from: '', msg: text };
    }

    // Record/refresh a station's last-heard record. Called from
    // ingestDecode the moment a frame arrives — earlier than
    // emitFeedEntry, because the HB-ACK decision needs to know the
    // sender's freq when it picks its own TX freq. Without this, two
    // independent stations replying to the same HB would both see an
    // empty station db and pick the same default freq (collision).
    function noteStation(from, freq, snr) {
        if (!from || from.charAt(0) === '@') return;
        var s = S.stations.get(from) || { count: 0 };
        s.lastUtc = Date.now();
        if (typeof snr === 'number')  s.snr  = snr;
        if (typeof freq === 'number') s.freq = freq;
        s.count++;
        S.stations.set(from, s);
    }

    // Heartbeat traffic directed at us (an HB ACK — "<me> HEARTBEAT SNR
    // +N" — or a bare "<me> HEARTBEAT") is network housekeeping, not a
    // conversation. It must NOT auto-open a per-peer chat tab: a single
    // HB beacon can draw ACKs from a dozen stations and we don't want a
    // dozen tabs. It still lands in Monitor and updates the Heard list.
    function isHeartbeatCmd(cmd) {
        return cmd === 'HEARTBEAT SNR' || cmd === 'HEARTBEAT';
    }

    function emitFeedEntry(entry) {
        S.feed.unshift(entry);
        if (S.feed.length > S.feedMax) S.feed.length = S.feedMax;
        // Station record was already updated up front in ingestDecode;
        // we still call here for direct emitFeedEntry callers (none today,
        // but cheap insurance).
        noteStation(entry.from, entry.freq, entry.snr);
        // Auto-route into a chat tab.
        //   * Directed-at-me from a real callsign → per-peer chat tab.
        //   * Directed at @ALLCALL → always auto-route (broadcast
        //     channel; every station listens, so the tab subscribes
        //     itself on first hit).
        //   * Directed at any other group (@SAR, @GROUP1, …) → only
        //     when the operator has already subscribed (= a tab for
        //     that group is open). Unsubscribed groups stay in Monitor
        //     so a station that overhears @GROUP1 traffic doesn't get
        //     a tab forced open.
        // The `>` (relay) case is excluded — that's a forwarding
        // request handled in processBufferedDirected, not chat. Echoes
        // of our own TX (fromMe / from === myCall) are also skipped.
        var myCall = getMyCall();
        if (!entry.fromMe && entry.from && entry.from.charAt(0) !== '@'
            && entry.cmd !== '>' && !isHeartbeatCmd(entry.cmd)
            && (!myCall || entry.from !== myCall)) {
            if (myCall && entry.target === myCall) {
                routeEntryToQso(entry, entry.from);
            } else if (entry.target && entry.target.charAt(0) === '@'
                       && (entry.target === '@ALLCALL'
                           || S.qsos.has(entry.target))) {
                routeEntryToQso(entry, entry.target);
            }
        }
        renderFeed();
        renderStations();
    }

    function closeChain(key) {
        // The chain's feed entry was unshifted on the first frame and has
        // been mutated in-place ever since; closing is just bookkeeping.
        S.chains.delete(key);
    }

    // ─── CRC-16 KERMIT (mirrors Varicode::checksum16) ─────────────────
    // JS8Call appends a 3-char CRC-16 KERMIT checksum to buffered-cmd
    // messages (>, MSG, QUERY, QUERY MSGS?, QUERY CALL, GRID, CMD) so
    // receivers can detect corruption. The 16-bit value is packed into 3
    // chars from JS8's 41-char alphabet (digits + uppercase + +-./?).
    // Helpers below match the C++ implementation in Varicode.cpp lines
    // 482-490 and 729-742.
    var JS8_ALPHABET   = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?';
    var JS8_ALPHABET_N = 41;

    function js8Crc16Kermit(str) {
        var crc = 0;
        for (var i = 0; i < str.length; ++i) {
            crc ^= str.charCodeAt(i);
            for (var j = 0; j < 8; ++j) {
                crc = (crc & 1) ? ((crc >>> 1) ^ 0x8408) : (crc >>> 1);
            }
        }
        return crc & 0xFFFF;
    }
    function js8Pack16Bits(packed) {
        var n = JS8_ALPHABET_N;
        var c0 = Math.floor(packed / (n * n));
        var c1 = Math.floor((packed - c0 * n * n) / n);
        var c2 = packed % n;
        return JS8_ALPHABET.charAt(c0) + JS8_ALPHABET.charAt(c1) + JS8_ALPHABET.charAt(c2);
    }
    // Only strip if the trailing 3 chars validate as a CRC of the body —
    // that way legitimate 3-char message endings (e.g. "73", "RR", or a
    // grid like "FN30" with a stray space) aren't truncated. The CRC body
    // depends on the buffered cmd: JS8Call computes it over the text
    // AFTER the directed-cmd marker (with leading whitespace lstripped),
    // not over the full chain.entry.msg. For relay (`>`), that's the
    // substring after the first `>`; for word cmds like " MSG" / " QUERY"
    // it's the substring after the cmd word. Try both candidates.
    function stripValidChecksum(text, cmd) {
        if (text.length < 5) return text;
        var sp = text.length - 4;
        if (text.charAt(sp) !== ' ') return text;
        var tag = text.substr(sp + 1);
        var candidates = [text.substr(0, sp)]; // legacy: full body
        if (cmd === '>') {
            var gt = text.indexOf('>');
            if (gt >= 0 && gt < sp) {
                candidates.push(text.substring(gt + 1, sp).replace(/^\s+/, ''));
            }
        } else if (cmd) {
            var ix = text.indexOf(' ' + cmd);
            if (ix >= 0 && ix + 1 + cmd.length < sp) {
                candidates.push(text.substring(ix + 1 + cmd.length, sp).replace(/^\s+/, ''));
            }
        }
        for (var i = 0; i < candidates.length; ++i) {
            if (js8Pack16Bits(js8Crc16Kermit(candidates[i])) === tag) {
                return text.substr(0, sp);
            }
        }
        return text;
    }
    // For relay forwarding: strip the trailing 3-char CRC tag
    // unconditionally. The CRC in an incoming `>` was computed over the
    // FULL chain (with hops we're about to strip out), so we can't
    // re-validate it against the remaining payload. But we KNOW the
    // sender attached one — `>` is in JS8Call's checksum_cmds — so the
    // last " XXX" is guaranteed to be the CRC, not message text.
    function stripBufferedChecksum(text) {
        if (text.length < 4) return text;
        if (text.charAt(text.length - 4) !== ' ') return text;
        return text.substr(0, text.length - 4);
    }

    // JS8Call's "buffered" directed commands (checksummed multi-frame
    // messages): >, MSG, MSG TO:, QUERY, QUERY MSGS?, QUERY CALL, CMD.
    // GRID is also buffered per Varicode.cpp:128. wfweb only acts on `>`
    // today; the rest are recognised but otherwise pass through as
    // ordinary feed entries.
    function isBufferedDirectedCmd(cmd) {
        return cmd === '>' || cmd === 'MSG' || cmd === 'MSG TO:' ||
               cmd === 'QUERY' || cmd === 'QUERY MSGS?' || cmd === 'QUERY MSGS' ||
               cmd === 'QUERY CALL' || cmd === 'CMD' || cmd === 'GRID';
    }

    // Handle a completed buffered-cmd chain that was directed at mycall.
    // For `>` (relay), strip the "TARGET> " prefix and re-transmit the
    // rest — buildMessageFrames will route further relay hops or
    // free-text delivery automatically based on what comes next.
    function processBufferedDirected(chain) {
        var info = chain.buffered;
        if (!info || info.cmd !== '>') return;   // only relay implemented
        var myCall = getMyCall();
        if (!myCall) return;
        if (info.from === myCall) return;        // our own echo, ignore
        // chain.entry.msg = full concatenated text after the "FROM: "
        // prefix; for relay it looks like "ME> PAYLOAD". Split on the
        // first ">" to recover the payload.
        var fullMsg = chain.entry.msg || '';
        var gt = fullMsg.indexOf('>');
        if (gt < 0) return;
        var payload = fullMsg.substr(gt + 1).trim();
        if (!payload) return;
        // Strip the original CRC-16. We can't re-validate it (the CRC
        // was computed over the full chain, including hops we're about
        // to drop), so use the unconditional strip — `>` always carries
        // a CRC per JS8Call's checksum_cmds table. Without this, the
        // old tag stays glued onto the payload and ends up displayed at
        // the final hop as if it were message text.
        payload = stripBufferedChecksum(payload);
        // Per-from cooldown so a repeated relay frame doesn't fire us
        // twice. Uses the same lastAutoReply map as the query auto-
        // responder.
        var ckey = 'RELAY:' + info.from;
        var now  = Date.now();
        if (now - (S.lastAutoReply.get(ckey) || 0) < 60 * 1000) return;
        S.lastAutoReply.set(ckey, now);
        // Don't bounce back to the sender on a "ME> SENDER MSG" form —
        // that's the simplest loop scenario.
        var firstToken = payload.split(/\s+/)[0];
        if (firstToken === info.from) return;
        // bypassSel: pack with empty selectedCall so the payload's own
        // routing (next-hop callsign or directed-cmd) survives intact.
        enqueueTx(payload, 'RELAY ' + info.from + '>', { bypassSel: true });
    }

    function scheduleChainFlushTimer() {
        if (S.chainFlushTimer) return;
        S.chainFlushTimer = setInterval(function () {
            var cutoff = Date.now() - 60000;
            var stale = [];
            S.chains.forEach(function (c, key) {
                if (c.lastUtc < cutoff) stale.push(key);
            });
            for (var i = 0; i < stale.length; ++i) closeChain(stale[i]);
            if (S.chains.size === 0) {
                clearInterval(S.chainFlushTimer);
                S.chainFlushTimer = null;
            }
        }, 5000);
    }

    function ingestDecode(d) {
        // PRESERVE the trailing whitespace on `rawText`: JSC packs an
        // `appendSpace` marker onto the last codeword of a word, so a data
        // frame that ended a word mid-message carries that space as the
        // last char of its decoded text. Trimming here strips that marker
        // and the next continuation frame ends up glued to this one —
        // e.g. "VK2ITM CIAO " + "DRW" → "VK2ITM CIAODRW", which then makes
        // the buffered-CRC strip miss the " DRW" tail and we forward the
        // whole stale CRC blob to the next hop. Use `text` (trimmed) for
        // parsing/dispatch only; use `rawText` for chain accumulation.
        var rawText = d.text || '';
        var text = rawText.trim();
        if (!text) return;
        // d.type is the raw 3-bit field carried in the JS8 frame:
        //   bit 0 = JS8CallFirst, bit 1 = JS8CallLast, bit 2 = JS8CallData.
        // Multi-frame messages mark the first frame with First and the
        // last with Last; the Data bit is only set in Fast/JS8 40/Slow/JS8 60
        // submodes (and is set on every frame of those, not just the
        // ends). A single-frame message has First+Last.
        var bits = (d.type | 0);
        var isFirst = (bits & JS8_BIT_FIRST) !== 0;
        var isLast  = (bits & JS8_BIT_LAST)  !== 0;
        // Record the sender BEFORE the ACK decision so pickAutoFreq sees
        // their freq as occupied and doesn't land on top of them. The
        // ACK itself is enqueued AFTER the HB feed entry is emitted (see
        // the end of this function) so the resulting feed order — newer
        // on top — shows the ACK above the HB it's replying to.
        var parsedHead = parseFrom(text);
        noteStation(parsedHead.from, d.freq, d.snr);
        // Find a chain to join. Frames from the same TX can drift several
        // Hz between the directed compound and the data frames (different
        // submodes' decoders, different timing windows), so a fixed-bin
        // bucket like Math.round(freq/50) breaks the chain whenever the
        // jitter crosses a bin boundary — e.g. 2222 Hz → bin 44 vs 2227 Hz
        // → bin 45 splits the relay's compound from its "CIAO" data frame.
        // Match instead by absolute Hz proximity: any chain whose key is
        // within FREQ_TOL Hz of this frame's freq counts as the same TX.
        var freq = d.freq || 0;
        // 40 Hz covers the few-Hz inter-frame jitter we actually see in
        // ALL.TXT logs (2222 vs 2227 was the smoking gun for the broken
        // chain). Stays comfortably below the ~50 Hz audio bandwidth of a
        // single JS8 Normal tone bundle so adjacent simultaneous TXs don't
        // accidentally merge into one chain.
        var FREQ_TOL = 40;
        var key = null;
        var bestDist = Infinity;
        S.chains.forEach(function (_c, k) {
            var dist = Math.abs(k - freq);
            if (dist < FREQ_TOL && dist < bestDist) {
                bestDist = dist;
                key = k;
            }
        });
        if (key === null) key = freq; // new chain, key = this frame's freq

        var parsed = parseFrom(text);
        // Preserve the trailing whitespace from the un-trimmed source: the
        // directed-compound decoder appends the cmd marker as trailing
        // space(s) (cmd " " for freetext, cmd "X" suffixes etc.), and the
        // JSC inter-word marker on data frames lives in the same trailing
        // slot. Trimming both during parsing strips the only separator we
        // had between consecutive chain parts. parsed.from / parsed.msg
        // (trimmed) are still used for dispatch decisions.
        var rawMsg;
        if (parsed.from) {
            var colon = rawText.indexOf(':');
            if (colon >= 0) {
                rawMsg = rawText.substr(colon + 1).replace(/^\s+/, '');
            } else {
                // No colon — bare-callsign form from a deCompound frame
                // with no grid. Strip the sender callsign and any
                // leading whitespace so this frame contributes an
                // empty payload to the chain (the trailing whitespace
                // after the callsign was JSC padding, not message
                // content).
                rawMsg = rawText.substr(parsed.from.length).replace(/^\s+/, '');
            }
        } else {
            rawMsg = rawText;
        }
        var now = Date.now();
        var chain = S.chains.get(key);

        if (isFirst || !chain) {
            // Open a new chain. If one was already open at this frequency,
            // its entry is already in the feed — just close it out and
            // make a new entry. Push the feed entry NOW (not at chain
            // close) so the user sees each frame as it's decoded; the
            // chain object holds a reference to that entry and mutates it
            // in place as continuations arrive.
            if (chain) closeChain(key);
            // Try to extract the directed target up front so the feed
            // can colour entries addressed to me. parseDirected only
            // succeeds on FrameDirected (frameType 3); for other frames
            // there's no target and the entry stays in the neutral
            // Monitor colour.
            var pdHead = (d.frameType === 3) ? parseDirected(text) : null;
            var entry = {
                t:      utcStamp(),
                from:   parsed.from,
                target: pdHead ? pdHead.target : '',
                cmd:    pdHead ? pdHead.cmd    : '',
                msg:    parsed.msg,
                snr:    d.snr,
                freq:   d.freq,
                fromMe: false,
            };
            emitFeedEntry(entry);
            // If this is a buffered directed command (>, MSG, QUERY, …),
            // record it on the chain so we can act when the chain closes
            // after the continuation data frames arrive.
            var bufferedInfo = null;
            if (pdHead && isBufferedDirectedCmd(pdHead.cmd)) {
                bufferedInfo = pdHead;
            }
            chain = {
                entry:    entry,
                parts:    [rawMsg],
                lastUtc:  now,
                buffered: bufferedInfo,
            };
            S.chains.set(key, chain);
        } else {
            // Continuation frame. JSC-encoded data frames carry their own
            // trailing space when more text follows, so concatenate with
            // no separator. A continuation may or may not have its own
            // "FROM: " preamble; keep the chain's original sender either
            // way. Use rawMsg (post-preamble strip but trailing whitespace
            // intact) so the JSC inter-word marker is preserved when we
            // join parts.
            chain.parts.push(rawMsg);
            chain.entry.msg  = chain.parts.join('');
            chain.entry.snr  = d.snr;
            chain.entry.freq = d.freq;
            chain.lastUtc    = now;
            renderFeed();
            if (S.activeTab !== 'monitor') renderQso();
        }

        if (isLast) {
            // Single-frame (First+Last) and last-of-multi-frame both land
            // here — the entry already shows the complete text. Run the
            // buffered-cmd processor first so a relay's re-TX lands above
            // the originating frame in the newest-on-top feed.
            // Diagnostic for the "12 34567" join bug: log multi-frame
            // chain parts on close so we can see whether the space comes
            // from a frame payload's trailing JSC marker or from somewhere
            // else. Single-frame messages skip the log to keep RX noise
            // down.
            if (chain && chain.parts && chain.parts.length > 1) {
                console.log('[JS8 RX chain]', JSON.stringify(chain.parts),
                            '→', JSON.stringify(chain.entry.msg));
            }
            // Recover the target for compound multi-frame messages.
            // When EITHER the sender OR the target isn't in JS8Call's
            // basecalls list (only @GROUP/0..@GROUP/9 with slashes and
            // a small set of plain calls are), JS8 emits a compound
            // chain instead of a single FrameDirected:
            //   compound-sender base+compound-target (e.g. K1FM → @ALLCALL)
            //     frame 1: compound directed     → "@ALLCALL  " (no FROM:)
            //     frame 2: data                  → "PAYLOAD"
            //   compound-sender (e.g. VK2ITM/P → @GROUP1)
            //     frame 1: compound de-compound  → "VK2ITM/P: "
            //     frame 2: compound directed     → "@GROUP1  "
            //     frame 3: data                  → "PAYLOAD"
            // emitFeedEntry on the head sees entry.target='' because
            // parseDirected requires the standard "FROM: TARGET …"
            // shape — the message stays in Monitor with no QSO tab.
            // By isLast we have the joined msg; extract the target from
            // its first token and re-route.
            if (chain && chain.entry && !chain.entry.target) {
                var fullMsg = chain.entry.msg || '';
                var tm = fullMsg.match(/^(@?[A-Z0-9\/]{2,})(\s|$)/);
                // Only promote the first token to "target" if it actually
                // looks like a routable address: an @-group, or a real
                // callsign (must contain a digit per ITU). Otherwise this
                // is a partial decode whose head frame we missed — the
                // tail starts with a plain word like "IS ALAIN" and
                // promoting "IS" would render in Monitor as a misleading
                // sender→target pill.
                var candidate = tm ? tm[1] : null;
                var looksLikeTarget = candidate && (
                    candidate.charAt(0) === '@' ||
                    (/\d/.test(candidate) && candidate.length >= 3)
                );
                if (looksLikeTarget) {
                    chain.entry.target = candidate;
                    var myCallNow = getMyCall();
                    var fromCall = chain.entry.from || '';
                    // Drop our own echo (only checkable when we have
                    // a real sender), drop relay forwards (`>`), and
                    // drop any `from === @something` (system traffic).
                    var notEcho   = !myCallNow || !fromCall || fromCall !== myCallNow;
                    var notSysFrm = !fromCall || fromCall.charAt(0) !== '@';
                    if (notSysFrm && chain.entry.cmd !== '>'
                        && !isHeartbeatCmd(chain.entry.cmd) && notEcho) {
                        if (myCallNow && chain.entry.target === myCallNow && fromCall) {
                            routeEntryToQso(chain.entry, fromCall);
                        } else if (chain.entry.target.charAt(0) === '@' &&
                                   (chain.entry.target === '@ALLCALL' ||
                                    S.qsos.has(chain.entry.target))) {
                            // Unknown sender (base-to-@ALLCALL case
                            // drops the de-compound frame) — show '?'
                            // so the group bubble doesn't read blank.
                            if (!chain.entry.from) chain.entry.from = '?';
                            routeEntryToQso(chain.entry, chain.entry.target);
                        }
                    }
                }
            }
            if (chain && chain.buffered &&
                chain.buffered.target === getMyCall()) {
                processBufferedDirected(chain);
            }
            // Hide the CRC tag from the feed: buffered-cmd messages
            // arrive with a trailing "<space><3-char CRC>" that's noise
            // to the user. Only strip if the CRC actually validates so
            // we don't accidentally truncate a real 3-char message tail.
            if (chain && chain.buffered) {
                var cleaned = stripValidChecksum(chain.entry.msg || '',
                                                 chain.buffered.cmd);
                if (cleaned !== chain.entry.msg) {
                    chain.entry.msg = cleaned;
                    renderFeed();
                    if (S.activeTab !== 'monitor') renderQso();
                }
            }
            closeChain(key);
        } else {
            scheduleChainFlushTimer();
        }

        // ACK / auto-respond *after* the originating frame has been
        // pushed to the feed so the newer reply lands above it in the
        // newest-on-top view.
        if (d.frameType === 0) {
            maybeAckHb(d, parsedHead.from);
        }
        maybeAutoRespond(d);
    }

    // QSO-pause gate: true while we should hold all non-essential
    // auto-TX (HB beacon, HB-ACKs to third parties, auto-replies to
    // anyone other than the QSO partner). Two signals trigger it:
    //   (1) a multi-frame inbound chain is being assembled — TXing now
    //       would step on the very thing we're trying to receive
    //   (2) we recently sent a directed query and the reply window is
    //       still open (capped so a missed reply doesn't deadlock us)
    var QSO_PENDING_MS = 120 * 1000;   // 2 min — ~4 Normal slots
    function inActiveQso() {
        if (S.chains && S.chains.size > 0) return true;
        if (S.qsoPendingAt && (Date.now() - S.qsoPendingAt) < QSO_PENDING_MS) {
            return true;
        }
        return false;
    }

    // ─── HB network scheduler + ACK responder ─────────────────────────
    // Auto-broadcast a heartbeat every hbInterval minutes when HB
    // Network is on. Quiet during TX, no callsign means nothing to send.
    function maybeFireHb() {
        if (!S.hbNet || !S.rxEnabled || S.txBusy) return;
        if (S.txQueue.length > 0) return;
        // Hold scheduled HB while we're mid-QSO — same TX-collision
        // logic as HB-ACK below.
        if (inActiveQso()) return;
        var myCall = getMyCall();
        if (!myCall) return;
        var now = Date.now();
        var intervalMs = (S.hbInterval || 15) * 60 * 1000;
        if (now - (S.lastHbTx || 0) < intervalMs) return;
        S.lastHbTx = now;
        // Wire form must match JS8Call's HB button exactly so other
        // clients recognize it as a heartbeat and auto-ACK. JS8Call
        // emits `@HB HEARTBEAT <GRID4>` (compound directed to @HB,
        // cmd = HEARTBEAT, extra = 4-char grid). A bare "HB EM78"
        // string pack()s as plain text and looks like nothing to the
        // network. Grid is clamped to 4 chars per JS8Call convention.
        var grid4 = (getMyGridLocal() || '').slice(0, 4);
        var text = grid4 ? ('@HB HEARTBEAT ' + grid4) : '@HB HEARTBEAT';
        // bypassSel: HB is a broadcast (@HB on air) — must never be
        // re-targeted to whatever's in the TO field. Without this, if
        // the user is sitting in an @GROUP1 tab, pack() would prepend
        // "@GROUP1 " and the resulting frame stops looking like an HB
        // to other stations' decoders.
        enqueueTx(text, 'HB auto', { hb: true, bypassSel: true, target: '@HB' });
    }

    // Recognized directed commands, longest first so multi-word forms
    // match before their substrings (e.g. "HEARTBEAT SNR" before "SNR",
    // "QUERY MSGS?" before "QUERY").
    var JS8_DIRECTED_CMDS = [
        'HEARTBEAT SNR', 'QUERY MSGS?', 'QUERY MSGS', 'QUERY CALL',
        'HW CPY?', 'MSG TO:', 'DIT DIT', 'HEARING?', 'STATUS?',
        'GRID?', 'INFO?', 'SNR?', 'QSL?', 'AGN?',
        'HEARTBEAT', 'HEARING', 'STATUS', 'QUERY', 'NACK',
        'GRID', 'INFO', 'CMD', 'MSG', 'ACK', 'SNR', 'QSL',
        'YES', 'RR', 'SK', 'FB', 'HB', 'NO', '73', '>'
    ];

    // Parse a decoded message into { from, target, cmd, args }. Returns
    // null if the text isn't a directed message. `>` is the relay marker
    // and lives glued to the target callsign (e.g. "VK2ITM>"), so it's
    // handled before the space-delimited scan.
    function parseDirected(text) {
        var p = parseFrom(text);
        if (!p.from) return null;
        var rest = p.msg;
        var tm = rest.match(/^(@?[A-Z0-9\/]{2,})(.*)$/);
        if (!tm) return null;
        var target = tm[1];
        var after  = tm[2];
        if (after.charAt(0) === '>') {
            return { from: p.from, target: target, cmd: '>', args: after.substr(1).trim() };
        }
        for (var i = 0; i < JS8_DIRECTED_CMDS.length; ++i) {
            var c = JS8_DIRECTED_CMDS[i];
            if (c === '>') continue;
            if (after.startsWith(' ' + c)) {
                var tail = after.substr(1 + c.length);
                if (tail === '' || tail.charAt(0) === ' ') {
                    return { from: p.from, target: target, cmd: c, args: tail.trim() };
                }
            }
        }
        return { from: p.from, target: target, cmd: '', args: after.trim() };
    }

    // Auto-reply to a directed command sent to mycall. Mirrors the JS8Call
    // autoreply set (SNR?, NACK, HEARING?, GRID?, STATUS?, MSG, MSG TO:,
    // QUERY, QUERY MSGS?, QUERY CALL, ACK, INFO?, AGN?) — wfweb implements
    // the simple ones (SNR?, GRID?, INFO?, STATUS?, HEARING?, AGN?, QSL?).
    // MSG / QUERY MSGS / QUERY CALL / CMD need a message store, deferred.
    function maybeAutoRespond(d) {
        if (!S.autoRespond) return;
        if (d.frameType !== 3) return;          // FrameDirected only
        var parsed = parseDirected(d.text || '');
        if (!parsed || !parsed.cmd) return;
        var myCall = getMyCall();
        if (!myCall) return;
        if (parsed.target !== myCall) return;   // only when directed at us
        if (parsed.from   === myCall) return;   // don't reply to ourselves
        // Mid-QSO: only continue talking to the partner. A query from
        // a different station gets dropped on the floor — they'll
        // re-send if it mattered. Without this, a stray INFO? in the
        // middle of a slow multi-frame reply turns into a TX that
        // collides with the partner's continuation.
        if (inActiveQso() && parsed.from !== S.qsoPendingPeer) return;
        // 30 s per-(from, cmd) cooldown — covers the case where a remote
        // re-sends the same query while we're still TXing the reply.
        var ckey = parsed.from + ':' + parsed.cmd;
        var now  = Date.now();
        var prev = S.lastAutoReply.get(ckey) || 0;
        if (now - prev < 30 * 1000) return;

        var replyText = null;
        var label = 'AUTO ' + parsed.cmd;
        var fromCall = parsed.from;
        if (parsed.cmd === 'SNR?') {
            if (typeof d.snr !== 'number') return;
            var snr = Math.max(-31, Math.min(31, Math.round(d.snr)));
            replyText = fromCall + ' SNR ' + (snr >= 0 ? '+' : '') + snr;
        } else if (parsed.cmd === 'GRID?') {
            var g = getMyGridLocal();
            if (!g) return;
            replyText = fromCall + ' GRID ' + g;
        } else if (parsed.cmd === 'INFO?') {
            var info = (S.infoText || '').trim();
            if (!info) return;
            replyText = fromCall + ' INFO ' + info;
        } else if (parsed.cmd === 'STATUS?') {
            var st = (S.statusText || '').trim();
            if (!st) return;
            replyText = fromCall + ' STATUS ' + st;
        } else if (parsed.cmd === 'HEARING?') {
            var heard = [];
            S.stations.forEach(function (s, call) {
                if (call.charAt(0) === '@') return;
                if (call === myCall || call === fromCall) return;
                heard.push(call);
            });
            // 5 calls keeps the reply under one Normal frame's worth of
            // packed chars; older entries fall off naturally.
            if (heard.length === 0) return;
            replyText = fromCall + ' HEARING ' + heard.slice(0, 5).join(' ');
        } else if (parsed.cmd === 'AGN?') {
            if (!S.lastTxText) return;
            replyText = S.lastTxText;
            label = 'AUTO AGN';
        } else if (parsed.cmd === 'QSL?') {
            replyText = fromCall + ' QSL';
        } else {
            return;   // unsupported cmd → no reply
        }
        S.lastAutoReply.set(ckey, now);
        // bypassSel: the reply wire already starts with the sender's
        // callsign (e.g. "IZ6BYY INFO IC-7300"). If pack() were to
        // hand buildMessageFrames a non-empty selectedCall (= S.dxCall,
        // which is whatever tab the operator happens to be sitting in
        // — often @GROUP1 or another peer), the @-group case would
        // bake "@GROUP1 " in front of the wire and the reply lands in
        // the wrong channel. Same bug pattern that broke HB ACK and
        // relay forwarding.
        // Route the auto-reply into the QSO with the original sender so
        // the chat reads naturally: their query above, our auto-reply
        // below, both threaded under the same peer tab.
        enqueueTx(replyText, label, { qsoPeer: fromCall, bypassSel: true });
    }

    // Called from ingestDecode when a Heartbeat frame arrives. If HB
    // ACK is enabled and we haven't recently ACKed this station, queue
    // a directed SNR report. Cooldown prevents ACK storms on a busy
    // band — once per station per 10 minutes is plenty.
    function maybeAckHb(d, fromCall) {
        if (!S.hbAck) return;
        if (!fromCall || fromCall.charAt(0) === '@') return;
        if (fromCall === getMyCall()) return;
        // Mid-QSO: an unsolicited HB from a third party would TX over
        // our partner's reply. Drop the courtesy ACK — they'll re-HB
        // in a few minutes and we'll catch them then.
        if (inActiveQso()) return;
        if (typeof d.snr !== 'number') return;
        var now = Date.now();
        var prev = S.lastAckTo.get(fromCall) || 0;
        var cooldownMs = (S.hbAckCooldown || 2) * 60 * 1000;
        if (now - prev < cooldownMs) return;
        S.lastAckTo.set(fromCall, now);
        // Format SNR per JS8Call convention: signed dB clipped to ±31
        // (the codec only carries 5 bits + sign).
        var snr = Math.max(-31, Math.min(31, Math.round(d.snr)));
        var snrStr = (snr >= 0 ? '+' : '') + snr;
        // " HEARTBEAT SNR " (cmd 29) is the canonical JS8Call HB-ACK form,
        // distinct from plain " SNR " (cmd 25, a general SNR report). The
        // packed-bit layout flags it via the second bit of the SNR-encoded
        // command byte (see Varicode::packCmd in Varicode.cpp).
        //
        // bypassSel: the payload encodes its own target ("K1FM HEARTBEAT
        // SNR +5"). If pack() were to hand buildMessageFrames a non-empty
        // selectedCall (= S.dxCall, often a different peer the user was
        // chatting with), AUTO_PREPEND_DIRECTED reshuffles the payload —
        // same bug pattern that broke relay forwarding. Result on-air is
        // a malformed frame that no one decodes as a valid HB ACK.
        //
        // No qsoPeer: an HB-ACK is a one-shot courtesy reply to network
        // housekeeping, not a conversation. The triggering HB lives in
        // Monitor (its target is @HB, never us), so routing our ACK to
        // a per-peer tab would be asymmetric — and would open a fresh
        // chat tab for every station we ever ACK. The ACK still shows
        // in Monitor as a from-me row.
        enqueueTx(fromCall + ' HEARTBEAT SNR ' + snrStr, 'HB ACK',
                  { hb: true, bypassSel: true, target: fromCall });
    }

    // ─── TX pipeline ──────────────────────────────────────────────────
    function pack(text) {
        var myCall = getMyCall();
        if (!myCall) { alert('Set your callsign first (gear icon).'); return null; }
        var myGrid = getMyGridLocal();
        var dx = (S.dxCall || '').trim();
        // Real callsigns ride in selectedCall — buildMessageFrames'
        // AUTO_PREPEND_DIRECTED prepends them to form a directed
        // compound. @-prefixed groups (@ALLCALL, @GROUP1, ...) must be
        // baked into the text instead and selectedCall left empty:
        // buildMessageFrames won't synthesize a compound for a group
        // in selectedCall, which is why @ALLCALL sends previously went
        // nowhere — the target was simply dropped on the floor.
        var sel = (dx && dx.charAt(0) !== '@') ? dx : '';
        var packText = text;
        if (dx && dx.charAt(0) === '@') {
            // Don't double-prefix when the caller already prepended
            // the group (e.g. SEND inside a @GROUP1 tab builds
            // "@GROUP1 HELLO" itself).
            var dxEsc = dx.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            if (!new RegExp('^' + dxEsc + '\\b').test(text)) {
                packText = dx + ' ' + text;
            }
        }
        return js8.pack(myCall, myGrid, sel, packText, S.submode);
    }

    // Pick an audio TX frequency that avoids stations we've recently
    // decoded. HB traffic stays in the 500–1000 Hz sub-channel per JS8Call
    // convention; everything else goes in the main 1000–2500 Hz region.
    // We scan the chosen band in 50 Hz steps and pick the frequency
    // furthest from any station heard in the last 5 minutes. If no one
    // has been heard, fall back to the middle of the band — that's the
    // "what a reasonable user would pick" default.
    // crypto-strong random uint32. Math.random is process-local in V8,
    // but using crypto removes any lingering doubt about correlated PRNG
    // state between simultaneously-launched browser instances.
    function randU32() {
        if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
            var a = new Uint32Array(1);
            crypto.getRandomValues(a);
            return a[0];
        }
        return (Math.random() * 0x100000000) >>> 0;
    }

    function pickAutoFreq(isHb) {
        var lo = isHb ? 500  : 1000;
        var hi = isHb ? 1000 : 2500;
        var margin = 50;          // keep tones away from the band edge
        var step = 25;            // snap to 25 Hz grid
        var minSep = 80;          // ≥ 80 Hz from any heard station
        var now = Date.now();
        var occupied = [];
        S.stations.forEach(function (s) {
            if (typeof s.freq === 'number' && s.freq >= 200 && s.freq <= 2800 &&
                (now - (s.lastUtc || 0)) < 5 * 60 * 1000) {
                occupied.push(s.freq);
            }
        });
        function dist(f) {
            var d = Infinity;
            for (var i = 0; i < occupied.length; ++i) {
                var dd = Math.abs(f - occupied[i]);
                if (dd < d) d = dd;
            }
            return d;
        }
        var loCand = lo + margin;
        var hiCand = hi - margin;
        var rangeSteps = Math.floor((hiCand - loCand) / step) + 1;
        // Try random picks until one is clear. With ~17 candidate slots
        // on the HB band, 16 attempts and minSep=80 cover any band
        // density up to fully crowded.
        for (var t = 0; t < 32; ++t) {
            var k = randU32() % rangeSteps;
            var f = loCand + k * step;
            if (dist(f) >= minSep) return f;
        }
        // Crowded band — fall back to the freq furthest from any heard
        // station so we at least pick the least-bad slot.
        var best = (lo + hi) / 2, bestD = -1;
        for (var f2 = loCand; f2 <= hiCand; f2 += step) {
            var d2 = dist(f2);
            if (d2 > bestD) { bestD = d2; best = f2; }
        }
        return best;
    }

    function setTxFreq(hz) {
        S.baseHz = hz;
        var el = document.getElementById('js8TxFreq');
        if (el && document.activeElement !== el) el.value = hz;
    }

    function enqueueTx(text, label, opts) {
        opts = opts || {};
        var frames;
        if (opts.bypassSel) {
            // Relay path: the payload encodes its own routing
            // (next-hop + cmd or final-target + text). We must NOT pass
            // S.dxCall as selectedCall, because AUTO_PREPEND_DIRECTED in
            // buildMessageFrames would munge the payload (e.g. silently
            // turn "B> C HELLO" into "K1FM B> C HELLO" if dxCall=K1FM),
            // which then fails packDirectedMessage and returns 0 frames.
            var myCall = getMyCall();
            if (!myCall) { alert('Set your callsign first (gear icon).'); return null; }
            frames = js8.pack(myCall, getMyGridLocal(), '', text, S.submode);
        } else {
            frames = pack(text);
        }
        if (!frames || frames.length === 0) {
            console.warn('[JS8] pack returned no frames for "' + text + '"');
            return null;
        }
        // Decide the audio freq for THIS transmission. The user can
        // override by editing the TX freq box (Auto Freq turned off, or
        // we pick auto then they type a custom value before the slot
        // fires). Each frame carries its own baseHz so back-to-back
        // mixed traffic could in theory hop, though the current code
        // drains them all at once.
        var hz = S.autoFreq ? pickAutoFreq(!!opts.hb) : S.baseHz;
        setTxFreq(hz);
        var entryId = 'tx-' + (++S.txIdCounter);
        for (var i = 0; i < frames.length; i++) {
            frames[i].baseHz = hz;
            frames[i].entryId = entryId;
            S.txQueue.push(frames[i]);
        }
        // Capture an inline label as a CMD chip on the QSO bubble (e.g.
        // "AUTO SNR?", "HB ACK", "RELAY → VK2ITM"). Falls back to nothing
        // for plain SEND so the bubble reads as bare message text.
        // target drives the address pill in the Monitor feed. qsoPeer
        // also opens/focuses a chat tab; opts.target is the
        // display-only flavour for broadcasts (CQ → @ALLCALL, HB →
        // @HB, HB-ACK → original sender) that don't want a tab opened.
        var entry = {
            id: entryId,
            t: utcStamp(),
            from: getMyCall(),
            target: opts.qsoPeer || opts.target || '',
            cmd: label || '',
            msg: text,
            label: label || '',
            fromMe: true,
            framesTotal: frames.length,
            framesSent: 0,
            txStatus: 'queued',
        };
        S.feed.unshift(entry);
        if (S.feed.length > S.feedMax) S.feed.length = S.feedMax;
        // Remember the last user-content TX so we can re-send it on AGN?.
        // HB beacons and auto-replies shouldn't be re-sent — they're
        // self-generated and not "the message" the remote is asking us
        // to repeat.
        var isAuto = !!opts.hb || /^AUTO /.test(label || '');
        if (!isAuto) {
            S.lastTxText = text;
        }
        // Arm the QSO-pause window when the user (not an auto-reply)
        // directs a message at a real peer. Hold any subsequent
        // unsolicited auto-TX until the partner finishes replying or
        // QSO_PENDING_MS expires.
        if (!isAuto && opts.qsoPeer && opts.qsoPeer.charAt(0) !== '@') {
            S.qsoPendingPeer = opts.qsoPeer;
            S.qsoPendingAt   = Date.now();
        }
        // Auto-open/focus a QSO tab when the caller declared a peer.
        // Plumbed through opts (qsoPeer + optional relay qsoPath) by
        // callers that know they're starting/continuing a chat — Send
        // button while a QSO tab is active, quickcmd to a real call,
        // auto-replies, HB ACK, Relay modal, group sends, and @ALLCALL
        // sends (so the operator sees their own broadcast in the
        // @ALLCALL tab next to incoming broadcasts).
        // CQ/HB broadcasts and relay-forwarding (we're an intermediate
        // hop) don't pass qsoPeer at all, so they skip naturally.
        if (opts.qsoPeer) {
            routeEntryToQso(entry, opts.qsoPeer, { path: opts.qsoPath });
            // User-driven TX auto-focuses the tab so they see their
            // message land. Auto-replies/ACKs skip this so a back-
            // ground HB ACK doesn't yank focus away from Monitor.
            if (!opts.hb && !/^AUTO /.test(opts.label || label || '')) {
                if (S.activeTab !== opts.qsoPeer) setActiveTab(opts.qsoPeer);
            }
        }
        renderFeed();
        console.log('[JS8 TX] queued ' + frames.length + ' frame(s) @ '
            + hz + 'Hz: "' + text + '"');
        return entry;
    }

    function findFeedEntryById(id) {
        for (var i = 0; i < S.feed.length; i++) {
            if (S.feed[i].id === id) return S.feed[i];
        }
        return null;
    }

    function fireNextTxFrame() {
        if (S.txQueue.length === 0) return;
        // Drain the whole queue into one continuous audio buffer so the
        // frames go out in consecutive slots (back-to-back), not every
        // other slot. The official JS8Call client behaves this way; JS8
        // has no odd/even alternation like FT8.
        var taken = S.txQueue.splice(0, S.txQueue.length);
        // Bin frames by feed-entry, preserving queue order, so the
        // progress timer can advance one entry's "frames sent" counter at
        // a time as the audio plays out.
        var entryGroups = [];
        var byId = new Map();
        for (var gi = 0; gi < taken.length; gi++) {
            var fid = taken[gi].entryId;
            var ent = byId.get(fid);
            if (!ent) {
                var fe = findFeedEntryById(fid);
                if (!fe) continue;
                ent = { entry: fe, count: 0 };
                entryGroups.push(ent);
                byId.set(fid, ent);
            }
            ent.count++;
        }
        var buffers = [];
        for (var bi = 0; bi < taken.length; bi++) {
            var f = taken[bi];
            var tones = js8.encode(f.type, f.frame, S.submode);
            if (!tones) { console.error('[JS8 TX] encode failed for', f); continue; }
            var hz = (typeof f.baseHz === 'number') ? f.baseHz : S.baseHz;
            buffers.push(synthesize(tones, { submode: S.submode, baseHz: hz }));
        }
        if (buffers.length === 0) return;
        var total = buffers.reduce(function (s, b) { return s + b.length; }, 0);
        var combined = new Float32Array(total);
        var off = 0;
        for (var ci = 0; ci < buffers.length; ++ci) {
            combined.set(buffers[ci], off);
            off += buffers[ci].length;
        }
        var totalFrames = buffers.length;
        var slotMs = getSubmode(S.submode).slotSeconds * 1000;
        console.log('[JS8 TX] firing ' + totalFrames + ' frame(s) = '
            + (total / 12000).toFixed(1) + 's of audio');
        S.txBusy = true;
        setStatus('TX', 'tx');
        if (typeof digiTxActive !== 'undefined') digiTxActive = true;
        // Arm waterfall TX loopback (same mechanism FT8 uses — tickTimer
        // drains digiTxWfSamples into wfBuf so the user sees their own
        // signal on the JS8 waterfall during transmit).
        if (typeof digiTxWfSamples !== 'undefined') {
            digiTxWfSamples = combined;
            digiTxWfPos = 0;
        }
        if (typeof send === 'function') send({ cmd: 'enableMic', value: true });

        var startMs = 0;
        function tickProgress() {
            if (!startMs) return;
            var elapsed = Date.now() - startMs;
            var done = Math.min(totalFrames, Math.floor(elapsed / slotMs));
            var rem = done;
            for (var i = 0; i < entryGroups.length; i++) {
                var g = entryGroups[i];
                var sent = Math.min(rem, g.count);
                g.entry.framesSent = sent;
                if (sent >= g.count)      g.entry.txStatus = 'done';
                else if (sent > 0)        g.entry.txStatus = 'tx';
                else if (rem === 0 && firstUnfinished(entryGroups) === i)
                                          g.entry.txStatus = 'tx';
                else                      g.entry.txStatus = 'queued';
                rem -= sent;
            }
            updateTxRowsDom();
        }
        function firstUnfinished(arr) {
            for (var i = 0; i < arr.length; i++)
                if (arr[i].entry.framesSent < arr[i].count) return i;
            return -1;
        }

        S.activeTx = { entries: entryGroups, totalFrames: totalFrames,
                       slotMs: slotMs, timer: null };

        streamDigiAudio(
            combined,
            function (aborted) {
                if (S.activeTx && S.activeTx.timer) {
                    clearInterval(S.activeTx.timer);
                }
                // Settle final state on every entry. Aborted entries keep
                // their last framesSent count (so the bar reflects what
                // actually went out), but get the red 'aborted' badge if
                // they had unsent frames remaining.
                for (var i = 0; i < entryGroups.length; i++) {
                    var g = entryGroups[i];
                    if (aborted) {
                        if (g.entry.framesSent < g.count)
                            g.entry.txStatus = 'aborted';
                        else
                            g.entry.txStatus = 'done';
                    } else {
                        g.entry.framesSent = g.count;
                        g.entry.txStatus = 'done';
                    }
                }
                S.activeTx = null;
                S.txBusy = false;
                if (typeof digiTxActive !== 'undefined') digiTxActive = false;
                if (typeof digiTxWfSamples !== 'undefined') digiTxWfSamples = null;
                send({ cmd: 'setPTT', value: false });
                setStatus(S.rxEnabled ? 'RX' : 'IDLE', S.rxEnabled ? 'rx' : '');
                renderFeed();
            },
            function () {
                send({ cmd: 'setPTT', value: true });
                startMs = Date.now();
                if (S.activeTx) S.activeTx.timer = setInterval(tickProgress, 200);
            }
        );
    }

    // ─── QSO tab model ────────────────────────────────────────────────
    // A "QSO" is one open conversation with a peer. The tab strip shows
    // Monitor (always present) + one pill per QSO. Sending while a QSO
    // tab is active routes via that QSO's stored relay path; sending
    // from Monitor uses the TO field as before. Messages live on the
    // QSO object (same entry references as the global feed) so render
    // is just iterating qso.messages, not filtering S.feed every paint.
    function ensureQso(peer, path) {
        if (!peer) return null;
        // @-groups (@ALLCALL, @SAR, @ARES, @GROUP1, ...) open their own
        // multi-party tab where the per-message sender is shown next
        // to each RX bubble (vs. individual chats where the sender is
        // implicit from the tab header). Monitor stays the catch-all
        // view of every decode; the @ALLCALL tab is the subset
        // explicitly addressed to all stations.
        var q = S.qsos.get(peer);
        if (!q) {
            q = { peer: peer, path: path || [], messages: [],
                  unread: 0, lastUtc: Date.now(), lastSnr: null,
                  isGroup: peer.charAt(0) === '@' };
            S.qsos.set(peer, q);
            S.tabOrder.push(peer);
            renderTabs();
        } else if (path && path.length && !q.path.length) {
            // Promote an empty-path QSO once we observe a relay chain
            // — first message was direct, follow-up arrived via relay.
            q.path = path;
        }
        return q;
    }

    function routeEntryToQso(entry, peer, opts) {
        var q = ensureQso(peer, opts && opts.path);
        if (!q) return;
        q.messages.push(entry);
        q.lastUtc = Date.now();
        if (typeof entry.snr === 'number') q.lastSnr = entry.snr;
        // Unread badge bumps only when the message is RX and the tab is
        // not the one currently shown.
        if (!entry.fromMe && S.activeTab !== peer) q.unread++;
        // QSO-pause release: the partner just spoke back. Drop the hold
        // so subsequent unrelated HBs / queries can be auto-handled
        // again without waiting for the 2-min cap.
        if (!entry.fromMe && peer === S.qsoPendingPeer) {
            S.qsoPendingPeer = '';
            S.qsoPendingAt   = 0;
        }
        if (S.activeTab === peer) renderQso();
        renderTabs();
    }

    function setActiveTab(name) {
        S.activeTab = name || 'monitor';
        var feedEl = document.getElementById('js8Feed');
        var qsoEl  = document.getElementById('js8QsoView');
        if (S.activeTab === 'monitor') {
            feedEl.classList.remove('hidden');
            qsoEl.classList.add('hidden');
            document.body.classList.remove('js8-qso-tab');
        } else {
            feedEl.classList.add('hidden');
            qsoEl.classList.remove('hidden');
            document.body.classList.add('js8-qso-tab');
            var q = S.qsos.get(S.activeTab);
            if (q) { q.unread = 0; }
        }
        // Sync TO input + dxCall to the active peer for compose context.
        if (S.activeTab !== 'monitor') {
            S.dxCall = S.activeTab;
            var dxEl = document.getElementById('js8DxCall');
            if (dxEl) dxEl.value = S.dxCall;
        }
        renderTabs();
        renderQso();
        renderStations();
    }

    function closeQso(peer) {
        if (!S.qsos.has(peer)) return;
        S.qsos.delete(peer);
        var ix = S.tabOrder.indexOf(peer);
        if (ix >= 0) S.tabOrder.splice(ix, 1);
        if (S.activeTab === peer) setActiveTab('monitor');
        else { renderTabs(); }
    }

    // Render the relay path as a sequence of pills:
    //   <ME> → <HOP1> → <HOP2> → <PEER>
    // For a direct QSO (no relay) it's just <ME> → <PEER>. The HTML is
    // safe-escaped; callsigns come from local state only but cheap to
    // be defensive.
    function pathHtml(peer, path) {
        var me = escapeHtml(getMyCall() || 'ME');
        var html = '<span class="hop me">' + me + '</span>';
        var arrow = '<span class="arrow">→</span>';
        if (path && path.length) {
            for (var i = 0; i < path.length; ++i) {
                html += arrow + '<span class="hop">' + escapeHtml(path[i]) + '</span>';
            }
        }
        html += arrow + '<span class="hop peer">' + escapeHtml(peer) + '</span>';
        return html;
    }

    function renderTabs() {
        var el = document.getElementById('js8Tabs');
        if (!el) return;
        var html = '';
        // Monitor tab — always first, no close button.
        var monActive = S.activeTab === 'monitor' ? ' active' : '';
        html += '<div class="js8-tab' + monActive + '" data-tab="monitor">'
             +  '<span>Monitor</span></div>';
        for (var i = 0; i < S.tabOrder.length; ++i) {
            var peer = S.tabOrder[i];
            var q = S.qsos.get(peer);
            if (!q) continue;
            var cls = 'js8-tab' + (S.activeTab === peer ? ' active' : '');
            html += '<div class="' + cls + '" data-tab="' + escapeHtml(peer) + '">'
                 +  '<span>' + escapeHtml(peer) + '</span>';
            if (q.unread > 0) {
                html += '<span class="js8-tab-unread">' + q.unread + '</span>';
            }
            html += '<button class="js8-tab-close" data-close="'
                 +  escapeHtml(peer) + '" title="Close">×</button>'
                 +  '</div>';
        }
        el.innerHTML = html;
    }

    function renderQso() {
        var headerEl = document.getElementById('js8QsoPath');
        var msgsEl   = document.getElementById('js8QsoMessages');
        if (!headerEl || !msgsEl) return;
        if (S.activeTab === 'monitor') return;
        var q = S.qsos.get(S.activeTab);
        if (!q) { setActiveTab('monitor'); return; }
        headerEl.innerHTML = pathHtml(q.peer, q.path);
        if (q.messages.length === 0) {
            msgsEl.innerHTML = '<div class="js8-qso-empty">Type a message below to start the QSO with '
                + escapeHtml(q.peer) + '.</div>';
            return;
        }
        // Width-capped inner column keeps RX/TX bubbles close together
        // on wide windows — flex-end on a 100%-wide parent would push
        // TX bubbles to the far right edge.
        var html = '<div class="js8-thread">';
        var peerPrefix = q.peer + ' ';
        var myCall = getMyCall();
        for (var i = 0; i < q.messages.length; ++i) {
            var m = q.messages[i];
            var dirCls = m.fromMe ? 'tx' : 'rx';
            html += '<div class="js8-bubble ' + dirCls + '" '
                 +  (m.id ? 'data-id="' + escapeHtml(m.id) + '"' : '') + '>';
            // Group tabs: show the sender at the top of each RX bubble
            // because multiple stations participate. Individual tabs:
            // sender is implicit from the tab header — no per-bubble
            // sender. TX bubbles never need a sender (it's me).
            if (q.isGroup && !m.fromMe && m.from) {
                html += '<span class="js8-bubble-from">'
                     +  escapeHtml(m.from) + '</span>';
            }
            if (m.cmd) {
                html += '<span class="cmd-chip">' + escapeHtml(m.cmd) + '</span>';
            }
            // Strip the redundant callsign prefix at the start of the
            // on-air payload. The wire form always starts with the
            // *target*: peer/group on TX, myCall on RX (individual),
            // group on RX (group tab). Strip whichever applies so the
            // bubble reads as a plain message body.
            var body = m.msg || '';
            var stripPrefix;
            if (m.fromMe) {
                stripPrefix = peerPrefix;
            } else if (q.isGroup) {
                stripPrefix = peerPrefix;
            } else {
                stripPrefix = myCall ? myCall + ' ' : '';
            }
            if (stripPrefix && body.indexOf(stripPrefix) === 0) {
                body = body.substr(stripPrefix.length);
                // Compound directed frames decode with a double trailing
                // space ("@GROUP1  ") — after stripping one of those the
                // body would carry a leading space. Drop any remaining
                // leading whitespace so the bubble reads cleanly.
                body = body.replace(/^\s+/, '');
            }
            // If the remaining body is identical to the cmd chip, drop
            // it — quickcmd sends like `PEER 73` would otherwise read
            // as a duplicated "73 · 73" pair.
            if (m.cmd && body === m.cmd) body = '';
            html += escapeHtml(body);
            var metaParts = [];
            if (m.t) metaParts.push(m.t);
            if (typeof m.snr === 'number' && !m.fromMe) {
                metaParts.push((m.snr >= 0 ? '+' : '') + m.snr + ' dB');
            }
            if (m.fromMe && m.framesTotal) {
                var st = m.txStatus || 'queued';
                metaParts.push(txBadgeLabel(st, m.framesSent, m.framesTotal));
            }
            html += '<span class="meta">' + escapeHtml(metaParts.join(' · ')) + '</span>';
            html += '</div>';
        }
        html += '</div>';
        msgsEl.innerHTML = html;
        // Always show the latest message — scroll to bottom.
        msgsEl.scrollTop = msgsEl.scrollHeight;
    }

    // ─── Rendering ────────────────────────────────────────────────────
    function txBadgeLabel(status, sent, total) {
        var label = status === 'queued'  ? 'QUEUED'
                  : status === 'tx'      ? 'TX'
                  : status === 'aborted' ? 'STOPPED'
                  :                        'SENT';
        return label + ' ' + (sent || 0) + '/' + total;
    }

    function renderFeed() {
        var el = document.getElementById('js8Feed');
        if (!el) return;
        var myCall = getMyCall();
        var html = '';
        for (var i = 0; i < S.feed.length; i++) {
            var f = S.feed[i];
            var rowId = f.id ? ' data-id="' + escapeHtml(f.id) + '"' : '';
            // Theme classes: sent = red (.from-me), directed at me = green
            // (.to-me). Mutually exclusive — a TX I originated never lands
            // here as to-me. We only flag .to-me when the parsed target
            // matches our own callsign, not for @ALLCALL/@HB.
            var cls = 'js8-feed-row';
            if (f.fromMe) cls += ' from-me';
            else if (myCall && f.target && f.target === myCall) cls += ' to-me';
            html += '<div class="' + cls + '"' + rowId + '>';
            html += '<span class="ts">' + f.t + '</span>';
            // Group from/arrow/to into a single visual pill so the
            // address line reads distinctly from the message body —
            // otherwise the arrow blurs into `>` characters that appear
            // inside relay or replayed message text. Symmetric for
            // fromMe and RX rows so our own CQ/HB shows `→ @ALLCALL`/
            // `→ @HB` the same way an incoming station's CQ does.
            var hasAddr = !!f.from || !!f.target;
            if (hasAddr) {
                html += '<span class="addr">';
                if (f.from) {
                    html += '<span class="from" data-call="' + escapeHtml(f.from) + '">'
                         +  escapeHtml(f.from) + '</span>';
                }
                if (f.target) {
                    if (f.from) html += '<span class="arrow">→</span>';
                    html += '<span class="to">' + escapeHtml(f.target) + '</span>';
                }
                html += '</span>';
                html += '<span class="sep">│</span>';
            }
            var msgText = f.msg || '';
            if (f.fromMe && f.label) msgText += '  (' + f.label + ')';
            html += '<span class="msg">' + escapeHtml(msgText) + '</span>';
            if (typeof f.snr === 'number') html += '<span class="snr">' + f.snr + 'dB</span>';
            if (f.fromMe && f.framesTotal) {
                var st = f.txStatus || 'queued';
                html += '<span class="js8-tx-badge ' + st + '" data-tx-badge>'
                     +  txBadgeLabel(st, f.framesSent, f.framesTotal)
                     +  '</span>';
                // Skip the live progress bar once the TX has finished —
                // the badge alone is enough; the bar would otherwise
                // accumulate as a permanent stripe in the feed.
                if (st !== 'done') {
                    var pct = Math.max(0, Math.min(100,
                                (f.framesSent || 0) / f.framesTotal * 100));
                    html += '<div class="js8-tx-progress' +
                            (st === 'aborted' ? ' aborted' : '') +
                            '" data-tx-progress><div class="fill" style="width:'
                         +  pct + '%"></div></div>';
                }
            }
            html += '</div>';
        }
        el.innerHTML = html;
    }

    // Targeted DOM update for in-flight TX rows. Called from the progress
    // timer instead of renderFeed() so user text-selections elsewhere in
    // the feed aren't wiped out 5x/second.
    function updateTxRowsDom() {
        if (!S.activeTx) return;
        var qsoActive = S.activeTab !== 'monitor';
        for (var i = 0; i < S.activeTx.entries.length; i++) {
            var g = S.activeTx.entries[i];
            var f = g.entry;
            var row = document.querySelector('.js8-feed-row[data-id="' + f.id + '"]');
            if (row) {
                var badge = row.querySelector('[data-tx-badge]');
                var prog  = row.querySelector('[data-tx-progress]');
                if (badge) {
                    badge.className = 'js8-tx-badge ' + (f.txStatus || 'queued');
                    badge.textContent = txBadgeLabel(f.txStatus, f.framesSent, f.framesTotal);
                }
                if (prog) {
                    prog.classList.toggle('aborted', f.txStatus === 'aborted');
                    // Hide the bar the instant TX completes — keeps the
                    // feed from collecting bright stripes after each send.
                    prog.classList.toggle('done', f.txStatus === 'done');
                    var fill = prog.querySelector('.fill');
                    if (fill) {
                        var pct = Math.max(0, Math.min(100,
                                    (f.framesSent || 0) / f.framesTotal * 100));
                        fill.style.width = pct + '%';
                    }
                }
            }
            // Update the matching bubble's meta line when a QSO tab is
            // active. Cheap enough to do every tick — one querySelector
            // per in-flight entry, max ~3.
            if (qsoActive) {
                var bubble = document.querySelector(
                    '.js8-bubble[data-id="' + f.id + '"]');
                if (bubble) {
                    var meta = bubble.querySelector('.meta');
                    if (meta) {
                        var parts = [];
                        if (f.t) parts.push(f.t);
                        parts.push(txBadgeLabel(f.txStatus, f.framesSent, f.framesTotal));
                        meta.textContent = parts.join(' · ');
                    }
                }
            }
        }
    }

    // Map SNR (dB) to a 1..5 bar count. JS8 SNRs run roughly -25..+10 in
    // practice; these thresholds keep typical-band signals around 2-3 bars
    // and only push to 5 for genuinely strong copy. Returns 0 if snr is
    // missing — the bar then renders all-empty.
    function snrToBars(snr) {
        if (typeof snr !== 'number' || isNaN(snr)) return 0;
        if (snr >= 0)   return 5;
        if (snr >= -5)  return 4;
        if (snr >= -10) return 3;
        if (snr >= -15) return 2;
        return 1;
    }
    function sbarHtml(snr) {
        var n = snrToBars(snr);
        var html = '<div class="js8-sbar" title="' +
            (typeof snr === 'number' ? snr + ' dB' : 'no signal') + '">';
        for (var i = 1; i <= 5; i++) {
            html += '<span class="' + (i <= n ? 'on lvl' + i : '') + '"></span>';
        }
        html += '</div>';
        return html;
    }

    // Heard-list TTL: drop entries we haven't decoded in over an hour so
    // the list reflects the band's current state and doesn't grow without
    // bound across a long session.
    var HEARD_TTL_MS = 60 * 60 * 1000;
    function pruneStaleStations() {
        var cutoff = Date.now() - HEARD_TTL_MS;
        var drop = [];
        S.stations.forEach(function (s, call) {
            if ((s.lastUtc || 0) < cutoff) drop.push(call);
        });
        for (var i = 0; i < drop.length; i++) S.stations.delete(drop[i]);
        return drop.length;
    }
    // Compact "how long ago" badge: now, 30s, 5m, 42m, 1h. Returns "—"
    // when we have no timestamp (shouldn't happen post-prune but keeps
    // the cell safe). Caller is responsible for re-rendering on a timer
    // so the value stays current while no decodes are coming in.
    function formatAge(lastUtc) {
        if (!lastUtc) return '—';
        var s = Math.max(0, Math.floor((Date.now() - lastUtc) / 1000));
        if (s < 15)    return 'now';
        if (s < 60)    return s + 's';
        var m = Math.floor(s / 60);
        if (m < 60)    return m + 'm';
        var h = Math.floor(m / 60);
        return h + 'h';
    }

    function renderStations() {
        var el = document.getElementById('js8Stations');
        if (!el) return;
        pruneStaleStations();
        var arr = Array.from(S.stations.entries());
        arr.sort(function (a, b) { return (b[1].lastUtc || 0) - (a[1].lastUtc || 0); });
        var html = '';
        for (var i = 0; i < arr.length && i < 50; i++) {
            var call = arr[i][0], s = arr[i][1];
            var sel = call === S.dxCall ? ' selected' : '';
            var dbm = typeof s.snr === 'number'
                ? (s.snr >= 0 ? '+' : '') + s.snr + ' dB'
                : '—';
            var age = formatAge(s.lastUtc);
            html += '<div class="js8-station' + sel + '" data-call="' + escapeHtml(call) + '">';
            html += '<div class="js8-station-row1">';
            html += '<span class="js8-station-call">' + escapeHtml(call) + '</span>';
            html += '<span class="js8-station-meta">';
            html += '<span class="js8-station-age" title="Last heard">' + age + '</span>';
            html += '<span class="js8-station-dbm">' + dbm + '</span>';
            html += '</span>';
            html += '</div>';
            html += sbarHtml(s.snr);
            html += '</div>';
        }
        el.innerHTML = html;
    }

    // ─── Open / close ─────────────────────────────────────────────────
    // Standard JS8Call dial frequencies (USB). Hoisted above open() so
    // it can pick the nearest band and tune on panel open — same pattern
    // toggleDigiBar uses for FT8/FT4.
    var JS8_BANDS = [
        { label: '160m', hz: 1842000  },
        { label: '80m',  hz: 3578000  },
        { label: '40m',  hz: 7078000  },
        { label: '30m',  hz: 10130000 },
        { label: '20m',  hz: 14078000 },
        { label: '17m',  hz: 18104000 },
        { label: '15m',  hz: 21078000 },
        { label: '12m',  hz: 24922000 },
        { label: '10m',  hz: 28078000 },
        { label: '6m',   hz: 50318000 }
    ];
    function getCurrentJs8Band() {
        if (typeof currentFreq !== 'number' || !currentFreq) return null;
        var best = null, bestDist = Infinity;
        for (var i = 0; i < JS8_BANDS.length; i++) {
            var dist = Math.abs(currentFreq - JS8_BANDS[i].hz);
            if (dist < bestDist) { bestDist = dist; best = JS8_BANDS[i]; }
        }
        // 500 kHz tolerance — keeps a 14.345 MHz USB QSO from snapping to
        // 14.078, but does let 14.085 (chatty digital region) snap.
        return (bestDist < 500000) ? best : null;
    }

    function open() {
        if (S.visible) return;
        S.visible = true;
        document.getElementById('js8Bar').classList.remove('hidden');
        document.body.classList.add('js8-open');
        // Persist that the panel is open so a page reload restores it.
        try { window.localStorage.setItem('js8Open', '1'); } catch (e) {}
        if (typeof closeOtherModes === 'function') closeOtherModes('js8');
        if (typeof currentMode !== 'undefined' && currentMode !== 'USB'
            && typeof setMode === 'function') setMode('USB');
        // Tune to the JS8 dial frequency for the current band — same flow
        // FT8 uses on open. Updates the BAND selector to reflect the
        // chosen band. Skipped silently when the rig isn't on or near
        // any JS8 band (currentFreq far from every entry).
        var jb = getCurrentJs8Band();
        if (jb) {
            var bsel = document.getElementById('js8BandSel');
            if (bsel) bsel.value = jb.label;
            send({ cmd: 'setFrequency', value: jb.hz });
        }
        if (typeof initDigiTxCtx === 'function') initDigiTxCtx();
        if (S.decoder) S.decoder.free();
        S.decoder = js8.newDecoder(0);
        resetRxBuffer();
        // Reset the shared waterfall FFT state so stale FT8 samples don't
        // bleed into the first JS8 frame.
        if (typeof wfBufFull !== 'undefined') {
            wfBufFull = false; wfBufPos = 0;
        }
        S.rxEnabled = true;
        S.slotIndex = -1;
        for (var i = 0; i < RX_MODES.length; ++i) S.lastSlot[RX_MODES[i]] = -1;
        S.chains.clear();
        updateRxToggle();
        updateAutoTag();
        updateHbButtonVisibility();
        document.getElementById('js8TxFreq').value = S.baseHz;
        document.getElementById('js8SubmodeSel').value = S.submode;
        if (S.timer) clearInterval(S.timer);
        S.timer = setInterval(tickTimer, 100);
        setStatus('RX', 'rx');
        // Reset to Monitor tab on open. Existing QSO state is preserved
        // across panel close/open — once you're chatting with someone,
        // closing the JS8 panel shouldn't lose the conversation.
        S.activeTab = 'monitor';
        renderTabs();
        setActiveTab('monitor');
        renderFeed();
        renderStations();
        // Stagger the first auto-HB so the user has a moment of audible
        // silence before we start beaconing. Subtract half an interval
        // — fast feedback the feature works, but not "TX the instant
        // the panel opens".
        S.lastHbTx = Date.now() - (S.hbInterval || 15) * 60 * 1000 / 2;
    }

    function close() {
        if (!S.visible) return;
        S.visible = false;
        document.getElementById('js8Bar').classList.add('hidden');
        document.body.classList.remove('js8-open');
        document.body.classList.remove('js8-qso-tab');
        try { window.localStorage.setItem('js8Open', '0'); } catch (e) {}
        closeCmdPalette();
        if (S.timer) { clearInterval(S.timer); S.timer = null; }
        if (S.chainFlushTimer) {
            clearInterval(S.chainFlushTimer); S.chainFlushTimer = null;
        }
        if (S.decoder) { S.decoder.free(); S.decoder = null; }
        S.rxEnabled = false;
        // If we were mid-TX, halt the pacer so audio stops immediately
        // (haltDigiTx's onDone callback releases PTT and clears flags).
        if (S.txBusy && typeof window.haltDigiTx === 'function') window.haltDigiTx();
        if (S.txBusy) { send({ cmd: 'setPTT', value: false }); S.txBusy = false; }
        // Tune shares the digi TX path — if the user closes the panel
        // while TUNE is active, the rig would otherwise stay keyed.
        if (window.digiTuneActive && typeof window.stopDigiTune === 'function') {
            window.stopDigiTune();
        }
        S.txQueue.length = 0;
        S.chains.clear();
    }

    function updateRxToggle() {
        var btn = document.getElementById('js8RxToggle');
        if (!btn) return;
        btn.classList.toggle('active', S.rxEnabled);
        btn.textContent = S.rxEnabled ? 'RX ON' : 'RX OFF';
    }

    // ─── Event wiring ─────────────────────────────────────────────────
    document.getElementById('js8CloseBtn').addEventListener('click', close);
    document.getElementById('js8RxToggle').addEventListener('click', function () {
        S.rxEnabled = !S.rxEnabled;
        updateRxToggle();
        if (S.rxEnabled) resetRxBuffer();
        setStatus(S.rxEnabled ? 'RX' : 'IDLE', S.rxEnabled ? 'rx' : '');
    });

    (function initJs8BandSel() {
        var sel = document.getElementById('js8BandSel');
        if (!sel) return;
        var opt0 = document.createElement('option');
        opt0.value = ''; opt0.textContent = 'Band';
        sel.appendChild(opt0);
        for (var i = 0; i < JS8_BANDS.length; i++) {
            var o = document.createElement('option');
            o.value = JS8_BANDS[i].label;
            o.textContent = JS8_BANDS[i].label;
            sel.appendChild(o);
        }
        sel.addEventListener('change', function () {
            var v = this.value;
            for (var j = 0; j < JS8_BANDS.length; j++) {
                if (JS8_BANDS[j].label === v) {
                    send({ cmd: 'setFrequency', value: JS8_BANDS[j].hz });
                    break;
                }
            }
        });
    })();

    document.getElementById('js8TuneBtn').addEventListener('click', function () {
        if (typeof window.initDigiTxCtx === 'function') window.initDigiTxCtx();
        if (window.digiTuneActive) {
            if (typeof window.stopDigiTune === 'function') window.stopDigiTune();
        } else {
            var inp = document.getElementById('js8TxFreq');
            var f = parseInt(inp && inp.value, 10);
            if (!isFinite(f) || f < 200 || f > 3000) f = 1500;
            if (typeof window.startDigiTune === 'function') window.startDigiTune(f);
        }
        updateJs8TuneBtn();
    });
    function updateJs8TuneBtn() {
        var b = document.getElementById('js8TuneBtn');
        if (!b) return;
        b.classList.toggle('active', !!window.digiTuneActive);
    }
    // Poll the shared digi tune flag so the JS8 button reflects state
    // even when tune was started or stopped from elsewhere (e.g. ESC).
    setInterval(updateJs8TuneBtn, 250);

    // Waterfall click/touch → set TX base freq. Click position is treated
    // as the visual CENTER of the tone span; we subtract js8CenterOffset()
    // to recover baseHz (tone 0). Auto-freq is disengaged on user click
    // so the manual pick sticks.
    (function wireJs8WfClick() {
        var wfBar = document.getElementById('js8WfBar');
        if (!wfBar) return;
        var handle = function (e) {
            e.preventDefault();
            var rect = wfBar.getBoundingClientRect();
            var clientX = e.touches ? e.touches[0].clientX : e.clientX;
            var raw = WF_FREQ_LOW + Math.round(((clientX - rect.left) / rect.width)
                * (WF_FREQ_HIGH - WF_FREQ_LOW)) - Math.round(js8CenterOffset());
            var hz = Math.max(200, Math.min(2800, raw));
            S.baseHz = hz;
            S.autoFreq = false;
            try { window.localStorage.setItem('js8AutoFreq', '0'); } catch (er) {}
            var inp = document.getElementById('js8TxFreq');
            if (inp) inp.value = hz;
            if (typeof updateAutoTag === 'function') updateAutoTag();
            drawJs8Waterfall();
        };
        wfBar.addEventListener('mousedown', handle);
        wfBar.addEventListener('touchstart', handle, { passive: false });
    })();
    // HB participates only in Slow / Normal / Fast (JS8Call 3.0 user
    // guide: "HBs are intentionally restricted … HB is disabled and will
    // not appear on the button for JS8 40 and JS8 60"). Hide the HB
    // chip in the CMD palette when the active TX submode is one of those.
    function updateHbButtonVisibility() {
        var hb = document.getElementById('js8PaletteHbBtn');
        if (!hb) return;
        // 2 = JS8 40, 8 = JS8 60 — both excluded from the HB network.
        var hbAllowed = S.submode !== 2 && S.submode !== 8;
        hb.style.display = hbAllowed ? '' : 'none';
    }
    document.getElementById('js8SubmodeSel').addEventListener('change', function (e) {
        // Only affects TX speed; RX always decodes all five submodes
        // simultaneously, so the decoder doesn't need to be rebuilt.
        S.submode = parseInt(e.target.value, 10);
        S.slotIndex = -1;
        updateHbButtonVisibility();
        // Persist the header-bar speed change. Without this, switching
        // submode from the dropdown sticks for the session but evaporates
        // on the next page reload — only the Settings modal's "Default
        // Speed" field saved through to localStorage.
        saveSettings();
    });
    document.getElementById('js8DxCall').addEventListener('input', function (e) {
        S.dxCall = e.target.value.toUpperCase().trim() || '@ALLCALL';
        renderStations();
        renderDxSuggest();  // refresh prefix-match suggestions as user types
    });
    // Pressing Enter in the TO field opens a chat tab for the typed
    // target without transmitting — useful for joining a group window
    // to listen in (e.g. type @ARES, press Enter, monitor group
    // traffic) rather than having to send a message just to subscribe.
    document.getElementById('js8DxCall').addEventListener('keydown', function (e) {
        if (e.key !== 'Enter') return;
        e.preventDefault();
        var target = (e.target.value || '').toUpperCase().trim();
        if (!target) return;
        closeDxSuggest();
        ensureQso(target, []);
        setActiveTab(target);
        document.getElementById('js8Compose').focus();
    });

    // ─── TO-field suggestion popover ─────────────────────────────────
    // Replaces the old @ALLCALL placeholder default. Lists groups
    // (@ALLCALL, @HB) plus the most-recently-heard stations as one-tap
    // chips. Opens on hover or focus, closes on blur or outside-click.
    function renderDxSuggest() {
        var grid = document.getElementById('js8DxSuggestGrid');
        if (!grid) return;
        var typed = (document.getElementById('js8DxCall').value || '')
                    .toUpperCase().trim();
        // Groups always shown — they're the broadcasts most users
        // never think to type by hand.
        var groups = ['@ALLCALL', '@HB'];
        // Most-recently-heard stations from the Heard column (sorted
        // by lastUtc), capped so the popover doesn't fill the screen.
        var heard = Array.from(S.stations.entries())
            .sort(function (a, b) { return (b[1].lastUtc || 0) - (a[1].lastUtc || 0); })
            .map(function (e) { return e[0]; })
            .slice(0, 8);
        // If the user is typing, prefix-filter both lists. An empty
        // input shows everything.
        function matchPrefix(s) {
            if (!typed) return true;
            return s.indexOf(typed) === 0;
        }
        var groupHits = groups.filter(matchPrefix);
        var heardHits = heard.filter(matchPrefix);
        var html = '';
        groupHits.forEach(function (g) {
            html += '<button type="button" class="js8-dx-suggest-btn group" '
                  + 'data-val="' + escapeHtml(g) + '">'
                  + escapeHtml(g) + '</button>';
        });
        heardHits.forEach(function (c) {
            html += '<button type="button" class="js8-dx-suggest-btn" '
                  + 'data-val="' + escapeHtml(c) + '">'
                  + escapeHtml(c) + '</button>';
        });
        if (!html) {
            html = '<div class="js8-dx-suggest-empty">'
                 + 'No matches. Press Enter to open a tab for ' + escapeHtml(typed) + '.'
                 + '</div>';
        }
        grid.innerHTML = html;
    }
    var dxSuggestCloseTimer = null;
    function openDxSuggest() {
        renderDxSuggest();
        var pop = document.getElementById('js8DxSuggest');
        if (pop) pop.classList.add('open');
        if (dxSuggestCloseTimer) { clearTimeout(dxSuggestCloseTimer); dxSuggestCloseTimer = null; }
    }
    function closeDxSuggest() {
        var pop = document.getElementById('js8DxSuggest');
        if (pop) pop.classList.remove('open');
    }
    function closeDxSuggestSoon() {
        // Small delay so a click on a suggestion chip lands before the
        // blur from the input tears the popover down.
        if (dxSuggestCloseTimer) clearTimeout(dxSuggestCloseTimer);
        dxSuggestCloseTimer = setTimeout(closeDxSuggest, 180);
    }
    (function wireDxSuggest() {
        var input = document.getElementById('js8DxCall');
        var pop   = document.getElementById('js8DxSuggest');
        if (!input || !pop) return;
        input.addEventListener('focus',      openDxSuggest);
        input.addEventListener('mouseenter', openDxSuggest);
        input.addEventListener('blur',       closeDxSuggestSoon);
        // Keep open while the pointer is over the popover itself —
        // otherwise leaving the input to click a chip would close it.
        pop.addEventListener('mouseenter', function () {
            if (dxSuggestCloseTimer) { clearTimeout(dxSuggestCloseTimer); dxSuggestCloseTimer = null; }
        });
        pop.addEventListener('mouseleave', closeDxSuggestSoon);
        // Chip click → fill input + dispatch input event so S.dxCall
        // syncs through the existing input listener.
        pop.addEventListener('click', function (e) {
            var btn = e.target.closest('.js8-dx-suggest-btn');
            if (!btn) return;
            var v = btn.getAttribute('data-val') || '';
            input.value = v;
            input.dispatchEvent(new Event('input', { bubbles: true }));
            closeDxSuggest();
            document.getElementById('js8Compose').focus();
        });
        // Outside click closes.
        document.addEventListener('mousedown', function (e) {
            if (!pop.classList.contains('open')) return;
            if (e.target === input || pop.contains(e.target)) return;
            closeDxSuggest();
        });
    })();
    document.getElementById('js8Compose').addEventListener('keydown', function (e) {
        if (e.key === 'Enter') { document.getElementById('js8SendBtn').click(); }
    });
    document.getElementById('js8SendBtn').addEventListener('click', function () {
        var input = document.getElementById('js8Compose');
        // JSC dictionary is uppercase-only; lower-case text would hand
        // buildMessageFrames a string nothing can consume, and the upstream
        // loop spins forever. Same convention FT8/FT4 follow on TX.
        var text = input.value.trim().toUpperCase();
        if (!text) return;
        // The TO field (= S.dxCall) is the authoritative target. The
        // active tab is just visual context — if the user edited TO
        // to a different peer or group, the message goes to THAT
        // target and we open/focus its tab in enqueueTx's auto-route.
        // Previously SEND inside a QSO tab pulled the target from the
        // tab object itself, so editing TO had no effect and every
        // subsequent send still went to the first tab opened.
        var target = (S.dxCall || '').trim() || '@ALLCALL';
        var q = S.qsos.get(target);
        var path = (q && q.path && q.path.length) ? q.path : [];
        var wire, bypass;
        if (path.length) {
            // Relay: build explicit "HOP1> HOP2> TARGET text" and let
            // pack() know not to touch selectedCall.
            wire = buildRelayWire(path, target, text);
            bypass = true;
        } else {
            // Direct send — always prepend the target into the wire so
            // buildMessageFrames sees an unambiguous "TARGET msg" form.
            // Relying on AUTO_PREPEND_DIRECTED to inject the target via
            // selectedCall was the regression source: parseCallsigns +
            // start-with-base-call heuristics inside that path can
            // suppress the prepend for innocuous-looking message text,
            // leaving the data frame uncredited to any recipient.
            wire = target + ' ' + text;
            bypass = false;
        }
        enqueueTx(wire, '', { qsoPeer: target, qsoPath: path, bypassSel: bypass });
        input.value = '';
    });
    // CQ and HB are exposed only through the CMD palette (Action
    // section). The functions stay top-level so fireCmd() can dispatch
    // them by name.
    function doCq() {
        var myCall = getMyCall(); var myGrid = getMyGridLocal();
        if (!myCall) { alert('Set your callsign first.'); return; }
        // JS8Call's heartbeat_re recognises "CQ CQ CQ" as a triple-CQ
        // heartbeat type and "CQ" alone as a bare type. The official
        // client's CQ button always emits the triple form with grid
        // (e.g. "CQ CQ CQ FN30") — which packs to "@ALLCALL CQ CQ CQ FN30"
        // on air. We send a 4-char grid (Maidenhead 4) when set, matching
        // K1FM's behaviour.
        var grid4 = (myGrid || '').slice(0, 4);
        // bypassSel: CQ is a broadcast (packs to "@ALLCALL CQ CQ CQ FN30"
        // on air). If the operator is sitting in a group tab when they
        // click CQ, pack()'s @-group handling would prepend that group
        // to the wire and the CQ goes to the wrong channel.
        //
        // qsoPeer '@ALLCALL' routes the CQ into the @ALLCALL tab (opening
        // and focusing it) so the operator's own CQ sits alongside the
        // incoming CQs that already populate that group window — instead
        // of being visible only in Monitor. @ALLCALL is @-prefixed so it
        // doesn't arm the QSO-pause gate.
        enqueueTx('CQ CQ CQ' + (grid4 ? ' ' + grid4 : ''), 'CQ',
                  { bypassSel: true, qsoPeer: '@ALLCALL' });
    }
    function doHb() {
        var myCall = getMyCall();
        if (!myCall) { alert('Set your callsign first.'); return; }
        // Match JS8Call's HB button wire format: "@HB HEARTBEAT <GRID4>"
        // (or just "@HB HEARTBEAT" with no grid set). Anything else
        // pack()s as plain text and other JS8Call clients won't see it
        // as a heartbeat — so they never auto-ACK and the operator
        // thinks the network is dead. hb=true puts this in the
        // 500–1000 Hz HB sub-channel when Auto Freq is on.
        var grid4 = (getMyGridLocal() || '').slice(0, 4);
        var text  = grid4 ? ('@HB HEARTBEAT ' + grid4) : '@HB HEARTBEAT';
        S.lastHbTx = Date.now();  // reset the auto-HB interval
        // bypassSel: see maybeFireHb — HB must not inherit the active
        // group as its target.
        enqueueTx(text, 'HB', { hb: true, bypassSel: true, target: '@HB' });
    }
    // Resolve the target callsign for a CMD: prefer the active QSO tab's
    // peer (most natural reading: chips inside a QSO go to that QSO),
    // then fall back to the TO field. Returns null + alerts when there's
    // no real callsign and the cmd isn't @ALLCALL-friendly.
    function resolveCmdTarget(cmd) {
        var to;
        if (S.activeTab !== 'monitor') {
            to = S.activeTab;
        } else {
            to = (document.getElementById('js8DxCall').value || '').toUpperCase().trim()
              || S.dxCall || '@ALLCALL';
        }
        if (to === '@ALLCALL' && cmd !== 'HEARING?') {
            alert('Pick a TO callsign first (or use HEARING? for an @ALLCALL query).');
            return null;
        }
        return to;
    }
    function fireCmd(cmd) {
        if (cmd === 'RELAY') { openRelayModal(); return; }
        if (cmd === 'CQ')    { doCq(); return; }
        if (cmd === 'HB')    { doHb(); return; }
        var to = resolveCmdTarget(cmd);
        if (!to) return;
        enqueueTx(to + ' ' + cmd, cmd, { qsoPeer: to });
    }
    // Palette buttons — fire the cmd, then close the palette.
    document.querySelectorAll('.js8-cmd-palette-btn').forEach(function (btn) {
        btn.addEventListener('click', function () {
            closeCmdPalette();
            fireCmd(btn.dataset.cmd);
        });
    });

    // ─── CMD palette overlay ──────────────────────────────────────────
    // Anchored just above the compose row; opened by clicking the ⌘ chip
    // or pressing `/` as the first character in the compose input.
    function openCmdPalette() {
        var pal = document.getElementById('js8CmdPalette');
        var bd  = document.getElementById('js8CmdPaletteBackdrop');
        var action = document.querySelector('.js8-action-row');
        if (!pal || !bd || !action) return;
        // Position above the action row. The palette is `position:
        // absolute` inside the .js8-bar so coords are relative to the
        // bar. Pin to the bar's right padding so the palette doesn't
        // disappear off-screen on narrow windows.
        var bar = document.getElementById('js8Bar');
        var barRect = bar.getBoundingClientRect();
        var actRect = action.getBoundingClientRect();
        pal.style.left   = '8px';
        pal.style.right  = '8px';
        pal.style.bottom = (barRect.bottom - actRect.top + 4) + 'px';
        pal.classList.add('open');
        bd.classList.add('open');
    }
    function closeCmdPalette() {
        var pal = document.getElementById('js8CmdPalette');
        var bd  = document.getElementById('js8CmdPaletteBackdrop');
        if (pal) pal.classList.remove('open');
        if (bd)  bd.classList.remove('open');
    }
    document.getElementById('js8CmdChip').addEventListener('click', openCmdPalette);
    document.getElementById('js8CmdPaletteBackdrop').addEventListener('mousedown', function (e) {
        e.preventDefault();
        closeCmdPalette();
    });
    // `/` as first char of empty compose opens the palette.
    document.getElementById('js8Compose').addEventListener('keydown', function (e) {
        if (e.key === '/' && !e.target.value) {
            e.preventDefault();
            openCmdPalette();
        }
    });
    // Esc closes the palette from anywhere — the keystroke is rarely
    // captured by anything else, and binding to document survives the
    // case where the user opened the palette via the ⌘ chip and never
    // gave compose focus.
    document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape' &&
            document.getElementById('js8CmdPalette').classList.contains('open')) {
            closeCmdPalette();
        }
    });

    // ─── Relay modal ──────────────────────────────────────────────────
    // Build the wire form for a relay chain "VIA1> VIA2> ... TO MSG" and
    // show a live preview. The first VIA hop is the immediate TO of the
    // packet; everything after the last ">" is the final target + the
    // free-text payload.
    function buildRelayWire(viaList, target, message) {
        var hops = viaList.filter(function (s) { return !!s; });
        if (hops.length === 0 || !target) return '';
        // "<HOP1>> <HOP2>> ... <TARGET> <MSG>"
        return hops.join('> ') + '> ' + target + (message ? ' ' + message : '');
    }
    function updateRelayPreview() {
        var via    = (document.getElementById('js8RelayVia').value  || '').toUpperCase().trim();
        var target = (document.getElementById('js8RelayTo').value   || '').toUpperCase().trim();
        var msg    = (document.getElementById('js8RelayMsg').value  || '').toUpperCase().trim();
        var hops   = via.split(/\s+/).filter(Boolean);
        // PATH visual: ME → HOP1 → HOP2 → ... → TARGET
        var pathEl = document.getElementById('js8RelayPath');
        if (pathEl) {
            var me = escapeHtml(getMyCall() || '(YOU)');
            var path = '<span class="me">' + me + '</span>';
            for (var i = 0; i < hops.length; ++i) {
                path += '<span class="arrow">→</span><span class="hop">'
                     +  escapeHtml(hops[i]) + '</span>';
            }
            if (target) {
                path += '<span class="arrow">→</span><span class="target">'
                     +  escapeHtml(target) + '</span>';
            }
            pathEl.innerHTML = path;
        }
        // WIRE preview: actual on-air payload string
        var wire = buildRelayWire(hops, target, msg);
        var wireEl = document.getElementById('js8RelayPreview');
        if (wireEl) wireEl.textContent = wire || '(fill in VIA and TO)';
        // Only enable Send when we have at least one hop, a target, and
        // a message.
        var sendBtn = document.getElementById('js8RelaySendBtn');
        if (sendBtn) sendBtn.disabled = !(hops.length > 0 && target && msg);
    }
    function openRelayModal() {
        var via = document.getElementById('js8RelayVia');
        var to  = document.getElementById('js8RelayTo');
        var msg = document.getElementById('js8RelayMsg');
        // Pre-fill VIA with the current TO callsign as a sensible default
        // (the station you're already talking to is often the first hop).
        var curTo = (document.getElementById('js8DxCall').value || '').toUpperCase().trim();
        if (curTo && curTo !== '@ALLCALL') via.value = curTo;
        if (to)  to.value  = '';
        if (msg) msg.value = '';
        updateRelayPreview();
        document.getElementById('js8RelayModal').classList.remove('hidden');
        setTimeout(function () { via.focus(); }, 50);
    }
    document.getElementById('js8RelayVia').addEventListener('input', updateRelayPreview);
    document.getElementById('js8RelayTo').addEventListener('input',  updateRelayPreview);
    document.getElementById('js8RelayMsg').addEventListener('input', updateRelayPreview);
    document.getElementById('js8RelayCancelBtn').addEventListener('click', function () {
        document.getElementById('js8RelayModal').classList.add('hidden');
    });
    document.getElementById('js8RelaySendBtn').addEventListener('click', function () {
        var via    = (document.getElementById('js8RelayVia').value || '').toUpperCase().trim();
        var target = (document.getElementById('js8RelayTo').value  || '').toUpperCase().trim();
        var msg    = (document.getElementById('js8RelayMsg').value || '').toUpperCase().trim();
        var hops   = via.split(/\s+/).filter(Boolean);
        if (hops.length === 0 || !target || !msg) return;
        var wire = buildRelayWire(hops, target, msg);
        // bypassSel: keep the payload routing intact — DON'T let
        // AUTO_PREPEND_DIRECTED reshuffle the chain based on S.dxCall.
        // qsoPeer = final target, qsoPath = relay hops — the QSO tab
        // opens with the full chain visible above the bubbles, and any
        // reply within that tab will reuse the same path.
        enqueueTx(wire, 'RELAY → ' + target,
                  { bypassSel: true, qsoPeer: target, qsoPath: hops });
        document.getElementById('js8RelayModal').classList.add('hidden');
        setActiveTab(target);
    });
    // Monitor feed clear is exposed via console as window.js8ClearFeed —
    // the old in-panel "clear" link was retired when Monitor became a tab.
    window.js8ClearFeed = function () { S.feed = []; renderFeed(); };
    document.getElementById('js8SettingsBtn').addEventListener('click', function () {
        document.getElementById('js8SettingMycall').value = getMyCall();
        document.getElementById('js8SettingMygrid').value = getMyGridLocal();
        document.getElementById('js8SettingSubmode').value = S.submode;
        document.getElementById('js8SettingAutoFreq').checked   = S.autoFreq;
        document.getElementById('js8SettingHbNet').checked      = S.hbNet;
        document.getElementById('js8SettingHbInterval').value   = S.hbInterval;
        document.getElementById('js8SettingHbAck').checked      = S.hbAck;
        document.getElementById('js8SettingHbAckCooldown').value = S.hbAckCooldown;
        document.getElementById('js8SettingAutoRespond').checked = S.autoRespond;
        document.getElementById('js8SettingInfoText').value      = S.infoText   || '';
        document.getElementById('js8SettingStatusText').value    = S.statusText || '';
        document.getElementById('js8SettingsModal').classList.remove('hidden');
    });
    document.getElementById('js8SettingsCloseBtn').addEventListener('click', function () {
        var newCall = (document.getElementById('js8SettingMycall').value || '').toUpperCase();
        if (newCall && window.App && window.App.callsign) window.App.callsign.set(newCall);
        var newGrid = (document.getElementById('js8SettingMygrid').value || '').toUpperCase();
        var gridEl = document.getElementById('digiGrid');
        if (newGrid && gridEl) gridEl.value = newGrid;
        S.submode  = parseInt(document.getElementById('js8SettingSubmode').value, 10) || 0;
        S.autoFreq = document.getElementById('js8SettingAutoFreq').checked;
        S.hbNet    = document.getElementById('js8SettingHbNet').checked;
        var iv     = parseInt(document.getElementById('js8SettingHbInterval').value, 10);
        if (!isNaN(iv) && iv >= 2 && iv <= 120) S.hbInterval = iv;
        S.hbAck    = document.getElementById('js8SettingHbAck').checked;
        var ac     = parseInt(document.getElementById('js8SettingHbAckCooldown').value, 10);
        if (!isNaN(ac) && ac >= 0 && ac <= 60) S.hbAckCooldown = ac;
        S.autoRespond = document.getElementById('js8SettingAutoRespond').checked;
        S.infoText    = (document.getElementById('js8SettingInfoText').value   || '').toUpperCase();
        S.statusText  = (document.getElementById('js8SettingStatusText').value || '').toUpperCase();
        document.getElementById('js8SubmodeSel').value = S.submode;
        updateAutoTag();
        updateHbButtonVisibility();
        saveSettings();
        document.getElementById('js8SettingsModal').classList.add('hidden');
    });
    document.getElementById('js8TxFreq').addEventListener('change', function (e) {
        var v = parseInt(e.target.value, 10);
        if (isNaN(v) || v < 200 || v > 2800) { e.target.value = S.baseHz; return; }
        S.baseHz = v;
        saveSettings();
        drawJs8Waterfall();
    });
    document.getElementById('js8AutoTag').addEventListener('click', function () {
        // Quick toggle for Auto Freq right from the header.
        S.autoFreq = !S.autoFreq;
        updateAutoTag();
        saveSettings();
    });
    function updateAutoTag() {
        var el = document.getElementById('js8AutoTag');
        if (!el) return;
        el.classList.toggle('off', !S.autoFreq);
        el.textContent = S.autoFreq ? 'AUTO' : 'MANUAL';
        el.title = S.autoFreq
            ? 'Auto-pick TX frequency — click to switch to manual'
            : 'Manual TX frequency — click to enable auto-pick';
    }
    document.getElementById('js8Stations').addEventListener('click', function (e) {
        var el = e.target.closest('.js8-station');
        if (!el) return;
        var call = el.dataset.call;
        if (!call) return;
        S.dxCall = call;
        document.getElementById('js8DxCall').value = call;
        // Click a heard station → open a QSO with them and focus that
        // tab. Skips broadcast aliases (@ALLCALL/@HB) — those stay as
        // Monitor-tab destinations only.
        if (call.charAt(0) !== '@') {
            ensureQso(call, []);
            setActiveTab(call);
        } else {
            renderStations();
        }
    });
    // Tab strip — click switches active view; ✕ closes the QSO.
    document.getElementById('js8Tabs').addEventListener('click', function (e) {
        var closeBtn = e.target.closest('.js8-tab-close');
        if (closeBtn) {
            e.stopPropagation();
            closeQso(closeBtn.dataset.close);
            return;
        }
        var tab = e.target.closest('.js8-tab');
        if (!tab) return;
        setActiveTab(tab.dataset.tab);
    });
    document.getElementById('js8QsoCloseBtn').addEventListener('click', function () {
        if (S.activeTab !== 'monitor') closeQso(S.activeTab);
    });
    document.getElementById('js8Feed').addEventListener('click', function (e) {
        var el = e.target.closest('.from');
        if (!el) return;
        var call = el.dataset.call;
        if (!call || call.charAt(0) === '@') return;
        S.dxCall = call;
        document.getElementById('js8DxCall').value = call;
        renderStations();
    });

    window.toggleJs8Bar       = function () { if (S.visible) close(); else open(); };
    window.js8MessengerVisible = function () { return S.visible; };
    window.js8Ready            = Promise.resolve(js8);

    // Restore panel visibility from the previous session. Browsers block
    // AudioContext start without a user gesture, so the panel comes back
    // visually but the mic/decoder run lazily — same constraint that
    // affects every wfweb mode that touches audio on first load.
    //
    // Done inline at end of init (mirrors packet.js's restoreLayoutFromStorage
    // call site). Wrapping this in a `load` event listener was a dead end:
    // the JS8 module awaits the WASM codec init, so by the time the IIFE
    // gets here `load` has long since fired and a freshly-attached handler
    // never runs. document.readyState check is belt-and-braces in case
    // this ever moves earlier.
    try {
        if (window.localStorage.getItem('js8Open') === '1') {
            if (document.readyState === 'loading') {
                document.addEventListener('DOMContentLoaded', open);
            } else if (!S.visible) {
                open();
            }
        }
    } catch (e) {}

    // Debug entry kept for devtools / corpus tests
    window.txJs8 = function (msg, opts) {
        opts = Object.assign({ frameType: 0, baseHz: 1500, submode: 0 }, opts || {});
        if (typeof msg !== 'string' || msg.length !== 12) {
            console.error('[JS8 TX] message must be exactly 12 chars from "0-9A-Za-z-+"');
            return;
        }
        var tones = js8.encode(opts.frameType, msg, opts.submode);
        if (!tones) { console.error('[JS8 TX] encode failed'); return; }
        var samples = synthesize(tones, opts);
        if (typeof digiTxActive !== 'undefined') digiTxActive = true;
        send({ cmd: 'enableMic', value: true });
        streamDigiAudio(samples,
            function () { if (typeof digiTxActive !== 'undefined') digiTxActive = false;
                          send({ cmd: 'setPTT', value: false }); },
            function () { send({ cmd: 'setPTT', value: true }); });
    };

    console.log('[JS8] messenger panel ready');
})();
