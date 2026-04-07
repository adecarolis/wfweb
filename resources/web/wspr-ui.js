function isWsprMode() { return digiMode === 'WSPR'; }
function getWsprSlotInfoAtMs(ms) {
    var slotIndex = Math.floor(ms / WSPR_RX_SLOT_MS);
    var slotStartMs = slotIndex * WSPR_RX_SLOT_MS;
    return {
        slotIndex: slotIndex,
        slotStartMs: slotStartMs,
        slotPhaseMs: ms - slotStartMs,
        remainingMs: WSPR_RX_SLOT_MS - (ms - slotStartMs)
    };
}

function formatWsprSlotLabel(slotIndex) {
    if (slotIndex < 0) return '--';
    return formatUtcTime(slotIndex * WSPR_RX_SLOT_MS);
}

function canBuildWsprMessageSilently() {
    if (!window.wsprEncodeLib) return false;
    var call = getMyCall();
    var grid = getMyGrid();
    return call.length > 0 && grid.length >= 4;
}

function ensureWsprRxBuffer() {
    if (!digiWsprRxBuffer || digiWsprRxBuffer.length !== WSPR_RX_BUFFER_SAMPLES) {
        digiWsprRxBuffer = new Float32Array(WSPR_RX_BUFFER_SAMPLES);
    }
}

function resetWsprRxState(preserveSummaries) {
    resetDigiAudioClock();
    digiWsprRxWritePos = 0;
    digiWsprRxSlotIndex = -1;
    digiWsprRxSlotStartMs = 0;
    digiWsprRxDialFrequencyHz = 0;
    digiWsprRxAudioFrequencyHz = 1500;
    digiWsprRxReporter = null;
    digiWsprRxSlotEligible = false;
    digiWsprRetuneBlockedSlotIndex = -1;
    digiWsprRxSampleClockMs = 0;
    digiWsprLastCapturedSlotIndex = -1;
    digiWsprDecodeInProgress = false;
    digiWsprPendingDecodes = [];
    digiWsprDecodeEpoch++;
    digiWsprLastFillRatio = 0;
    digiWsprLastSampleCount = 0;
    digiWsprLastDecodeDebug = null;
    digiWsprLastDecodeError = '';
    if (wsprWorker) {
        try { wsprWorker.terminate(); } catch (e) {}
        wsprWorker = null;
    }
    if (!preserveSummaries) {
        digiWsprLastRxSummary = '--';
        digiWsprLastDecodeSummary = '--';
    }
}

function willWsprTransmitInSlot(slotIndex) {
    if (slotIndex < 0) return false;
    if (digiWsprTargetSlotIndex >= 0 && digiWsprTargetSlotIndex === slotIndex) return true;
    if (digiTxActive && getWsprSlotInfoAtMs(getServerNowMs()).slotIndex === slotIndex) return true;
    if (!digiTxEnabled) return false;
    if (!canBuildWsprMessageSilently()) return false;
    return digiWsprManualEnable && digiWsprTargetSlotIndex === slotIndex;
}

function isWsprRxCaptureEligible(slotIndex) {
    if (!isWsprMode()) return false;
    if (!digiRxEnabled) return false;
    if (digiTxActive) return false;
    return !willWsprTransmitInSlot(slotIndex);
}

function getWsprRxStateText() {
    if (!isWsprMode()) return 'OFF';
    if (!digiRxEnabled) return 'MON OFF';
    if (digiWsprDecodeInProgress) return 'DECODING';
    if (digiTxActive) return 'TX';
    if (digiWsprRxSlotIndex >= 0 && digiWsprRxSlotEligible && digiWsprTargetFreq && !isWsprRetuneReady()) return 'RETUNE';
    if (digiWsprRxSlotIndex >= 0 && digiWsprRxSlotEligible) return 'CAPTURE';
    if (digiWsprRxSlotIndex >= 0 && willWsprTransmitInSlot(digiWsprRxSlotIndex)) return 'SKIP TX';
    return 'IDLE';
}

function renderWsprSpots() {
    var panel = document.getElementById('digiWsprSpotsPanel');
    if (!panel) return;
    if (!digiWsprSpots.length) {
        panel.innerHTML = '';
        return;
    }
    var html = '';
    for (var i = 0; i < digiWsprSpots.length; i++) {
        var spot = digiWsprSpots[i];
        html += '<div class="digi-wspr-spot-row">' +
                '<span class="digi-wspr-spot-time">' + escapeHtml(spot.time || '--') + '</span>' +
                '<span class="digi-wspr-spot-text">' + escapeHtml(spot.text || '') + '</span>' +
                '<span class="digi-wspr-spot-meta">' + escapeHtml(spot.meta || '') + '</span>' +
                '</div>';
    }
    panel.innerHTML = html;
}

function clearWsprSpots() {
    digiWsprSpots = [];
    renderWsprSpots();
    persistWsprSpots();
    updateWsprInfo(getDigiSlotInfo());
}

function syncStationSettingsUi() {
    var wsprCb = document.getElementById('digiSettingsWsprnetEnable');
    if (wsprCb) wsprCb.checked = !!digiWsprUploadEnabled;
    var pskrCb = document.getElementById('digiSettingsPskReporterEnable');
    if (pskrCb) pskrCb.checked = !!digiPskReporterEnabled;
    var reporterCb = document.getElementById('digiReporterEnable');
    if (reporterCb) reporterCb.checked = !!reporterEnabled;
}

function sanitizeWsprUploadReport(raw) {
    if (!raw || typeof raw !== 'object') return null;
    var receivedUtcMs = parseInt(raw.receivedUtcMs, 10);
    if (isNaN(receivedUtcMs)) receivedUtcMs = 0;
    var retries = parseInt(raw.retries, 10);
    if (isNaN(retries)) retries = 0;
    var httpStatus = parseInt(raw.httpStatus, 10);
    if (isNaN(httpStatus)) httpStatus = 0;
    var rejected = parseInt(raw.rejected, 10);
    if (isNaN(rejected)) rejected = 0;
    var accepted = parseInt(raw.accepted, 10);
    if (isNaN(accepted)) accepted = 0;
    var duplicates = parseInt(raw.duplicates, 10);
    if (isNaN(duplicates)) duplicates = 0;
    return {
        receivedUtcMs: receivedUtcMs,
        status: String(raw.status || '').trim().toLowerCase(),
        functionName: String(raw.function || '').trim().toLowerCase(),
        summary: String(raw.summary || '').trim(),
        detail: String(raw.detail || raw.error || '').trim(),
        responseKind: String(raw.responseKind || '').trim().toLowerCase(),
        batchId: String(raw.batchId || '').trim(),
        retries: retries,
        httpStatus: httpStatus,
        rejected: rejected,
        accepted: accepted,
        duplicates: duplicates
    };
}

function setWsprUploadHistory(status) {
    var recent = [];
    if (status && Array.isArray(status.recent)) {
        for (var i = 0; i < status.recent.length; i++) {
            var clean = sanitizeWsprUploadReport(status.recent[i]);
            if (clean) recent.push(clean);
        }
    }
    recent.sort(function(a, b) { return (b.receivedUtcMs || 0) - (a.receivedUtcMs || 0); });
    if (recent.length > 20) recent = recent.slice(0, 20);
    digiWsprUploadRecentReports = recent;
    digiWsprUploadLatestReport = sanitizeWsprUploadReport(status && status.latest ? status.latest : recent[0]);
    renderWsprUploadReports();
}

function getWsprUploadReportBadge(entry) {
    if (!entry) return 'IDLE';
    switch (entry.status) {
        case 'local_rejected': return 'LOCAL REJECT';
        case 'queued': return 'QUEUED';
        case 'uploading': return 'POSTING';
        case 'duplicate': return 'DUPLICATE';
        case 'retry': return 'RETRY ' + entry.retries;
        case 'accepted':
            if (entry.functionName === 'wsprstat') return entry.responseKind === 'html_ok' ? 'STATUS OK' : 'STATUS OK';
            return entry.responseKind === 'html_ok' ? 'REPORT OK' : 'REPORT OK';
        case 'error':
            return entry.responseKind === 'html_error' ? 'HTML ERR' : 'REMOTE ERR';
        default:
            return entry.status ? entry.status.toUpperCase() : 'IDLE';
    }
}

function getWsprUploadReportTone(entry) {
    if (!entry) return '';
    if (entry.status === 'accepted') return 'ok';
    if (entry.status === 'error' || entry.status === 'local_rejected') return 'err';
    if (entry.status === 'retry' || entry.status === 'duplicate') return 'warn';
    return '';
}

function renderWsprUploadReports() {
    var panel = document.getElementById('digiWsprReportsPanel');
    if (!panel) return;
    if (!digiWsprUploadRecentReports.length) {
        panel.innerHTML = '';
        return;
    }
    var html = '';
    for (var i = 0; i < digiWsprUploadRecentReports.length; i++) {
        var entry = digiWsprUploadRecentReports[i];
        var timeText = entry.receivedUtcMs ? formatUtcTime(entry.receivedUtcMs) : '--:--:--Z';
        var badge = getWsprUploadReportBadge(entry);
        var tone = getWsprUploadReportTone(entry);
        var summary = escapeHtml(entry.summary || '--');
        var detail = entry.detail ? ('<div class="digi-wspr-report-detail">' + escapeHtml(entry.detail) + '</div>') : '';
        html += '<div class="digi-wspr-report-row ' + tone + '">' +
                '<span class="digi-wspr-report-time">' + timeText + '</span>' +
                '<span class="digi-wspr-report-status">' + escapeHtml(badge) + '</span>' +
                '<div class="digi-wspr-report-text">' +
                '<div class="digi-wspr-report-summary">' + summary + '</div>' +
                detail +
                '</div>' +
                '</div>';
    }
    panel.innerHTML = html;
}

