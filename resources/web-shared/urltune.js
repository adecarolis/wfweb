// urltune.js — jump-to-spot hyperlinks (issue #83).
//
// Lets a URL carry an initial frequency and mode so links like
//   https://host:8080/?f=14250&m=USB
//   https://host:8080/?f=7024.5&m=CW
//   https://host:8080/?f=14236&m=RADE
// tune the radio on arrival. `f` is in kHz (decimals allowed, DX-cluster
// convention). `m` is either a rig mode matched case-insensitively against
// the rig's mode list, or one of the app modes below, which open the
// corresponding panel exactly as a manual tap would (the panel's own open
// path forces the right rig mode and closes competing panels):
//   FT8 / FT4  — DIGI bar          JS8 — JS8 messenger
//   RADE       — FreeDV/RADE       PKT (or PACKET) — packet terminal
//
// Two stages, each at most once per page load (reconnects and rig
// power-cycles never re-fire):
//  1. Rig-ready — both SPAs call window.applyUrlTune() from their
//     rigInfo/rigConnected handlers. Frequency and plain rig modes apply
//     here. Frequency goes first so an explicit m= wins over any
//     band-default mode.
//  2. App open — additionally waits for the first user gesture. On the
//     server build the WebSocket connects before the "CLICK TO START"
//     splash is dismissed, and opening the FT8/JS8 panels pre-gesture
//     would create a permanently-suspended AudioContext (the initDigiTxCtx
//     guard never re-creates it). On the standalone build the rig can only
//     connect from a click, so the gesture is always already in the past.
//
// Reads the host page's globals at call time: send(), setMode(),
// availableModes, toggleDigiBar()/digiBarVisible/digiMode,
// toggleJs8Bar()/js8MessengerVisible(), toggleFreeDV()/freedvEnabled,
// window.Packet.
(function () {
    'use strict';

    var applied = false;      // stage 1 done
    var appOpened = false;    // stage 2 done
    var rigReady = false;
    var gestureSeen = false;

    var freqHz = 0;
    var modeReq = '';
    var appReq = '';

    var APP_ALIASES = { FT8: 'FT8', FT4: 'FT4', JS8: 'JS8', RADE: 'RADE',
                        PKT: 'PKT', PACKET: 'PKT' };

    try {
        var qp = new URLSearchParams(location.search);
        var f = parseFloat(qp.get('f'));
        if (isFinite(f) && f > 0) freqHz = Math.round(f * 1000);
        modeReq = (qp.get('m') || '').trim().toUpperCase();
        appReq = APP_ALIASES[modeReq] || '';
    } catch (e) { /* ignore malformed URLs */ }

    if (appReq) {
        // Any first interaction counts — on the server build it's the
        // CLICK TO START splash, on the standalone it's the connect button.
        var onGesture = function () {
            document.removeEventListener('click', onGesture, true);
            document.removeEventListener('keydown', onGesture, true);
            gestureSeen = true;
            maybeOpenApp();
        };
        document.addEventListener('click', onGesture, true);
        document.addEventListener('keydown', onGesture, true);
    }

    window.applyUrlTune = function () {
        rigReady = true;
        if (!applied) {
            applied = true;
            if (freqHz) window.send({ cmd: 'setFrequency', value: freqHz });
            if (modeReq && !appReq) {
                // Resolve to the canonical name from the rig's mode list —
                // the standalone CI-V codec only accepts exact known names.
                var modes = window.availableModes || [];
                var match = '';
                for (var i = 0; i < modes.length; i++) {
                    if (String(modes[i]).toUpperCase() === modeReq) { match = modes[i]; break; }
                }
                if (match) window.setMode(match);
                else console.warn('urltune: unknown mode in ?m=', modeReq);
            }
        }
        maybeOpenApp();
    };

    function maybeOpenApp() {
        if (!appReq || appOpened || !rigReady || !gestureSeen) return;
        appOpened = true;
        // Small settle so the freq echo lands first: RADE's sideband pick
        // and the DIGI/JS8 band-dial retune both read the current frequency.
        setTimeout(openApp, 400);
    }

    function openApp() {
        switch (appReq) {
            case 'FT8':
            case 'FT4':
                if (window.digiBarVisible) break;
                if (appReq === 'FT4') {
                    // Same pre-open flip the panel's own mode label performs.
                    window.digiMode = 'FT4';
                    var lbl = document.getElementById('digiModeLabel');
                    if (lbl) lbl.textContent = 'FT4';
                }
                window.toggleDigiBar();
                break;
            case 'JS8':
                openJs8(40);
                return; // reassertFreq is scheduled once the module is up
            case 'RADE':
                // toggleFreeDV starts freedvModes[0]; both builds list RADE
                // first (standalone has only RADE).
                if (!window.freedvEnabled) window.toggleFreeDV();
                break;
            case 'PKT':
                if (window.Packet && !window.Packet.state.visible) window.Packet.show();
                break;
        }
        reassertFreq();
    }

    // js8-panel.mjs installs toggleJs8Bar only after its WASM codec loads,
    // which can lose the race against the rig connecting — poll briefly.
    function openJs8(triesLeft) {
        if (typeof window.toggleJs8Bar !== 'function') {
            if (triesLeft > 0) setTimeout(function () { openJs8(triesLeft - 1); }, 250);
            else console.warn('urltune: JS8 module never became ready');
            return;
        }
        if (!window.js8MessengerVisible()) window.toggleJs8Bar();
        reassertFreq();
    }

    function reassertFreq() {
        // The DIGI/JS8 open paths retune to their band's standard dial;
        // an explicit f= in the URL wins (think DXpedition fox frequency).
        if (freqHz) setTimeout(function () {
            window.send({ cmd: 'setFrequency', value: freqHz });
        }, 600);
    }
})();
