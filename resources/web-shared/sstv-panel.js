// SSTV panel — slow-scan television for both builds, one copy.
// Opens from the MODE grid (window.toggleSstvBar) as an overlay peer of the
// FT8 DIGI bar and JS8: it fills #scopeArea (body.sstv-open hides the scope
// canvases, sstv-panel.css) and holds:
//   - RX: the rig's PCM from handleAudioData goes to sstv-worker.js
//     (SstvPanel.feedRx), which demodulates, decodes with the `sstv` crate's
//     WebAssembly and posts the image row by row; the rows are painted as
//     they come, and the finished image (complete or not) goes to the
//     gallery.  RX MODE pins a mode (also catches a signal whose VIS header
//     was missed) or AUTO reads the VIS;
//     START decodes from this moment in the RX mode (the last received
//     mode, or the TX mode, under AUTO) for a picture whose header was
//     missed: the crate's headerless start;
//   - TX: an image from a file, the phone camera or the gallery, fitted
//     (letterbox) or filled (centre crop) to the TX MODE's resolution with
//     an optional text overlay (the callsign by default) and an optional
//     picture-in-picture from the gallery (PIP, a corner), synthesized by
//     the worker at 12 kHz and keyed through the FT8 TX path
//     (streamDigiAudio, enableMic, setPTT {digital: true}); while it goes
//     out a sweep line crosses the picture and dims what is still to come;
//   - a tuning strip: a waterfall of the worker's input spectrum 1000-2500 Hz
//     with the sync / black / leader / white marks (1200 / 1500 / 1900 /
//     2300); a click moves the dial so the clicked signal lands on 1900 Hz
//     (on a sideband; FM and AM do not move the audio with the dial);
//   - a gallery shared across devices, behind the GALLERY button (a pane
//     tab on phones), in two sections: RECEIVED (every decoded picture) and
//     LOADED (every picture picked for TX, kept at 640 px at most): on the
//     Server build through /api/v1/sstv/images (the server tells every
//     browser of a change with the `sstvGallery` WebSocket message), on
//     Standalone in IndexedDB (other tabs via BroadcastChannel).  A tap
//     opens the viewer: TO TX loads the picture to send (a received one can
//     be sent back), PIP puts it in a corner of the TX picture, SAVE
//     downloads a PNG, DELETE (pressed twice: no confirm() dialog, which
//     would stall the audio and the decoder) removes it for everyone.
// Opening keeps a voice mode (USB / LSB / FM / AM: SSTV on 2 m is FM) and
// otherwise sets USB; the BAND select tunes the usual SSTV frequencies and
// sets their mode.  The mic level goes to DIGI_MOD_LEVEL while the panel is
// open, as the DIGI bar does, and the VFO snapshot is put back on close.
// Settings: wfSstvTxMode, wfSstvRxMode, wfSstvFit, wfSstvOverlay,
// wfSstvOverlayOn, wfSstvPipCorner, wfSstvOpen.
(function () {
    'use strict';

    var STRIP_LO = 1000, STRIP_HI = 2500;
    var MARKS = [{ hz: 1200, label: 'SYNC' }, { hz: 1500, label: 'BLACK' }, { hz: 1900, label: '1900' }, { hz: 2300, label: 'WHITE' }];
    var LEADER_HZ = 1900;
    var TX_RATE = 12000;
    var VOICE_MODES = ['USB', 'LSB', 'FM', 'AM'];
    var GALLERY_LIMIT = 200;
    var LOADED_MAX_PX = 640;                // a picture loaded for TX is kept at this size at most (the largest mode is 640 wide)
    var PIP_CORNERS = ['tr', 'br', 'bl', 'tl'];
    var TX_TICK_MS = 100;
    // The usual SSTV meeting frequencies.  `num` gates a band on the rig's
    // own band table where the picker has no key for it.
    var SSTV_BANDS = [
        { label: '80m 3.845', hz: 3845000, mode: 'LSB' },
        { label: '80m 3.730', hz: 3730000, mode: 'LSB' },
        { label: '40m 7.171', hz: 7171000, mode: 'LSB' },
        { label: '20m 14.230', hz: 14230000, mode: 'USB' },
        { label: '20m 14.233', hz: 14233000, mode: 'USB' },
        { label: '15m 21.340', hz: 21340000, mode: 'USB' },
        { label: '10m 28.680', hz: 28680000, mode: 'USB' },
        { label: '2m 144.500', hz: 144500000, mode: 'FM' },
        { label: 'ISS 145.800', hz: 145800000, mode: 'FM' },
    ];

    var S = {
        visible: false, suspended: false,
        modes: [],                          // from the worker: [{id, key, name, w, h}]
        txModeName: 'Robot 36', rxModeName: 'AUTO',
        fit: 'fit', overlayOn: true, overlayText: '',
        inputRate: 0,
        rx: null,                           // {mode, name, w, h, rows, startedAt, via: 'vis' | 'sync'}
        rxForce: null,                      // START pressed: {mode} (null = the worker tells it from the sync pulses), until that picture ends
        rxCanvas: null, txCanvas: null,
        status: 'Waiting for a signal…', progress: 0,
        txBusy: false, txSeconds: 0, txHeaderSeconds: 0, txStartedAt: 0, txImage: null, txName: '',
        pip: null, pipCorner: 'tr',         // picture-in-picture on the TX image: an ImageBitmap / <img> and its corner
        micWasOn: false, savedModLevel: -1,
        gallery: [], galleryKind: '',
        spec: null,
        pane: 'rx',
    };
    var bar = null, worker = null, scope = null, scopeCtx = null, column = null, colorLUT = null;
    var store = null, txTimer = null, viewerId = null, deleteArmed = null;
    var host = function (name) { return window[name]; };
    function hostSend(msg) { var s = host('send'); if (typeof s === 'function') s(msg); }
    function myCall() { return (window.App && window.App.callsign && window.App.callsign.get()) || ''; }
    function mode() { return String(host('currentMode') || '').toUpperCase(); }
    function isVoiceMode(m) { return VOICE_MODES.indexOf(m) >= 0; }
    function $(id) { return document.getElementById(id); }
    function esc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }

    // ------------------------------------------------------------------
    // Markup
    // ------------------------------------------------------------------
    function build() {
        var area = $('scopeArea');
        if (!area || $('sstvBar')) return;
        var el = document.createElement('div');
        el.id = 'sstvBar';
        el.className = 'sstv-bar mode-sstv wf-panel hidden';
        el.dataset.pane = S.pane;
        el.innerHTML =
            '<div class="sstv-header wf-panel-header">' +
              '<span class="wf-badge">SSTV</span>' +
              '<label class="sstv-lbl sstv-wide">RX</label>' +
              '<select id="sstvRxModeSel" class="wf-field sstv-sel" title="Receive mode: AUTO reads the VIS header; a pinned mode also catches a signal whose header was missed"></select>' +
              '<label class="sstv-lbl sstv-wide">TX</label>' +
              '<select id="sstvTxModeSel" class="wf-field sstv-sel" title="Transmit mode"></select>' +
              '<select id="sstvBandSel" class="wf-field sstv-sel" title="Tune to an SSTV meeting frequency (sets the mode too)"></select>' +
              '<span id="sstvStatus" class="sstv-status"></span>' +
              '<button id="sstvGalleryBtn" class="wf-btn toggle sstv-desk" title="The pictures received and loaded, shared across devices">GALLERY</button>' +
              '<button id="sstvLogBtn" class="wf-btn" title="Log a QSO in SSTV">LOG<span class="sstv-wide"> QSO</span></button>' +
              '<button id="sstvCloseBtn" class="wf-btn sq">✕</button>' +
            '</div>' +
            '<div class="sstv-pane-tabs">' +
              '<button class="wf-tab" data-pane="rx">RX</button>' +
              '<button class="wf-tab" data-pane="tx">TX</button>' +
              '<button class="wf-tab" data-pane="gallery">GALLERY</button>' +
            '</div>' +
            '<div class="sstv-body">' +
              '<section class="sstv-card sstv-card-rx" data-pane="rx">' +
                '<div class="sstv-card-head"><span>RX</span><span id="sstvRxMeta" class="sstv-meta"></span></div>' +
                '<div class="sstv-frame"><canvas id="sstvRxCanvas" width="320" height="256"></canvas></div>' +
                '<div class="wf-progress"><div id="sstvRxProgress" class="wf-progress-bar"></div></div>' +
                '<div class="sstv-card-btns">' +
                  '<button id="sstvRxStartBtn" class="wf-btn" title="Start decoding now, for a picture whose header was missed: in the RX mode, or under AUTO the last received mode (else the TX mode)">START</button>' +
                  '<button id="sstvRxSaveBtn" class="wf-btn" title="Download the image on screen as PNG">SAVE</button>' +
                  '<button id="sstvRxDiscardBtn" class="wf-btn" title="Drop the image being received and wait for the next">DISCARD</button>' +
                '</div>' +
              '</section>' +
              '<section class="sstv-card sstv-card-tx" data-pane="tx">' +
                '<div class="sstv-card-head"><span>TX</span><span id="sstvTxMeta" class="sstv-meta"></span></div>' +
                '<div class="sstv-frame"><canvas id="sstvTxCanvas" width="320" height="256"></canvas><div id="sstvTxSweep" class="sstv-sweep"></div></div>' +
                '<div class="sstv-card-btns">' +
                  '<input type="file" id="sstvFileInput" accept="image/*" hidden>' +
                  '<input type="file" id="sstvCameraInput" accept="image/*" capture="environment" hidden>' +
                  '<button id="sstvPickBtn" class="wf-btn" title="Choose an image file (it goes to the gallery too)">IMAGE</button>' +
                  '<button id="sstvCameraBtn" class="wf-btn sstv-camera" title="Take a photo (it goes to the gallery too)">CAMERA</button>' +
                  '<button id="sstvFitBtn" class="wf-btn toggle" title="FIT letterboxes the picture on black; FILL crops it to the frame">FIT</button>' +
                  '<button id="sstvPipBtn" class="wf-btn toggle" hidden title="The picture in the corner: move it round the corners, then off">PIP</button>' +
                  '<button id="sstvSendBtn" class="wf-btn" title="Transmit the picture">SEND</button>' +
                  '<button id="sstvAbortBtn" class="wf-btn danger" title="Stop transmitting">ABORT</button>' +
                '</div>' +
                '<div class="sstv-overlay-row">' +
                  '<button id="sstvOverlayBtn" class="wf-btn toggle" title="Write this text on the picture (bottom left)">TEXT</button>' +
                  '<input type="text" id="sstvOverlayInput" class="wf-field sstv-overlay-input" maxlength="24" autocomplete="off" autocapitalize="characters" placeholder="CALLSIGN">' +
                '</div>' +
              '</section>' +
              '<section class="sstv-card sstv-card-gallery" data-pane="gallery">' +
                '<div class="sstv-card-head"><span>GALLERY</span><span id="sstvGalleryMeta" class="sstv-meta"></span></div>' +
                '<div id="sstvGallery" class="sstv-gallery"></div>' +
              '</section>' +
            '</div>' +
            '<div class="sstv-scope-row">' +
              '<canvas id="sstvScopeCanvas" title="Click a signal to put it on 1900 Hz"></canvas>' +
              '<div class="sstv-marks"></div>' +
            '</div>';
        area.appendChild(el);
        bar = el;
        var viewer = document.createElement('div');
        viewer.id = 'sstvViewer';
        viewer.className = 'overlay mode-sstv sstv-viewer';
        viewer.style.display = 'none';
        viewer.innerHTML =
            '<div class="sstv-viewer-dialog">' +
              '<img id="sstvViewerImg" alt="">' +
              '<div id="sstvViewerMeta" class="sstv-viewer-meta"></div>' +
              '<div class="sstv-viewer-btns">' +
                '<button id="sstvViewerToTx" class="wf-btn" title="Load this picture to send">TO TX</button>' +
                '<button id="sstvViewerPip" class="wf-btn" title="Put this picture in a corner of the TX picture">PIP</button>' +
                '<button id="sstvViewerSave" class="wf-btn" title="Download as PNG">SAVE</button>' +
                '<button id="sstvViewerDelete" class="wf-btn danger" title="Delete for everyone (press twice)">DELETE</button>' +
                '<button id="sstvViewerClose" class="wf-btn">CLOSE</button>' +
              '</div>' +
            '</div>';
        el.parentNode.insertBefore(viewer, el.nextSibling);

        S.rxCanvas = $('sstvRxCanvas');
        S.txCanvas = $('sstvTxCanvas');
        scope = $('sstvScopeCanvas');
        scopeCtx = scope.getContext('2d');
        colorLUT = buildColorLUT();
        var marks = el.querySelector('.sstv-marks');
        MARKS.forEach(function (m) {
            var d = document.createElement('div');
            d.className = 'sstv-mark';
            d.style.top = ((1 - (m.hz - STRIP_LO) / (STRIP_HI - STRIP_LO)) * 100) + '%';
            d.innerHTML = '<span>' + m.label + '</span>';
            marks.appendChild(d);
        });

        $('sstvCloseBtn').addEventListener('click', function () { hide(); });
        $('sstvLogBtn').addEventListener('click', logQso);
        $('sstvGalleryBtn').addEventListener('click', function () { setPane(S.pane === 'gallery' ? 'rx' : 'gallery'); });
        $('sstvRxModeSel').addEventListener('change', function () {
            S.rxModeName = this.value || 'AUTO';
            S.rxForce = null;
            savePrefs();
            configureWorker();
        });
        $('sstvTxModeSel').addEventListener('change', function () {
            S.txModeName = this.value;
            savePrefs();
            renderTxImage();
        });
        $('sstvBandSel').addEventListener('change', function () {
            var b = SSTV_BANDS.filter(function (x) { return x.label === $('sstvBandSel').value; })[0];
            if (b) tuneBand(b);
        });
        el.querySelectorAll('.sstv-pane-tabs .wf-tab').forEach(function (t) {
            t.addEventListener('click', function () { setPane(t.dataset.pane); });
        });
        $('sstvRxSaveBtn').addEventListener('click', function () {
            if (!S.rx) return;
            S.rxCanvas.toBlob(function (blob) { if (blob) saveBlob(blob, fileName(S.rx.name, Date.now())); }, 'image/png');
        });
        $('sstvRxStartBtn').addEventListener('click', startRx);
        $('sstvRxDiscardBtn').addEventListener('click', discardRx);
        $('sstvPickBtn').addEventListener('click', function () { $('sstvFileInput').click(); });
        $('sstvCameraBtn').addEventListener('click', function () { $('sstvCameraInput').click(); });
        $('sstvFileInput').addEventListener('change', function () { if (this.files[0]) loadTxImage(this.files[0]); this.value = ''; });
        $('sstvCameraInput').addEventListener('change', function () { if (this.files[0]) loadTxImage(this.files[0]); this.value = ''; });
        $('sstvFitBtn').addEventListener('click', function () {
            S.fit = S.fit === 'fit' ? 'fill' : 'fit';
            savePrefs(); renderTxImage(); updateButtons();
        });
        $('sstvPipBtn').addEventListener('click', function () {
            // Round the corners, then off.
            var i = PIP_CORNERS.indexOf(S.pipCorner) + 1;
            if (i >= PIP_CORNERS.length) { setPip(null); S.pipCorner = PIP_CORNERS[0]; }
            else S.pipCorner = PIP_CORNERS[i];
            savePrefs(); renderTxImage();
        });
        $('sstvSendBtn').addEventListener('click', sendImage);
        $('sstvAbortBtn').addEventListener('click', stopTx);
        $('sstvOverlayBtn').addEventListener('click', function () {
            S.overlayOn = !S.overlayOn;
            savePrefs(); renderTxImage(); updateButtons();
        });
        var ov = $('sstvOverlayInput');
        ov.addEventListener('input', function () { S.overlayText = ov.value.toUpperCase(); savePrefs(); renderTxImage(); });
        window.addEventListener('appCallsignChanged', function () { if (!S.overlayText) renderTxImage(); setOverlayPlaceholder(); });
        $('sstvGallery').addEventListener('click', function (e) {
            var tile = e.target.closest('.sstv-thumb');
            if (tile) openViewer(tile.dataset.id);
        });
        $('sstvViewerClose').addEventListener('click', closeViewer);
        viewer.addEventListener('click', function (e) { if (e.target === viewer) closeViewer(); });
        $('sstvViewerToTx').addEventListener('click', function () {
            var item = galleryItem(viewerId);
            if (!item) return;
            store.blob(item.id).then(function (blob) {
                if (!blob) return;
                loadTxImage(blob, { name: item.name || (item.mode + ' ' + fmtTime(item.ts)), fromGallery: true });
                closeViewer();
            });
        });
        $('sstvViewerPip').addEventListener('click', function () {
            var item = galleryItem(viewerId);
            if (!item) return;
            store.blob(item.id).then(function (blob) { return blob ? decodeImage(blob) : null; }).then(function (img) {
                if (!img) return;
                setPip(img);
                closeViewer();
                setPane('tx');
            }, function () { setStatus('Cannot read that image'); });
        });
        $('sstvViewerSave').addEventListener('click', function () {
            var item = galleryItem(viewerId);
            if (!item) return;
            store.blob(item.id).then(function (blob) { if (blob) saveBlob(blob, fileName(itemLabel(item), Date.parse(item.ts) || Date.now(), item.freqHz)); });
        });
        // Two presses, no confirm(): a modal dialog stops the page's event
        // loop, and with it the audio and the decoder.
        $('sstvViewerDelete').addEventListener('click', function () {
            var item = galleryItem(viewerId);
            if (!item) return;
            if (!deleteArmed || deleteArmed.id !== item.id) { armDelete(item.id); return; }
            disarmDelete();
            store.remove(item.id).then(function () { closeViewer(); refreshGallery(); }, function (e) { setStatus('Delete failed: ' + (e && e.message || e)); });
        });
        scope.addEventListener('click', scopeClick);
        if (window.ResizeObserver) new ResizeObserver(sizeScope).observe(el);

        loadPrefs();
        setOverlayPlaceholder();
        populateBands();
        updateButtons();
        drawTxPlaceholder();
        if (localStorage.getItem('wfSstvOpen') === '1') {
            // Reopen once the page knows the rig (toggleDigiBar-style panels
            // need currentFreq for the band select and the VFO snapshot).
            var tries = 0, t = setInterval(function () {
                if ((host('currentFreq') > 0 && host('currentMode')) || ++tries > 50) { clearInterval(t); if (tries <= 50) show(); }
            }, 200);
        }
    }

    function loadPrefs() {
        try {
            S.txModeName = localStorage.getItem('wfSstvTxMode') || S.txModeName;
            S.rxModeName = localStorage.getItem('wfSstvRxMode') || 'AUTO';
            S.fit = localStorage.getItem('wfSstvFit') === 'fill' ? 'fill' : 'fit';
            S.overlayOn = localStorage.getItem('wfSstvOverlayOn') !== '0';
            S.overlayText = (localStorage.getItem('wfSstvOverlay') || '').toUpperCase();
            var c = localStorage.getItem('wfSstvPipCorner');
            if (PIP_CORNERS.indexOf(c) >= 0) S.pipCorner = c;
        } catch (e) { /* no storage */ }
        var ov = $('sstvOverlayInput');
        if (ov) ov.value = S.overlayText;
    }
    function savePrefs() {
        try {
            localStorage.setItem('wfSstvTxMode', S.txModeName);
            localStorage.setItem('wfSstvRxMode', S.rxModeName);
            localStorage.setItem('wfSstvFit', S.fit);
            localStorage.setItem('wfSstvOverlayOn', S.overlayOn ? '1' : '0');
            localStorage.setItem('wfSstvOverlay', S.overlayText);
            localStorage.setItem('wfSstvPipCorner', S.pipCorner);
        } catch (e) { /* no storage */ }
    }
    function setOverlayPlaceholder() {
        var ov = $('sstvOverlayInput');
        if (ov) ov.placeholder = myCall() || 'CALLSIGN';
    }
    function overlayText() { return S.overlayOn ? (S.overlayText || myCall()) : ''; }

    function populateModes() {
        var rx = $('sstvRxModeSel'), tx = $('sstvTxModeSel');
        rx.innerHTML = '<option value="AUTO">AUTO</option>';
        tx.innerHTML = '';
        S.modes.forEach(function (m) {
            var o = document.createElement('option');
            o.value = m.name; o.textContent = m.name + ' · ' + m.w + '×' + m.h;
            tx.appendChild(o);
            var o2 = document.createElement('option');
            o2.value = m.name; o2.textContent = m.name;
            rx.appendChild(o2);
        });
        if (!modeByName(S.txModeName)) S.txModeName = S.modes[0] ? S.modes[0].name : S.txModeName;
        tx.value = S.txModeName;
        rx.value = modeByName(S.rxModeName) ? S.rxModeName : 'AUTO';
    }
    function modeByName(name) { return S.modes.filter(function (m) { return m.name === name; })[0] || null; }
    function modeById(id) { return S.modes.filter(function (m) { return m.id === id; })[0] || null; }

    // Bands the rig can tune: the picker's band table (bandLabelOf) says
    // whether a frequency is one of the rig's bands.
    function populateBands() {
        var sel = $('sstvBandSel');
        if (!sel) return;
        var labelOf = host('bandLabelOf');
        sel.innerHTML = '<option value="">BAND</option>';
        SSTV_BANDS.forEach(function (b) {
            if (typeof labelOf === 'function' && labelOf(b.hz) === null) return;
            var o = document.createElement('option');
            o.value = b.label; o.textContent = b.label;
            sel.appendChild(o);
        });
        sel.value = '';
    }
    function tuneBand(b) {
        hostSend({ cmd: 'setFrequency', value: b.hz });
        if (mode() !== b.mode && typeof window.setMode === 'function') window.setMode(b.mode);
        $('sstvBandSel').value = '';
    }

    // Desktop: RX and TX side by side, or the gallery (the GALLERY header
    // button); phones: one card at a time behind the pane tabs.
    function setPane(p) {
        S.pane = p;
        if (!bar) return;
        bar.dataset.pane = p;
        bar.querySelectorAll('.sstv-pane-tabs .wf-tab').forEach(function (t) { t.classList.toggle('active', t.dataset.pane === p); });
        $('sstvGalleryBtn').classList.toggle('active', p === 'gallery');
    }

    // ------------------------------------------------------------------
    // Open / close
    // ------------------------------------------------------------------
    function show() {
        if (!bar || S.visible) return;
        if (typeof window.closeOtherModes === 'function') window.closeOtherModes('sstv');
        S.visible = true; S.suspended = false;
        bar.classList.remove('hidden');
        document.body.classList.add('sstv-open');
        try { localStorage.setItem('wfSstvOpen', '1'); } catch (e) { /* no storage */ }
        if (typeof window.saveVfoSnapshot === 'function') window.saveVfoSnapshot();
        if (!isVoiceMode(mode()) && typeof window.setMode === 'function') window.setMode('USB');
        // The mic level the DIGI bar uses, put back on close.
        var slider = $('micGainSlider');
        S.savedModLevel = slider ? parseInt(slider.value, 10) : -1;
        if (typeof window.throttledSend === 'function' && typeof window.DIGI_MOD_LEVEL === 'number') {
            window.throttledSend('micGain', 'setMicGain', window.DIGI_MOD_LEVEL);
            if (typeof window.updateSlider === 'function') window.updateSlider('micGain', window.DIGI_MOD_LEVEL);
        }
        if (typeof window.initDigiTxCtx === 'function') window.initDigiTxCtx();
        populateBands();
        startWorker();
        setupStore();
        refreshGallery();
        setPane(S.pane);
        setStatus('Waiting for a signal…');
        sizeScope();
        clearScope();
        if (typeof window.renderBottomBar === 'function') window.renderBottomBar();
    }
    function hide() {
        if (!bar) return;
        var wasOpen = S.visible || S.suspended;
        S.visible = false; S.suspended = false;
        bar.classList.add('hidden');
        document.body.classList.remove('sstv-open');
        closeViewer();
        if (!wasOpen) return;
        try { localStorage.setItem('wfSstvOpen', '0'); } catch (e) { /* no storage */ }
        stopTx();
        stopWorker();
        if (S.savedModLevel >= 0 && typeof window.throttledSend === 'function') {
            window.throttledSend('micGain', 'setMicGain', S.savedModLevel);
            if (typeof window.updateSlider === 'function') window.updateSlider('micGain', S.savedModLevel);
        }
        S.savedModLevel = -1;
        if (typeof window.restoreVfoSnapshot === 'function') window.restoreVfoSnapshot();
        if (typeof window.renderBottomBar === 'function') window.renderBottomBar();
    }
    function toggle() { if (S.visible || S.suspended) hide(); else show(); }
    // The page's setPanel hides the panels while an overlay is up.
    function suspend() {
        if (!S.visible) return;
        S.visible = false; S.suspended = true;
        bar.classList.add('hidden');
    }
    function resume() {
        if (!S.suspended) return;
        S.suspended = false; S.visible = true;
        bar.classList.remove('hidden');
    }

    // ------------------------------------------------------------------
    // Worker / RX
    // ------------------------------------------------------------------
    function startWorker() {
        if (worker) { configureWorker(); return; }
        try {
            worker = new Worker('sstv-worker.js' + (window.__WFWEB_V__ ? '?v=' + window.__WFWEB_V__ : ''), { type: 'module' });
        } catch (e) {
            setStatus('No decoder: ' + (e.message || e));
            return;
        }
        worker.onmessage = onWorkerMessage;
        worker.onerror = function (e) { console.error('[SSTV] worker: ' + (e.message || e)); setStatus('Decoder error'); };
        S.inputRate = 0;
    }
    function stopWorker() {
        if (worker) { worker.terminate(); worker = null; }
        S.rx = null; S.inputRate = 0;
        setProgress('sstvRxProgress', 0);
    }
    function configureWorker() {
        if (!worker || !S.modes.length) return;
        var rate = host('audioSampleRate') || 48000;
        S.inputRate = rate;
        var pin = modeByName(S.rxModeName);
        worker.postMessage({ type: 'config', rate: rate, pinMode: pin ? pin.id : -1 });
        S.rx = null;
        S.rxForce = null;
        setProgress('sstvRxProgress', 0);
        $('sstvRxMeta').textContent = pin ? pin.name : 'AUTO';
    }
    // START: a picture whose VIS was missed. The worker locks on the next
    // three line sync pulses (one line apart) and begins the picture at the
    // first of them -- it keeps the last seconds of audio, so lines already
    // heard are not lost -- in the pinned RX mode, or under AUTO in the
    // mode the pulse spacing names. A decode in progress is dropped.
    function startRx() {
        if (!worker || !S.modes.length) return;
        var pinned = modeByName(S.rxModeName) || null;
        S.rx = null;
        S.rxForce = { mode: pinned };
        worker.postMessage({ type: 'start', pinMode: pinned ? pinned.id : -1 });
        setProgress('sstvRxProgress', 0);
        setStatus((pinned ? pinned.name + ' · ' : '') + 'START · waiting for sync pulses…');
        $('sstvRxMeta').textContent = pinned ? pinned.name : 'START';
    }
    function onWorkerMessage(e) {
        var m = e.data;
        switch (m.type) {
            case 'ready':
                S.modes = m.modes || [];
                populateModes();
                configureWorker();
                renderTxImage();
                break;
            case 'image': {
                var md = modeById(m.mode);
                S.rx = { mode: m.mode, name: md ? md.name : ('mode ' + m.mode), w: m.w, h: m.h, rows: 0, startedAt: Date.now(), freqHz: host('currentFreq') || 0, via: m.via || 'vis' };
                var cv = S.rxCanvas;
                cv.width = m.w; cv.height = m.h;
                var g = cv.getContext('2d');
                g.fillStyle = '#000'; g.fillRect(0, 0, m.w, m.h);
                setStatus(S.rx.name + (m.via === 'sync' ? ' · locked on the sync pulses' : ' · receiving'));
                $('sstvRxMeta').textContent = S.rx.name + ' ' + m.w + '×' + m.h;
                setProgress('sstvRxProgress', 0);
                break;
            }
            case 'row': {
                if (!S.rx || m.index >= S.rx.h) break;
                var ctx = S.rxCanvas.getContext('2d');
                var img = ctx.createImageData(S.rx.w, 1), d = img.data, src = m.rgb;
                for (var x = 0, p = 0, q = 0; x < S.rx.w; x++, p += 3, q += 4) { d[q] = src[p]; d[q + 1] = src[p + 1]; d[q + 2] = src[p + 2]; d[q + 3] = 255; }
                ctx.putImageData(img, 0, m.index);
                S.rx.rows = Math.max(S.rx.rows, m.index + 1);
                setProgress('sstvRxProgress', S.rx.rows / S.rx.h);
                setStatus(S.rx.name + ' · line ' + S.rx.rows + '/' + S.rx.h);
                break;
            }
            case 'done':
                if (S.rx) finishRx(!!m.complete);
                break;
            case 'spectrum':
                S.spec = { f0: m.f0, df: m.df, bins: m.bins.length };
                drawColumn(m.bins);
                break;
            case 'encoded':
                onEncoded(m);
                break;
            case 'error':
                console.error('[SSTV] ' + m.message);
                setStatus('Decoder: ' + m.message);
                break;
        }
    }
    // RX PCM from handleAudioData (Int16 at audioSampleRate).  While we
    // transmit, a rig with MONITOR on would feed our own tones back: the
    // worker gets silence of the same length so its timeline stays whole.
    function feedRx(int16) {
        if (!worker || !S.modes.length || !(S.visible || S.suspended)) return;
        if ((host('audioSampleRate') || 48000) !== S.inputRate) configureWorker();
        if (S.txBusy) worker.postMessage({ type: 'gap', samples: int16.length });
        else { var copy = int16.slice(); worker.postMessage({ type: 'pcm', int16: copy }, [copy.buffer]); }
    }
    function finishRx(complete) {
        var rx = S.rx;
        S.rx = null;
        var forced = S.rxForce;
        S.rxForce = null;
        setProgress('sstvRxProgress', 1);
        setStatus(rx.name + (complete ? ' · done' : ' · ended at line ' + rx.rows) + ' · saving…');
        var meta = { kind: 'rx', ts: new Date().toISOString(), mode: rx.name, freqHz: rx.freqHz || host('currentFreq') || 0, call: myCall(), complete: complete, w: rx.w, h: rx.h };
        S.rxCanvas.toBlob(function (blob) {
            if (!blob || !store) { setStatus(rx.name + ' · done'); return; }
            store.add(blob, meta).then(function () {
                setStatus(rx.name + (complete ? ' · done' : ' · ' + rx.rows + ' lines') + ' · saved');
                refreshGallery();
            }, function (e) { setStatus('Save failed: ' + (e && e.message || e)); });
        }, 'image/png');
        // Keep the picture on screen; the next VIS repaints it.
        S.lastRx = { name: rx.name };
        // A start by hand was for this picture only: back to the RX mode.
        if (forced) configureWorker();
    }
    function discardRx() {
        S.rx = null;
        if (S.rxForce) { S.rxForce = null; configureWorker(); }
        else if (worker) worker.postMessage({ type: 'reset' });
        var g = S.rxCanvas.getContext('2d');
        g.fillStyle = '#000'; g.fillRect(0, 0, S.rxCanvas.width, S.rxCanvas.height);
        setProgress('sstvRxProgress', 0);
        setStatus('Waiting for a signal…');
    }
    function setStatus(t) { S.status = t; var el = $('sstvStatus'); if (el) el.textContent = t; }
    function setProgress(id, frac) {
        var el = $(id);
        if (el) el.style.width = (Math.max(0, Math.min(1, frac)) * 100) + '%';
        if (id === 'sstvRxProgress') S.progress = frac;
    }

    // ------------------------------------------------------------------
    // TX
    // ------------------------------------------------------------------
    // A Blob / File as something drawImage takes.
    function decodeImage(blob) {
        var p = window.createImageBitmap ? window.createImageBitmap(blob) : Promise.reject(new Error('no createImageBitmap'));
        return p.catch(function () {
            // Safari < 15: an <img> from an object URL.
            return new Promise(function (resolve, reject) {
                var url = URL.createObjectURL(blob), img = new Image();
                img.onload = function () { URL.revokeObjectURL(url); resolve(img); };
                img.onerror = function () { URL.revokeObjectURL(url); reject(new Error('Cannot read that image')); };
                img.src = url;
            });
        });
    }
    function imgSize(img) { return { w: img.width || img.naturalWidth, h: img.height || img.naturalHeight }; }
    // The TX picture from a file, the camera or the gallery (opts.fromGallery:
    // it is there already); a new one goes to the gallery's LOADED section.
    function loadTxImage(file, opts) {
        opts = opts || {};
        var name = opts.name || file.name || 'camera';
        decodeImage(file).then(function (img) {
            S.txImage = img; S.txName = name;
            renderTxImage();
            setPane('tx');
            if (!opts.fromGallery) addLoadedToGallery(img, name);
        }, function (e) { setStatus(e && e.message || 'Cannot read that image'); });
    }
    // Keep a picture picked for TX in the gallery, at 640 px at most (the
    // largest mode is 640 wide): a phone photo as PNG would be far over the
    // server's 4 MB.
    function addLoadedToGallery(img, name) {
        if (!store) return;
        var sz = imgSize(img), s = Math.min(1, LOADED_MAX_PX / Math.max(sz.w, sz.h));
        var w = Math.max(1, Math.round(sz.w * s)), h = Math.max(1, Math.round(sz.h * s));
        var cv = document.createElement('canvas');
        cv.width = w; cv.height = h;
        var g = cv.getContext('2d');
        g.imageSmoothingEnabled = true; g.imageSmoothingQuality = 'high';
        g.drawImage(img, 0, 0, w, h);
        var meta = { kind: 'tx', name: String(name || '').slice(0, 64), ts: new Date().toISOString(), mode: '', freqHz: 0, call: myCall(), complete: true, w: w, h: h };
        cv.toBlob(function (blob) {
            if (!blob) return;
            store.add(blob, meta).then(refreshGallery, function (e) { setStatus('Gallery: ' + (e && e.message || e)); });
        }, 'image/png');
    }
    function setPip(img) {
        S.pip = img || null;
        renderTxImage();
    }
    function drawTxPlaceholder() {
        var cv = S.txCanvas, g = cv.getContext('2d');
        g.fillStyle = '#000'; g.fillRect(0, 0, cv.width, cv.height);
        g.fillStyle = '#666'; g.font = '14px sans-serif'; g.textAlign = 'center';
        g.fillText('IMAGE or CAMERA', cv.width / 2, cv.height / 2);
        g.textAlign = 'left';
    }
    // The picture at the TX mode's resolution, FIT (letterbox) or FILL
    // (centre crop), with the overlay text.
    function renderTxImage() {
        var m = modeByName(S.txModeName), cv = S.txCanvas;
        if (!cv) return;
        if (m && (cv.width !== m.w || cv.height !== m.h)) { cv.width = m.w; cv.height = m.h; }
        $('sstvTxMeta').textContent = m ? m.name + ' ' + m.w + '×' + m.h : '';
        if (!S.txImage) { drawTxPlaceholder(); updateButtons(); return; }
        var g = cv.getContext('2d'), W = cv.width, H = cv.height;
        var sz = imgSize(S.txImage), iw = sz.w, ih = sz.h;
        g.fillStyle = '#000'; g.fillRect(0, 0, W, H);
        var s = S.fit === 'fill' ? Math.max(W / iw, H / ih) : Math.min(W / iw, H / ih);
        var dw = iw * s, dh = ih * s;
        g.imageSmoothingEnabled = true; g.imageSmoothingQuality = 'high';
        g.drawImage(S.txImage, (W - dw) / 2, (H - dh) / 2, dw, dh);
        if (S.pip) {
            // A third of the frame in the chosen corner, a thin white border.
            var ps = imgSize(S.pip), k = Math.min((W / 3) / ps.w, (H / 3) / ps.h), pw = Math.round(ps.w * k), ph = Math.round(ps.h * k);
            var mg = Math.max(3, Math.round(W / 64)), bd = Math.max(1, Math.round(W / 160));
            var px = S.pipCorner === 'tl' || S.pipCorner === 'bl' ? mg : W - mg - pw;
            var py = S.pipCorner === 'tl' || S.pipCorner === 'tr' ? mg : H - mg - ph;
            g.fillStyle = '#fff'; g.fillRect(px - bd, py - bd, pw + 2 * bd, ph + 2 * bd);
            g.drawImage(S.pip, px, py, pw, ph);
        }
        var text = overlayText();
        if (text) {
            var px = Math.max(14, Math.round(H / 10));
            g.font = 'bold ' + px + 'px sans-serif';
            g.textBaseline = 'bottom';
            g.lineJoin = 'round';
            g.lineWidth = Math.max(2, px / 6);
            g.strokeStyle = '#000'; g.strokeText(text, px / 2, H - px / 3);
            g.fillStyle = '#fff'; g.fillText(text, px / 2, H - px / 3);
            g.textBaseline = 'alphabetic';
        }
        updateButtons();
    }
    function sendImage() {
        if (S.txBusy || !S.txImage || !worker) return;
        var m = modeByName(S.txModeName);
        if (!m) return;
        if (!isVoiceMode(mode())) { setStatus('Put the rig on USB / LSB / FM to transmit'); return; }
        if (typeof window.streamDigiAudio !== 'function') return;
        if (typeof window.initDigiTxCtx === 'function') window.initDigiTxCtx();
        renderTxImage();
        var img = S.txCanvas.getContext('2d').getImageData(0, 0, m.w, m.h).data;
        var rgb = new Uint8Array(m.w * m.h * 3);
        for (var i = 0, p = 0, q = 0; i < m.w * m.h; i++, p += 4, q += 3) { rgb[q] = img[p]; rgb[q + 1] = img[p + 1]; rgb[q + 2] = img[p + 2]; }
        S.txBusy = true; S.txSeconds = 0; S.txStartedAt = 0;
        setStatus(m.name + ' · encoding…');
        updateButtons();
        worker.postMessage({ type: 'encode', mode: m.id, rgb: rgb, rate: TX_RATE }, [rgb.buffer]);
    }
    function onEncoded(m) {
        if (!S.txBusy) return;                       // aborted while encoding
        var int16 = m.int16, samples = new Float32Array(int16.length);
        for (var i = 0; i < int16.length; i++) samples[i] = int16[i] / 32768;
        S.txSeconds = m.seconds || int16.length / TX_RATE;
        S.txHeaderSeconds = m.headerSeconds || 0;
        // Like FT8 / JS8 / RTTY: enableMic sets up the TX audio chain and the
        // rig's MOD input; without it the rig keys with nothing on the
        // modulator.  Put the mic back the way it was after.
        S.micWasOn = !!host('micEnabled');
        hostSend({ cmd: 'enableMic', value: true });
        window.digiTxActive = true;                  // the ALC loop runs while this is set
        var done = false;
        window.streamDigiAudio(samples,
            function () { if (done) return; done = true; unkey(); },
            function () {
                hostSend({ cmd: 'setPTT', value: true, digital: true });
                S.txStartedAt = Date.now();
                if (txTimer) clearInterval(txTimer);
                txTimer = setInterval(txTick, TX_TICK_MS);
                txTick();
            });
    }
    // The sweep: a line crossing the TX picture at the row going out, what
    // is still to come dimmed under it.  The rows start after the header.
    function txTick() {
        if (!S.txBusy || !S.txStartedAt) return;
        var t = (Date.now() - S.txStartedAt) / 1000;
        var lines = Math.max(0.1, S.txSeconds - S.txHeaderSeconds);
        setSweep(Math.max(0, Math.min(1, (t - S.txHeaderSeconds) / lines)));
        setStatus(S.txModeName + ' · TX ' + Math.floor(t) + ' / ' + Math.round(S.txSeconds) + ' s');
    }
    // The sweep element covers the picture inside the frame (the canvas is
    // object-fit: contain, so the picture's rectangle is computed here).
    function setSweep(frac) {
        var el = $('sstvTxSweep'), cv = S.txCanvas;
        if (!el || !cv) return;
        var fw = cv.clientWidth, fh = cv.clientHeight;
        if (!fw || !fh) return;
        var s = Math.min(fw / cv.width, fh / cv.height), dw = cv.width * s, dh = cv.height * s;
        el.style.left = (cv.offsetLeft + (fw - dw) / 2) + 'px';
        el.style.top = (cv.offsetTop + (fh - dh) / 2) + 'px';
        el.style.width = dw + 'px';
        el.style.height = dh + 'px';
        el.style.setProperty('--p', frac);
        S.txFrac = frac;
    }
    function unkey() {
        hostSend({ cmd: 'setPTT', value: false });
        if (!S.micWasOn) hostSend({ cmd: 'enableMic', value: false });
        window.digiTxActive = false;
        if (txTimer) { clearInterval(txTimer); txTimer = null; }
        S.txBusy = false; S.txStartedAt = 0; S.txFrac = 0;
        setStatus('Waiting for a signal…');
        updateButtons();
    }
    function stopTx() {
        if (!S.txBusy) return;
        if (typeof window.haltDigiTx === 'function') window.haltDigiTx();
        unkey();
    }
    function updateButtons() {
        if (!bar) return;
        var send = $('sstvSendBtn'), abort = $('sstvAbortBtn');
        send.disabled = S.txBusy || !S.txImage;
        abort.disabled = !S.txBusy;
        bar.classList.toggle('sstv-tx', S.txBusy);
        $('sstvFitBtn').textContent = S.fit === 'fill' ? 'FILL' : 'FIT';
        $('sstvFitBtn').classList.toggle('active', S.fit === 'fill');
        $('sstvOverlayBtn').classList.toggle('active', S.overlayOn);
        var pip = $('sstvPipBtn');
        pip.hidden = !S.pip;
        pip.textContent = 'PIP ' + S.pipCorner.toUpperCase();
        pip.classList.toggle('active', !!S.pip);
        $('sstvRxSaveBtn').disabled = false;
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
        if (w && h && (scope.width !== w || scope.height !== h)) { scope.width = w; scope.height = h; column = null; }
    }
    function clearScope() {
        if (scope && scopeCtx) { scopeCtx.fillStyle = '#000'; scopeCtx.fillRect(0, 0, scope.width, scope.height); }
    }
    // The worker's bins are dBFS-ish; map -70..-10 dB onto the LUT.
    function specAt(bins, f) {
        var sp = S.spec, x = (f - sp.f0) / sp.df;
        if (x <= 0) return bins[0];
        if (x >= sp.bins - 1) return bins[sp.bins - 1];
        var i = Math.floor(x), t = x - i;
        return bins[i] * (1 - t) + bins[i + 1] * t;
    }
    function drawColumn(bins) {
        if (!scope || !scopeCtx || !S.visible || !S.spec) return;
        var w = scope.width, h = scope.height;
        if (!w || !h) return;
        scopeCtx.drawImage(scope, 0, 0, w, h, -1, 0, w, h);
        if (!column || column.height !== h) column = scopeCtx.createImageData(1, h);
        var buf = column.data, span = STRIP_HI - STRIP_LO;
        for (var y = 0; y < h; y++) {
            var f = STRIP_LO + (1 - y / Math.max(1, h - 1)) * span;
            var db = specAt(bins, f);
            var v = Math.max(0, Math.min(255, Math.round((db + 70) / 60 * 255)));
            var c = colorLUT[v], p = y * 4;
            buf[p] = c[0]; buf[p + 1] = c[1]; buf[p + 2] = c[2]; buf[p + 3] = 255;
        }
        scopeCtx.putImageData(column, w - 1, 0);
    }
    // USB: audio = RF - dial, so the dial moves by (f - 1900) to bring the
    // tone at f onto 1900; LSB the other way.  FM / AM: the audio does not
    // move with the dial.
    function scopeClick(e) {
        var m = mode();
        if (m !== 'USB' && m !== 'LSB') return;
        var rect = scope.getBoundingClientRect();
        var frac = 1 - (e.clientY - rect.top) / rect.height;
        var f = STRIP_LO + frac * (STRIP_HI - STRIP_LO);
        var dial = host('currentFreq') || 0;
        if (dial <= 0) return;
        var delta = (m === 'USB' ? 1 : -1) * (f - LEADER_HZ);
        hostSend({ cmd: 'setFrequency', value: Math.round(dial + delta) });
    }

    // ------------------------------------------------------------------
    // Gallery stores: the server's REST image store, or IndexedDB
    // ------------------------------------------------------------------
    function ServerStore() {
        this.kind = 'server';
        this.base = '/api/v1/sstv/images';
    }
    ServerStore.prototype.list = function () {
        return fetch(this.base, { cache: 'no-store' }).then(function (r) {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.json();
        }).then(function (j) { return j.images || []; });
    };
    ServerStore.prototype.add = function (blob, meta) {
        var q = [];
        ['kind', 'name', 'mode', 'call', 'ts'].forEach(function (k) { if (meta[k]) q.push(k + '=' + encodeURIComponent(meta[k])); });
        q.push('freq=' + Math.round(meta.freqHz || 0), 'complete=' + (meta.complete ? 1 : 0), 'w=' + meta.w, 'h=' + meta.h);
        return fetch(this.base + '?' + q.join('&'), { method: 'POST', headers: { 'Content-Type': 'image/png' }, body: blob })
            .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
            .then(function (j) { return j.id; });
    };
    ServerStore.prototype.url = function (id) { return this.base + '/' + encodeURIComponent(id) + '.png'; };
    ServerStore.prototype.blob = function (id) { return fetch(this.url(id)).then(function (r) { return r.ok ? r.blob() : null; }); };
    ServerStore.prototype.remove = function (id) {
        return fetch(this.base + '/' + encodeURIComponent(id), { method: 'DELETE' }).then(function (r) { if (!r.ok && r.status !== 404) throw new Error('HTTP ' + r.status); });
    };
    ServerStore.prototype.onChange = function (cb) { this.changed = cb; };   // fed by SstvPanel.onGalleryChanged (the sstvGallery WS message)

    function IdbStore() {
        this.kind = 'local';
        this.urls = {};
        var self = this;
        this.db = new Promise(function (resolve, reject) {
            if (!window.indexedDB) { reject(new Error('no IndexedDB')); return; }
            var req = indexedDB.open('wfweb-sstv', 1);
            req.onupgradeneeded = function () { req.result.createObjectStore('images', { keyPath: 'id' }); };
            req.onsuccess = function () { resolve(req.result); };
            req.onerror = function () { reject(req.error); };
        });
        if (window.BroadcastChannel) {
            this.bc = new BroadcastChannel('wfweb-sstv');
            this.bc.onmessage = function () { if (self.changed) self.changed(); };
        }
    }
    IdbStore.prototype._tx = function (mode, fn) {
        return this.db.then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction('images', mode), st = tx.objectStore('images'), out = fn(st);
                tx.oncomplete = function () { resolve(out && out.result !== undefined ? out.result : out); };
                tx.onerror = function () { reject(tx.error); };
            });
        });
    };
    IdbStore.prototype.list = function () {
        return this._tx('readonly', function (st) { return st.getAll(); }).then(function (rows) {
            return (rows || []).map(function (r) { var m = r.meta; m.id = r.id; m.bytes = r.blob ? r.blob.size : 0; return m; })
                .sort(function (a, b) { return a.ts < b.ts ? 1 : -1; });
        });
    };
    IdbStore.prototype.add = function (blob, meta) {
        var id = meta.ts.replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z') + '-' + Math.floor(Math.random() * 65536).toString(16).padStart(4, '0');
        var self = this;
        return this._tx('readwrite', function (st) { return st.put({ id: id, meta: meta, blob: blob }); }).then(function () {
            if (self.bc) self.bc.postMessage({ changed: true });
            return id;
        });
    };
    IdbStore.prototype.blob = function (id) {
        return this._tx('readonly', function (st) { return st.get(id); }).then(function (r) { return r && r.blob || null; });
    };
    IdbStore.prototype.url = function (id) {
        var self = this;
        if (this.urls[id]) return Promise.resolve(this.urls[id]);
        return this.blob(id).then(function (b) { if (!b) return ''; self.urls[id] = URL.createObjectURL(b); return self.urls[id]; });
    };
    IdbStore.prototype.remove = function (id) {
        var self = this;
        return this._tx('readwrite', function (st) { return st.delete(id); }).then(function () {
            if (self.urls[id]) { URL.revokeObjectURL(self.urls[id]); delete self.urls[id]; }
            if (self.bc) self.bc.postMessage({ changed: true });
        });
    };
    IdbStore.prototype.onChange = function (cb) { this.changed = cb; };

    function setupStore() {
        if (store) return;
        store = window.wsTransport ? new ServerStore() : new IdbStore();
        S.galleryKind = store.kind;
        store.onChange(refreshGallery);
    }
    function galleryItem(id) { return S.gallery.filter(function (g) { return g.id === id; })[0] || null; }
    function refreshGallery() {
        if (!store) return;
        store.list().then(function (items) {
            S.gallery = items.slice(0, GALLERY_LIMIT);
            renderGallery();
        }, function (e) {
            $('sstvGalleryMeta').textContent = 'unavailable: ' + (e && e.message || e);
        });
    }
    function fmtTime(ts) {
        var d = new Date(ts);
        if (isNaN(d)) return String(ts || '');
        return d.toISOString().slice(0, 16).replace('T', ' ') + 'z';
    }
    function fmtFreq(hz) { return hz ? (hz / 1e6).toFixed(3) : ''; }
    function isLoaded(g) { return g.kind === 'tx'; }
    function itemLabel(g) { return isLoaded(g) ? (g.name || 'picture') : (g.mode || 'picture'); }
    // Two sections: RECEIVED (decoded off the air) and LOADED (picked for
    // TX, at the ready to be sent again).
    function renderGallery() {
        var el = $('sstvGallery');
        if (!el) return;
        $('sstvGalleryMeta').textContent = S.gallery.length + (S.galleryKind === 'server' ? ' on the server' : ' in this browser');
        el.innerHTML = '';
        [{ title: 'RECEIVED', items: S.gallery.filter(function (g) { return !isLoaded(g); }) },
         { title: 'LOADED', items: S.gallery.filter(isLoaded) }].forEach(function (sect) {
            var head = document.createElement('div');
            head.className = 'sstv-gallery-sect';
            head.textContent = sect.title + ' · ' + sect.items.length;
            el.appendChild(head);
            if (!sect.items.length) {
                var none = document.createElement('div');
                none.className = 'sstv-gallery-none';
                none.textContent = sect.title === 'LOADED' ? 'IMAGE or CAMERA on the TX card puts a picture here' : 'nothing received yet';
                el.appendChild(none);
            }
            sect.items.forEach(function (g) {
                var tile = document.createElement('div');
                tile.className = 'sstv-thumb' + (g.complete === false ? ' partial' : '') + (isLoaded(g) ? ' loaded' : '');
                tile.dataset.id = g.id;
                tile.title = itemLabel(g) + ' ' + fmtTime(g.ts) + (g.freqHz ? ' ' + fmtFreq(g.freqHz) + ' MHz' : '') + (g.complete === false ? ' (incomplete)' : '');
                tile.innerHTML = '<img alt="" loading="lazy"><div class="sstv-thumb-cap"><span>' + esc(itemLabel(g)) + '</span><span>' + esc(fmtTime(g.ts).slice(5, 16)) + '</span></div>';
                var img = tile.querySelector('img');
                Promise.resolve(store.url(g.id)).then(function (u) { if (u) img.src = u; });
                el.appendChild(tile);
            });
        });
    }
    function armDelete(id) {
        disarmDelete();
        var b = $('sstvViewerDelete');
        b.textContent = 'SURE?'; b.classList.add('armed');
        deleteArmed = { id: id, timer: setTimeout(disarmDelete, 4000) };
    }
    function disarmDelete() {
        if (deleteArmed) clearTimeout(deleteArmed.timer);
        deleteArmed = null;
        var b = $('sstvViewerDelete');
        if (b) { b.textContent = 'DELETE'; b.classList.remove('armed'); }
    }
    function openViewer(id) {
        var item = galleryItem(id);
        if (!item) return;
        viewerId = id;
        var v = $('sstvViewer'), img = $('sstvViewerImg');
        img.removeAttribute('src');
        Promise.resolve(store.url(id)).then(function (u) { if (u && viewerId === id) img.src = u; });
        disarmDelete();
        $('sstvViewerMeta').textContent = (isLoaded(item) ? 'LOADED · ' : 'RECEIVED · ') + itemLabel(item) + ' · ' + fmtTime(item.ts) + (item.freqHz ? ' · ' + fmtFreq(item.freqHz) + ' MHz' : '') + (item.call ? ' · ' + item.call : '') + (item.complete === false ? ' · incomplete' : '') + (item.w && item.h ? ' · ' + item.w + '×' + item.h : '');
        v.style.display = 'flex';
    }
    function closeViewer() { viewerId = null; disarmDelete(); var v = $('sstvViewer'); if (v) v.style.display = 'none'; }
    function fileName(modeName, ms, hz) {
        var d = new Date(ms || Date.now());
        var stamp = d.toISOString().replace(/[-:]/g, '').replace(/\..+$/, '').replace('T', '_');
        var khz = Math.round((hz || host('currentFreq') || 0) / 1000);
        return 'wfweb_' + (myCall() || 'NOCALL') + '_' + stamp + '_' + khz + 'kHz_SSTV_' + String(modeName || '').replace(/[^A-Za-z0-9]+/g, '') + '.png';
    }
    function saveBlob(blob, name) {
        var url = URL.createObjectURL(blob), a = document.createElement('a');
        a.href = url; a.download = name;
        document.body.appendChild(a); a.click(); a.remove();
        setTimeout(function () { URL.revokeObjectURL(url); }, 60000);
    }

    // ------------------------------------------------------------------
    // Log
    // ------------------------------------------------------------------
    // The log modal with a new row to fill in (mode SSTV from
    // defaultLogMode); no prompt(), which would stall the page.
    function logQso() {
        if (typeof window.openLogModal === 'function') window.openLogModal();
        if (typeof window.addDigiQso === 'function') window.addDigiQso();
    }

    window.SstvPanel = {
        toggle: toggle, show: show, hide: hide,
        visible: function () { return S.visible; },
        suspend: suspend, resume: resume,
        feedRx: feedRx,
        onGalleryChanged: function () { if (store && store.changed) store.changed(); },
        // The TX picture from code (tests, a future paste / drop path): a
        // Blob / File, an ImageBitmap or an <img>.
        setTxImage: function (src, name, opts) {
            if (src && typeof src.arrayBuffer === 'function') { loadTxImage(src, Object.assign({ name: name }, opts || {})); return; }
            S.txImage = src || null; S.txName = name || ''; renderTxImage();
        },
        setPip: setPip,
        startRx: startRx,
        send: sendImage, abort: stopTx,
        state: S,
        bands: SSTV_BANDS,
    };
    window.toggleSstvBar = toggle;
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', build);
    else build();
})();