function summarizeWsprUploadState(status) {
    var pending = parseInt(status && status.pending, 10) || 0;
    var inFlight = !!(status && status.inFlight);
    var latest = digiWsprUploadLatestReport;

    if (latest && latest.status === 'local_rejected') {
        var latestRejected = Math.max(1, latest.rejected || 0);
        if ((latest.accepted || 0) > 0 || (latest.duplicates || 0) > 0) return 'PARTIAL ' + latestRejected;
        return 'LOCAL REJECT ' + latestRejected;
    }
    if (inFlight) return 'UPLOADING';
    if (digiWsprUploadQueue.length) return 'QUEUED ' + digiWsprUploadQueue.length;
    if (pending > 0) return 'SERVER ' + pending;
    if (latest) {
        if (latest.status === 'accepted') return latest.functionName === 'wsprstat' ? 'STATUS OK' : 'REPORT OK';
        if (latest.status === 'duplicate') return 'DUPLICATE';
        if (latest.status === 'retry') return 'RETRY ' + latest.retries;
        if (latest.status === 'error') return latest.responseKind === 'html_error' ? 'HTML ERR' : 'REMOTE ERR';
        if (latest.status === 'queued') return 'READY';
    }
    if (status && status.lastError) return 'ERROR';
    if (status && status.lastResult === 'done') return 'DONE';
    return 'IDLE';
}

function isPskReporterCallLike(value) {
    var token = String(value || '').trim().toUpperCase();
    return /^[A-Z0-9/]{3,15}$/.test(token) && /[A-Z]/.test(token) && /\d/.test(token);
}

function isPskReporterGridLike(value) {
    return /^[A-R]{2}[0-9]{2}([A-X]{2})?$/.test(String(value || '').trim().toUpperCase());
}

function sanitizePskReporterBatch(batch) {
    if (!batch || typeof batch !== 'object') return null;
    var reporter = batch.reporter || {};
    var reporterCall = String(reporter.call || '').trim().toUpperCase();
    var reporterGrid = String(reporter.grid || '').trim().toUpperCase();
    if (!isPskReporterCallLike(reporterCall) || !isPskReporterGridLike(reporterGrid)) return null;
    var slotStartMs = parseInt(batch.slotStartMs, 10);
    if (isNaN(slotStartMs) || slotStartMs <= 0) slotStartMs = Date.now();
    var spots = [];
    if (Array.isArray(batch.spots)) {
        for (var i = 0; i < batch.spots.length; i++) {
            var raw = batch.spots[i] || {};
            var call = String(raw.call || '').trim().toUpperCase();
            var grid = String(raw.grid || '').trim().toUpperCase();
            var mode = String(raw.mode || '').trim().toUpperCase();
            var freqHz = parseInt(raw.freqHz, 10);
            var snr = Math.round(Number(raw.snr));
            var flowStartSeconds = parseInt(raw.flowStartSeconds, 10);
            if (!isPskReporterCallLike(call) || !isPskReporterGridLike(grid) ||
                !isFinite(freqHz) || freqHz <= 0 ||
                mode !== 'WSPR' ||
                !isFinite(snr) ||
                !isFinite(flowStartSeconds) || flowStartSeconds <= 0) {
                continue;
            }
            spots.push({
                call: call,
                grid: grid,
                mode: mode,
                freqHz: freqHz,
                snr: snr,
                flowStartSeconds: flowStartSeconds
            });
        }
    }
    return {
        batchId: String(batch.batchId || ''),
        slotStartMs: slotStartMs,
        reporter: {
            call: reporterCall,
            grid: reporterGrid,
            software: String(reporter.software || ('wfweb/' + (serverVersion || 'dev'))).substr(0, 64)
        },
        spots: spots
    };
}

function sanitizePskReporterStatus(status) {
    if (!status || typeof status !== 'object') return { pending: 0, latest: null, recent: [] };
    var recent = Array.isArray(status.recent) ? status.recent : [];
    recent = recent.map(function(entry) {
        return {
            receivedUtcMs: parseInt(entry.receivedUtcMs, 10) || 0,
            status: String(entry.status || '').trim().toLowerCase(),
            mode: String(entry.mode || '').trim().toUpperCase(),
            summary: String(entry.summary || '').trim(),
            detail: String(entry.detail || entry.error || '').trim()
        };
    });
    recent.sort(function(a, b) { return (b.receivedUtcMs || 0) - (a.receivedUtcMs || 0); });
    var latest = null;
    if (status.latest && typeof status.latest === 'object') {
        latest = {
            receivedUtcMs: parseInt(status.latest.receivedUtcMs, 10) || 0,
            status: String(status.latest.status || '').trim().toLowerCase(),
            mode: String(status.latest.mode || '').trim().toUpperCase(),
            summary: String(status.latest.summary || '').trim(),
            detail: String(status.latest.detail || status.latest.error || '').trim()
        };
    }
    if (!latest && recent.length) latest = recent[0];
    return {
        pending: parseInt(status.pending, 10) || 0,
        lastResult: String(status.lastResult || ''),
        lastError: String(status.lastError || ''),
        nextFlushInMs: parseInt(status.nextFlushInMs, 10) || 0,
        latest: latest,
        recent: recent
    };
}

function summarizePskReporterState(status) {
    var clean = sanitizePskReporterStatus(status);
    digiPskReporterLatestReport = clean.latest;
    digiPskReporterRecentReports = clean.recent;

    if (!digiPskReporterEnabled) return 'PSKR OFF';
    if (digiPskReporterQueue.length) return 'PSKR Q ' + digiPskReporterQueue.length;
    if (digiPskReporterInFlight) return 'PSKR POST';
    if (clean.pending > 0) return 'PSKR WAIT ' + clean.pending;
    if (clean.latest) {
        if (clean.latest.status === 'local_rejected') return 'PSKR REJECT';
        if (clean.latest.status === 'partial') return 'PSKR PART';
        if (clean.latest.status === 'duplicate') return 'PSKR DUP';
        if (clean.latest.status === 'queued') return 'PSKR READY';
        if (clean.latest.status === 'sent') return 'PSKR SENT';
        if (clean.latest.status === 'error') return 'PSKR ERR';
    }
    if (clean.lastError) return 'PSKR ERR';
    return 'PSKR READY';
}

function updatePskReporterButton() {
    var btn = document.getElementById('digiPskReporterBtn');
    if (!btn) return;
    btn.textContent = digiPskReporterEnabled ? 'PSKR ON' : 'PSKR OFF';
    btn.classList.toggle('active', digiPskReporterEnabled);
    var latest = digiPskReporterLatestReport;
    btn.title = latest && latest.detail ? latest.detail : 'Upload WSPR decodes to PSK Reporter';
    syncStationSettingsUi();
}

function isPskReporterModeActive() {
    return digiPskReporterEnabled && isWsprMode();
}

function shouldRunPskReporterTransport() {
    return digiPskReporterEnabled && (isWsprMode() || digiPskReporterInFlight || digiPskReporterQueue.length > 0);
}

function updatePskReporterStatusUi() {
    var el = document.getElementById('digiPskReporterStatus');
    if (el) el.textContent = digiPskReporterStatusText;
    updatePskReporterButton();
}

function refreshPskReporterStatus() {
    if (!shouldRunPskReporterTransport() || !window.fetch) {
        digiPskReporterStatusText = digiPskReporterEnabled ? 'PSKR WSPR' : 'PSKR OFF';
        updatePskReporterStatusUi();
        return;
    }
    fetch('/api/v1/pskreporter/status', { method: 'GET' })
        .then(parseApiJsonResponse)
        .then(function(resp) {
            digiPskReporterStatusText = summarizePskReporterState(resp);
            updatePskReporterStatusUi();
        })
        .catch(function() {
            if (digiPskReporterEnabled && !digiPskReporterQueue.length && !digiPskReporterInFlight) {
                digiPskReporterStatusText = 'PSKR READY';
                updatePskReporterStatusUi();
            }
        });
}

function flushPskReporterQueue() {
    if (!shouldRunPskReporterTransport() || !window.fetch) return;
    if (digiPskReporterInFlight || !digiPskReporterQueue.length) return;
    var batch = sanitizePskReporterBatch(digiPskReporterQueue[0]);
    if (!batch || !batch.spots.length) {
        digiPskReporterQueue.shift();
        flushPskReporterQueue();
        return;
    }
    digiPskReporterInFlight = true;
    digiPskReporterStatusText = 'PSKR POST';
    updatePskReporterStatusUi();
    fetch('/api/v1/pskreporter/upload', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(batch)
    })
        .then(parseApiJsonResponse)
        .then(function(resp) {
            digiPskReporterInFlight = false;
            var accepted = parseInt(resp.accepted, 10) || 0;
            var duplicates = parseInt(resp.duplicates, 10) || 0;
            var rejected = parseInt(resp.rejected, 10) || 0;
            if (accepted > 0 || duplicates > 0 || rejected > 0) {
                digiPskReporterQueue.shift();
            }
            digiPskReporterStatusText = summarizePskReporterState(resp);
            updatePskReporterStatusUi();
            if (digiPskReporterQueue.length) flushPskReporterQueue();
        })
        .catch(function(err) {
            digiPskReporterInFlight = false;
            digiPskReporterStatusText = 'PSKR ERR';
            digiPskReporterLatestReport = {
                status: 'error',
                detail: String(err && err.message || err || 'upload failed')
            };
            updatePskReporterStatusUi();
            if (digiPskReporterRetryTimer) clearTimeout(digiPskReporterRetryTimer);
            digiPskReporterRetryTimer = setTimeout(function() {
                digiPskReporterRetryTimer = null;
                flushPskReporterQueue();
            }, 5000);
        });
}

function queuePskReporterBatch(batch) {
    var clean = sanitizePskReporterBatch(batch);
    if (!clean || !clean.spots.length) return;
    digiPskReporterQueue.push(clean);
    digiPskReporterStatusText = 'PSKR Q ' + digiPskReporterQueue.length;
    updatePskReporterStatusUi();
    flushPskReporterQueue();
}

