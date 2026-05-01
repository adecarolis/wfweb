// Reporter availability helper, shared by both the server and the
// standalone builds.
//
// FreeDV Reporter (qso.freedv.org) and PSK Reporter (pskreporter.info)
// both require a backend that can hold a long-lived connection to a
// non-CORS-friendly service.  In the server build the C++ wfweb daemon
// owns that connection; in the standalone (browser-only) build there
// is no daemon, and a direct WebSocket from the page is blocked by CORS
// / mixed-content rules.
//
// The plan is to add a small CORS proxy on the serving site (e.g. on
// k1fm.us) that the standalone build can talk to.  When that lands,
// flip `backend` to 'proxy' and set `proxyUrl`; the rest of the code
// can keep treating `WfwebReporters.available` as the single source
// of truth for "is it safe to send setReporter".
//
// Auto-detection here keeps the two index.html files identical at the
// markup level — the only divergence is which transport script is
// loaded, and that's already a hard fork.
(function() {
    var hasServer = (typeof WebSocketRigTransport !== 'undefined');

    var state = {
        // 'server' = native C++ daemon; 'proxy' = CORS proxy on serving site;
        // 'none'   = no backend reachable.
        backend: hasServer ? 'server' : 'none',
        proxyUrl: null
    };
    state.available = (state.backend !== 'none');

    function disable(el, reason) {
        if (!el) return;
        el.disabled = true;
        el.checked  = false;
        var label = el.closest && el.closest('label');
        if (label) {
            label.title = reason;
            label.style.opacity = '0.5';
            label.style.cursor  = 'not-allowed';
        } else {
            el.title = reason;
        }
    }

    function apply() {
        if (state.available) return;
        var reason = 'Reporter services need a server backend.\n'
                   + 'Not available in the standalone browser build yet — '
                   + 'a CORS proxy on the serving site is planned.';
        disable(document.getElementById('digiReporterEnable'),    reason);
        disable(document.getElementById('digiPskReporterEnable'), reason);
    }

    window.WfwebReporters = {
        get backend()   { return state.backend; },
        get proxyUrl()  { return state.proxyUrl; },
        get available() { return state.available; },
        apply: apply
    };
})();
