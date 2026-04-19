// Packet (Dire Wolf) decoded-frame panel for wfweb.
// Follows the cw-decoder.js pattern: classic script, self-inits on DOMContentLoaded,
// reaches out to window.send for command dispatch, exposes window.Packet.

(function() {
    'use strict';

    var FRAME_BUFFER_MAX = 200; // retain last N decoded frames

    var state = {
        visible: false,
        enabled: false,        // master packet modem enable
        mode: 1200,            // 300 (HF AFSK), 1200 (VHF AFSK), or 9600 (VHF G3RUH)
        frames: []
    };

    var barEl = null;
    var framesEl = null;

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
            '<div id="packetFrames" class="packet-frames"></div>';

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
        updateModeButtons();
    }

    function addStyles() {
        if (document.getElementById('packetStyles')) return;
        var style = document.createElement('style');
        style.id = 'packetStyles';
        style.textContent =
            '.packet-bar { position: fixed; top: 134px; left: 0; right: 0; bottom: 48px; background: #001008; border-top: 2px solid #0a0; z-index: 200; padding: 6px 8px; color: #cfc; font-family: monospace; font-size: 12px; display: flex; flex-direction: column; min-height: 0; box-sizing: border-box; }' +
            'body.packet-open #spectrumCanvas, body.packet-open #waterfallCanvas { display: none !important; }' +
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
            '.packet-frames { background: #000; border: 1px solid #0a0; border-radius: 3px; padding: 4px; flex: 1; min-height: 0; overflow-y: auto; font-size: 11px; line-height: 1.4; }' +
            '.packet-frame { padding: 2px 4px; border-bottom: 1px dotted #1a3a1a; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }' +
            '.packet-frame:last-child { border-bottom: none; }' +
            '.packet-frame .ts { color: #888; margin-right: 6px; }' +
            '.packet-frame .chan { color: #888; margin-right: 6px; }' +
            '.packet-frame .src { color: #ff0; }' +
            '.packet-frame .dst { color: #0ff; }' +
            '.packet-frame .path { color: #888; }' +
            '.packet-frame .info { color: #cfc; margin-left: 6px; }' +
            '.packet-empty { color: #666; padding: 8px; text-align: center; }' +
            '.flex-space { flex: 1; }';
        document.head.appendChild(style);
    }

    function show() {
        state.visible = true;
        if (barEl) barEl.classList.remove('hidden');
        document.body.classList.add('packet-open');
        setEnabled(true);
        renderFrames();
    }

    function hide() {
        state.visible = false;
        if (barEl) barEl.classList.add('hidden');
        document.body.classList.remove('packet-open');
        setEnabled(false);
    }

    function toggle() {
        if (state.visible) hide(); else show();
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
                '<div class="packet-frame">' +
                    '<span class="ts">' + escapeHtml(formatTs(f.ts)) + '</span>' +
                    '<span class="chan">ch' + escapeHtml(f.chan) + '</span>' +
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