function setPskReporterEnabled(enabled) {
    digiPskReporterEnabled = !!enabled;
    try { localStorage.setItem(PSK_REPORTER_ENABLED_KEY, digiPskReporterEnabled ? '1' : '0'); } catch (e) {}
    digiPskReporterStatusText = digiPskReporterEnabled
        ? (isWsprMode() ? 'PSKR READY' : 'PSKR WSPR')
        : 'PSKR OFF';
    updatePskReporterStatusUi();
    syncStationSettingsUi();
    if (isPskReporterModeActive()) {
        refreshPskReporterStatus();
        flushPskReporterQueue();
    }
}

function buildWsprPskReporterBatch(slotIndex, slotStartMs, reporter, spots) {
    if (!digiPskReporterEnabled || !reporter) return null;
    var reporterCall = String(reporter.call || getMyCall() || '').trim().toUpperCase();
    var reporterGrid = String(reporter.grid || getMyGridFull() || '').trim().toUpperCase();
    if (!isPskReporterCallLike(reporterCall) || !isPskReporterGridLike(reporterGrid)) return null;

    var cleanSpots = [];
    var seen = {};
    for (var i = 0; i < (spots || []).length; i++) {
        var clean = sanitizeWsprSpot(spots[i]);
        if (!clean || !isPskReporterCallLike(clean.call) || !isPskReporterGridLike(clean.grid)) continue;
        var freqHz = parseInt(clean.freqHz, 10);
        if (!isFinite(freqHz) || freqHz <= 0) continue;
        var flowStartSeconds = Math.floor((clean.slotStartMs || slotStartMs || Date.now()) / 1000);
        if (!isFinite(flowStartSeconds) || flowStartSeconds <= 0) continue;
        var dedupeKey = [clean.call, clean.grid, 'WSPR', freqHz, flowStartSeconds].join(':');
        if (seen[dedupeKey]) continue;
        seen[dedupeKey] = true;
        cleanSpots.push({
            call: clean.call,
            grid: clean.grid,
            mode: 'WSPR',
            freqHz: freqHz,
            snr: Math.round(clean.snr),
            flowStartSeconds: flowStartSeconds
        });
    }
    if (!cleanSpots.length) return null;

    return {
        batchId: ['psk', 'WSPR', slotIndex, reporterCall, reporterGrid].join(':'),
        slotStartMs: parseInt(slotStartMs, 10) || 0,
        reporter: {
            call: reporterCall,
            grid: reporterGrid,
            software: String(reporter.version || ('wfweb/' + (serverVersion || 'dev'))).substr(0, 64)
        },
        spots: cleanSpots
    };
}

function sanitizeWsprSpot(raw) {
    if (!raw) return null;
    var slotIndex = parseInt(raw.slotIndex, 10);
    if (isNaN(slotIndex)) slotIndex = -1;
    var slotStartMs = parseInt(raw.slotStartMs, 10);
    if (isNaN(slotStartMs)) slotStartMs = 0;
    var freqHz = parseInt(raw.freqHz, 10);
    if (isNaN(freqHz)) {
        var freqMHz = Number(raw.freq);
        if (!isFinite(freqMHz) || freqMHz <= 0) return null;
        freqHz = Math.round(freqMHz * 1000000);
    }
    if (!isFinite(freqHz) || freqHz <= 0) return null;

    var call = String(raw.call || '').trim().toUpperCase();
    var grid = String(raw.grid || '').trim().toUpperCase();
    var dbm = parseInt(raw.dbm, 10);
    if (isNaN(dbm)) dbm = 0;
    var drift = parseInt(raw.drift, 10);
    if (isNaN(drift)) drift = 0;
    var sync = parseInt(raw.sync, 10);
    if (isNaN(sync)) sync = 0;
    var snr = Number(raw.snr);
    if (!isFinite(snr)) snr = 0;
    var dt = Number(raw.dt);
    if (!isFinite(dt)) dt = 0;
    var date = String(raw.date || (slotStartMs ? formatDate6(slotStartMs) : ''));
    var time = slotStartMs ? formatUtcSpotTimestamp(slotStartMs) : String(raw.time || '');
    var text = String(raw.text || raw.message || '').trim();
    if (!text) {
        text = call;
        if (grid) text += ' ' + grid;
        text += ' ' + dbm;
    }
    var meta = String(raw.meta || (snr.toFixed(0) + ' dB | ' + dt.toFixed(1) + ' s | ' + (freqHz / 1000000).toFixed(6) + ' MHz | drift ' + drift));
    var spotId = String(raw.spotId || ['wspr', slotIndex, freqHz, call, grid, dbm, drift].join(':'));

    return {
        spotId: spotId,
        slotIndex: slotIndex,
        slotStartMs: slotStartMs,
        date: date,
        time: time,
        call: call,
        grid: grid,
        dbm: dbm,
        drift: drift,
        sync: sync,
        snr: snr,
        dt: dt,
        freqHz: freqHz,
        freq: freqHz / 1000000,
        dialFrequencyHz: parseInt(raw.dialFrequencyHz, 10) || 0,
        audioFrequencyHz: parseInt(raw.audioFrequencyHz, 10) || WSPR_DEFAULT_AUDIO_FREQ,
        message: String(raw.message || text),
        text: text,
        meta: meta
    };
}

function mergeWsprSpots(spots) {
    if (!spots || !spots.length) return 0;
    var existing = {};
    for (var i = 0; i < digiWsprSpots.length; i++) existing[digiWsprSpots[i].spotId] = true;
    var additions = [];
    for (var j = 0; j < spots.length; j++) {
        var clean = sanitizeWsprSpot(spots[j]);
        if (!clean || existing[clean.spotId]) continue;
        existing[clean.spotId] = true;
        additions.push(clean);
    }
    if (!additions.length) return 0;
    additions.sort(function(a, b) {
        if (a.slotStartMs !== b.slotStartMs) return b.slotStartMs - a.slotStartMs;
        return b.freqHz - a.freqHz;
    });
    digiWsprSpots = additions.concat(digiWsprSpots);
    if (digiWsprSpots.length > 100) digiWsprSpots.splice(100);
    renderWsprSpots();
    persistWsprSpots();
    return additions.length;
}

function sanitizeWsprUploadBatch(batch) {
    if (!batch || typeof batch !== 'object') return null;
    var reporter = batch.reporter || {};
    var call = String(reporter.call || '').trim().toUpperCase();
    var grid = String(reporter.grid || '').trim().toUpperCase();
    var rqrgHz = parseInt(reporter.rqrgHz, 10);
    if (!call || !grid || !isFinite(rqrgHz) || rqrgHz <= 0) return null;
    var tqrgHz = parseInt(reporter.tqrgHz, 10);
    if (!isFinite(tqrgHz) || tqrgHz <= 0) tqrgHz = rqrgHz;
    var txPct = sanitizeWsprTxPct(reporter.txPct);
    var dbm = parseInt(reporter.dbm, 10);
    if (isNaN(dbm)) dbm = digiWsprPower;
    var slotIndex = parseInt(batch.slotIndex, 10);
    if (isNaN(slotIndex)) slotIndex = -1;
    var slotStartMs = parseInt(batch.slotStartMs, 10);
    if (isNaN(slotStartMs)) slotStartMs = 0;
    var createdAtMs = parseInt(batch.createdAtMs, 10);
    if (isNaN(createdAtMs) || createdAtMs <= 0) createdAtMs = slotStartMs > 0 ? slotStartMs : Date.now();
    var spots = [];
    if (Array.isArray(batch.spots)) {
        for (var i = 0; i < batch.spots.length; i++) {
            var cleanSpot = sanitizeWsprSpot(batch.spots[i]);
            if (cleanSpot) spots.push(cleanSpot);
        }
    }
    return {
        batchId: String(batch.batchId || ['wspr', slotIndex, rqrgHz, call, grid].join(':')),
        slotIndex: slotIndex,
        slotStartMs: slotStartMs,
        createdAtMs: createdAtMs,
        mode: 'WSPR',
        reporter: {
            call: call,
            grid: grid,
            rqrgHz: rqrgHz,
            tqrgHz: tqrgHz,
            txPct: txPct,
            dbm: dbm,
            version: String(reporter.version || ('wfweb/' + (serverVersion || 'dev')))
        },
        spots: spots
    };
}

function isWsprUploadBatchExpired(batch, nowMs) {
    var clean = sanitizeWsprUploadBatch(batch);
    if (!clean) return true;
    var ageMs = (nowMs || Date.now()) - clean.createdAtMs;
    return ageMs > WSPR_UPLOAD_MAX_AGE_MS;
}

function mergeWsprUploadBatches(existingBatch, incomingBatch) {
    var existing = sanitizeWsprUploadBatch(existingBatch);
    var incoming = sanitizeWsprUploadBatch(incomingBatch);
    if (!existing) return incoming;
    if (!incoming) return existing;
    if (existing.batchId !== incoming.batchId) return existing;
    var merged = {
        batchId: existing.batchId,
        slotIndex: existing.slotIndex,
        slotStartMs: existing.slotStartMs,
        createdAtMs: Math.min(existing.createdAtMs || Date.now(), incoming.createdAtMs || Date.now()),
        mode: 'WSPR',
        reporter: incoming.reporter || existing.reporter,
        spots: []
    };
    var seen = {};
    var combined = (existing.spots || []).concat(incoming.spots || []);
    for (var i = 0; i < combined.length; i++) {
        var clean = sanitizeWsprSpot(combined[i]);
        if (!clean || seen[clean.spotId]) continue;
        seen[clean.spotId] = true;
        merged.spots.push(clean);
    }
    return sanitizeWsprUploadBatch(merged);
}

function persistWsprSpots() {
    try { localStorage.setItem(WSPR_SPOTS_STORAGE_KEY, JSON.stringify(digiWsprSpots.slice(0, 100))); } catch (e) { console.warn('[WFWEB] Failed to persist WSPR spots:', e); }
}

function persistWsprUploadState() {
    try { localStorage.setItem(WSPR_UPLOAD_ENABLED_KEY, digiWsprUploadEnabled ? '1' : '0'); } catch (e) { console.warn('[WFWEB] Failed to persist WSPR upload flag:', e); }
    try { localStorage.setItem(WSPR_UPLOAD_QUEUE_KEY, JSON.stringify(digiWsprUploadQueue)); } catch (e) { console.warn('[WFWEB] Failed to persist WSPR upload queue:', e); }
}

function loadWsprPersistedState() {
    var nowMs = Date.now();
    var expiredQueueCount = 0;
    try {
        var savedSpots = localStorage.getItem(WSPR_SPOTS_STORAGE_KEY);
        if (savedSpots) {
            var parsedSpots = JSON.parse(savedSpots);
            digiWsprSpots = [];
            mergeWsprSpots(parsedSpots);
        }
    } catch (e) {
        console.warn('[WFWEB] Failed to load persisted WSPR spots:', e);
    }

    try {
        digiWsprUploadEnabled = localStorage.getItem(WSPR_UPLOAD_ENABLED_KEY) === '1';
    } catch (e) {
        console.warn('[WFWEB] Failed to load WSPR upload enabled flag:', e);
    }
    try {
        digiPskReporterEnabled = localStorage.getItem(PSK_REPORTER_ENABLED_KEY) === '1';
    } catch (e) {
        console.warn('[WFWEB] Failed to load PSK Reporter enabled flag:', e);
    }

    try {
        var savedQueue = localStorage.getItem(WSPR_UPLOAD_QUEUE_KEY);
        if (savedQueue) {
            var parsedQueue = JSON.parse(savedQueue);
            digiWsprUploadQueue = [];
            for (var i = 0; i < parsedQueue.length; i++) {
                var cleanBatch = sanitizeWsprUploadBatch(parsedQueue[i]);
                if (cleanBatch && !isWsprUploadBatchExpired(cleanBatch, nowMs)) digiWsprUploadQueue.push(cleanBatch);
                else if (cleanBatch) expiredQueueCount++;
            }
        }
    } catch (e) {
        console.warn('[WFWEB] Failed to load persisted WSPR upload queue:', e);
    }
    if (expiredQueueCount > 0 && !digiWsprUploadQueue.length) {
        digiWsprUploadStatusText = 'EXPIRED ' + expiredQueueCount;
    }
}

function updateWsprHealthWarning() {
    if (Math.abs(serverClockOffsetMs) > 1000) digiWsprHealthWarning = 'CLOCK OFFSET ' + (serverClockOffsetMs / 1000).toFixed(1) + ' s';
    else if (document.hidden) digiWsprHealthWarning = 'TAB HIDDEN';
    else if (digiWsprPendingDecodes.length) digiWsprHealthWarning = 'DECODE BACKLOG';
    else digiWsprHealthWarning = '';
}

function updateWsprUploadButton() {
    var btn = document.getElementById('digiWsprUploadBtn');
    if (!btn) return;
    btn.textContent = digiWsprUploadEnabled ? 'WSPRNET ON' : 'WSPRNET OFF';
    btn.classList.toggle('active', digiWsprUploadEnabled);
    syncStationSettingsUi();
}

function stopWsprUploadStatusTimer() {
    if (digiWsprUploadStatusTimer) {
        clearInterval(digiWsprUploadStatusTimer);
        digiWsprUploadStatusTimer = null;
    }
}

function startWsprUploadStatusTimer() {
    if (!digiWsprUploadEnabled || !window.fetch || digiWsprUploadStatusTimer) return;
    digiWsprUploadStatusTimer = setInterval(function() {
        refreshWsprUploadStatus();
    }, 10000);
}

function buildWsprUploadBatch(slotIndex, slotStartMs, dialFrequencyHz, audioFrequencyHz, reporter, spots) {
    if (!dialFrequencyHz || !reporter || !reporter.call || !reporter.grid) return null;
    var cleanSpots = [];
    for (var i = 0; i < (spots || []).length; i++) {
        var clean = sanitizeWsprSpot(spots[i]);
        if (clean) cleanSpots.push(clean);
    }
    return sanitizeWsprUploadBatch({
        batchId: ['wspr', slotIndex, dialFrequencyHz, reporter.call, reporter.grid].join(':'),
        slotIndex: slotIndex,
        slotStartMs: slotStartMs,
        createdAtMs: slotStartMs || Date.now(),
        reporter: {
            call: reporter.call,
            grid: reporter.grid,
            rqrgHz: dialFrequencyHz,
            tqrgHz: dialFrequencyHz,
            txPct: sanitizeWsprTxPct(reporter.txPct),
            dbm: reporter.dbm,
            version: reporter.version || ('wfweb/' + (serverVersion || 'dev'))
        },
        spots: cleanSpots
    });
}

function postWsprDecodeTelemetry(eventType, slotIndex, slotStartMs, extra) {
    if (!window.fetch) return;

    var payload = {
        event: String(eventType || 'unknown'),
        mode: 'WSPR',
        slotIndex: parseInt(slotIndex, 10),
        slotStartMs: parseInt(slotStartMs, 10) || 0,
        state: getWsprRxStateText(),
        summary: digiWsprLastDecodeSummary,
        currentFreqHz: currentFreq || 0,
        targetFreqHz: digiWsprTargetFreq || 0,
        audioFrequencyHz: digiWsprRxAudioFrequencyHz || digiRxFreq || WSPR_DEFAULT_AUDIO_FREQ,
        searchHalfWidthHz: WSPR_SEARCH_HALF_WIDTH_HZ,
        fillRatio: Number(digiWsprLastFillRatio || 0),
        sampleCount: parseInt(digiWsprLastSampleCount, 10) || 0,
        pendingDecodes: digiWsprPendingDecodes.length,
        clientUtcMs: getServerNowMs()
    };

    var slotBand = getDigiBandForFrequency(payload.currentFreqHz, 'WSPR');
    if (slotBand) payload.band = slotBand.label;
    if (digiWsprTargetBandLabel) payload.targetBand = digiWsprTargetBandLabel;
    if (digiWsprLastDecodeError) payload.error = digiWsprLastDecodeError;
    if (digiWsprRxReporter) {
        payload.reporter = {
            call: digiWsprRxReporter.call || '',
            grid: digiWsprRxReporter.grid || '',
            txPct: digiWsprRxReporter.txPct || 0,
            dbm: digiWsprRxReporter.dbm || 0
        };
    }
    if (digiWsprLastDecodeDebug) payload.debug = JSON.parse(JSON.stringify(digiWsprLastDecodeDebug));

    if (extra && typeof extra === 'object') {
        for (var key in extra) {
            if (Object.prototype.hasOwnProperty.call(extra, key)) payload[key] = extra[key];
        }
    }

    fetch('/api/v1/wspr/telemetry', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
        keepalive: true
    }).catch(function(err) {
        console.warn('[WFWEB] Failed to post WSPR telemetry:', err);
    });
}

function refreshWsprUploadStatus() {
    if (!digiWsprUploadEnabled || !window.fetch) return;
    fetch('/api/v1/wsprnet/status', { method: 'GET' })
        .then(parseApiJsonResponse)
        .then(function(status) {
            setWsprUploadHistory(status);
            digiWsprUploadStatusText = summarizeWsprUploadState(status);
            updateWsprInfo(getDigiSlotInfo());
        })
        .catch(function() {
            if (digiWsprUploadEnabled) {
                digiWsprUploadStatusText = 'STATUS ERR';
                updateWsprInfo(getDigiSlotInfo());
            }
        });
}

function flushWsprUploadQueue() {
    if (!digiWsprUploadEnabled || !window.fetch) return;
    if (digiWsprUploadInFlight || !digiWsprUploadQueue.length) return;
    var batch = sanitizeWsprUploadBatch(digiWsprUploadQueue[0]);
    if (!batch) {
        digiWsprUploadQueue.shift();
        persistWsprUploadState();
        flushWsprUploadQueue();
        return;
    }
    if (isWsprUploadBatchExpired(batch)) {
        digiWsprUploadQueue.shift();
        digiWsprUploadStatusText = 'EXPIRED';
        persistWsprUploadState();
        updateWsprInfo(getDigiSlotInfo());
        flushWsprUploadQueue();
        return;
    }
    digiWsprUploadInFlight = true;
    digiWsprUploadStatusText = 'QUEUED ' + digiWsprUploadQueue.length;
    updateWsprInfo(getDigiSlotInfo());
    fetch('/api/v1/wsprnet/upload', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(batch)
    })
        .then(parseApiJsonResponse)
        .then(function(resp) {
            digiWsprUploadInFlight = false;
            setWsprUploadHistory(resp);
            var accepted = parseInt(resp.accepted, 10) || 0;
            var duplicates = parseInt(resp.duplicates, 10) || 0;
            var rejected = parseInt(resp.rejected, 10) || 0;
            var consumed = accepted > 0 || duplicates > 0 || rejected > 0;
            if (!consumed) {
                digiWsprUploadStatusText = 'ERR EMPTY';
                updateWsprInfo(getDigiSlotInfo());
                if (digiWsprUploadRetryTimer) clearTimeout(digiWsprUploadRetryTimer);
                digiWsprUploadRetryTimer = setTimeout(function() {
                    digiWsprUploadRetryTimer = null;
                    flushWsprUploadQueue();
                }, 5000);
                return;
            }
            if (rejected > 0 && accepted === 0 && duplicates === 0) {
                digiWsprUploadQueue.shift();
                persistWsprUploadState();
                digiWsprUploadStatusText = summarizeWsprUploadState(resp);
                updateWsprInfo(getDigiSlotInfo());
                if (digiWsprUploadQueue.length) flushWsprUploadQueue();
                return;
            }
            digiWsprUploadQueue.shift();
            persistWsprUploadState();
            if (rejected > 0 && accepted > 0) digiWsprUploadStatusText = 'PARTIAL ' + rejected;
            else digiWsprUploadStatusText = summarizeWsprUploadState(resp);
            updateWsprInfo(getDigiSlotInfo());
            refreshWsprUploadStatus();
            if (digiWsprUploadQueue.length) flushWsprUploadQueue();
        })
        .catch(function(err) {
            digiWsprUploadInFlight = false;
            digiWsprUploadStatusText = 'ERR ' + err.message;
            updateWsprInfo(getDigiSlotInfo());
            if (digiWsprUploadRetryTimer) clearTimeout(digiWsprUploadRetryTimer);
            digiWsprUploadRetryTimer = setTimeout(function() {
                digiWsprUploadRetryTimer = null;
                flushWsprUploadQueue();
            }, 5000);
        });
}

function queueWsprUploadBatch(batch) {
    var clean = sanitizeWsprUploadBatch(batch);
    if (!clean) return;
    if (isWsprUploadBatchExpired(clean)) return;
    for (var i = 0; i < digiWsprUploadQueue.length; i++) {
        if (digiWsprUploadQueue[i].batchId === clean.batchId) {
            digiWsprUploadQueue[i] = mergeWsprUploadBatches(digiWsprUploadQueue[i], clean);
            persistWsprUploadState();
            return;
        }
    }
    digiWsprUploadQueue.push(clean);
    persistWsprUploadState();
    digiWsprUploadStatusText = 'QUEUED ' + digiWsprUploadQueue.length;
    updateWsprInfo(getDigiSlotInfo());
    flushWsprUploadQueue();
}

function setWsprUploadEnabled(enabled) {
    digiWsprUploadEnabled = !!enabled;
    persistWsprUploadState();
    updateWsprUploadButton();
    digiWsprUploadStatusText = digiWsprUploadEnabled ? (digiWsprUploadQueue.length ? ('QUEUED ' + digiWsprUploadQueue.length) : 'READY') : 'LOCAL ONLY';
    updateWsprInfo(getDigiSlotInfo());
    syncStationSettingsUi();
    if (digiWsprUploadEnabled) {
        startWsprUploadStatusTimer();
        refreshWsprUploadStatus();
        flushWsprUploadQueue();
    } else {
        stopWsprUploadStatusTimer();
    }
}

function beginWsprRxSlot(slotInfo) {
    ensureWsprRxBuffer();
    syncWsprPowerFromRig();
    digiWsprRxSlotIndex = slotInfo.slotIndex;
    digiWsprRxSlotStartMs = slotInfo.slotStartMs;
    digiWsprRxDialFrequencyHz = currentFreq || 0;
    digiWsprRxAudioFrequencyHz = digiRxFreq || WSPR_DEFAULT_AUDIO_FREQ;
    digiWsprRxReporter = {
        call: getMyCall(),
        grid: getMyGridFull(),
        txPct: sanitizeWsprTxPct(digiWsprTxPct),
        dbm: digiWsprPower,
        version: 'wfweb/' + (serverVersion || 'dev')
    };
    digiWsprRxWritePos = 0;
    digiWsprRetuneBlockedSlotIndex = -1;
    digiWsprRxSlotEligible = isWsprRxCaptureEligible(slotInfo.slotIndex);
    digiWsprLastRxSummary = (digiWsprRxSlotEligible ? 'CAPTURE ' : 'SKIP TX ') + formatWsprSlotLabel(slotInfo.slotIndex);
}

function refreshWsprCurrentSlotEligibility() {
    if (!isWsprMode()) return;
    if (digiWsprRxSlotIndex < 0) return;
    digiWsprRxSlotEligible = isWsprRxCaptureEligible(digiWsprRxSlotIndex);
    digiWsprLastRxSummary = (digiWsprRxSlotEligible ? 'CAPTURE ' : 'SKIP TX ') + formatWsprSlotLabel(digiWsprRxSlotIndex);
}

function invalidateWsprRxSlot(slotIndex) {
    if (slotIndex < 0) return;
    if (digiWsprRxSlotIndex !== slotIndex) return;
    digiWsprRxSlotEligible = false;
    digiWsprLastRxSummary = 'SKIP TX ' + formatWsprSlotLabel(slotIndex);
}

function onWsprDecodeResult(payload) {
    if (!payload || payload.epoch !== digiWsprDecodeEpoch) return;
    digiWsprDecodeInProgress = false;
    if (!payload || payload.slotIndex == null) {
        digiWsprLastDecodeSummary = 'ERR';
        digiWsprLastDecodeError = 'invalid payload';
        postWsprDecodeTelemetry('decode_error', -1, 0, { error: digiWsprLastDecodeError });
        return;
    }
    digiWsprLastDecodeError = '';
    digiWsprLastDecodeDebug = payload.debug || null;
    if (digiWsprLastDecodeDebug && isFinite(payload.decodeMs)) digiWsprLastDecodeDebug.decodeMs = payload.decodeMs;

    digiWsprLastDecodedSlotIndex = payload.slotIndex;
    var spots = Array.isArray(payload.spots) ? payload.spots : [];
    var normalizedSpots = [];
    for (var i = 0; i < spots.length; i++) {
        var cleanSpot = sanitizeWsprSpot(spots[i]);
        if (cleanSpot) normalizedSpots.push(cleanSpot);
    }
    mergeWsprSpots(normalizedSpots);
    digiWsprLastDecodeSummary = payload.summary || (formatWsprSlotLabel(payload.slotIndex) + ' ' + normalizedSpots.length + ' spot' + (normalizedSpots.length === 1 ? '' : 's'));
    postWsprDecodeTelemetry('decode_result', payload.slotIndex, payload.slotStartMs, {
        dialFrequencyHz: payload.dialFrequencyHz || 0,
        audioFrequencyHz: payload.audioFrequencyHz || (digiWsprRxAudioFrequencyHz || WSPR_DEFAULT_AUDIO_FREQ),
        decodeMs: isFinite(payload.decodeMs) ? payload.decodeMs : 0,
        resultCount: normalizedSpots.length,
        summary: digiWsprLastDecodeSummary,
        error: ''
    });
    if (digiWsprUploadEnabled) {
        var uploadBatch = buildWsprUploadBatch(payload.slotIndex, payload.slotStartMs,
                                               payload.dialFrequencyHz, payload.audioFrequencyHz,
                                               payload.reporter,
                                               normalizedSpots);
        if (uploadBatch) queueWsprUploadBatch(uploadBatch);
    }
    if (digiPskReporterEnabled) {
        var pskBatch = buildWsprPskReporterBatch(payload.slotIndex, payload.slotStartMs,
                                                 payload.reporter, normalizedSpots);
        if (pskBatch) queuePskReporterBatch(pskBatch);
        else {
            digiPskReporterStatusText = digiPskReporterEnabled ? 'PSKR READY' : 'PSKR OFF';
            updatePskReporterStatusUi();
        }
    }
    updateWsprInfo(getDigiSlotInfo());
    if (digiWsprPendingDecodes.length) {
        var pending = digiWsprPendingDecodes.shift();
        triggerWsprDecode(pending.slotIndex, pending.slotStartMs, pending.samples,
                          pending.dialFrequencyHz, pending.audioFrequencyHz, pending.reporter);
    }
}

function initWsprWorker() {
    if (wsprWorker) return;
    try {
        wsprWorker = new Worker('wspr-rx-worker.js');
        wsprWorker.onmessage = function(e) {
            var data = e.data || {};
            if (!data.ok) {
                digiWsprDecodeInProgress = false;
                digiWsprLastDecodeSummary = 'ERR';
                digiWsprLastDecodeError = String(data.error || 'worker error');
                digiWsprLastDecodeDebug = null;
                postWsprDecodeTelemetry('decode_error', data.slotIndex, data.slotStartMs, {
                    error: digiWsprLastDecodeError,
                    summary: digiWsprLastDecodeSummary
                });
                try { wsprWorker.terminate(); } catch (err) {}
                wsprWorker = null;
                if (digiWsprPendingDecodes.length) {
                var pending = digiWsprPendingDecodes.shift();
                triggerWsprDecode(pending.slotIndex, pending.slotStartMs, pending.samples,
                                      pending.dialFrequencyHz, pending.audioFrequencyHz, pending.reporter);
                }
                updateWsprInfo(getDigiSlotInfo());
                return;
            }
            onWsprDecodeResult(data);
        };
        wsprWorker.onerror = function() {
            digiWsprDecodeInProgress = false;
            digiWsprLastDecodeSummary = 'ERR';
            digiWsprLastDecodeError = 'worker exception';
            digiWsprLastDecodeDebug = null;
            postWsprDecodeTelemetry('decode_error', digiWsprLastCapturedSlotIndex, digiWsprRxSlotStartMs, {
                error: digiWsprLastDecodeError,
                summary: digiWsprLastDecodeSummary
            });
            try { wsprWorker.terminate(); } catch (err) {}
            wsprWorker = null;
            if (digiWsprPendingDecodes.length) {
                var pending = digiWsprPendingDecodes.shift();
                triggerWsprDecode(pending.slotIndex, pending.slotStartMs, pending.samples,
                                  pending.dialFrequencyHz, pending.audioFrequencyHz, pending.reporter);
            }
            updateWsprInfo(getDigiSlotInfo());
        };
    } catch (e) {
        wsprWorker = null;
    }
}

function triggerWsprDecode(slotIndex, slotStartMs, samples, dialFrequencyHz, audioFrequencyHz, reporter) {
    if (!samples || !samples.length) return;
    initWsprWorker();
    digiWsprDecodeInProgress = true;
    digiWsprLastDecodeSummary = 'QUEUED ' + formatWsprSlotLabel(slotIndex);
    digiWsprLastDecodeError = '';
    if (!wsprWorker) {
        digiWsprDecodeInProgress = false;
        digiWsprLastDecodeSummary = 'NO WORKER';
        digiWsprLastDecodeError = 'worker unavailable';
        postWsprDecodeTelemetry('decode_error', slotIndex, slotStartMs, {
            error: digiWsprLastDecodeError,
            summary: digiWsprLastDecodeSummary,
            dialFrequencyHz: dialFrequencyHz || 0,
            audioFrequencyHz: audioFrequencyHz || WSPR_DEFAULT_AUDIO_FREQ
        });
        return;
    }
    postWsprDecodeTelemetry('decode_queued', slotIndex, slotStartMs, {
        dialFrequencyHz: dialFrequencyHz || 0,
        audioFrequencyHz: audioFrequencyHz || WSPR_DEFAULT_AUDIO_FREQ,
        summary: digiWsprLastDecodeSummary
    });
    wsprWorker.postMessage({
        type: 'decode',
        epoch: digiWsprDecodeEpoch,
        slotIndex: slotIndex,
        slotStartMs: slotStartMs,
        sampleRate: WSPR_SAMPLE_RATE,
        dialFrequencyHz: dialFrequencyHz || 0,
        audioFrequencyHz: audioFrequencyHz || WSPR_DEFAULT_AUDIO_FREQ,
        searchHalfWidthHz: WSPR_SEARCH_HALF_WIDTH_HZ,
        reporter: reporter || null,
        samples: samples.buffer
    }, [samples.buffer]);
}

function finalizeWsprRxSlot(slotIndex) {
    if (slotIndex < 0) return { advanceHop: false };
    var fillRatio = digiWsprRxWritePos / WSPR_RX_BUFFER_SAMPLES;
    var retuneBlocked = digiWsprRetuneBlockedSlotIndex === slotIndex;
    digiWsprLastCapturedSlotIndex = slotIndex;
    digiWsprLastFillRatio = fillRatio;
    digiWsprLastSampleCount = digiWsprRxWritePos;
    digiWsprLastDecodeDebug = {
        searchCenterHz: digiWsprRxAudioFrequencyHz || WSPR_DEFAULT_AUDIO_FREQ,
        searchLowOffsetHz: -WSPR_SEARCH_HALF_WIDTH_HZ,
        searchHighOffsetHz: WSPR_SEARCH_HALF_WIDTH_HZ,
        sampleCount: digiWsprRxWritePos,
        peakCandidates: 0,
        filteredCandidates: 0,
        refinedCandidates: 0,
        decodePasses: 0,
        resultCount: 0,
        decodeMs: 0
    };

    if (!digiWsprRxSlotEligible) {
        digiWsprLastRxSummary = 'SKIP TX ' + formatWsprSlotLabel(slotIndex);
        postWsprDecodeTelemetry('slot_skip_tx', slotIndex, digiWsprRxSlotStartMs, {
            summary: digiWsprLastRxSummary,
            dialFrequencyHz: digiWsprRxDialFrequencyHz,
            audioFrequencyHz: digiWsprRxAudioFrequencyHz
        });
        return { advanceHop: false };
    }
    if (fillRatio < WSPR_RX_MIN_FILL_RATIO) {
        digiWsprLastRxSummary = (retuneBlocked ? 'RETUNE ' : 'PARTIAL ') +
            formatWsprSlotLabel(slotIndex) + (retuneBlocked ? '' : (' ' + Math.round(fillRatio * 100) + '%'));
        postWsprDecodeTelemetry(retuneBlocked ? 'slot_retune' : 'slot_partial', slotIndex, digiWsprRxSlotStartMs, {
            summary: digiWsprLastRxSummary,
            dialFrequencyHz: digiWsprRxDialFrequencyHz,
            audioFrequencyHz: digiWsprRxAudioFrequencyHz
        });
        return { advanceHop: false };
    }
    if (digiWsprDecodeInProgress) {
        digiWsprLastRxSummary = 'READY ' + formatWsprSlotLabel(slotIndex) + ' ' + Math.round(fillRatio * 100) + '%';
        digiWsprLastDecodeSummary = 'QUEUED ' + formatWsprSlotLabel(slotIndex);
        while (digiWsprPendingDecodes.length >= WSPR_MAX_PENDING_DECODES) {
            digiWsprPendingDecodes.shift();
        }
        digiWsprPendingDecodes.push({
            epoch: digiWsprDecodeEpoch,
            slotIndex: slotIndex,
            slotStartMs: digiWsprRxSlotStartMs,
            dialFrequencyHz: digiWsprRxDialFrequencyHz,
            audioFrequencyHz: digiWsprRxAudioFrequencyHz,
            reporter: digiWsprRxReporter,
            samples: digiWsprRxBuffer.slice(0, digiWsprRxWritePos)
        });
        postWsprDecodeTelemetry('slot_pending', slotIndex, digiWsprRxSlotStartMs, {
            summary: digiWsprLastRxSummary,
            dialFrequencyHz: digiWsprRxDialFrequencyHz,
            audioFrequencyHz: digiWsprRxAudioFrequencyHz
        });
        return { advanceHop: true };
    }

    digiWsprLastRxSummary = 'READY ' + formatWsprSlotLabel(slotIndex) + ' ' + Math.round(fillRatio * 100) + '%';
    var samples = digiWsprRxBuffer.slice(0, digiWsprRxWritePos);
    triggerWsprDecode(slotIndex, digiWsprRxSlotStartMs, samples, digiWsprRxDialFrequencyHz,
                      digiWsprRxAudioFrequencyHz, digiWsprRxReporter);
    return { advanceHop: true };
}

function syncWsprRxSlotForMs(ms) {
    if (!isWsprMode()) return;
    var slotInfo = getWsprSlotInfoAtMs(ms);
    if (digiWsprRxSlotIndex < 0) {
        beginWsprRxSlot(slotInfo);
        return;
    }
    if (slotInfo.slotIndex !== digiWsprRxSlotIndex) {
        var completedSlotIndex = digiWsprRxSlotIndex;
        var completedEligible = digiWsprRxSlotEligible;
        var completion = finalizeWsprRxSlot(completedSlotIndex) || { advanceHop: false };
        advanceWsprStandbyHop(slotInfo, completedSlotIndex, completedEligible && !!completion.advanceHop);
        beginWsprRxSlot(slotInfo);
    }
}

function getBandFreqForMode(band, mode) {
    if (!band) return 0;
    mode = mode || digiMode;
    if (mode === 'FT4') return band.ft4;
    if (mode === 'WSPR') return band.wspr;
    return band.ft8;
}

function getDigiBandByLabel(label) {
    if (!label) return null;
    for (var i = 0; i < DIGI_BANDS.length; i++) {
        if (DIGI_BANDS[i].label === label) return DIGI_BANDS[i];
    }
    return null;
}

function getDefaultWsprHopBandLabel() {
    var bandSel = document.getElementById('digiBandSel');
    if (bandSel && getDigiBandByLabel(bandSel.value)) return bandSel.value;
    var currentBand = getCurrentDigiBand();
    if (currentBand) return currentBand.label;
    return '';
}

function sanitizeWsprHopBands(labels) {
    var out = [];
    for (var i = 0; i < (labels || []).length; i++) {
        var label = String(labels[i] || '').trim();
        if (!getDigiBandByLabel(label)) continue;
        if (out.indexOf(label) < 0) out.push(label);
    }
    return out;
}

function syncDigiBandSelector(label) {
    var bandSel = document.getElementById('digiBandSel');
    if (bandSel && getDigiBandByLabel(label)) bandSel.value = label;
}

function persistWsprHopIndex() {
    try { localStorage.setItem(WSPR_HOP_INDEX_STORAGE_KEY, String(digiWsprHopIndex)); } catch (e) {}
}

function hasWsprBandSelection() {
    return sanitizeWsprHopBands(digiWsprHopBands).length > 0;
}

function isWsprRetuneReady() {
    if (!digiWsprTargetFreq) return false;
    if (currentMode !== 'USB') return false;
    if (!currentFreq) return false;
    return Math.abs(currentFreq - digiWsprTargetFreq) <= WSPR_RETUNE_TOLERANCE_HZ;
}

function requestWsprRetune(band, mode) {
    if (!band) return false;
    var targetMode = mode || 'USB';
    var targetFreq = getBandFreqForMode(band, 'WSPR');
    var now = Date.now();
    var targetChanged = (digiWsprTargetFreq !== targetFreq) || (digiWsprTargetBandLabel !== band.label);
    var needMode = currentMode !== targetMode;
    var needFreq = !currentFreq || Math.abs(currentFreq - targetFreq) > WSPR_RETUNE_TOLERANCE_HZ;

    syncDigiBandSelector(band.label);
    digiWsprTargetFreq = targetFreq;
    if (!needMode && !needFreq) return true;
    if (isWsprMode() && (needMode || needFreq)) invalidateWsprRxSlot(digiWsprRxSlotIndex);

    if (targetChanged || !digiWsprRetuneRequestAt || (now - digiWsprRetuneRequestAt) >= WSPR_RETUNE_RETRY_MS) {
        if (needMode) send({ cmd: 'setMode', value: targetMode });
        if (needFreq) {
            send({ cmd: 'setFrequency', value: targetFreq });
            wfLabels = [];
        }
        digiWsprRetuneRequestAt = now;
    }

    return false;
}

function setWsprHopBands(labels, keepIndex) {
    var preferredLabel = digiWsprTargetBandLabel;
    var currentLabels = sanitizeWsprHopBands(digiWsprHopBands);
    if (!preferredLabel && currentLabels.length) {
        var currentIndex = digiWsprHopIndex;
        if (currentIndex < 0 || currentIndex >= currentLabels.length) currentIndex = 0;
        preferredLabel = currentLabels[currentIndex];
    }
    var sanitized = sanitizeWsprHopBands(labels);
    if (!sanitized.length) {
        var fallback = getDefaultWsprHopBandLabel();
        if (fallback) sanitized = [fallback];
    }
    digiWsprHopBands = sanitized;
    if (keepIndex) {
        var preferredIndex = preferredLabel ? digiWsprHopBands.indexOf(preferredLabel) : -1;
        if (preferredIndex >= 0) digiWsprHopIndex = preferredIndex;
        else if (digiWsprHopIndex >= digiWsprHopBands.length) digiWsprHopIndex = 0;
    } else {
        digiWsprHopIndex = 0;
    }
    try { localStorage.setItem(WSPR_HOP_BANDS_STORAGE_KEY, JSON.stringify(digiWsprHopBands)); } catch (e) {}
    persistWsprHopIndex();
    renderWsprHopButtons();
}

function ensureWsprHopSelection() {
    var sanitized = sanitizeWsprHopBands(digiWsprHopBands);
    if (!sanitized.length) {
        var fallback = getDefaultWsprHopBandLabel();
        if (fallback) sanitized = [fallback];
    }
    digiWsprHopBands = sanitized;
    if (digiWsprHopIndex >= sanitized.length) digiWsprHopIndex = 0;
    if (sanitized.length) {
        try { localStorage.setItem(WSPR_HOP_BANDS_STORAGE_KEY, JSON.stringify(digiWsprHopBands)); } catch (e) {}
        persistWsprHopIndex();
    }
    return digiWsprHopBands.slice();
}

function getWsprHopSelection() {
    var labels = ensureWsprHopSelection();
    var bands = [];
    for (var i = 0; i < labels.length; i++) {
        var band = getDigiBandByLabel(labels[i]);
        if (band) bands.push(band);
    }
    return bands;
}

function renderWsprHopButtons() {
    var host = document.getElementById('digiWsprBandList');
    if (!host) return;
    var activeLabels = ensureWsprHopSelection();
    host.innerHTML = '';
    for (var i = 0; i < DIGI_BANDS.length; i++) {
        var band = DIGI_BANDS[i];
        var btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'digi-wspr-band-btn' + (activeLabels.indexOf(band.label) >= 0 ? ' active' : '');
        btn.textContent = band.label;
        btn.dataset.band = band.label;
        btn.setAttribute('aria-pressed', activeLabels.indexOf(band.label) >= 0 ? 'true' : 'false');
        host.appendChild(btn);
    }
}

function toggleWsprHopBandSelection(label) {
    var nextBands = digiWsprHopBands.slice();
    var idx = nextBands.indexOf(label);
    if (idx >= 0) {
        if (nextBands.length === 1) return false;
        nextBands.splice(idx, 1);
    } else {
        nextBands.push(label);
    }
    setWsprHopBands(nextBands, true);
    if (digiTxActive) {
        updateWsprInfo(getDigiSlotInfo());
        return true;
    }
    if (!digiTxArmed && !digiTxActive) {
        clearWsprTargetBand();
        if (isWsprMode() && digiWsprHopBands.length === 1) {
            var onlyBand = getDigiBandByLabel(digiWsprHopBands[0]);
            if (onlyBand) requestWsprRetune(onlyBand, 'USB');
        }
    } else if (digiWsprTargetBandLabel && digiWsprHopBands.indexOf(digiWsprTargetBandLabel) < 0) {
        clearWsprTargetBand();
        if (digiTxQueued) prepareWsprTargetBand();
    }
    updateWsprInfo(getDigiSlotInfo());
    return true;
}

function chooseWsprTargetBand() {
    var bands = getWsprHopSelection();
    if (!bands.length) return { band: null, index: -1 };
    var index = digiWsprHopIndex;
    if (index < 0 || index >= bands.length) index = 0;
    return { band: bands[index], index: index };
}

function prepareWsprTargetBand() {
    var currentChoice = digiWsprTargetBandLabel ? getDigiBandByLabel(digiWsprTargetBandLabel) : null;
    if (currentChoice && digiWsprPendingHopIndex >= 0) {
        requestWsprRetune(currentChoice, 'USB');
        return currentChoice;
    }
    var choice = chooseWsprTargetBand();
    digiWsprTargetBandLabel = choice.band ? choice.band.label : '';
    digiWsprPendingHopIndex = choice.index;
    if (choice.band) requestWsprRetune(choice.band, 'USB');
    return choice.band;
}

function parkWsprStandbyBand() {
    if (!isWsprMode()) return null;
    if (!hasWsprBandSelection()) {
        clearWsprTargetBand();
        return null;
    }
    return prepareWsprTargetBand();
}

function clearWsprTargetBand() {
    digiWsprPendingHopIndex = -1;
    digiWsprTargetBandLabel = '';
    digiWsprTargetFreq = 0;
    digiWsprRetuneRequestAt = 0;
}

function commitWsprTargetBandProgression() {
    if (digiWsprManualEnable) {
        digiWsprPendingHopIndex = -1;
        return;
    }
    var bands = getWsprHopSelection();
    if (bands.length > 1 && digiWsprPendingHopIndex >= 0) {
        digiWsprHopIndex = (digiWsprPendingHopIndex + 1) % bands.length;
    } else {
        digiWsprHopIndex = 0;
    }
    digiWsprPendingHopIndex = -1;
    persistWsprHopIndex();
}

function advanceWsprStandbyHop(nextSlotInfo, completedSlotIndex, completedEligible) {
    if (!isWsprMode()) return false;
    if (!completedEligible || completedSlotIndex < 0) return false;
    if (!(digiWsprLastFillRatio >= WSPR_RX_MIN_FILL_RATIO)) return false;
    if (digiTxActive) return false;

    var bands = getWsprHopSelection();
    if (bands.length <= 1) return false;

    if (digiWsprManualEnable && digiTxArmed && digiWsprTargetSlotIndex >= 0) return false;
    if (digiWsprTargetSlotIndex >= 0 && nextSlotInfo && ((digiWsprTargetSlotIndex - nextSlotInfo.slotIndex) <= 1)) {
        return false;
    }

    var currentIndex = digiWsprHopIndex;
    if (currentIndex < 0 || currentIndex >= bands.length) currentIndex = 0;
    digiWsprHopIndex = (currentIndex + 1) % bands.length;
    persistWsprHopIndex();

    clearWsprTargetBand();
    var nextChoice = chooseWsprTargetBand();
    if (!nextChoice.band) return false;

    digiWsprTargetBandLabel = nextChoice.band.label;
    digiWsprPendingHopIndex = nextChoice.index;
    requestWsprRetune(nextChoice.band, 'USB');
    return true;
}

function getWsprArmLeadBudget(targetBand) {
    var targetFreq = targetBand ? getBandFreqForMode(targetBand, 'WSPR') : 0;
    var sameBand = targetFreq > 0 &&
                   currentFreq > 0 &&
                   Math.abs(currentFreq - targetFreq) <= WSPR_RETUNE_TOLERANCE_HZ &&
                   currentMode === 'USB';
    return sameBand ? WSPR_SAME_BAND_ARM_LEAD : WSPR_HOP_ARM_LEAD;
}

function getWsprScheduledSlot(info, targetBand) {
    var leadBudget = getWsprArmLeadBudget(targetBand);
    var secondsUntilTx = WSPR_TX_START_OFFSET - info.slotPhase;
    return (secondsUntilTx >= leadBudget) ? info.slotIndex : (info.slotIndex + 1);
}

function getWsprSlotsPerHour() {
    return sanitizeWsprTxPct(digiWsprTxPct) * 0.15;
}

function shouldScheduleDeterministicWsprSlot(slotIndex) {
    var txPct = sanitizeWsprTxPct(digiWsprTxPct);
    if (txPct <= 0) return false;
    if ((slotIndex & 1) !== 0) return false;
    if (txPct >= 100) return true;
    var eligibleIndex = Math.floor(slotIndex / 2);
    var slotInCycle = ((eligibleIndex % 100) + 100) % 100;
    return Math.floor((slotInCycle + 1) * txPct / 100) !== Math.floor(slotInCycle * txPct / 100);
}

function findNextDeterministicWsprSlot(info, targetBand) {
    for (var offset = 0; offset < 500; offset++) {
        var slotIndex = info.slotIndex + offset;
        if (!shouldScheduleDeterministicWsprSlot(slotIndex)) continue;
        if (offset === 0 && getWsprScheduledSlot(info, targetBand) !== info.slotIndex) continue;
        return slotIndex;
    }
    return -1;
}

function findFutureDeterministicWsprSlot(slotIndex) {
    for (var offset = 0; offset < 500; offset++) {
        var candidate = slotIndex + offset;
        if (shouldScheduleDeterministicWsprSlot(candidate)) return candidate;
    }
    return -1;
}

function sanitizeWsprTxPct(value) {
    value = parseInt(value, 10);
    if (isNaN(value)) value = 0;
    return Math.max(0, Math.min(100, value));
}

function getCurrentRfPowerValue() {
    var slider = document.getElementById('rfPowerSlider');
    var val = slider ? parseInt(slider.value, 10) : currentRfPower;
    if (isNaN(val)) val = currentRfPower;
    if (isNaN(val)) val = 128;
    currentRfPower = Math.max(0, Math.min(255, val));
    return currentRfPower;
}

function quantizeWsprDbm(dbm) {
    if (!window.wsprEncodeLib) return digiWsprPower;
    var levels = window.wsprEncodeLib.getValidPowerLevels();
    var best = levels[0];
    var bestDiff = Math.abs(best - dbm);
    for (var i = 1; i < levels.length; i++) {
        var diff = Math.abs(levels[i] - dbm);
        if (diff < bestDiff) {
            best = levels[i];
            bestDiff = diff;
        }
    }
    return best;
}

function getWsprPowerInfo() {
    var raw = getCurrentRfPowerValue();
    var percent = Math.max(0, Math.min(100, Math.round(raw / 255 * 100)));
    var watts = percent <= 0 ? 0 : Math.max(1, percent);
    var exactDbm = watts > 0 ? (30 + 10 * Math.log10(watts)) : 0;
    var wsprDbm = watts > 0 ? quantizeWsprDbm(exactDbm) : 0;
    return { raw: raw, percent: percent, watts: watts, exactDbm: exactDbm, wsprDbm: wsprDbm };
}

function syncWsprPowerFromRig() {
    var powerInfo = getWsprPowerInfo();
    digiWsprPower = powerInfo.wsprDbm;

    var powerInfoEl = document.getElementById('digiWsprPowerInfo');
    if (powerInfoEl) {
        if (powerInfo.watts > 0) powerInfoEl.textContent = powerInfo.watts + ' W (' + powerInfo.percent + '%, ' + powerInfo.wsprDbm + ' dBm)';
        else powerInfoEl.textContent = '0 W (0%, 0 dBm)';
    }

    return powerInfo;
}

function getWsprStateText() {
    if (digiTxActive) return 'TX';
    if (digiTxArmed && digiWsprTargetSlotIndex >= 0 && !isWsprRetuneReady()) return 'RETUNE';
    if (digiTxArmed && digiWsprTargetSlotIndex >= 0) return digiWsprManualEnable ? 'BEACON' : 'ARMED';
    if (digiTxEnabled && sanitizeWsprTxPct(digiWsprTxPct) > 0) return 'AUTO';
    if (digiTxEnabled) return 'ON';
    return 'OFF';
}

function getWsprMessageText() {
    if (!window.wsprEncodeLib) throw new Error('WSPR encoder is not loaded');
    syncWsprPowerFromRig();
    return window.wsprEncodeLib.buildMessageText(getMyCall(), getMyGrid(), digiWsprPower);
}

function formatUtcTime(ms) {
    return new Date(ms).toISOString().substr(11, 8) + 'Z';
}

function formatUtcSpotTimestamp(ms) {
    var iso = new Date(ms).toISOString();
    return iso.substr(0, 10) + ' ' + iso.substr(11, 8) + 'Z';
}

function formatDate6(ms) {
    var d = new Date(ms);
    return String(d.getUTCFullYear()).slice(-2) +
           String(d.getUTCMonth() + 1).padStart(2, '0') +
           String(d.getUTCDate()).padStart(2, '0');
}

function formatTime4(ms) {
    var d = new Date(ms);
    return String(d.getUTCHours()).padStart(2, '0') +
           String(d.getUTCMinutes()).padStart(2, '0');
}

function getWsprNextSlotTime(slotIndex) {
    return formatUtcTime(slotIndex * 120000 + Math.round(WSPR_TX_START_OFFSET * 1000));
}

function resetWsprSlotDecision() {
    digiWsprLastDecisionSlotIndex = -1;
}

function ensureWsprAudioFrequency(force) {
    if (!isWsprMode()) return;

    var txInWindow = digiTxFreq >= WSPR_AUDIO_WINDOW_LOW && digiTxFreq <= WSPR_AUDIO_WINDOW_HIGH;
    var rxInWindow = digiRxFreq >= WSPR_AUDIO_WINDOW_LOW && digiRxFreq <= WSPR_AUDIO_WINDOW_HIGH;

    if (force || !txInWindow) digiTxFreq = WSPR_DEFAULT_AUDIO_FREQ;
    if (force || !rxInWindow) digiRxFreq = WSPR_DEFAULT_AUDIO_FREQ;

    updateDigiTxFreqDisplay();
    updateDigiRxFreqDisplay();
}

function updateWsprInfo(info) {
    var msgEl = document.getElementById('digiWsprMessage');
    var queueEl = document.getElementById('digiWsprQueued');
    var rxStateEl = document.getElementById('digiWsprRxState');
    var lastDecodeEl = document.getElementById('digiWsprLastDecode');
    var spotCountEl = document.getElementById('digiWsprSpotCount');
    var uploadStatusEl = document.getElementById('digiWsprUploadStatus');
    var pskrStatusEl = document.getElementById('digiWsprPskReporterStatus');
    var targetBandEl = document.getElementById('digiWsprTargetBandInfo');
    var nextEl = document.getElementById('digiWsprNextTx');
    var alertEl = document.getElementById('digiWsprAlert');
    updateWsprHealthWarning();
    syncWsprPowerFromRig();
    ensureWsprHopSelection();
    renderWsprHopButtons();

    if (msgEl) {
        try { msgEl.textContent = getWsprMessageText(); }
        catch (e) { msgEl.textContent = 'SET CALL/GRID'; }
    }

    if (queueEl) {
        queueEl.textContent = getWsprStateText();
    }

    if (rxStateEl) {
        rxStateEl.textContent = getWsprRxStateText();
    }

    if (lastDecodeEl) {
        lastDecodeEl.textContent = digiWsprLastDecodeSummary;
    }

    if (spotCountEl) {
        spotCountEl.textContent = String(digiWsprSpots.length);
    }

    if (uploadStatusEl) {
        uploadStatusEl.textContent = digiWsprUploadEnabled ? digiWsprUploadStatusText : 'LOCAL ONLY';
    }

    if (pskrStatusEl) {
        pskrStatusEl.textContent = digiPskReporterStatusText;
    }

    if (targetBandEl) {
        var nextBand = chooseWsprTargetBand().band;
        targetBandEl.textContent = digiWsprTargetBandLabel || (nextBand ? nextBand.label : '--');
    }

    if (nextEl) {
        if (digiWsprTargetSlotIndex >= 0) nextEl.textContent = getWsprNextSlotTime(digiWsprTargetSlotIndex);
        else if (info && hasWsprBandSelection() && digiTxEnabled && getWsprSlotsPerHour() > 0) {
            var nextAutoSlot = findNextDeterministicWsprSlot(info, chooseWsprTargetBand().band);
            nextEl.textContent = nextAutoSlot >= 0 ? getWsprNextSlotTime(nextAutoSlot) : '--:--:--Z';
        } else if (info) {
            nextEl.textContent = getWsprNextSlotTime(info.slotPhase < WSPR_TX_START_OFFSET ? info.slotIndex : info.slotIndex + 1);
        }
        else nextEl.textContent = '--:--:--Z';
    }

    if (alertEl) {
        if (digiWsprHealthWarning) {
            alertEl.textContent = digiWsprHealthWarning;
            alertEl.classList.add('visible');
        } else {
            alertEl.textContent = '';
            alertEl.classList.remove('visible');
        }
    }

}

function clearWsprQueue() {
    digiTxQueued = null;
    digiTxArmed = false;
    digiTxAttempts = 0;
    digiWsprTargetSlotIndex = -1;
    digiWsprManualEnable = false;
    clearWsprTargetBand();
    resetWsprSlotDecision();
    refreshWsprCurrentSlotEligibility();
    updateDigiStatus('IDLE', '');
    updateWsprInfo(getDigiSlotInfo());
    updateDigiMsgRows();
}

function armWsprBeaconNext() {
    if (!checkDigiCallGrid()) return;
    if (!hasWsprBandSelection()) {
        updateDigiStatus('SET BAND', '');
        updateWsprInfo(getDigiSlotInfo());
        return;
    }
    if (!window.wsprEncodeLib) {
        updateDigiStatus('WSPR ERR', '');
        return;
    }

    try {
        digiTxQueued = getWsprMessageText();
    } catch (e) {
        updateDigiStatus('WSPR ERR', '');
        return;
    }

    var info = getDigiSlotInfo();
    var targetBand = chooseWsprTargetBand().band;
    digiWsprManualEnable = !digiTxEnabled;
    digiTxEnabled = true;
    digiTxArmed = true;
    digiTxAttempts = 0;
    digiTxSlotParity = -1;
    resetWsprSlotDecision();
    digiWsprTargetSlotIndex = getWsprScheduledSlot(info, targetBand);
    invalidateWsprRxSlot(digiWsprTargetSlotIndex);
    prepareWsprTargetBand();
    initDigiTxCtx();
    var enableBtn = document.getElementById('digiEnableTxBtn');
    if (enableBtn) enableBtn.classList.add('active');
    updateDigiStatus('ARMED', '');
    updateWsprInfo(info);
    updateDigiMsgRows();
}

function maybeScheduleWsprTx(info) {
    if (!isWsprMode()) return;

    if (digiWsprTargetSlotIndex >= info.slotIndex && digiTxQueued) {
        prepareWsprTargetBand();
        updateWsprInfo(info);
        return;
    }

    if (!digiTxEnabled) {
        clearWsprTargetBand();
        updateWsprInfo(info);
        return;
    }

    if (!hasWsprBandSelection()) {
        clearWsprTargetBand();
        digiTxQueued = null;
        digiTxArmed = false;
        digiWsprTargetSlotIndex = -1;
        updateDigiStatus('SET BAND', '');
        updateWsprInfo(info);
        return;
    }

    if (!getMyCall() || !getMyGrid()) {
        updateDigiStatus('SET CALL', '');
        updateWsprInfo(info);
        return;
    }

    if (!window.wsprEncodeLib) {
        updateDigiStatus('WSPR ERR', '');
        updateWsprInfo(info);
        return;
    }

    var txPct = sanitizeWsprTxPct(digiWsprTxPct);
    if (txPct <= 0) {
        clearWsprTargetBand();
        digiTxQueued = null;
        digiTxArmed = false;
        digiWsprTargetSlotIndex = -1;
        updateWsprInfo(info);
        return;
    }

    var targetBand = chooseWsprTargetBand().band;
    var nextSlot = findNextDeterministicWsprSlot(info, targetBand);
    if (nextSlot < 0) {
        digiTxQueued = null;
        digiTxArmed = false;
        digiWsprTargetSlotIndex = -1;
        clearWsprTargetBand();
        updateDigiStatus('IDLE', '');
        updateWsprInfo(info);
        return;
    }

    try {
        digiTxQueued = getWsprMessageText();
    } catch (e) {
        updateDigiStatus('WSPR ERR', '');
        digiTxQueued = null;
        digiTxArmed = false;
        digiWsprTargetSlotIndex = -1;
        clearWsprTargetBand();
        updateWsprInfo(info);
        return;
    }

    digiTxArmed = true;
    digiTxAttempts = 0;
    digiTxSlotParity = -1;
    digiWsprTargetSlotIndex = nextSlot;
    invalidateWsprRxSlot(digiWsprTargetSlotIndex);
    prepareWsprTargetBand();
    updateDigiStatus(nextSlot > info.slotIndex ? 'WAIT TX' : 'ARMED', '');
    updateWsprInfo(info);
}
